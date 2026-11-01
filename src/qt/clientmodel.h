// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2014-2021 The Dash Core developers
// Copyright (c) 2020-2022 The Babacoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_CLIENTMODEL_H
#define BITCOIN_QT_CLIENTMODEL_H

#include <evo/deterministicmns.h>
#include <interfaces/node.h>
#include <sync.h>

#include <QObject>
#include <QString>
#include <QDateTime>

#include <atomic>
#include <memory>

class BanTableModel;
class OptionsModel;
class PeerTableModel;

class CBlockIndex;

QT_BEGIN_NAMESPACE
class QTimer;
class QThread;
QT_END_NAMESPACE

enum class BlockSource {
    NONE,
    REINDEX,
    DISK,
    NETWORK
};

enum NumConnections {
    CONNECTIONS_NONE = 0,
    CONNECTIONS_IN   = (1U << 0),
    CONNECTIONS_OUT  = (1U << 1),
    CONNECTIONS_ALL  = (CONNECTIONS_IN | CONNECTIONS_OUT),
};

/** Model for Babacoin network client. */
class ClientModel : public QObject
{
    Q_OBJECT

public:
    explicit ClientModel(interfaces::Node& node, OptionsModel *optionsModel, QObject *parent = 0);
    ~ClientModel();

    interfaces::Node& node() const { return m_node; }
    interfaces::Smartnode::Sync& smartnodeSync() const { return m_node.smartnodeSync(); }
#ifdef ENABLE_WALLET
    interfaces::CoinJoin::Options& coinJoinOptions() const { return m_node.coinJoinOptions(); }
#endif
    OptionsModel *getOptionsModel();
    PeerTableModel *getPeerTableModel();
    BanTableModel *getBanTableModel();

    //! Return number of connections, default is in- and outbound (total)
    int getNumConnections(unsigned int flags = CONNECTIONS_ALL) const;
    int getHeaderTipHeight() const;
    int64_t getHeaderTipTime() const;

    // GUI-thread snapshot only. Never acquire a validation lock while painting
    // or handling a connection/status update.
    struct BlockTip {
        int height{-1};
        QDateTime date;
        QString hash;
        double verificationProgress{0};
    };
    const BlockTip& getCachedBlockTip() const { return cachedBlockTip; }
    struct ImportStatus {
        int phase{0};
        qulonglong processed{0};
        qulonglong total{0};
        int newBlocks{0};
    };
    const ImportStatus& getCachedImportProgress() const { return cachedImportProgress; }

    void setSmartnodeList(const CDeterministicMNList& mnList);
    CDeterministicMNList getSmartnodeList() const;
    void refreshSmartnodeList();

    //! Returns enum BlockSource of the current importing/syncing state
    enum BlockSource getBlockSource() const;
    //! Return warnings to be displayed in status bar
    QString getStatusBarWarnings() const;

    QString formatFullVersion() const;
    QString formatSubVersion() const;
    bool isReleaseVersion() const;
    QString formatClientStartupTime() const;
    QString dataDir() const;

    // caches for the best header
    mutable std::atomic<int> cachedBestHeaderHeight;
    mutable std::atomic<int64_t> cachedBestHeaderTime;

private:
    interfaces::Node& m_node;
    std::unique_ptr<interfaces::Handler> m_handler_show_progress;
    std::unique_ptr<interfaces::Handler> m_handler_block_import_progress;
    std::unique_ptr<interfaces::Handler> m_handler_notify_num_connections_changed;
    std::unique_ptr<interfaces::Handler> m_handler_notify_network_active_changed;
    std::unique_ptr<interfaces::Handler> m_handler_notify_alert_changed;
    std::unique_ptr<interfaces::Handler> m_handler_banned_list_changed;
    std::unique_ptr<interfaces::Handler> m_handler_notify_block_tip;
    std::unique_ptr<interfaces::Handler> m_handler_notify_header_tip;
    std::unique_ptr<interfaces::Handler> m_handler_notify_smartnodelist_changed;
    std::unique_ptr<interfaces::Handler> m_handler_notify_additional_data_sync_progess_changed;
    OptionsModel *optionsModel;
    PeerTableModel *peerTableModel;
    BanTableModel *banTableModel;

    QTimer *pollTimer;
    QThread* pollThread{nullptr};
    QObject* pollWorker{nullptr};
    std::atomic<bool> stopping{false};
    std::atomic<bool> smartnodeRefreshRequested{true};
    int64_t lastLockCountPoll{0}; // worker thread only
    bool backgroundInitialSync{true}; // worker thread only
    BlockTip cachedBlockTip; // GUI thread only
    ImportStatus cachedImportProgress; // GUI thread only
    qulonglong lastImportSequence{0};
    void pollCore();

    // The cache for mn list is not technically needed because CDeterministicMNManager
    // caches it internally for recent blocks but it's not enough to get consistent
    // representation of the list in UI during initial sync/reindex, so we cache it here too.
    mutable CCriticalSection cs_mnlinst; // protects mnListCached
    CDeterministicMNList mnListCached;

    void subscribeToCoreSignals();
    void unsubscribeFromCoreSignals();

Q_SIGNALS:
    void requestedPoll();
    void blockImportProgress(int phase, qulonglong processed, qulonglong total, int newBlocks);
    void numConnectionsChanged(int count);
    void smartnodeListChanged() const;
    void numBlocksChanged(int count, const QDateTime& blockDate, const QString& blockHash, double nVerificationProgress, bool header);
    void additionalDataSyncProgressChanged(double nSyncProgress);
    void mempoolSizeChanged(long count, size_t mempoolSizeInBytes);
    void islockCountChanged(size_t count);
    void networkActiveChanged(bool networkActive);
    void alertsChanged(const QString &warnings);

    //! Fired when a message should be reported to the user
    void message(const QString &title, const QString &message, unsigned int style);

    // Show progress dialog e.g. for verifychain
    void showProgress(const QString &title, int nProgress);

public Q_SLOTS:
    void updateTip(int count, const QDateTime& date, const QString& hash, double progress, bool header);
    void updateMempoolStats(qulonglong count, qulonglong bytes);
    void updateInstantLockCount(qulonglong count);
    void updateBlockImportProgress(int phase, qulonglong processed, qulonglong total, int newBlocks, qulonglong sequence);
    void updateTimer();
    void updateNumConnections(int numConnections);
    void updateNetworkActive(bool networkActive);
    void updateAlert();
    void updateBanlist();
};

#endif // BITCOIN_QT_CLIENTMODEL_H
