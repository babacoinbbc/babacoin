// Copyright (c) 2026 The Babacoin developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#ifndef BABACOIN_QT_EXPLORERPEERS_H
#define BABACOIN_QT_EXPLORERPEERS_H

#include <protocol.h>
#include <QByteArray>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <string>
#include <vector>

namespace interfaces { class Node; }
class QNetworkReply;

/** Opt-in additional peer hints. Never grants manual-peer or consensus trust. */
class ExplorerPeers final : public QObject
{
public:
    struct Policy {
        bool enabled{false};
        std::string network{"main"};
        bool active{true};
        bool shuttingDown{false};
        bool explicitPeers{false};
        bool restrictedNetworks{false};
        bool proxy{false};
        bool dnsSeed{true};
        int64_t maxConnections{1};
    };
    static constexpr int MAX_RESPONSE_BYTES = 65536;
    static constexpr size_t MAX_PEERS = 16;
    static bool Allowed(const Policy& policy);
    static std::vector<CAddress> Parse(const QByteArray& body, int64_t now);

    explicit ExplorerPeers(interfaces::Node& node, QObject* parent = nullptr);
    ~ExplorerPeers() override;
    void setEnabled(bool enabled);
    void stop();

private:
    Policy policy() const;
    void fetch();
    interfaces::Node& m_node;
    QNetworkAccessManager m_manager;
    QTimer m_timer;
    QPointer<QNetworkReply> m_reply;
    QByteArray m_body;
    bool m_enabled{false};
    bool m_oversized{false};
};
#endif
