// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2012-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_UI_INTERFACE_H
#define BITCOIN_UI_INTERFACE_H

#include <memory>
#include <mutex>
#include <stdint.h>
#include <string>
#include <algorithm>

#include <boost/signals2/last_value.hpp>
#include <boost/signals2/signal.hpp>

class CWallet;
class CBlockIndex;
class CDeterministicMNList;

/** Observer-only import status. Reading a file is not chain validation. */
struct BlockImportProgress
{
    enum Phase { IDLE = 0, READING = 1, VALIDATING = 2 };
    Phase phase;
    uint64_t processed_bytes;
    uint64_t total_bytes;
    int new_blocks;
    uint64_t sequence{0};
    BlockImportProgress(Phase phaseIn, uint64_t processed = 0, uint64_t total = 0, int blocks = 0)
        : phase(phaseIn), processed_bytes(processed), total_bytes(total), new_blocks(blocks) {}
};

/** Read-only block-position progress. Completion never implies wallet readiness. */
struct WalletScanProgress
{
    std::string wallet_name;
    int start_height;
    int current_height;
    int target_height;
    bool complete;
    int64_t totalBlocks() const { return std::max<int64_t>(0, int64_t(target_height) - start_height + 1); }
    int64_t scannedBlocks() const { return std::max<int64_t>(0, std::min<int64_t>(totalBlocks(), int64_t(current_height) - start_height + 1)); }
    int basisPoints() const
    {
        const int64_t total = totalBlocks();
        if (!total) return complete ? 10000 : 0;
        return int(std::min<int64_t>(complete ? 10000 : 9999, scannedBlocks() * 10000 / total));
    }
};

/** General change type (added, updated, removed). */
enum ChangeType
{
    CT_NEW,
    CT_UPDATED,
    CT_DELETED
};

/** Signals for UI communication. */
class CClientUIInterface
{
public:
    /** Flags for CClientUIInterface::ThreadSafeMessageBox */
    enum MessageBoxFlags
    {
        ICON_INFORMATION    = 0,
        ICON_WARNING        = (1U << 0),
        ICON_ERROR          = (1U << 1),
        /**
         * Mask of all available icons in CClientUIInterface::MessageBoxFlags
         * This needs to be updated, when icons are changed there!
         */
        ICON_MASK = (ICON_INFORMATION | ICON_WARNING | ICON_ERROR),

        /** These values are taken from qmessagebox.h "enum StandardButton" to be directly usable */
        BTN_OK      = 0x00000400U, // QMessageBox::Ok
        BTN_YES     = 0x00004000U, // QMessageBox::Yes
        BTN_NO      = 0x00010000U, // QMessageBox::No
        BTN_ABORT   = 0x00040000U, // QMessageBox::Abort
        BTN_RETRY   = 0x00080000U, // QMessageBox::Retry
        BTN_IGNORE  = 0x00100000U, // QMessageBox::Ignore
        BTN_CLOSE   = 0x00200000U, // QMessageBox::Close
        BTN_CANCEL  = 0x00400000U, // QMessageBox::Cancel
        BTN_DISCARD = 0x00800000U, // QMessageBox::Discard
        BTN_HELP    = 0x01000000U, // QMessageBox::Help
        BTN_APPLY   = 0x02000000U, // QMessageBox::Apply
        BTN_RESET   = 0x04000000U, // QMessageBox::Reset
        /**
         * Mask of all available buttons in CClientUIInterface::MessageBoxFlags
         * This needs to be updated, when buttons are changed there!
         */
        BTN_MASK = (BTN_OK | BTN_YES | BTN_NO | BTN_ABORT | BTN_RETRY | BTN_IGNORE |
                    BTN_CLOSE | BTN_CANCEL | BTN_DISCARD | BTN_HELP | BTN_APPLY | BTN_RESET),

        /** Force blocking, modal message box dialog (not just OS notification) */
        MODAL               = 0x10000000U,

        /** Do not print contents of message to debug log */
        SECURE              = 0x40000000U,

        /** Predefined combinations for certain default usage cases */
        MSG_INFORMATION = ICON_INFORMATION,
        MSG_WARNING = (ICON_WARNING | BTN_OK | MODAL),
        MSG_ERROR = (ICON_ERROR | BTN_OK | MODAL)
    };

    /** Show message box. */
    boost::signals2::signal<bool (const std::string& message, const std::string& caption, unsigned int style), boost::signals2::last_value<bool> > ThreadSafeMessageBox;

    /** If possible, ask the user a question. If not, falls back to ThreadSafeMessageBox(noninteractive_message, caption, style) and returns false. */
    boost::signals2::signal<bool (const std::string& message, const std::string& noninteractive_message, const std::string& caption, unsigned int style), boost::signals2::last_value<bool> > ThreadSafeQuestion;

    /** Progress message during initialization. */
    boost::signals2::signal<void (const std::string &message)> InitMessage;
    boost::signals2::signal<void (const WalletScanProgress&)> NotifyWalletScanProgress;
    boost::signals2::signal<void (const BlockImportProgress&)> NotifyBlockImportProgress;
    void ShowBlockImportProgress(const BlockImportProgress& progress);
    BlockImportProgress GetBlockImportProgress() const;

    /** Number of network connections changed. */
    boost::signals2::signal<void (int newNumConnections)> NotifyNumConnectionsChanged;

    /** Network activity state changed. */
    boost::signals2::signal<void (bool networkActive)> NotifyNetworkActiveChanged;

    /**
     * Status bar alerts changed.
     */
    boost::signals2::signal<void ()> NotifyAlertChanged;

    /** A wallet has been loaded. */
    boost::signals2::signal<void (std::shared_ptr<CWallet> wallet)> LoadWallet;

    /**
     * Show progress e.g. for verifychain.
     * resume_possible indicates shutting down now will result in the current progress action resuming upon restart.
     */
    boost::signals2::signal<void (const std::string &title, int nProgress, bool resume_possible)> ShowProgress;

    /** New block has been accepted */
    boost::signals2::signal<void (bool, const CBlockIndex *)> NotifyBlockTip;

    /** Best header has changed */
    boost::signals2::signal<void (bool, const CBlockIndex *)> NotifyHeaderTip;

    /** Smartnode list has changed */
    boost::signals2::signal<void (const CDeterministicMNList&)> NotifySmartnodeListChanged;

    /** Additional data sync progress changed */
    boost::signals2::signal<void (double nSyncProgress)> NotifyAdditionalDataSyncProgressChanged;

    /** Banlist did change. */
    boost::signals2::signal<void (void)> BannedListChanged;
private:
    mutable std::mutex m_import_mutex;
    BlockImportProgress m_import_progress{BlockImportProgress::IDLE};
};

/** Show warning message **/
void InitWarning(const std::string& str);

/** Show error message **/
bool InitError(const std::string& str);

std::string AmountHighWarn(const std::string& optname);

std::string AmountErrMsg(const char* const optname, const std::string& strValue);

extern CClientUIInterface uiInterface;

#endif // BITCOIN_UI_INTERFACE_H
