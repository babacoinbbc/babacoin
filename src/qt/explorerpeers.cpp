// Copyright (c) 2026 The Babacoin developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#include <qt/explorerpeers.h>

#include <interfaces/node.h>
#include <netbase.h>
#include <util.h>
#include <utiltime.h>

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QUrl>
#include <set>

bool ExplorerPeers::Allowed(const Policy& p)
{
    return p.enabled && p.network == "main" && p.active && !p.shuttingDown &&
           !p.explicitPeers && !p.restrictedNetworks && !p.proxy && p.dnsSeed && p.maxConnections > 0;
}

std::vector<CAddress> ExplorerPeers::Parse(const QByteArray& body, int64_t now)
{
    std::vector<CAddress> result;
    if (body.size() > MAX_RESPONSE_BYTES) return result;
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(body, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return result;
    const auto value = document.object().value("addnode_peers");
    if (!value.isArray()) return result;
    const QRegularExpression numeric("\\A[0-9a-fA-F:.]+\\z");
    std::set<std::vector<unsigned char>> groups;
    int inspected = 0;
    for (const auto& item : value.toArray()) {
        if (++inspected > 256 || result.size() >= MAX_PEERS) break;
        if (!item.isObject()) continue;
        const auto object = item.toObject();
        if (!object.value("address").isString()) continue;
        QString host = object.value("address").toString();
        if (host.startsWith('[') && host.endsWith(']')) host = host.mid(1, host.size() - 2);
        if (host.isEmpty() || host.size() > 64 || !numeric.match(host).hasMatch()) continue;
        const auto port = object.value("port");
        if (!((port.isString() && port.toString() == "6678") ||
              (port.isDouble() && port.toDouble() == 6678))) continue;
        const CService service = LookupNumeric(host.toStdString().c_str(), 6678);
        if (!service.IsValid() || !service.IsRoutable() || service.GetPort() != 6678 ||
            (service.GetNetwork() != NET_IPV4 && service.GetNetwork() != NET_IPV6)) continue;
        // One hint per network group, with a single source bucket in addrman.
        if (!groups.insert(service.GetGroup()).second) continue;
        CAddress address(service, NODE_NETWORK);
        address.nTime = now - 3 * 24 * 60 * 60;
        result.push_back(address);
    }
    return result;
}

ExplorerPeers::ExplorerPeers(interfaces::Node& node, QObject* parent) :
    QObject(parent), m_node(node), m_manager(this), m_timer(this)
{
    m_timer.setInterval(30 * 60 * 1000);
    connect(&m_timer, &QTimer::timeout, this, [this] { fetch(); });
}

ExplorerPeers::~ExplorerPeers() { stop(); }

ExplorerPeers::Policy ExplorerPeers::policy() const
{
    Policy p;
    p.enabled = m_enabled;
    p.network = m_node.getNetwork();
    p.active = m_node.getNetworkActive();
    p.shuttingDown = m_node.shutdownRequested();
    p.explicitPeers = gArgs.IsArgSet("-connect");
    p.restrictedNetworks = gArgs.IsArgSet("-onlynet");
    proxyType proxy;
    p.proxy = gArgs.IsArgSet("-proxy") || m_node.getProxy(NET_IPV4, proxy) ||
              m_node.getProxy(NET_IPV6, proxy) || HaveNameProxy();
    p.dnsSeed = gArgs.GetBoolArg("-dnsseed", true);
    p.maxConnections = gArgs.GetArg("-maxconnections", 125);
    return p;
}

void ExplorerPeers::setEnabled(bool enabled)
{
    if (!enabled) { stop(); return; }
    if (m_enabled) return;
    m_enabled = true;
    m_timer.start();
    QTimer::singleShot(2000, this, [this] { fetch(); });
}

void ExplorerPeers::stop()
{
    m_enabled = false;
    m_timer.stop();
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply.clear();
    }
    m_body.clear();
}

void ExplorerPeers::fetch()
{
    if (m_reply || !Allowed(policy())) return;
    if (!QSslSocket::supportsSsl()) {
        qWarning() << "Explorer peers: HTTPS support unavailable; retaining normal discovery.";
        return;
    }
    QNetworkRequest request(QUrl("https://explorer.babacoin.network/ext/getnetworkpeers/internal"));
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("User-Agent", "Babacoin-Core-V2-PeerDiscovery");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    auto ssl = QSslConfiguration::defaultConfiguration();
    ssl.setPeerVerifyMode(QSslSocket::VerifyPeer);
    ssl.setProtocol(QSsl::TlsV1_2OrLater);
    request.setSslConfiguration(ssl);
    m_body.clear();
    m_oversized = false;
    QNetworkReply* reply = m_manager.get(request);
    m_reply = reply;
    reply->setReadBufferSize(MAX_RESPONSE_BYTES + 1);
    connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
        m_body += reply->read(MAX_RESPONSE_BYTES + 1 - m_body.size());
        if (m_body.size() > MAX_RESPONSE_BYTES) {
            m_oversized = true;
            reply->abort();
        }
    });
    QTimer::singleShot(15000, reply, [reply] { if (!reply->isFinished()) reply->abort(); });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        m_body += reply->read(MAX_RESPONSE_BYTES + 1 - m_body.size());
        const bool valid = !m_oversized && m_body.size() <= MAX_RESPONSE_BYTES &&
            reply->error() == QNetworkReply::NoError &&
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200 &&
            reply->attribute(QNetworkRequest::RedirectionTargetAttribute).isNull();
        const auto peers = valid && Allowed(policy()) ? Parse(m_body, GetTime()) : std::vector<CAddress>();
        if (!peers.empty()) {
            m_node.addPeerAddresses(peers);
            qInfo() << "Explorer peers: offered" << peers.size() << "validated public mainnet peer hints to ordinary discovery.";
        } else {
            qWarning() << "Explorer peers: no usable response; retaining normal discovery.";
        }
        m_body.clear();
        m_reply.clear();
        reply->deleteLater();
    });
}
