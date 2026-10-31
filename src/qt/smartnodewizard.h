// Copyright (c) 2026 The Babacoin developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#ifndef BABACOIN_QT_SMARTNODEWIZARD_H
#define BABACOIN_QT_SMARTNODEWIZARD_H

#include <amount.h>
#include <primitives/transaction.h>
#include <QPointer>
#include <QWizard>
#include <functional>
#include <string>
#include <vector>

class WalletModel;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QCheckBox;
class UniValue;

namespace SmartnodeSetup {
/** Reject any funding transaction that spends collateral or sends change elsewhere. */
bool CheckFunding(const CMutableTransaction& tx, const COutPoint& collateral,
                  const CScript& changeScript, CAmount inputAmount, CAmount maxFee, CAmount& fee);
}

/** Self-operated smartnode registration using existing collateral and local keys.
 * The remote operator secret is never requested; only its public BLS key is used.
 */
class SmartnodeWizard final : public QWizard
{
    Q_OBJECT
public:
    using FundCollateral = std::function<void(const QString&, CAmount)>;
    SmartnodeWizard(WalletModel* wallet, FundCollateral fundCollateral, QWidget* parent = nullptr);
    ~SmartnodeWizard() override;
    void accept() override;

protected:
    bool validateCurrentPage() override;
    void initializePage(int id) override;

private:
    struct Candidate { COutPoint outpoint; QString address; CAmount amount{0}; };
    UniValue rpc(const std::string& method, const UniValue& parameters);
    void refreshCollateral();
    void releasePreparation();
    bool checkReadiness();
    bool prepare();
    bool verifyPrepared();
    void exportServerTemplate();
    void showError(const QString& message);
    QPointer<WalletModel> m_wallet;
    FundCollateral m_fundCollateral;
    QComboBox* m_collateral;
    QComboBox* m_feeSource;
    QLabel* m_readiness;
    QLineEdit* m_service;
    QLineEdit* m_owner;
    QLineEdit* m_payout;
    QLineEdit* m_operator;
    QPlainTextEdit* m_review;
    QCheckBox* m_consent;
    std::vector<Candidate> m_candidates;
    Candidate m_selected;
    std::vector<COutPoint> m_temporaryLocks;
    QString m_signedHex;
    QString m_unsignedHex;
    CAmount m_fee{0};
    CAmount m_required{0};
    bool m_submissionAttempted{false};
    bool m_submitted{false};
};
#endif
