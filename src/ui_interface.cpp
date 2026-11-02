// Copyright (c) 2010-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <ui_interface.h>
#include <util.h>

CClientUIInterface uiInterface;

void CClientUIInterface::ShowBlockImportProgress(const BlockImportProgress& progress)
{
    BlockImportProgress snapshot = progress;
    {
        std::lock_guard<std::mutex> lock(m_import_mutex);
        snapshot.sequence = m_import_progress.sequence + 1;
        m_import_progress = snapshot;
    }
    try {
        NotifyBlockImportProgress(snapshot);
    } catch (const std::exception& e) {
        LogPrintf("Block import display notification failed: %s\n", e.what());
    }
}

BlockImportProgress CClientUIInterface::GetBlockImportProgress() const
{
    std::lock_guard<std::mutex> lock(m_import_mutex);
    return m_import_progress;
}

bool InitError(const std::string& str)
{
    uiInterface.ThreadSafeMessageBox(str, "", CClientUIInterface::MSG_ERROR);
    return false;
}

void InitWarning(const std::string& str)
{
    uiInterface.ThreadSafeMessageBox(str, "", CClientUIInterface::MSG_WARNING);
}

std::string AmountHighWarn(const std::string& optname)
{
    return strprintf(_("%s is set very high!"), optname);
}

std::string AmountErrMsg(const char* const optname, const std::string& strValue)
{
    return strprintf(_("Invalid amount for -%s=<amount>: '%s'"), optname, strValue);
}
