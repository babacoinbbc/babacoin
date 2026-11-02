#include <qt/test/wallettests.h>

#include <coinjoin/coinjoin-client.h>
#include <interfaces/node.h>
#include <qt/bitcoinamountfield.h>
#include <qt/callback.h>
#include <qt/clientmodel.h>
#include <qt/optionsmodel.h>
#include <qt/qvalidatedlineedit.h>
#include <qt/sendcoinsdialog.h>
#include <qt/sendcoinsentry.h>
#include <qt/transactiontablemodel.h>
#include <qt/transactionview.h>
#include <qt/walletmodel.h>
#include <qt/bitcoingui.h>
#include <qt/networkstyle.h>
#include <qt/guiutil.h>
#include <qt/utilitydialog.h>
#include <qt/smartnodewizard.h>
#include <qt/smartnodelist.h>
#include <qt/webwalletpage.h>
#include <qt/rpcconsole.h>
#include <qt/modaloverlay.h>
#include <wallet/rpcwallet.h>
#include <rpc/server.h>
#include <validationinterface.h>
#include <chainparamsbase.h>
#include <key_io.h>
#include <test/test_babacoin.h>
#include <validation.h>
#include <wallet/wallet.h>
#include <qt/overviewpage.h>
#include <qt/receivecoinsdialog.h>
#include <qt/recentrequeststablemodel.h>
#include <qt/receiverequestdialog.h>

#include <memory>
#include <atomic>
#include <chrono>
#include <thread>
#include <limits>

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QPushButton>
#include <QTimer>
#include <QThread>
#include <QElapsedTimer>
#include <QDebug>
#include <QSettings>
#include <QScrollArea>
#include <QScrollBar>
#include <QToolButton>
#include <QDir>
#include <QVBoxLayout>
#include <QTextEdit>
#include <QListView>
#include <QDialogButtonBox>
#include <QComboBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QLocale>
#include <QFile>
#include <QTemporaryDir>
#include <QRegularExpression>
#include <QProgressBar>
#include <QLabel>
#include <ui_interface.h>

void WalletTests::syncUiResponsiveness()
{
    TestChain100Setup chain;
    auto node = interfaces::MakeNode();
    OptionsModel options(*node);
    ClientModel model(*node, &options);
    RPCConsole console(*node, nullptr, Qt::WindowFlags());
    OverviewPage page;
    ModalOverlay overlay(nullptr);
    auto* hide = overlay.findChild<QPushButton*>("closeButton");
    QVERIFY(hide);
    overlay.showHide(false, true);

    // Simulate a header batch/import holding the validation lock. A finite
    // fallback makes the old GUI fail this test instead of hanging CI forever.
    model.cachedBestHeaderHeight = -1;
    model.cachedBestHeaderTime = -1;
    std::atomic<bool> held{false}, release{false}, timedOut{false};
    std::thread validation([&] {
        LOCK(cs_main);
        held.store(true);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!release.load() && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        timedOut.store(!release.load());
        held.store(false);
    });
    while (!held.load()) QTest::qWait(1);
    bool hideWorkedWhileLocked = false;
    int heartbeatsWhileLocked = 0;
    QTimer heartbeat;
    QObject::connect(&heartbeat, &QTimer::timeout, [&] {
        if (held.load()) ++heartbeatsWhileLocked;
    });
    heartbeat.start(1);
    QTimer::singleShot(0, &overlay, [&] {
        hide->click();
        hideWorkedWhileLocked = held.load() && !overlay.isLayerVisible();
    });
    model.getHeaderTipHeight();
    model.getHeaderTipTime();
    model.updateTimer();
    model.refreshSmartnodeList();
    console.setClientModel(&model);
    page.setClientModel(&model);
    QTest::qWait(100);
    release.store(true);
    validation.join();
    heartbeat.stop();
    // Tear down the RPC executor before an assertion can return from the test.
    console.setClientModel(nullptr);
    page.setClientModel(nullptr);
    QVERIFY2(!timedOut.load(), "GUI waited for the validation lock during header sync/import");
    QVERIFY(heartbeatsWhileLocked > 0);
    QVERIFY(hideWorkedWhileLocked);

    auto* progress = overlay.findChild<QProgressBar*>("progressBar");
    auto* caption = overlay.findChild<QLabel*>("labelSyncDone");
    auto* info = overlay.findChild<QLabel*>("infoTextStrong");
    QVERIFY(progress && caption && info);
    const QDateTime now = QDateTime::currentDateTime();
    overlay.setKnownBestHeight(750, now.addSecs(-250 * Params().GetConsensus().nPowTargetSpacing));
    overlay.tipUpdate(700, now, 1.0); // stale transaction estimate said 100%
    QCOMPARE(progress->maximum(), 10000);
    QCOMPARE(progress->value(), 7500);
    QVERIFY(caption->text().contains("Header"));
    overlay.setKnownBestHeight(1000, now);
    QCOMPARE(progress->value(), 7000);
    overlay.tipUpdate(999, now, 1.0);
    QVERIFY(progress->value() < progress->maximum());
    overlay.tipUpdate(1000, now, 1.0);
    QCOMPARE(progress->value(), progress->maximum());
    overlay.tipUpdate(999, now, std::numeric_limits<double>::quiet_NaN());
    QCOMPARE(progress->value(), 0);
    const QString warning = info->text();
    overlay.setImporting(true);
    QVERIFY(info->text().contains(warning));
    QVERIFY(info->text().contains("Importing blocks from disk"));
    overlay.setImporting(false);
    QCOMPARE(info->text(), warning);

    // Exercise the real core -> node interface -> queued GUI progress bridge.
    QObject::connect(&model, &ClientModel::blockImportProgress, &overlay, &ModalOverlay::setImportProgress);
    uiInterface.ShowBlockImportProgress(BlockImportProgress(BlockImportProgress::READING, 128, 256, 10));
    QTRY_COMPARE(progress->value(), 5000);
    QVERIFY(caption->text().contains("Reading blockchain file"));
    {
        // Import may begin before the GUI/model is constructed. A retained
        // snapshot must survive attachment, and delayed older events lose.
        ClientModel lateModel(*node, &options);
        QCOMPARE(lateModel.getCachedImportProgress().processed, qulonglong(128));
        const auto snapshot = uiInterface.GetBlockImportProgress();
        QVERIFY(snapshot.sequence > 0);
        lateModel.updateBlockImportProgress(BlockImportProgress::IDLE, 0, 0, 0, snapshot.sequence - 1);
        QCOMPARE(lateModel.getCachedImportProgress().phase, int(BlockImportProgress::READING));
    }
    overlay.setKnownBestHeight(2000, now); // headers must not overwrite file progress
    QCOMPARE(progress->value(), 5000);
    uiInterface.ShowBlockImportProgress(BlockImportProgress(BlockImportProgress::VALIDATING));
    QTRY_VERIFY(caption->text().contains("Validating imported blocks"));
    QVERIFY(progress->value() < progress->maximum());
    uiInterface.ShowBlockImportProgress(BlockImportProgress(BlockImportProgress::IDLE));
    QTRY_COMPARE(info->text(), warning);
}

void WalletTests::balancePollResponsiveness()
{
    TestChain100Setup chain;
    auto wallet = std::make_shared<CWallet>(WalletLocation(), WalletDatabase::CreateMock());
    AddWallet(wallet);
    struct Cleanup {
        std::shared_ptr<CWallet> wallet;
        ~Cleanup() { RemoveWallet(wallet); }
    } cleanup{wallet};
    bool firstRun;
    wallet->LoadWallet(firstRun);
    auto node = interfaces::MakeNode();
    OptionsModel options(*node);
    WalletModel model(std::move(node->getWallets()[0]), *node, &options);
    model.pollTimer->stop();
    QVERIFY(model.balancePollThread != QThread::currentThread());

    // A deliberately slow worker task models an expensive balance traversal.
    // The GUI must keep ticking and must not queue one traversal per timer tick.
    std::atomic<bool> busy{false}, release{false}, timedOut{false};
    QTimer::singleShot(0, model.balancePollWorker, [&] {
        busy.store(true);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!release.load() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        timedOut.store(!release.load());
        busy.store(false);
    });
    QTRY_VERIFY(busy.load());
    int requests = 0, heartbeats = 0;
    QObject::connect(&model, &WalletModel::requestedBalancePoll, [&] { ++requests; });
    QTimer heartbeat;
    QObject::connect(&heartbeat, &QTimer::timeout, [&] { if (busy.load()) ++heartbeats; });
    heartbeat.start(1);
    QElapsedTimer elapsed;
    elapsed.start();
    for (int i = 0; i < 100; ++i) model.pollBalanceChanged();
    const auto scheduleMillis = elapsed.elapsed();
    model.updateTransaction(); // must survive completion of an older request
    QTest::qWait(100);
    release.store(true);
    QTRY_VERIFY(!model.balancePollInFlight);
    heartbeat.stop();
    QVERIFY(!timedOut.load());
    QVERIFY(heartbeats > 0);
    QVERIFY(scheduleMillis < 500);
    QCOMPARE(requests, 1);
    QVERIFY(!model.hasBalanceSnapshot()); // the older in-flight result was rejected
    QVERIFY(model.fForceCheckBalanceChanged);
    model.pollBalanceChanged();
    QTRY_VERIFY(!model.balancePollInFlight);
    QCOMPARE(requests, 2);
    QVERIFY(model.hasBalanceSnapshot());
    QCOMPARE(model.getCachedBalances().balance, wallet->GetBalance());
    QVERIFY(!model.fForceCheckBalanceChanged);
    // Unchanged height/options do not schedule another wallet traversal.
    for (int i = 0; i < 100; ++i) model.pollBalanceChanged();
    QCOMPARE(requests, 2);
    qDebug() << "Balance poll: 100 requests scheduled in" << scheduleMillis
             << "ms; GUI heartbeats during worker delay:" << heartbeats;

    // Unloading with a queued result must join the worker and discard its
    // receiver-bound completion rather than access a destroyed wallet/model.
    auto* unloading = new WalletModel(std::move(node->getWallets()[0]), *node, &options);
    unloading->pollTimer->stop();
    unloading->pollBalanceChanged();
    delete unloading;
    QTest::qWait(20);
}

void WalletTests::desktopLayout()
{
#if !defined(Q_OS_WIN) && !defined(Q_OS_MAC)
    QSKIP("Responsive desktop shell changes target Windows and macOS only");
#else
    struct RestoreAppearance {
        QString applicationName{QApplication::applicationName()};
        QVariant theme{QSettings().value("theme")};
        int scale{GUIUtil::getFontScale()};
        ~RestoreAppearance() {
            QApplication::setApplicationName(applicationName);
            if (theme.isValid()) QSettings().setValue("theme", theme);
            else QSettings().remove("theme");
            GUIUtil::setFontScale(scale);
            GUIUtil::loadTheme(true);
        }
    } restore;
    if (!GUIUtil::fontsLoaded()) { QVERIFY(GUIUtil::loadFonts()); GUIUtil::setApplicationFont(); }
    TestChain100Setup chain;
    auto wallet = std::make_shared<CWallet>(WalletLocation(), WalletDatabase::CreateMock());
    AddWallet(wallet);
    struct Cleanup { std::shared_ptr<CWallet> wallet; ~Cleanup() { RemoveWallet(wallet); } } cleanup{wallet};
    bool firstRun;
    wallet->LoadWallet(firstRun);
    auto node = interfaces::MakeNode();
    OptionsModel options(*node);
    WalletModel model(std::move(node->getWallets()[0]), *node, &options);
    ClientModel client(*node, &options);
    std::unique_ptr<const NetworkStyle> networkStyle(NetworkStyle::instantiate("regtest"));
    QSettings().setValue("fShowSmartnodesTab", true);
    BitcoinGUI gui(*node, networkStyle.get());
    struct DetachClient {
        BitcoinGUI& gui;
        ~DetachClient() { gui.removeAllWallets(); gui.setClientModel(nullptr); }
    } detachClient{gui};
    gui.setWindowTitle("Babacoin Desktop QA - isolated regtest");
    gui.setClientModel(&client);
    QVERIFY(gui.addWallet(&model));
    QVERIFY(gui.setCurrentWallet(model.getWalletName()));
    gui.finishStartup();
    gui.show();
    auto* dashboard = gui.findChild<QScrollArea*>("walletDashboardScroll");
    auto* navigation = gui.findChild<QScrollArea*>("walletSidebarScroll");
    auto* settingsButton = gui.findChild<QToolButton*>("walletSettings");
    auto* content = gui.findChild<QScrollArea*>("walletContentScroll");
    QVERIFY(dashboard && navigation && settingsButton && content);
    const QString reviewDirectory = QString::fromLocal8Bit(qgetenv("BBC_UI_REVIEW_DIR"));
    auto snapshot = [&](QWidget& widget, const QString& name) {
        if (!reviewDirectory.isEmpty()) {
            QDir().mkpath(reviewDirectory);
            return widget.grab().save(QDir(reviewDirectory).filePath(name + ".png"));
        }
        return true;
    };
    for (const QString& theme : {QString("Dark"), QString("Light")}) {
        QSettings().setValue("theme", theme);
        QCOMPARE(GUIUtil::getActiveTheme(), theme);
        GUIUtil::loadTheme(true);
        for (int scale : {0, 50}) {
            GUIUtil::setFontScale(scale);
            for (const QSize size : {QSize(800, 600), QSize(1024, 640), QSize(1440, 900)}) {
                gui.resize(size);
                QTest::qWait(80);
                qDebug() << "Desktop geometry" << theme << scale << size << gui.size()
                         << "content" << content->viewport()->size() << content->widget()->minimumSizeHint()
                         << "dashboard" << dashboard->viewport()->size()
                         << dashboard->parentWidget()->minimumSizeHint();
                if (content->horizontalScrollBar()->maximum() > 0) {
                    for (auto* widget : content->widget()->findChildren<QWidget*>()) {
                        const QString type = widget->metaObject()->className();
                        if (type == "WalletView" || type == "OverviewPage" || type == "QStackedWidget")
                            qDebug() << "Stack geometry" << type << widget->minimumSize()
                                     << widget->minimumSizeHint() << widget->sizeHint();
                    }
                }
                QVERIFY2(gui.width() <= size.width(), "Hidden forms forced the desktop window wider than requested");
                QTRY_COMPARE(dashboard->horizontalScrollBar()->maximum(), 0);
                QTRY_COMPARE(content->horizontalScrollBar()->maximum(), 0);
                navigation->ensureWidgetVisible(settingsButton);
                QTest::qWait(10);
                QVERIFY(navigation->viewport()->rect().contains(settingsButton->mapTo(navigation->viewport(), settingsButton->rect().center())));
                QVERIFY(snapshot(gui, QString("desktop-%1-%2-%3x%4").arg(theme).arg(scale).arg(size.width()).arg(size.height())));
            }
            SmartnodeWizard wizard(&model, {}, &gui);
            wizard.resize(520, 400);
            wizard.show();
            QTest::qWait(80);
            QVERIFY(wizard.width() <= 520);
            QVERIFY(wizard.height() <= 400);
            auto* pageScroll = wizard.currentPage()->findChild<QScrollArea*>("smartnodePageScroll");
            QVERIFY(pageScroll);
            QTRY_COMPARE(pageScroll->horizontalScrollBar()->maximum(), 0);
            QVERIFY(!wizard.button(QWizard::NextButton)->isEnabled());
            QVERIFY(snapshot(wizard, QString("wizard-%1-%2").arg(theme).arg(scale)));
            wizard.reject();
        }
    }
    HelpMessageDialog about(*node, &gui, HelpMessageDialog::about);
    about.resize(600, 420);
    about.show();
    QTest::qWait(60);
    auto* license = about.findChild<QLabel*>("aboutMessage");
    QVERIFY(license);
    QVERIFY(license->text().contains("https://github.com/babacoinbbc/babacoin"));
    QVERIFY(!license->text().contains("Raptor3um"));
    QCOMPARE(license->text().count("The Dash Core developers"), 1);
    QVERIFY(license->height() >= license->heightForWidth(license->width()));
    QVERIFY(snapshot(about, "about-source"));
    auto* aboutScroll = about.findChild<QScrollArea*>("scrollArea");
    QVERIFY(aboutScroll);
    aboutScroll->verticalScrollBar()->setValue(aboutScroll->verticalScrollBar()->maximum());
    QVERIFY(snapshot(about, "about-source-bottom"));
    about.hide();
    gui.hide();
#endif
}

void WalletTests::smartnodeFundingSafety()
{
    const COutPoint collateral(uint256S("01"), 0);
    const COutPoint feeInput(uint256S("02"), 1);
    const CScript change = CScript() << OP_TRUE;
    CMutableTransaction tx;
    tx.nVersion = 3;
    tx.nType = TRANSACTION_PROVIDER_REGISTER;
    tx.vin.emplace_back(feeInput);
    tx.vout.emplace_back(9000, change);
    CAmount fee;
    QVERIFY(SmartnodeSetup::CheckFunding(tx, collateral, change, 10000, 1000, fee));
    QCOMPARE(fee, CAmount(1000));
    for (int attack = 0; attack != 8; ++attack) {
        auto unsafe = tx;
        CAmount input = 10000, limit = 1000;
        switch (attack) {
        case 0: unsafe.vin[0].prevout = collateral; break;
        case 1: unsafe.vin.push_back(unsafe.vin[0]); break;
        case 2: unsafe.vout[0].scriptPubKey = CScript() << OP_FALSE; break;
        case 3: unsafe.vout[0].nValue = 10001; break;
        case 4: unsafe.vout[0].nValue = -1; break;
        case 5: limit = 999; break;
        case 6: input = 9000; break;
        case 7: unsafe.nType = TRANSACTION_NORMAL; break;
        }
        QVERIFY(!SmartnodeSetup::CheckFunding(unsafe, collateral, change, input, limit, fee));
    }
}

namespace
{
//! Press "Ok" button in message box dialog.
void ConfirmMessage(QString* text = nullptr)
{
    QTimer::singleShot(0, makeCallback([text](Callback* callback) {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            if (widget->inherits("QMessageBox")) {
                QMessageBox* messageBox = qobject_cast<QMessageBox*>(widget);
                if (text) *text = messageBox->text();
                messageBox->defaultButton()->click();
            }
        }
        delete callback;
    }), SLOT(call()));
}

//! Press "Yes" or "Cancel" buttons in modal send confirmation dialog.
void ConfirmSend(QString* text = nullptr, bool cancel = false)
{
    QTimer::singleShot(0, makeCallback([text, cancel](Callback* callback) {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            if (widget->inherits("SendConfirmationDialog")) {
                SendConfirmationDialog* dialog = qobject_cast<SendConfirmationDialog*>(widget);
                if (text) *text = dialog->text();
                QAbstractButton* button = dialog->button(cancel ? QMessageBox::Cancel : QMessageBox::Yes);
                button->setEnabled(true);
                button->click();
            }
        }
        delete callback;
    }), SLOT(call()));
}

//! Send coins to address and return txid.
uint256 SendCoins(CWallet& wallet, SendCoinsDialog& sendCoinsDialog, const CTxDestination& address, CAmount amount)
{
    QVBoxLayout* entries = sendCoinsDialog.findChild<QVBoxLayout*>("entries");
    SendCoinsEntry* entry = qobject_cast<SendCoinsEntry*>(entries->itemAt(0)->widget());
    entry->findChild<QValidatedLineEdit*>("payTo")->setText(QString::fromStdString(EncodeDestination(address)));
    entry->findChild<BitcoinAmountField*>("payAmount")->setValue(amount);
    uint256 txid;
    boost::signals2::scoped_connection c(wallet.NotifyTransactionChanged.connect([&txid](CWallet*, const uint256& hash, ChangeType status) {
        if (status == CT_NEW) txid = hash;
    }));
    ConfirmSend();
    QMetaObject::invokeMethod(&sendCoinsDialog, "on_sendButton_clicked");
    return txid;
}

//! Find index of txid in transaction list.
QModelIndex FindTx(const QAbstractItemModel& model, const uint256& txid)
{
    QString hash = QString::fromStdString(txid.ToString());
    int rows = model.rowCount({});
    for (int row = 0; row < rows; ++row) {
        QModelIndex index = model.index(row, 0, {});
        if (model.data(index, TransactionTableModel::TxHashRole) == hash) {
            return index;
        }
    }
    return {};
}

//! Simple qt wallet tests.
//
// Test widgets can be debugged interactively calling show() on them and
// manually running the event loop, e.g.:
//
//     sendCoinsDialog.show();
//     QEventLoop().exec();
//
// This also requires overriding the default minimal Qt platform:
//
//     src/qt/test/test_babacoin-qt -platform xcb      # Linux
//     src/qt/test/test_babacoin-qt -platform windows  # Windows
//     src/qt/test/test_babacoin-qt -platform cocoa    # macOS
void TestGUI()
{
    // Set up wallet and chain with 105 blocks (5 mature blocks for spending).
    TestChain100Setup test;
    for (int i = 0; i < 5; ++i) {
        test.CreateAndProcessBlock({}, GetScriptForRawPubKey(test.coinbaseKey.GetPubKey()));
    }
    std::shared_ptr<CWallet> wallet = std::make_shared<CWallet>(WalletLocation(), WalletDatabase::CreateMock());
    AddWallet(wallet);
    struct RegisteredWalletCleanup {
        std::shared_ptr<CWallet> wallet;
        ~RegisteredWalletCleanup() { RemoveWallet(wallet); }
    } registeredWalletCleanup{wallet};
    bool firstRun;
    wallet->LoadWallet(firstRun);
    {
        LOCK(wallet->cs_wallet);
        wallet->SetAddressBook(test.coinbaseKey.GetPubKey().GetID(), "", "receive");
        wallet->AddKeyPubKey(test.coinbaseKey, test.coinbaseKey.GetPubKey());
    }
    {
        LOCK(cs_main);
        WalletRescanReserver reserver(wallet.get());
        reserver.reserve();
        wallet->ScanForWalletTransactions(chainActive.Genesis(), nullptr, reserver, true);
    }
    wallet->SetBroadcastTransactions(true);

    // Create widgets for sending coins and listing transactions.
    SendCoinsDialog sendCoinsDialog;
    TransactionView transactionView;
    auto node = interfaces::MakeNode();
    OptionsModel optionsModel(*node);
    ClientModel clientModel(*node, &optionsModel);
    WalletModel walletModel(std::move(node->getWallets()[0]), *node, &optionsModel);;
    sendCoinsDialog.setModel(&walletModel);
    transactionView.setModel(&walletModel);

    // Send two transactions, and verify they are added to transaction list.
    TransactionTableModel* transactionTableModel = walletModel.getTransactionTableModel();
    QVERIFY(transactionTableModel->isLoadingHistory());
    QCOMPARE(transactionTableModel->rowCount({}), 0);
    bool exportDeferred = false;
    QObject::connect(&transactionView, &TransactionView::message,
        [&](const QString&, const QString&, unsigned int) { exportDeferred = true; });
    transactionView.exportClicked();
    QVERIFY(exportDeferred);
    const QString existingHash = QString::fromStdString(walletModel.wallet().getWalletTxIds().front().ToString());
    QVERIFY(QMetaObject::invokeMethod(transactionTableModel, "updateTransaction", Qt::DirectConnection,
        Q_ARG(QString, existingHash), Q_ARG(int, int(CT_NEW)), Q_ARG(bool, true)));
    bool eventLoopResponsive = false;
    bool sawPartialHistory = false;
    QTimer heartbeat;
    QObject::connect(&heartbeat, &QTimer::timeout, [&] {
        if (transactionTableModel->isLoadingHistory()) eventLoopResponsive = true;
    });
    QObject::connect(transactionTableModel, &TransactionTableModel::historyLoadProgress,
        [&](int processed, int total) {
            if (processed > 0 && processed < total) {
                sawPartialHistory = true;
                // No repeated growing-table updates for accessibility clients.
                QCOMPARE(transactionTableModel->rowCount({}), 0);
            }
        });
    SmartnodeList smartnodes;
    smartnodes.setClientModel(&clientModel);
    smartnodes.setWalletModel(&walletModel);
    auto* mySmartnodes = smartnodes.findChild<QCheckBox*>("checkBoxMySmartnodesOnly");
    QVERIFY(mySmartnodes);
    mySmartnodes->setChecked(true);
    heartbeat.start(0);
    std::atomic<bool> historyLockHeld{false}, releaseHistoryLock{false}, historyWaitTimedOut{false};
    std::thread headerBatch([&] {
        LOCK(cs_main);
        historyLockHeld.store(true);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!releaseHistoryLock.load() && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        historyWaitTimedOut.store(!releaseHistoryLock.load());
    });
    // No event processing until the lock is held: the first history batch
    // must meet contention rather than completing before the test starts.
    while (!historyLockHeld.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    bool historyHeartbeatWhileLocked = false;
    QTimer::singleShot(1, [&] { historyHeartbeatWhileLocked = !releaseHistoryLock.load(); });
    // The owner-only Smartnode filter also performs periodic UTXO queries.
    // It must retry with its refresh request intact while validation is busy.
    QMetaObject::invokeMethod(&smartnodes, "on_checkBoxMySmartnodesOnly_stateChanged", Qt::DirectConnection,
        Q_ARG(int, int(Qt::Checked)));
    QMetaObject::invokeMethod(&smartnodes, "updateDIP3ListScheduled", Qt::DirectConnection);
    QTest::qWait(100);
    releaseHistoryLock.store(true);
    headerBatch.join();
    QVERIFY2(!historyWaitTimedOut.load(), "History loading waited for the validation lock on the GUI thread");
    QVERIFY(historyHeartbeatWhileLocked);
    QMetaObject::invokeMethod(&smartnodes, "updateDIP3ListScheduled", Qt::DirectConnection);
    auto* nodeCount = smartnodes.findChild<QLabel*>("countLabelDIP3");
    QVERIFY(nodeCount);
    QCOMPARE(nodeCount->text(), QString("0"));
    QVERIFY(transactionTableModel->isLoadingHistory());
    QCOMPARE(transactionTableModel->rowCount({}), 0);
    QTRY_VERIFY(!transactionTableModel->isLoadingHistory());
    heartbeat.stop();
    QVERIFY(eventLoopResponsive);
    QVERIFY(sawPartialHistory);
    QCOMPARE(transactionTableModel->rowCount({}), 105);
    QTRY_VERIFY(walletModel.hasBalanceSnapshot());
    // Destruction cancels scheduled batches before they access model state.
    auto* cancelledHistory = new TransactionTableModel(&walletModel);
    QVERIFY(cancelledHistory->isLoadingHistory());
    delete cancelledHistory;
    QCoreApplication::processEvents();
    uint256 txid1 = SendCoins(*wallet.get(), sendCoinsDialog, CKeyID(), 5 * COIN);
    uint256 txid2 = SendCoins(*wallet.get(), sendCoinsDialog, CKeyID(), 10 * COIN);
    QCOMPARE(transactionTableModel->rowCount({}), 107);
    QVERIFY(FindTx(*transactionTableModel, txid1).isValid());
    QVERIFY(FindTx(*transactionTableModel, txid2).isValid());

    // A live notification also retries without losing a deferred update.
    std::atomic<bool> updateLockHeld{false}, releaseUpdateLock{false}, updateWaitTimedOut{false};
    std::thread updateBatch([&] {
        LOCK(cs_main);
        updateLockHeld.store(true);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!releaseUpdateLock.load() && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        updateWaitTimedOut.store(!releaseUpdateLock.load());
    });
    while (!updateLockHeld.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    QMetaObject::invokeMethod(transactionTableModel, "updateTransaction", Qt::DirectConnection,
        Q_ARG(QString, QString::fromStdString(txid1.ToString())), Q_ARG(int, int(CT_UPDATED)), Q_ARG(bool, false));
    QTest::qWait(100);
    const int rowsWhileBusy = transactionTableModel->rowCount({});
    releaseUpdateLock.store(true);
    updateBatch.join();
    QVERIFY(!updateWaitTimedOut.load());
    QCOMPARE(rowsWhileBusy, 107);
    QTRY_COMPARE(transactionTableModel->rowCount({}), 106);
    QMetaObject::invokeMethod(transactionTableModel, "updateTransaction", Qt::DirectConnection,
        Q_ARG(QString, QString::fromStdString(txid1.ToString())), Q_ARG(int, int(CT_UPDATED)), Q_ARG(bool, true));
    QTRY_COMPARE(transactionTableModel->rowCount({}), 107);
    QVERIFY(FindTx(*transactionTableModel, txid1).isValid());

    // Check current balance on OverviewPage
    OverviewPage overviewPage;
    overviewPage.setClientModel(&clientModel);
    overviewPage.setWalletModel(&walletModel);
    QLabel* balanceLabel = overviewPage.findChild<QLabel*>("labelBalance");
    QString balanceText = balanceLabel->text();
    int unit = walletModel.getOptionsModel()->getDisplayUnit();
    CAmount balance = walletModel.wallet().getBalance();
    QString balanceComparison = BitcoinUnits::floorLocalizedHtmlWithUnit(unit, balance, false, BitcoinUnits::separatorAlways);
    QCOMPARE(balanceText, balanceComparison);

    // Dashboard heights represent validated blocks, never a header-only tip.
    auto* height = overviewPage.findChild<QLabel*>("blockHeightValue");
    auto* connection = overviewPage.findChild<QLabel*>("connectionValue");
    auto* synchronization = overviewPage.findChild<QLabel*>("syncValue");
    QVERIFY(height && connection && synchronization);
    const int validatedHeight = node->getNumBlocks();
    const QString originalHeight = QLocale::system().toString(validatedHeight);
    QTRY_COMPARE(height->text(), originalHeight);
    Q_EMIT clientModel.numBlocksChanged(validatedHeight + 1000, QDateTime(), QString(), 1.0, true);
    QCOMPARE(height->text(), originalHeight);
    Q_EMIT clientModel.numBlocksChanged(validatedHeight + 1, QDateTime(), QString(), 1.0, false);
    QCOMPARE(height->text(), QLocale::system().toString(validatedHeight + 1));
    overviewPage.setClientModel(nullptr);
    Q_EMIT clientModel.numBlocksChanged(validatedHeight + 2, QDateTime(), QString(), 1.0, false);
    QCOMPARE(height->text(), QString::fromUtf8("—"));
    overviewPage.setClientModel(&clientModel);
    QCOMPARE(height->text(), originalHeight);
    const bool wasActive = node->getNetworkActive();
    node->setNetworkActive(false);
    Q_EMIT clientModel.networkActiveChanged(false);
    overviewPage.showOutOfSyncWarning(false);
    QCOMPARE(connection->text(), QString("Network paused"));
    QCOMPARE(synchronization->text(), QString("Offline view"));
    node->setNetworkActive(wasActive);
    overviewPage.showOutOfSyncWarning(true);

    // Check Request Payment button
    ReceiveCoinsDialog receiveCoinsDialog;
    receiveCoinsDialog.setModel(&walletModel);
    RecentRequestsTableModel* requestTableModel = walletModel.getRecentRequestsTableModel();

    // Label input
    QLineEdit* labelInput = receiveCoinsDialog.findChild<QLineEdit*>("reqLabel");
    labelInput->setText("TEST_LABEL_1");

    // Amount input
    BitcoinAmountField* amountInput = receiveCoinsDialog.findChild<BitcoinAmountField*>("reqAmount");
    amountInput->setValue(1);

    // Message input
    QLineEdit* messageInput = receiveCoinsDialog.findChild<QLineEdit*>("reqMessage");
    messageInput->setText("TEST_MESSAGE_1");
    int initialRowCount = requestTableModel->rowCount({});
    QPushButton* requestPaymentButton = receiveCoinsDialog.findChild<QPushButton*>("receiveButton");
    requestPaymentButton->click();
    for (QWidget* widget : QApplication::topLevelWidgets()) {
        if (widget->inherits("ReceiveRequestDialog")) {
            ReceiveRequestDialog* receiveRequestDialog = qobject_cast<ReceiveRequestDialog*>(widget);
            QTextEdit* rlist = receiveRequestDialog->QObject::findChild<QTextEdit*>("outUri");
            QString paymentText = rlist->toPlainText();
            QStringList paymentTextList = paymentText.split('\n');
            QCOMPARE(paymentTextList.at(0), QString("Payment information"));
            QVERIFY(paymentTextList.at(2).indexOf(QString("URI: babacoin:")) != -1);
            QVERIFY(paymentTextList.at(3).indexOf(QString("Address:")) != -1);
            QCOMPARE(paymentTextList.at(4), QString("Amount: 0.00000001 ") + BitcoinUnits::name(unit));
            QCOMPARE(paymentTextList.at(5), QString("Label: TEST_LABEL_1"));
            QCOMPARE(paymentTextList.at(6), QString("Message: TEST_MESSAGE_1"));
        }
    }

    // Clear button
    QPushButton* clearButton = receiveCoinsDialog.findChild<QPushButton*>("clearButton");
    clearButton->click();
    QCOMPARE(labelInput->text(), QString(""));
    QCOMPARE(amountInput->value(), CAmount(0));
    QCOMPARE(messageInput->text(), QString(""));

    // Check addition to history
    int currentRowCount = requestTableModel->rowCount({});
    QCOMPARE(currentRowCount, initialRowCount+1);

    // Check Remove button
    QTableView* table = receiveCoinsDialog.findChild<QTableView*>("recentRequestsView");
    table->selectRow(currentRowCount-1);
    QPushButton* removeRequestButton = receiveCoinsDialog.findChild<QPushButton*>("removeRequestButton");
    removeRequestButton->click();
    QCOMPARE(requestTableModel->rowCount({}), currentRowCount-1);
}

}

void WalletTests::walletTests()
{
    TestGUI();
}

void WalletTests::smartnodeRegistrationWorkflow()
{
    // This fixture creates only a private regtest chain; no network threads start.
    TestChainSetup chain(500);
    QVERIFY(Params().NetworkIDString() == CBaseChainParams::REGTEST);
    auto& consensus = const_cast<Consensus::Params&>(Params().GetConsensus());
    struct RestoreTiers {
        Consensus::Params& params;
        SmartnodeCollaterals original;
        ~RestoreTiers() { params.nCollaterals = original; }
    } restoreTiers{consensus, consensus.nCollaterals};
    // Both tiers are payable now. Exercise the larger tier before the older
    // one expires, matching mainnet's early 2M registration workflow.
    consensus.nCollaterals = SmartnodeCollaterals({{1000, 10 * COIN}, {INT_MAX, 20 * COIN}}, {{INT_MAX, 20}});
    RegisterWalletRPCCommands(tableRPC);
    auto wallet = std::make_shared<CWallet>(WalletLocation(), WalletDatabase::CreateMock());
    AddWallet(wallet);
    bool firstRun;
    QCOMPARE(wallet->LoadWallet(firstRun), DBErrors::LOAD_OK);
    {
        LOCK(wallet->cs_wallet);
        wallet->AddKeyPubKey(chain.coinbaseKey, chain.coinbaseKey.GetPubKey());
    }
    {
        LOCK(cs_main);
        WalletRescanReserver reserver(wallet.get());
        QVERIFY(reserver.reserve());
        wallet->ScanForWalletTransactions(chainActive.Genesis(), nullptr, reserver, true);
    }
    wallet->SetBroadcastTransactions(true);
    RegisterValidationInterface(wallet.get());
    struct Cleanup {
        std::shared_ptr<CWallet> wallet;
        ~Cleanup() {
            SyncWithValidationInterfaceQueue();
            UnregisterValidationInterface(wallet.get());
            RemoveWallet(wallet);
        }
    } cleanup{wallet};
    auto node = interfaces::MakeNode();
    OptionsModel options(*node);
    WalletModel model(std::move(node->getWallets()[0]), *node, &options);
    auto call = [&](const std::string& name, const UniValue& parameters) {
        return node->executeRpc(name, parameters, "/wallet/");
    };
    auto newAddress = [&] { return call("getnewaddress", UniValue(UniValue::VARR)).get_str(); };
    const auto collateralAddress = newAddress();
    const auto feeAddress = newAddress();
    const auto ownerAddress = newAddress();
    const auto payoutAddress = newAddress();
    UniValue recipients(UniValue::VOBJ);
    recipients.pushKV(collateralAddress, 20);
    recipients.pushKV(feeAddress, 5);
    UniValue send(UniValue::VARR); send.push_back(""); send.push_back(recipients);
    const uint256 fundingHash = uint256S(call("sendmany", send).get_str());
    const auto funding = model.wallet().getTx(fundingHash);
    QVERIFY(funding);
    chain.CreateAndProcessBlock({CMutableTransaction(*funding)}, GetScriptForRawPubKey(chain.coinbaseKey.GetPubKey()));
    SyncWithValidationInterfaceQueue();
    COutPoint collateral;
    for (size_t i = 0; i < funding->vout.size(); ++i) {
        if (funding->vout[i].nValue == 20 * COIN) collateral = COutPoint(fundingHash, i);
    }
    QVERIFY(!collateral.IsNull());
    QVERIFY(!model.wallet().isLockedCoin(collateral));
    CBLSSecretKey operatorSecret;
    operatorSecret.MakeNewKey();
    const QString publicKey = QString::fromStdString(operatorSecret.GetPublicKey().ToString());
    const auto initialMempool = mempool.size();

    for (bool submit : {false, true}) {
        QVERIFY(!model.wallet().isLockedCoin(collateral)); // Cancelling the previous preparation restored its new lock.
        SmartnodeWizard wizard(&model, {});
        auto* amount = wizard.findChild<QComboBox*>("smartnodeCollateralAmount");
        QVERIFY(amount);
        QCOMPARE(amount->count(), 2);
        QCOMPARE(amount->currentData().toLongLong(), qlonglong(10 * COIN));
        const int larger = amount->findData(QVariant::fromValue<qlonglong>(20 * COIN));
        QVERIFY(larger >= 0);
        amount->setCurrentIndex(larger);
        QString message;
        bool allowBroadcast = false;
        QTimer dialogs(&wizard);
        dialogs.setInterval(10);
        connect(&dialogs, &QTimer::timeout, &wizard, [&] {
            for (QWidget* widget : QApplication::topLevelWidgets()) {
                auto* box = qobject_cast<QMessageBox*>(widget);
                if (!box || box->parentWidget() != &wizard) continue;
                message = box->text();
                if (box->button(QMessageBox::Yes)) box->button(allowBroadcast ? QMessageBox::Yes : QMessageBox::Cancel)->click();
                else if (box->button(QMessageBox::Ok)) box->button(QMessageBox::Ok)->click();
            }
        });
        dialogs.start();
        wizard.show();
        wizard.next();
        QCOMPARE(wizard.currentId(), 1);
        auto set = [&](const char* name, const QString& value) {
            auto* field = wizard.findChild<QLineEdit*>(name);
            if (!field) return false;
            field->setText(value);
            return true;
        };
        QVERIFY(set("smartnodeService", QString("127.0.0.1:%1").arg(Params().GetDefaultPort())));
        QVERIFY(set("smartnodeOperatorPublic", publicKey));
        QVERIFY(set("smartnodeOwner", QString::fromStdString(ownerAddress)));
        QVERIFY(set("smartnodePayout", QString::fromStdString(payoutAddress)));
        auto* source = wizard.findChild<QComboBox*>("smartnodeFeeSource");
        QVERIFY(source);
        source->setEditText(QString::fromStdString(feeAddress));
        wizard.next();
        QVERIFY2(wizard.currentId() == 2, qPrintable(message));
        QCOMPARE(mempool.size(), initialMempool); // Preparation and dry-run do not relay or insert.
        QVERIFY(model.wallet().isLockedCoin(collateral));
        if (!submit) {
            wizard.reject();
        } else {
            wizard.accept(); // An unchecked consent box must prevent broadcasting.
            QCOMPARE(mempool.size(), initialMempool);
            auto* consent = wizard.findChild<QCheckBox*>("smartnodeRegistrationConsent");
            QVERIFY(consent);
            consent->setChecked(true);
            allowBroadcast = true; // Explicitly permit only this fixture's regtest confirmation.
            wizard.accept();
            QVERIFY2(wizard.result() == QDialog::Accepted, qPrintable(message));
            QCOMPARE(mempool.size(), initialMempool + 1);
        }
    }
    QVERIFY(model.wallet().isLockedCoin(collateral));
    UniValue query(UniValue::VARR); query.push_back(collateral.hash.ToString()); query.push_back(int64_t(collateral.n));
    const auto stillUnspent = call("gettxout", query);
    QVERIFY(!stillUnspent.isNull());
    QCOMPARE(AmountFromValue(stillUnspent["value"]), 20 * COIN);
    WebWalletPage web;
    auto* openWeb = web.findChild<QPushButton*>("webWalletOpenButton");
    QVERIFY(openWeb && !openWeb->isEnabled()); // Test coins cannot launch the mainnet Web Wallet.
}

extern UniValue createConfigFile(std::string, std::string, std::string);

void WalletTests::legacyConfigExportSafety()
{
    BasicTestingSetup test(CBaseChainParams::REGTEST);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    struct RestoreDirectory {
        fs::path original{fs::current_path()};
        ~RestoreDirectory() { boost::system::error_code error; fs::current_path(original, error); }
    } restore;
    fs::current_path(directory.path().toStdString());
    const auto first = createConfigFile("TEST-ONLY-NOT-A-SECRET", "127.0.0.1", "test-collateral").get_str();
    QFile file(QString::fromStdString(first));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto original = file.readAll();
    file.close();
    QVERIFY(original.contains("rpcbind=127.0.0.1\n"));
    QVERIFY(original.startsWith("regtest=1\n"));
    QVERIFY(original.contains(QByteArray("rpcport=") + QByteArray::number(BaseParams().RPCPort()) + '\n'));
    QVERIFY(QRegularExpression("rpcpassword=[A-Za-z0-9_-]{20}\\n").match(QString::fromUtf8(original)).hasMatch());
    const auto second = createConfigFile("TEST-ONLY-NOT-A-SECRET", "127.0.0.1", "test-collateral").get_str();
    QVERIFY(first != second);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), original);
    file.close();
#ifndef WIN32
    QVERIFY((fs::status(first).permissions() & (fs::group_all | fs::others_all)) == fs::no_perms);
#endif
}
