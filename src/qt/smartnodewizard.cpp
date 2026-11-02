// Copyright (c) 2026 The Babacoin developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#include <qt/smartnodewizard.h>

#include <qt/bitcoinunits.h>
#include <qt/guiutil.h>
#include <qt/walletmodel.h>
#include <chainparams.h>
#include <chainparamsbase.h>
#include <core_io.h>
#include <consensus/validation.h>
#include <evo/deterministicmns.h>
#include <evo/providertx.h>
#include <evo/specialtx.h>
#include <interfaces/node.h>
#include <interfaces/wallet.h>
#include <key_io.h>
#include <messagesigner.h>
#include <netbase.h>
#include <rpc/server.h>
#include <script/standard.h>
#include <utilstrencodings.h>
#include <validation.h>

#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTemporaryFile>
#include <QTextStream>
#include <QUrl>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QScreen>
#include <QGuiApplication>
#include <QWindow>
#include <set>
#include <algorithm>
#include <climits>

namespace {
#if defined(Q_OS_WIN) || defined(Q_OS_MAC)
constexpr bool DESKTOP_LAYOUT = true;
#else
constexpr bool DESKTOP_LAYOUT = false;
#endif
class ScrollableSetupPage : public QWizardPage {
    QWidget* m_body;
public:
    ScrollableSetupPage(const QString& title, const QString& description)
    {
#if !defined(Q_OS_WIN) && !defined(Q_OS_MAC)
        setTitle(title);
        setSubTitle(description);
        m_body = this;
        return;
#endif
        // Keep headings inside the themed, scrollable content. The native
        // ModernStyle header paints a white panel with dark-theme text.
        auto* outer = new QVBoxLayout(this);
        outer->setContentsMargins(0, 0, 0, 0);
        auto* scroll = new QScrollArea(this);
        scroll->setObjectName("smartnodePageScroll");
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto* content = new QWidget;
        content->setObjectName("smartnodePageContent");
        auto* layout = new QVBoxLayout(content);
        layout->setContentsMargins(20, 18, 20, 18);
        layout->setSpacing(12);
        auto* heading = new QLabel(title, content);
        heading->setObjectName("smartnodePageTitle");
        heading->setWordWrap(true);
        auto* hint = new QLabel(description, content);
        hint->setObjectName("smartnodePageDescription");
        hint->setWordWrap(true);
        layout->addWidget(heading);
        layout->addWidget(hint);
        m_body = new QWidget(content);
        m_body->setObjectName("smartnodePageBody");
        layout->addWidget(m_body, 1);
        scroll->setWidget(content);
        outer->addWidget(scroll);
        setAccessibleName(title);
        GUIUtil::setFont({heading}, GUIUtil::FontWeight::Bold, 16);
        GUIUtil::setFont({hint}, GUIUtil::FontWeight::Normal, 10);
    }
    QWidget* body() const { return m_body; }
};
class CollateralPage final : public ScrollableSetupPage {
    bool m_ready{false};
public:
    using ScrollableSetupPage::ScrollableSetupPage;
    bool isComplete() const override { return m_ready; }
    void setReady(bool ready) { m_ready = ready; Q_EMIT completeChanged(); }
};
QString BBC(CAmount amount) {
    return DESKTOP_LAYOUT ? BitcoinUnits::formatLocalizedWithUnit(BitcoinUnits::BBC, amount)
                          : BitcoinUnits::formatWithUnit(BitcoinUnits::BBC, amount);
}
UniValue ParamsArray() { return UniValue(UniValue::VARR); }
QString Failure(const UniValue& error) {
    return error.isObject() && error.exists("message") ? QString::fromStdString(error["message"].get_str()) : QObject::tr("RPC request failed.");
}
}

bool SmartnodeSetup::CheckFunding(const CMutableTransaction& tx, const COutPoint& collateral,
                                 const CScript& changeScript, CAmount inputAmount, CAmount maxFee, CAmount& fee)
{
    fee = 0;
    if (tx.nType != TRANSACTION_PROVIDER_REGISTER || tx.nVersion != 3 || tx.vin.empty() ||
        tx.vout.empty() || !MoneyRange(inputAmount) || inputAmount == 0 || maxFee <= 0) return false;
    std::set<COutPoint> seen;
    for (const auto& input : tx.vin) {
        if (input.prevout == collateral || !seen.insert(input.prevout).second) return false;
    }
    CAmount outputAmount = 0;
    for (const auto& output : tx.vout) {
        if (!MoneyRange(output.nValue) || output.nValue > inputAmount - outputAmount) return false;
        if (output.scriptPubKey != changeScript && !(output.nValue == 0 && output.scriptPubKey == (CScript() << OP_RETURN))) return false;
        outputAmount += output.nValue;
    }
    const CAmount difference = inputAmount - outputAmount;
    if (difference <= 0 || difference > maxFee) return false;
    fee = difference;
    return true;
}

SmartnodeWizard::SmartnodeWizard(WalletModel* wallet, FundCollateral fundCollateral, QWidget* parent) :
    QWizard(parent), m_wallet(wallet), m_fundCollateral(std::move(fundCollateral))
{
    setObjectName("smartnodeSetupWizard");
    setWindowTitle(tr("Smartnode setup wizard"));
#if defined(Q_OS_WIN) || defined(Q_OS_MAC)
    setProperty("desktopWizard", true);
    setWizardStyle(QWizard::ClassicStyle);
    setMinimumSize(420, 320);
    QScreen* screen = parent && parent->windowHandle() ? parent->windowHandle()->screen() : QGuiApplication::primaryScreen();
    resize(screen ? QSize(740, 580).boundedTo(screen->availableGeometry().size() - QSize(48, 64)) : QSize(740, 580));
#else
    setWizardStyle(QWizard::ModernStyle);
    setMinimumSize(740, 580);
#endif
    setOption(QWizard::NoBackButtonOnStartPage);
    setButtonText(QWizard::FinishButton, tr("Register smartnode"));

    auto* collateralPage = new CollateralPage(tr("1. Check collateral and synchronization"),
        tr("Use an existing confirmed collateral output. Funding a new output uses the normal Send confirmation screen."));
    auto* collateralLayout = new QVBoxLayout(collateralPage->body());
    if (DESKTOP_LAYOUT) {
        collateralLayout->setContentsMargins(0, 0, 0, 0);
        collateralLayout->setSpacing(12);
    }
    m_readiness = new QLabel;
    m_readiness->setWordWrap(true);
    m_readiness->setTextInteractionFlags(Qt::TextSelectableByMouse);
    collateralLayout->addWidget(m_readiness);
    m_collateralAmount = new QComboBox;
    m_collateralAmount->setObjectName("smartnodeCollateralAmount");
    m_collateralAmount->setAccessibleName(tr("Collateral amount"));
    if (DESKTOP_LAYOUT) {
        m_collateralAmount->setMinimumContentsLength(20);
        m_collateralAmount->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        m_collateralAmount->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    }
    collateralLayout->addWidget(m_collateralAmount);
    connect(m_collateralAmount, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { refreshCollateral(); });
    m_collateral = new QComboBox;
    m_collateral->setObjectName("smartnodeCollateral");
    m_collateral->setMinimumContentsLength(25);
    if (DESKTOP_LAYOUT) {
        m_collateral->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        m_collateral->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    }
    collateralLayout->addWidget(m_collateral);
    auto* refresh = new QPushButton(tr("Refresh eligible outputs"));
    if (DESKTOP_LAYOUT) refresh->setText(tr("Refresh outputs"));
    collateralLayout->addWidget(refresh);
    connect(refresh, &QPushButton::clicked, this, [this] { refreshCollateral(); });
    auto* fund = new QPushButton(tr("Prepare a collateral transfer in Send"));
    fund->setToolTip(fund->text());
    if (DESKTOP_LAYOUT) fund->setText(tr("Prepare collateral transfer"));
    fund->setObjectName("smartnodePrepareCollateral");
    collateralLayout->addWidget(fund);
    connect(fund, &QPushButton::clicked, this, [this] {
        if (!m_wallet || m_required <= 0 || !m_fundCollateral) return;
        try {
            const int height = m_wallet->node().getNumBlocks();
            const auto& schedule = Params().GetConsensus().nCollaterals;
            if (m_wallet->node().isInitialBlockDownload() || !schedule.isPayableCollateral(height, m_required) ||
                !schedule.isPayableCollateral(height + 1, m_required))
                throw std::runtime_error("Wait for synchronization and refresh the current collateral requirement before funding.");
            auto unlock = m_wallet->requestUnlock();
            if (!unlock.isValid()) return;
            auto parameters = ParamsArray();
            parameters.push_back("Smartnode collateral");
            const QString address = QString::fromStdString(rpc("getnewaddress", parameters).get_str());
            reject();
            m_fundCollateral(address, m_required);
        } catch (const UniValue& error) { showError(Failure(error)); }
          catch (const std::exception& error) { showError(QString::fromUtf8(error.what())); }
    });
    auto* collateralHint = new QLabel(tr("Keep a wallet backup and separate fee funds. Historical collateral amounts are not automatically converted. Existing registered collateral is excluded from this new-node wizard."));
    collateralHint->setWordWrap(true);
    collateralLayout->addWidget(collateralHint);
    collateralLayout->addStretch();
    addPage(collateralPage);

    auto* detailsPage = new ScrollableSetupPage(tr("2. Server and public identities"),
        tr("Self-operated node, 0% operator reward. Keep the server's BLS secret on the server; paste only its public key here."));
    auto* details = new QFormLayout(detailsPage->body());
    if (DESKTOP_LAYOUT) {
        details->setContentsMargins(0, 0, 0, 0);
        details->setSpacing(12);
        details->setRowWrapPolicy(QFormLayout::WrapLongRows);
        details->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    }
    m_service = new QLineEdit;
    m_service->setObjectName("smartnodeService");
    m_service->setPlaceholderText(QString("203.0.113.10:%1").arg(Params().GetDefaultPort()));
    details->addRow(tr("Server IPv4 and P2P port"), m_service);
    m_operator = new QLineEdit;
    m_operator->setObjectName("smartnodeOperatorPublic");
    m_operator->setMaxLength(96);
    m_operator->setPlaceholderText(tr("96 hexadecimal characters: public BLS key"));
    details->addRow(tr("Operator public key"), m_operator);
    m_owner = new QLineEdit;
    m_owner->setObjectName("smartnodeOwner");
    details->addRow(tr("Owner / voting address"), m_owner);
    m_payout = new QLineEdit;
    m_payout->setObjectName("smartnodePayout");
    details->addRow(tr("Reward payout address"), m_payout);
    auto* newAddresses = new QPushButton(tr("Create new owner and payout addresses in this wallet"));
    newAddresses->setToolTip(newAddresses->text());
    if (DESKTOP_LAYOUT) newAddresses->setText(tr("Create wallet addresses"));
    details->addRow(newAddresses);
    connect(newAddresses, &QPushButton::clicked, this, [this] {
        if (!m_wallet) return;
        try {
            auto unlock = m_wallet->requestUnlock();
            if (!unlock.isValid()) return;
            auto owner = ParamsArray(); owner.push_back("Smartnode owner");
            auto payout = ParamsArray(); payout.push_back("Smartnode rewards");
            m_owner->setText(QString::fromStdString(rpc("getnewaddress", owner).get_str()));
            m_payout->setText(QString::fromStdString(rpc("getnewaddress", payout).get_str()));
        } catch (const UniValue& error) { showError(Failure(error)); }
          catch (const std::exception& error) { showError(QString::fromUtf8(error.what())); }
    });
    m_feeSource = new QComboBox;
    m_feeSource->setEditable(true);
    m_feeSource->setObjectName("smartnodeFeeSource");
    details->addRow(tr("Fee source address"), m_feeSource);
    auto* serverHelp = new QLabel(tr("On your separately installed and synchronized server, run babacoin-cli bls generate. Store the secret there and paste the public result above. The wizard does not access SSH, install a remote OS or request a private key."));
    serverHelp->setWordWrap(true);
    details->addRow(serverHelp);
    auto* serverTemplate = new QPushButton(tr("Save public server configuration template"));
    serverTemplate->setToolTip(serverTemplate->text());
    if (DESKTOP_LAYOUT) serverTemplate->setText(tr("Save server template"));
    details->addRow(serverTemplate);
    connect(serverTemplate, &QPushButton::clicked, this, [this] { exportServerTemplate(); });
    addPage(detailsPage);

    auto* reviewPage = new ScrollableSetupPage(tr("3. Review the prepared registration"),
        tr("A signed transaction is prepared locally. Nothing is broadcast until the final confirmation."));
    auto* reviewLayout = new QVBoxLayout(reviewPage->body());
    if (DESKTOP_LAYOUT) reviewLayout->setContentsMargins(0, 0, 0, 0);
    m_review = new QPlainTextEdit;
    m_review->setObjectName("smartnodeRegistrationReview");
    m_review->setReadOnly(true);
    reviewLayout->addWidget(m_review);
    const QString consent = tr("I reviewed the collateral, server, addresses and fee, and want to broadcast this registration.");
    if (DESKTOP_LAYOUT) {
        auto* consentExplanation = new QLabel(consent);
        consentExplanation->setWordWrap(true);
        reviewLayout->addWidget(consentExplanation);
    }
    m_consent = new QCheckBox(DESKTOP_LAYOUT ? tr("Confirm registration") : consent);
    m_consent->setAccessibleDescription(consent);
    m_consent->setObjectName("smartnodeRegistrationConsent");
    reviewLayout->addWidget(m_consent);
    addPage(reviewPage);
    refreshCollateral();
    GUIUtil::updateFonts();
}

SmartnodeWizard::~SmartnodeWizard() { releasePreparation(); }

UniValue SmartnodeWizard::rpc(const std::string& method, const UniValue& parameters)
{
    if (!m_wallet) throw std::runtime_error("The selected wallet is no longer loaded.");
    const std::string uri = "/wallet/" + QUrl::toPercentEncoding(m_wallet->getWalletName()).toStdString();
    return m_wallet->node().executeRpc(method, parameters, uri);
}

void SmartnodeWizard::showError(const QString& message)
{
    QMessageBox::warning(this, tr("Smartnode setup"), message);
}

void SmartnodeWizard::releasePreparation()
{
    if (m_wallet) {
        for (const auto& outpoint : m_temporaryLocks) {
            // An attempted broadcast may have been accepted even if its result is unclear.
            if (m_submissionAttempted && outpoint == m_selected.outpoint) continue;
            m_wallet->wallet().unlockCoin(outpoint);
        }
    }
    m_temporaryLocks.clear();
    m_signedHex.clear();
    m_unsignedHex.clear();
    m_fee = 0;
}

void SmartnodeWizard::refreshCollateral()
{
    releasePreparation();
    m_candidates.clear();
    m_collateral->clear();
    m_feeSource->clear();
    static_cast<CollateralPage*>(page(0))->setReady(false);
    if (!m_wallet) { m_readiness->setText(tr("No wallet is loaded.")); return; }
    const int height = m_wallet->node().getNumBlocks();
    const auto& consensus = Params().GetConsensus();
    {
        const QSignalBlocker blocker(m_collateralAmount);
        const CAmount selected = m_collateralAmount->currentData().toLongLong();
        const auto& schedule = consensus.nCollaterals;
        const CAmount current = schedule.getCollateral(std::max(0, height));
        const CAmount finalTier = schedule.getCollateral(INT_MAX);
        m_collateralAmount->clear();
        m_collateralAmount->addItem(BBC(current), QVariant::fromValue<qlonglong>(current));
        if (finalTier != current && schedule.isPayableCollateral(height, finalTier) &&
            schedule.isPayableCollateral(height + 1, finalTier)) {
            m_collateralAmount->addItem(tr("%1 — register ahead of the collateral transition").arg(BBC(finalTier)),
                                        QVariant::fromValue<qlonglong>(finalTier));
        }
        const int previous = m_collateralAmount->findData(QVariant::fromValue<qlonglong>(selected));
        if (previous >= 0) m_collateralAmount->setCurrentIndex(previous);
        m_required = m_collateralAmount->currentData().toLongLong();
    }
    m_readiness->setText(tr("Network: %1\nCurrent height: %2\nSelected collateral: %3\nMinimum confirmations: %4\nA registration fee is separate from the collateral.")
        .arg(QString::fromStdString(m_wallet->node().getNetwork())).arg(height).arg(BBC(m_required)).arg(consensus.nSmartnodeMinimumConfirmations));
    if (Params().NetworkIDString() == CBaseChainParams::MAIN &&
        m_required != consensus.nCollaterals.getCollateral(std::max(0, height))) {
        m_readiness->setText(m_readiness->text() + "\n" +
            tr("Early final-tier registration requires upgraded network validators. Older nodes may reject its block."));
    }
    std::vector<COutPoint> registered;
    m_wallet->wallet().listProTxCoins(registered);
    const std::set<COutPoint> used(registered.begin(), registered.end());
    std::set<QString> feeAddresses;
    for (const auto& group : m_wallet->wallet().listCoins()) {
        for (const auto& coin : group.second) {
            const auto& outpoint = std::get<0>(coin);
            const auto& output = std::get<1>(coin);
            if (output.is_spent || !m_wallet->wallet().isSpendable(output.txout.scriptPubKey)) continue;
            CTxDestination destination;
            if (!ExtractDestination(output.txout.scriptPubKey, destination)) continue;
            const QString address = QString::fromStdString(EncodeDestination(destination));
            if (output.txout.nValue == m_required && output.depth_in_main_chain >= consensus.nSmartnodeMinimumConfirmations &&
                !used.count(outpoint) && boost::get<CKeyID>(&destination)) {
                m_candidates.push_back({outpoint, address, output.txout.nValue});
                m_collateral->addItem(QString("%1 — %2:%3 (%4 confirmations)").arg(BBC(output.txout.nValue))
                    .arg(QString::fromStdString(outpoint.hash.ToString())).arg(outpoint.n).arg(output.depth_in_main_chain));
            }
            if (output.depth_in_main_chain > 0 && !consensus.nCollaterals.isValidCollateral(output.txout.nValue) &&
                !m_wallet->wallet().isLockedCoin(outpoint)) feeAddresses.insert(address);
        }
    }
    for (const auto& address : feeAddresses) m_feeSource->addItem(address);
    if (m_candidates.empty()) m_collateral->addItem(tr("No eligible confirmed collateral output found"));
    static_cast<CollateralPage*>(page(0))->setReady(!m_candidates.empty() && !m_wallet->node().isInitialBlockDownload());
}

bool SmartnodeWizard::checkReadiness()
{
    if (!m_wallet) throw std::runtime_error("The selected wallet is no longer loaded.");
    auto& node = m_wallet->node();
    if (node.shutdownRequested() || node.getImporting() || node.getReindex() || node.isInitialBlockDownload())
        throw std::runtime_error("Wait for synchronization and database processing to finish.");
    const int height = node.getNumBlocks();
    const auto& consensus = Params().GetConsensus();
    const CAmount required = m_required;
    if (required <= 0 || !consensus.nCollaterals.isPayableCollateral(height, required) ||
        !consensus.nCollaterals.isPayableCollateral(height + 1, required))
        throw std::runtime_error("The collateral schedule is changing. Refresh after the next block and review again.");
    if (m_selected.amount != required) throw std::runtime_error("The selected collateral amount is no longer current.");
    const auto outputs = m_wallet->wallet().getCoins({m_selected.outpoint});
    if (outputs.size() != 1 || outputs[0].is_spent || outputs[0].txout.nValue != required ||
        outputs[0].depth_in_main_chain < consensus.nSmartnodeMinimumConfirmations ||
        !m_wallet->wallet().isSpendable(outputs[0].txout.scriptPubKey))
        throw std::runtime_error("The collateral is spent, immature, unavailable or not controlled by this wallet.");
    auto parameters = ParamsArray();
    parameters.push_back(m_selected.outpoint.hash.ToString());
    parameters.push_back(int64_t(m_selected.outpoint.n));
    const auto liveCoin = rpc("gettxout", parameters);
    if (liveCoin.isNull() || AmountFromValue(liveCoin["value"]) != required ||
        liveCoin["confirmations"].get_int() < consensus.nSmartnodeMinimumConfirmations)
        throw std::runtime_error("The collateral is no longer a confirmed unspent output.");
    LOCK(cs_main);
    const auto list = deterministicMNManager->GetListAtChainTip();
    if (list.GetMNByCollateral(m_selected.outpoint)) throw std::runtime_error("This collateral already belongs to a registered smartnode.");
    return true;
}

bool SmartnodeWizard::validateCurrentPage()
{
    try {
        if (m_submitted) return true; // QWizard::done validates again when closing after a successful send.
        if (m_submissionAttempted) throw std::runtime_error("Close this wizard and check the previous submission before starting another registration.");
        if (currentId() == 0) {
            const int index = m_collateral->currentIndex();
            if (index < 0 || size_t(index) >= m_candidates.size()) throw std::runtime_error("Select an eligible output, or prepare a collateral transfer and wait for confirmations.");
            m_selected = m_candidates[index];
            return checkReadiness();
        }
        if (currentId() == 1) return prepare();
        return true;
    } catch (const UniValue& error) { releasePreparation(); showError(Failure(error)); }
      catch (const std::exception& error) { releasePreparation(); showError(QString::fromUtf8(error.what())); }
    return false;
}

void SmartnodeWizard::initializePage(int id)
{
    if (id < 2) {
        releasePreparation();
        m_consent->setChecked(false);
    }
    QWizard::initializePage(id);
}

bool SmartnodeWizard::prepare()
{
    releasePreparation();
    checkReadiness();
    CService service;
    if (!Lookup(m_service->text().toStdString().c_str(), service, Params().GetDefaultPort(), false) || !service.IsIPv4() ||
        service.GetPort() != Params().GetDefaultPort() || (Params().RequireRoutableExternalIP() && !service.IsRoutable()))
        throw std::runtime_error("Enter a valid server IPv4 address with this network's P2P port.");
    const CTxDestination owner = DecodeDestination(m_owner->text().trimmed().toStdString());
    const auto* ownerKey = boost::get<CKeyID>(&owner);
    const CTxDestination payout = DecodeDestination(m_payout->text().trimmed().toStdString());
    const CTxDestination source = DecodeDestination(m_feeSource->currentText().trimmed().toStdString());
    CBLSPublicKey operatorKey;
    if (!ownerKey || !m_wallet->wallet().isSpendable(owner) || !IsValidDestination(payout) ||
        !m_wallet->wallet().isSpendable(payout) || !IsValidDestination(source) || !m_wallet->wallet().isSpendable(source))
        throw std::runtime_error("Owner, payout and fee source must be valid addresses controlled by this wallet; owner must be P2PKH.");
    if (owner == payout || EncodeDestination(owner) == m_selected.address.toStdString())
        throw std::runtime_error("Owner/voting must be separate from collateral and payout addresses.");
    if (!operatorKey.SetHexStr(m_operator->text().trimmed().toStdString()) || !operatorKey.IsValid())
        throw std::runtime_error("Invalid public BLS key. Paste the public key generated on the server.");
    {
        LOCK(cs_main);
        const auto list = deterministicMNManager->GetListAtChainTip();
        if (list.GetMNByService(service) || list.HasUniqueProperty(*ownerKey) || list.HasUniqueProperty(operatorKey))
            throw std::runtime_error("Server address, owner or operator key is already registered. Use the update/recovery flow for an existing node.");
    }
    auto unlock = m_wallet->requestUnlock();
    if (!unlock.isValid()) throw std::runtime_error("Wallet unlocking was cancelled.");
    // Protect all known collateral denominations from the legacy funding helper.
    for (const auto& group : m_wallet->wallet().listCoins()) {
        for (const auto& coin : group.second) {
            const auto& outpoint = std::get<0>(coin);
            if (Params().GetConsensus().nCollaterals.isValidCollateral(std::get<1>(coin).txout.nValue) &&
                !m_wallet->wallet().isLockedCoin(outpoint)) {
                m_wallet->wallet().lockCoin(outpoint);
                m_temporaryLocks.push_back(outpoint);
            }
        }
    }
    auto parameters = ParamsArray();
    parameters.push_back("register_prepare");
    parameters.push_back(m_selected.outpoint.hash.ToString());
    parameters.push_back(int64_t(m_selected.outpoint.n));
    parameters.push_back(service.ToStringIPPort());
    parameters.push_back(EncodeDestination(owner));
    parameters.push_back(operatorKey.ToString());
    parameters.push_back(EncodeDestination(owner));
    parameters.push_back(0);
    parameters.push_back(EncodeDestination(payout));
    parameters.push_back(EncodeDestination(source));
    const auto prepared = rpc("protx", parameters);
    CMutableTransaction tx;
    CProRegTx payload;
    if (!DecodeHexTx(tx, prepared["tx"].get_str()) || !GetTxPayload(tx, payload) ||
        payload.collateralOutpoint != m_selected.outpoint || payload.addr != service ||
        payload.keyIDOwner != *ownerKey || payload.keyIDVoting != *ownerKey || payload.pubKeyOperator != operatorKey ||
        payload.nOperatorReward != 0 || payload.scriptPayout != GetScriptForDestination(payout) ||
        prepared["collateralAddress"].get_str() != m_selected.address.toStdString() ||
        prepared["signMessage"].get_str() != payload.MakeSignString())
        throw std::runtime_error("Prepared registration does not match the reviewed inputs.");
    auto sign = ParamsArray();
    sign.push_back(m_selected.address.toStdString());
    sign.push_back(payload.MakeSignString());
    const auto signature = rpc("signmessage", sign).get_str();
    payload.vchSig = DecodeBase64(signature.c_str());
    SetTxPayload(tx, payload);
    m_unsignedHex = QString::fromStdString(EncodeHexTx(tx));
    auto signInputs = ParamsArray(); signInputs.push_back(EncodeHexTx(tx));
    const auto signedTx = rpc("signrawtransactionwithwallet", signInputs);
    if (!signedTx["complete"].get_bool()) throw std::runtime_error("Not every fee input could be signed.");
    m_signedHex = QString::fromStdString(signedTx["hex"].get_str());
    if (!verifyPrepared()) throw std::runtime_error("Funding safety checks failed.");
    m_review->setPlainText(tr("Network: %1\nCollateral retained: %2\nCollateral output: %3:%4\nServer: %5\nOwner / voting: %6\nOperator public key: %7\nOperator reward: 0%\nReward payout: %8\nFee source and change: %9\nExact registration fee: %10\n\nOnly the fee transaction is broadcast. The collateral output is referenced and remains locked. The server must run separately with the matching BLS secret. Registration is pending until confirmed; rewards are not guaranteed.")
        .arg(QString::fromStdString(m_wallet->node().getNetwork())).arg(BBC(m_selected.amount))
        .arg(QString::fromStdString(m_selected.outpoint.hash.ToString())).arg(m_selected.outpoint.n)
        .arg(QString::fromStdString(service.ToStringIPPort())).arg(m_owner->text()).arg(m_operator->text())
        .arg(m_payout->text()).arg(m_feeSource->currentText()).arg(BBC(m_fee)));
    m_consent->setChecked(false);
    return true;
}

bool SmartnodeWizard::verifyPrepared()
{
    checkReadiness();
    CMutableTransaction tx;
    if (m_signedHex.isEmpty() || !DecodeHexTx(tx, m_signedHex.toStdString())) return false;
    CMutableTransaction expected;
    if (!DecodeHexTx(expected, m_unsignedHex.toStdString()) || tx.nVersion != expected.nVersion ||
        tx.nType != expected.nType || tx.nLockTime != expected.nLockTime || tx.vout != expected.vout ||
        tx.vExtraPayload != expected.vExtraPayload || tx.vin.size() != expected.vin.size()) return false;
    for (size_t i = 0; i < tx.vin.size(); ++i) {
        if (tx.vin[i].prevout != expected.vin[i].prevout || tx.vin[i].nSequence != expected.vin[i].nSequence) return false;
    }
    CAmount inputAmount = 0;
    const auto sourceScript = GetScriptForDestination(DecodeDestination(m_feeSource->currentText().trimmed().toStdString()));
    for (const auto& input : tx.vin) {
        auto parameters = ParamsArray(); parameters.push_back(input.prevout.hash.ToString()); parameters.push_back(int64_t(input.prevout.n));
        const auto coin = rpc("gettxout", parameters);
        if (coin.isNull()) return false;
        const CAmount amount = AmountFromValue(coin["value"]);
        if (Params().GetConsensus().nCollaterals.isValidCollateral(amount) || !MoneyRange(amount) || amount > MAX_MONEY - inputAmount) return false;
        const auto outputs = m_wallet->wallet().getCoins({input.prevout});
        CTxDestination inputDestination;
        if (outputs.size() != 1 || outputs[0].is_spent || !ExtractDestination(outputs[0].txout.scriptPubKey, inputDestination) ||
            inputDestination != DecodeDestination(m_feeSource->currentText().trimmed().toStdString()) ||
            outputs[0].txout.nValue != amount || !m_wallet->wallet().isSpendable(outputs[0].txout.scriptPubKey)) return false;
        inputAmount += amount;
    }
    CAmount fee;
    if (!SmartnodeSetup::CheckFunding(tx, m_selected.outpoint, sourceScript, inputAmount, m_wallet->node().getMaxTxFee(), fee)) return false;
    if (m_fee && m_fee != fee) return false;
    {
        LOCK(cs_main);
        CValidationState state;
        bool missing = false;
        // Existing dry-run path checks policy and ProReg rules without insertion
        // or relay. The final send still performs full input-script validation.
        if (!AcceptToMemoryPool(mempool, state, MakeTransactionRef(tx), &missing, false, m_wallet->node().getMaxTxFee(), true))
            throw std::runtime_error("Registration preflight failed: " + FormatStateMessage(state));
    }
    m_fee = fee;
    return true;
}

void SmartnodeWizard::accept()
{
    if (m_submitted) return;
    try {
        if (m_submissionAttempted) throw std::runtime_error("Check the previous submission before trying again. The selected collateral remains locked.");
        if (!m_consent->isChecked()) throw std::runtime_error("Review the details and select the explicit broadcast consent checkbox.");
        if (!verifyPrepared()) throw std::runtime_error("The prepared inputs or fee changed. Return and prepare again.");
        if (!m_wallet->node().getNetworkActive()) throw std::runtime_error("Network activity is disabled. Enable it deliberately before submitting.");
        if (QMessageBox::question(this, tr("Confirm smartnode registration"),
                tr("Broadcast this exact registration and spend %1 in fees? The %2 collateral is not spent.").arg(BBC(m_fee)).arg(BBC(m_selected.amount)),
                QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return;
        if (!verifyPrepared()) throw std::runtime_error("Inputs changed while confirming. Prepare and review again.");
        auto parameters = ParamsArray(); parameters.push_back(m_signedHex.toStdString());
        m_submissionAttempted = true;
        button(QWizard::BackButton)->setEnabled(false);
        button(QWizard::FinishButton)->setEnabled(false);
        const auto txid = rpc("sendrawtransaction", parameters);
        m_submitted = true;
        releasePreparation();
        QMessageBox::information(this, tr("Registration submitted"),
            tr("Transaction: %1\nWait for confirmations and check Smartnodes. Keep the collateral locked and retain your wallet backup.").arg(QString::fromStdString(txid.get_str())));
        QDialog::accept(); // All checks are complete; do not re-enter wizard validation after submission.
    } catch (const UniValue& error) { showError(Failure(error) + (m_submissionAttempted ? tr("\nSubmission status must be checked. The selected collateral remains locked.") : QString())); }
      catch (const std::exception& error) { showError(QString::fromUtf8(error.what())); }
}

void SmartnodeWizard::exportServerTemplate()
{
    QString networkOption;
    const auto network = Params().NetworkIDString();
    if (network == CBaseChainParams::REGTEST) networkOption = "regtest=1\n";
    else if (network == CBaseChainParams::TESTNET) networkOption = "testnet=1\n";
    else if (network != CBaseChainParams::MAIN) {
        showError(tr("Named devnet configuration requires its original network parameters and must be prepared manually."));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, tr("Save a new public server template"), "babacoin-smartnode-template.conf", tr("Configuration (*.conf)"));
    if (path.isEmpty()) return;
    if (QFile::exists(path)) { showError(tr("Choose a new filename; existing configuration files are preserved.")); return; }
    QTemporaryFile file(path + ".XXXXXX");
    if (!file.open()) { showError(tr("Could not create a new template file.")); return; }
    QTextStream stream(&file);
    stream << "# Public template only. Install the matching Babacoin server separately.\n"
           << "# Generate BLS keys locally on the server: babacoin-cli bls generate\n"
           << "# Keep the secret on that server and paste only its public key into the wallet wizard.\n"
           << networkOption << "server=1\nlisten=1\nrpcbind=127.0.0.1\nrpcallowip=127.0.0.1\n"
           << "port=" << Params().GetDefaultPort() << "\nrpcport=" << BaseParams().RPCPort() << "\n"
           << "# externalip=YOUR_SERVER_IPV4:" << Params().GetDefaultPort() << "\n"
           << "# smartnodeblsprivkey=YOUR_SERVER_GENERATED_SECRET\n";
    stream.flush();
    if (stream.status() != QTextStream::Ok || !file.flush() || !file.rename(path)) {
        showError(tr("Could not save the new template.")); return;
    }
    file.setAutoRemove(false);
    QMessageBox::information(this, tr("Server template saved"), tr("This public template contains no wallet or operator secret. Add the matching BLS secret locally on the server and keep RPC private."));
}
