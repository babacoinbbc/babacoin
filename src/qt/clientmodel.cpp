// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2014-2021 The Dash Core developers
// Copyright (c) 2020-2022 The Babacoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/clientmodel.h>
// Boost 1.73+: _1, _2 placeholders moved to boost::placeholders namespace
#include <boost/bind/bind.hpp>
using namespace boost::placeholders;


#include <qt/bantablemodel.h>
#include <qt/guiconstants.h>
#include <qt/guiutil.h>
#include <qt/peertablemodel.h>

#include <chain.h>
#include <chainparams.h>
#include <checkpoints.h>
#include <clientversion.h>
#include <interfaces/handler.h>
#include <interfaces/node.h>
#include <validation.h>
#include <net.h>
#include <txmempool.h>
#include <ui_interface.h>
#include <util.h>
#include <warnings.h>

#include <stdint.h>

#include <QDebug>
#include <QTimer>
#include <QThread>

static int64_t nLastHeaderTipUpdateNotification = 0;
static int64_t nLastBlockTipUpdateNotification = 0;

ClientModel::ClientModel(interfaces::Node& node, OptionsModel *_optionsModel, QObject *parent) :
    QObject(parent),
    m_node(node),
    optionsModel(_optionsModel),
    peerTableModel(0),
    banTableModel(0),
    pollTimer(0)
{
    cachedBestHeaderHeight = -1;
    cachedBestHeaderTime = -1;
    peerTableModel = new PeerTableModel(m_node, this);
    banTableModel = new BanTableModel(m_node, this);
    qRegisterMetaType<size_t>("size_t");
    subscribeToCoreSignals();
    const auto progress = m_node.getBlockImportProgress();
    updateBlockImportProgress(int(progress.phase), progress.processed_bytes, progress.total_bytes,
                             progress.new_blocks, progress.sequence);
    // InstantSend counting iterates a database. Mempool and chain queries can
    // wait for validation locks. None of these belong on the GUI thread.
    pollThread = new QThread(this);
    pollThread->setObjectName("clientModelPollThread");
    pollWorker = new QObject;
    pollWorker->moveToThread(pollThread);
    connect(pollThread, &QThread::finished, pollWorker, &QObject::deleteLater);
    connect(this, &ClientModel::requestedPoll, pollWorker, [this] { pollCore(); }, Qt::QueuedConnection);
    connect(pollThread, &QThread::started, pollWorker, [this] {
        if (stopping.load()) return;
        pollTimer = new QTimer(pollWorker);
        connect(pollTimer, &QTimer::timeout, pollWorker, [this] { pollCore(); });
        pollTimer->start(1000);
        pollCore();
    });
    pollThread->start();
}

ClientModel::~ClientModel()
{
    stopping.store(true);
    unsubscribeFromCoreSignals();
    pollThread->requestInterruption();
    pollThread->quit();
    // The application deletes ClientModel before shutting down the node/DBs.
    // Join here so no statistics task can outlive those resources.
    pollThread->wait();
    pollTimer = nullptr;
}

int ClientModel::getNumConnections(unsigned int flags) const
{
    CConnman::NumConnections connections = CConnman::CONNECTIONS_NONE;

    if(flags == CONNECTIONS_IN)
        connections = CConnman::CONNECTIONS_IN;
    else if (flags == CONNECTIONS_OUT)
        connections = CConnman::CONNECTIONS_OUT;
    else if (flags == CONNECTIONS_ALL)
        connections = CConnman::CONNECTIONS_ALL;

    return m_node.getNodeCount(connections);
}

void ClientModel::setSmartnodeList(const CDeterministicMNList& mnList)
{
    {
        LOCK(cs_mnlinst);
        if (mnListCached.GetBlockHash() == mnList.GetBlockHash()) return;
        mnListCached = mnList;
    }
    Q_EMIT smartnodeListChanged();
}

CDeterministicMNList ClientModel::getSmartnodeList() const
{
    LOCK(cs_mnlinst);
    return mnListCached;
}

void ClientModel::refreshSmartnodeList()
{
    // Do not hold the GUI cache mutex while asking the core for a list: core
    // callbacks can hold cs_main and need this cache mutex in the opposite order.
    smartnodeRefreshRequested.store(true);
}

int ClientModel::getHeaderTipHeight() const
{
    return cachedBestHeaderHeight;
}

int64_t ClientModel::getHeaderTipTime() const
{
    return cachedBestHeaderTime;
}

void ClientModel::updateTimer()
{
    Q_EMIT requestedPoll();
}

void ClientModel::pollCore()
{
    if (stopping.load()) return;
    const auto progress = m_node.getBlockImportProgress();
    QMetaObject::invokeMethod(this, "updateBlockImportProgress", Qt::QueuedConnection,
        Q_ARG(int, int(progress.phase)), Q_ARG(qulonglong, qulonglong(progress.processed_bytes)),
        Q_ARG(qulonglong, qulonglong(progress.total_bytes)), Q_ARG(int, progress.new_blocks),
        Q_ARG(qulonglong, qulonglong(progress.sequence)));
    {
        // A header batch or bootstrap import may hold cs_main for a long time.
        // Keep the last complete snapshot and retry rather than waiting.
        TRY_LOCK(cs_main, locked);
        if (locked) {
            int headerHeight;
            int64_t headerTime;
            if (m_node.getHeaderTip(headerHeight, headerTime)) {
                QMetaObject::invokeMethod(this, "updateTip", Qt::QueuedConnection,
                    Q_ARG(int, headerHeight), Q_ARG(QDateTime, QDateTime::fromTime_t(headerTime)),
                    Q_ARG(QString, QString()), Q_ARG(double, 0.0), Q_ARG(bool, true));
            }
            QMetaObject::invokeMethod(this, "updateTip", Qt::QueuedConnection,
                Q_ARG(int, m_node.getNumBlocks()), Q_ARG(QDateTime, QDateTime::fromTime_t(m_node.getLastBlockTime())),
                Q_ARG(QString, QString::fromStdString(m_node.getLastBlockHash())),
                Q_ARG(double, m_node.getVerificationProgress()), Q_ARG(bool, false));
            backgroundInitialSync = m_node.isInitialBlockDownload();
            if (!backgroundInitialSync && smartnodeRefreshRequested.exchange(false)) {
                setSmartnodeList(m_node.evo().getListAtChainTip());
            }
        }
    }
    if (stopping.load()) return;
    {
        TRY_LOCK(::mempool.cs, locked_mempool);
        if (locked_mempool) {
            QMetaObject::invokeMethod(this, "updateMempoolStats", Qt::QueuedConnection,
                Q_ARG(qulonglong, qulonglong(m_node.getMempoolSize())),
                Q_ARG(qulonglong, qulonglong(m_node.getMempoolDynamicUsage())));
        }
    }
    const int64_t now = GetTimeMillis();
    if (!stopping.load() && !backgroundInitialSync && now - lastLockCountPoll >= 5000) {
        const auto count = m_node.llmq().getInstantSentLockCount();
        QMetaObject::invokeMethod(this, "updateInstantLockCount", Qt::QueuedConnection,
            Q_ARG(qulonglong, qulonglong(count)));
        lastLockCountPoll = now;
    }
}

void ClientModel::updateMempoolStats(qulonglong count, qulonglong bytes)
{
    Q_EMIT mempoolSizeChanged(long(count), size_t(bytes));
}

void ClientModel::updateInstantLockCount(qulonglong count)
{
    Q_EMIT islockCountChanged(size_t(count));
}

void ClientModel::updateBlockImportProgress(int phase, qulonglong processed, qulonglong total, int newBlocks, qulonglong sequence)
{
    if (sequence < lastImportSequence) return;
    if (sequence == lastImportSequence && cachedImportProgress.phase == phase &&
        cachedImportProgress.processed == processed && cachedImportProgress.total == total &&
        cachedImportProgress.newBlocks == newBlocks) return;
    lastImportSequence = sequence;
    cachedImportProgress.phase = phase;
    cachedImportProgress.processed = processed;
    cachedImportProgress.total = total;
    cachedImportProgress.newBlocks = newBlocks;
    Q_EMIT blockImportProgress(phase, processed, total, newBlocks);
}

void ClientModel::updateTip(int count, const QDateTime& date, const QString& hash, double progress, bool header)
{
    if (header) {
        if (cachedBestHeaderHeight == count && cachedBestHeaderTime == date.toTime_t()) return;
        cachedBestHeaderHeight = count;
        cachedBestHeaderTime = date.toTime_t();
    } else {
        if (cachedBlockTip.height == count && cachedBlockTip.date == date &&
            cachedBlockTip.hash == hash && cachedBlockTip.verificationProgress == progress) return;
        cachedBlockTip.height = count;
        cachedBlockTip.date = date;
        cachedBlockTip.hash = hash;
        cachedBlockTip.verificationProgress = progress;
    }
    Q_EMIT numBlocksChanged(count, date, hash, progress, header);
}

void ClientModel::updateNumConnections(int numConnections)
{
    Q_EMIT numConnectionsChanged(numConnections);
}

void ClientModel::updateNetworkActive(bool networkActive)
{
    Q_EMIT networkActiveChanged(networkActive);
}

void ClientModel::updateAlert()
{
    Q_EMIT alertsChanged(getStatusBarWarnings());
}

enum BlockSource ClientModel::getBlockSource() const
{
    if (m_node.getReindex())
        return BlockSource::REINDEX;
    else if (m_node.getImporting())
        return BlockSource::DISK;
    else if (getNumConnections() > 0)
        return BlockSource::NETWORK;

    return BlockSource::NONE;
}

QString ClientModel::getStatusBarWarnings() const
{
    return QString::fromStdString(m_node.getWarnings("gui"));
}

OptionsModel *ClientModel::getOptionsModel()
{
    return optionsModel;
}

PeerTableModel *ClientModel::getPeerTableModel()
{
    return peerTableModel;
}

BanTableModel *ClientModel::getBanTableModel()
{
    return banTableModel;
}

QString ClientModel::formatFullVersion() const
{
    return QString::fromStdString(FormatFullVersion());
}

QString ClientModel::formatSubVersion() const
{
    return QString::fromStdString(strSubVersion);
}

bool ClientModel::isReleaseVersion() const
{
    return CLIENT_VERSION_IS_RELEASE;
}

QString ClientModel::formatClientStartupTime() const
{
    return QDateTime::fromTime_t(GetStartupTime()).toString();
}

QString ClientModel::dataDir() const
{
    return GUIUtil::boostPathToQString(GetDataDir());
}

void ClientModel::updateBanlist()
{
    banTableModel->refresh();
}

// Handlers for core signals
static void ShowProgress(ClientModel *clientmodel, const std::string &title, int nProgress)
{
    // emits signal "showProgress"
    QMetaObject::invokeMethod(clientmodel, "showProgress", Qt::QueuedConnection,
                              Q_ARG(QString, QString::fromStdString(title)),
                              Q_ARG(int, nProgress));
}

static void NotifyNumConnectionsChanged(ClientModel *clientmodel, int newNumConnections)
{
    // Too noisy: qDebug() << "NotifyNumConnectionsChanged: " + QString::number(newNumConnections);
    QMetaObject::invokeMethod(clientmodel, "updateNumConnections", Qt::QueuedConnection,
                              Q_ARG(int, newNumConnections));
}

static void NotifyNetworkActiveChanged(ClientModel *clientmodel, bool networkActive)
{
    QMetaObject::invokeMethod(clientmodel, "updateNetworkActive", Qt::QueuedConnection,
                              Q_ARG(bool, networkActive));
}

static void NotifyAlertChanged(ClientModel *clientmodel)
{
    qDebug() << "NotifyAlertChanged";
    QMetaObject::invokeMethod(clientmodel, "updateAlert", Qt::QueuedConnection);
}

static void BannedListChanged(ClientModel *clientmodel)
{
    qDebug() << QString("%1: Requesting update for peer banlist").arg(__func__);
    QMetaObject::invokeMethod(clientmodel, "updateBanlist", Qt::QueuedConnection);
}

static void BlockTipChanged(ClientModel *clientmodel, bool initialSync, int height, int64_t blockTime, const std::string& strBlockHash, double verificationProgress, bool fHeader)
{
    // lock free async UI updates in case we have a new block tip
    // during initial sync, only update the UI if the last update
    // was > 250ms (MODEL_UPDATE_DELAY) ago
    int64_t now = 0;
    if (initialSync)
        now = GetTimeMillis();

    int64_t& nLastUpdateNotification = fHeader ? nLastHeaderTipUpdateNotification : nLastBlockTipUpdateNotification;

    // if we are in-sync, update the UI regardless of last update time
    if (!initialSync || now - nLastUpdateNotification > MODEL_UPDATE_DELAY) {
        //pass an async signal to the UI thread
        QMetaObject::invokeMethod(clientmodel, "updateTip", Qt::QueuedConnection,
                                  Q_ARG(int, height),
                                  Q_ARG(QDateTime, QDateTime::fromTime_t(blockTime)),
                                  Q_ARG(QString, QString::fromStdString(strBlockHash)),
                                  Q_ARG(double, verificationProgress),
                                  Q_ARG(bool, fHeader));
        nLastUpdateNotification = now;
    }
}

static void NotifySmartnodeListChanged(ClientModel *clientmodel, const CDeterministicMNList& newList)
{
    clientmodel->setSmartnodeList(newList);
}

static void NotifyAdditionalDataSyncProgressChanged(ClientModel *clientmodel, double nSyncProgress)
{
    QMetaObject::invokeMethod(clientmodel, "additionalDataSyncProgressChanged", Qt::QueuedConnection,
                              Q_ARG(double, nSyncProgress));
}

void ClientModel::subscribeToCoreSignals()
{
    // Connect signals to client
    m_handler_show_progress = m_node.handleShowProgress(boost::bind(ShowProgress, this, _1, _2));
    m_handler_block_import_progress = m_node.handleBlockImportProgress([this](const BlockImportProgress& progress) {
        QMetaObject::invokeMethod(this, "updateBlockImportProgress", Qt::QueuedConnection,
            Q_ARG(int, int(progress.phase)), Q_ARG(qulonglong, qulonglong(progress.processed_bytes)),
            Q_ARG(qulonglong, qulonglong(progress.total_bytes)), Q_ARG(int, progress.new_blocks),
            Q_ARG(qulonglong, qulonglong(progress.sequence)));
    });
    m_handler_notify_num_connections_changed = m_node.handleNotifyNumConnectionsChanged(boost::bind(NotifyNumConnectionsChanged, this, _1));
    m_handler_notify_network_active_changed = m_node.handleNotifyNetworkActiveChanged(boost::bind(NotifyNetworkActiveChanged, this, _1));
    m_handler_notify_alert_changed = m_node.handleNotifyAlertChanged(boost::bind(NotifyAlertChanged, this));
    m_handler_banned_list_changed = m_node.handleBannedListChanged(boost::bind(BannedListChanged, this));
    m_handler_notify_block_tip = m_node.handleNotifyBlockTip(boost::bind(BlockTipChanged, this, _1, _2,_3, _4, _5, false));
    m_handler_notify_header_tip = m_node.handleNotifyHeaderTip(boost::bind(BlockTipChanged, this, _1, _2, _3, _4, _5, true));
    m_handler_notify_smartnodelist_changed = m_node.handleNotifySmartnodeListChanged(boost::bind(NotifySmartnodeListChanged, this, _1));
    m_handler_notify_additional_data_sync_progess_changed = m_node.handleNotifyAdditionalDataSyncProgressChanged(boost::bind(NotifyAdditionalDataSyncProgressChanged, this, _1));
}

void ClientModel::unsubscribeFromCoreSignals()
{
    // Disconnect signals from client
    m_handler_show_progress->disconnect();
    m_handler_block_import_progress->disconnect();
    m_handler_notify_num_connections_changed->disconnect();
    m_handler_notify_network_active_changed->disconnect();
    m_handler_notify_alert_changed->disconnect();
    m_handler_banned_list_changed->disconnect();
    m_handler_notify_block_tip->disconnect();
    m_handler_notify_header_tip->disconnect();
    m_handler_notify_smartnodelist_changed->disconnect();
    m_handler_notify_additional_data_sync_progess_changed->disconnect();
}
