// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2014-2021 The Dash Core developers
// Copyright (c) 2020-2022 The Babacoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include <config/babacoin-config.h>
#endif

#include <qt/utilitydialog.h>

#include <qt/forms/ui_helpmessagedialog.h>

#include <qt/bitcoingui.h>
#include <qt/clientmodel.h>
#include <qt/guiconstants.h>
#include <qt/guiutil.h>
#include <qt/intro.h>
#include <qt/paymentrequestplus.h>

#include <clientversion.h>
#include <init.h>
#include <interfaces/node.h>
#include <util.h>
#include <ui_interface.h>
#include <QLocale>

#include <stdio.h>

#include <QCloseEvent>
#include <QLabel>
#include <QRegExp>
#include <QTextTable>
#include <QTextCursor>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QClipboard>
#include <QApplication>

StartupPanel::StartupPanel(const QString& version, const QString& network, const QString& dataDirectory, QWidget* parent) :
    QWidget(parent), m_version(version), m_network(network), m_directory(dataDirectory)
{
    setObjectName("walletPage");
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(40, 30, 40, 30);
    root->addStretch();
    auto* title = new QLabel(tr("Starting Babacoin"), this);
    title->setObjectName("walletPageTitle");
    auto* heading = new QHBoxLayout;
    const QPixmap branding(":/images/babacoin_brand_gold");
    if (!branding.isNull()) {
        auto* logo = new QLabel(this);
        logo->setFixedSize(96, 96);
        const qreal ratio = devicePixelRatioF();
        QPixmap coin = branding.copy(0, 0, branding.height(), branding.height()).scaled(
            qRound(96 * ratio), qRound(96 * ratio), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        coin.setDevicePixelRatio(ratio);
        logo->setPixmap(coin);
        heading->addWidget(logo);
    }
    heading->addWidget(title, 1);
    root->addLayout(heading);
    auto* networkLabel = new QLabel(m_network == "main" ? tr("Main network · BBC") : tr("Test network: %1").arg(m_network.toUpper()), this);
    networkLabel->setObjectName("walletNetworkBadge");
    root->addWidget(networkLabel, 0, Qt::AlignLeft);
    auto* explanation = new QLabel(tr("Checking your wallet and blockchain data. Balances and sending become available after these checks finish."), this);
    explanation->setWordWrap(true);
    root->addWidget(explanation);
    m_phase = new QLabel(tr("Preparing the wallet..."), this);
    m_phase->setObjectName("startupPhase");
    m_phase->setWordWrap(true);
    root->addSpacing(24);
    root->addWidget(m_phase);
    m_progress = new QProgressBar(this);
    m_progress->setObjectName("startupProgress");
    m_progress->setRange(0, 0);
    root->addWidget(m_progress);
    m_scanSummary = new QLabel(this);
    m_scanSummary->setObjectName("startupScanSummary");
    m_scanSummary->setTextFormat(Qt::PlainText);
    m_scanSummary->setWordWrap(true);
    m_scanSummary->hide();
    root->addWidget(m_scanSummary);
    m_elapsed = new QLabel(this);
    m_elapsed->setObjectName("startupElapsed");
    root->addWidget(m_elapsed);
    auto* folder = new QLabel(tr("Wallet data folder: %1").arg(m_directory), this);
    folder->setObjectName("startupDataDirectory");
    folder->setWordWrap(true);
    folder->setTextFormat(Qt::PlainText);
    root->addSpacing(12);
    root->addWidget(folder);
    auto* actions = new QHBoxLayout;
    auto* copy = new QPushButton(tr("Copy diagnostics"), this);
    copy->setObjectName("copyStartupDiagnostics");
    connect(copy, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(diagnostics()); });
    actions->addWidget(copy);
    actions->addStretch();
    m_stop = new QPushButton(tr("Close safely"), this);
    m_stop->setObjectName("stopStartup");
    connect(m_stop, &QPushButton::clicked, this, &StartupPanel::requestStop);
    actions->addWidget(m_stop);
    root->addSpacing(16);
    root->addLayout(actions);
    root->addStretch();
    GUIUtil::setFont({title}, GUIUtil::FontWeight::Bold, 24);
    GUIUtil::setFont({copy, m_stop, networkLabel}, GUIUtil::FontWeight::Normal, 10);
    GUIUtil::setFont({explanation, m_phase, folder, m_elapsed, m_scanSummary}, GUIUtil::FontWeight::Normal, 11);
    m_clock.start();
    auto updateElapsed = [this] {
        const qint64 seconds = m_clock.elapsed() / 1000;
        m_elapsed->setText(tr("Elapsed: %1:%2").arg(seconds / 60, 2, 10, QChar('0')).arg(seconds % 60, 2, 10, QChar('0')));
    };
    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, updateElapsed);
    timer->start(1000);
    updateElapsed();
}

void StartupPanel::setPhase(const QString& message)
{
    if (m_stopping) return;
    m_phase->setText(message);
    m_phase->setTextFormat(Qt::PlainText);
    m_progress->setRange(0, 0);
    m_scanSummary->clear();
    m_scanSummary->hide();
}

void StartupPanel::setProgress(const QString& title, int progress)
{
    if (m_stopping) return;
    m_phase->setText(tr("%1 — %2%").arg(title).arg(qBound(0, progress, 100)));
    m_phase->setTextFormat(Qt::PlainText);
    m_progress->setRange(0, 100);
    m_progress->setFormat("%p%");
    m_progress->setValue(qBound(0, progress, 100));
    m_scanSummary->clear();
    m_scanSummary->hide();
}

void StartupPanel::setWalletScanProgress(const QString& wallet, int start, int current, int target, bool complete)
{
    if (m_stopping) return;
    const WalletScanProgress scan{wallet.toStdString(), start, current, target, complete};
    const QLocale locale = QLocale::system();
    const int value = scan.basisPoints();
    const QString percent = locale.toString(value / 100) + locale.decimalPoint() +
        QString("%1").arg(value % 100, 2, 10, QChar('0')) + "%";
    m_phase->setText(wallet.isEmpty() ? tr("Rescanning wallet") : tr("Rescanning wallet: %1").arg(wallet));
    m_phase->setTextFormat(Qt::PlainText);
    m_progress->setRange(0, 10000);
    m_progress->setValue(value);
    m_progress->setFormat(percent);
    // Theme progress bars deliberately hide their text. Keep the percentage in
    // a normal label so it stays readable in either theme and at every scale.
    m_scanSummary->setText(tr("%1 complete · Blocks checked: %2 / %3\nCurrent height: %4 / %5")
        .arg(percent, locale.toString(qlonglong(scan.scannedBlocks())), locale.toString(qlonglong(scan.totalBlocks())),
            locale.toString(qlonglong(current)), locale.toString(qlonglong(target))));
    m_scanSummary->show();
}

void StartupPanel::requestStop()
{
    if (m_stopping) return;
    m_stopping = true;
    m_phase->setText(tr("Stopping safely. Please wait..."));
    m_progress->setRange(0, 0);
    m_stop->setEnabled(false);
    Q_EMIT stopRequested();
}

QString StartupPanel::diagnostics() const
{
    return QString("Babacoin startup diagnostics\nVersion: %1\nNetwork: %2\nData directory: %3\nPhase: %4\nElapsed seconds: %5\nWallet readiness: startup checks incomplete\n")
        .arg(m_version, m_network, m_directory, m_phase->text()).arg(m_clock.elapsed() / 1000) +
        (m_scanSummary->text().isEmpty() ? QString() : m_scanSummary->text() + "\nProgress: " + m_progress->format() + "\n");
}

/** "Help message" or "About" dialog box */
HelpMessageDialog::HelpMessageDialog(interfaces::Node& node, QWidget *parent, HelpMode helpMode) :
    QDialog(parent),
    ui(new Ui::HelpMessageDialog)
{
    ui->setupUi(this);

    GUIUtil::updateFonts();

    QString version = tr(PACKAGE_NAME) + " " + tr("version") + " " + QString::fromStdString(FormatFullVersion());
    /* On x86 add a bit specifier to the version so that users can distinguish between
     * 32 and 64 bit builds. On other architectures, 32/64 bit may be more ambiguous.
     */
#if defined(__x86_64__)
    version += " " + tr("(%1-bit)").arg(64);
#elif defined(__i386__ )
    version += " " + tr("(%1-bit)").arg(32);
#endif

    if (helpMode == about)
    {
        setWindowTitle(tr("About %1").arg(tr(PACKAGE_NAME)));

        /// HTML-format the license message from the core
        QString licenseInfo = QString::fromStdString(LicenseInfo());
        QString licenseInfoHTML = licenseInfo;

        // Make URLs clickable
        QRegExp uri("<(.*)>", Qt::CaseSensitive, QRegExp::RegExp2);
        uri.setMinimal(true); // use non-greedy matching
        licenseInfoHTML.replace(uri, QString("<a style=\"%1\"href=\"\\1\">\\1</a>").arg(GUIUtil::getThemedStyleQString(GUIUtil::ThemedStyle::TS_COMMAND)));
        // Replace newlines with HTML breaks
        licenseInfoHTML.replace("\n", "<br>");

        ui->aboutMessage->setTextFormat(Qt::RichText);
        ui->scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        text = version + "\n" + licenseInfo;
        ui->aboutMessage->setText(version + "<br><br>" + licenseInfoHTML);
        ui->aboutMessage->setWordWrap(true);
        ui->helpMessage->setVisible(false);
    } else if (helpMode == cmdline) {
        setWindowTitle(tr("Command-line options"));
        QString header = "Usage:\n"
            "  babacoin-qt [command-line options]                     \n";
        QTextCursor cursor(ui->helpMessage->document());
        cursor.insertText(version);
        cursor.insertBlock();
        cursor.insertText(header);
        cursor.insertBlock();

        std::string strUsage = gArgs.GetHelpMessage();
        QString coreOptions = QString::fromStdString(strUsage);
        text = version + "\n" + header + "\n" + coreOptions;

        QTextTableFormat tf;
        tf.setBorderStyle(QTextFrameFormat::BorderStyle_None);
        tf.setCellPadding(2);
        QVector<QTextLength> widths;
        widths << QTextLength(QTextLength::PercentageLength, 35);
        widths << QTextLength(QTextLength::PercentageLength, 65);
        tf.setColumnWidthConstraints(widths);

        QTextCharFormat bold;
        bold.setFontWeight(QFont::Bold);

        for (const QString &line : coreOptions.split("\n")) {
            if (line.startsWith("  -"))
            {
                cursor.currentTable()->appendRows(1);
                cursor.movePosition(QTextCursor::PreviousCell);
                cursor.movePosition(QTextCursor::NextRow);
                cursor.insertText(line.trimmed());
                cursor.movePosition(QTextCursor::NextCell);
            } else if (line.startsWith("   ")) {
                cursor.insertText(line.trimmed()+' ');
            } else if (line.size() > 0) {
                //Title of a group
                if (cursor.currentTable())
                    cursor.currentTable()->appendRows(1);
                cursor.movePosition(QTextCursor::Down);
                cursor.insertText(line.trimmed(), bold);
                cursor.insertTable(1, 2, tf);
            }
        }

        ui->helpMessage->moveCursor(QTextCursor::Start);
        ui->scrollArea->setVisible(false);
    } else if (helpMode == pshelp) {
        setWindowTitle(tr("%1 information").arg("CoinJoin"));

        ui->aboutMessage->setTextFormat(Qt::RichText);
        ui->scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        ui->aboutMessage->setText(tr("\
<h3>%1 Basics</h3> \
%1 gives you true financial privacy by obscuring the origins of your funds. \
All the Babacoin in your wallet is comprised of different \"inputs\" which you can think of as separate, discrete coins.<br> \
%1 uses an innovative process to mix your inputs with the inputs of two or more other people, without having your coins ever leave your wallet. \
You retain control of your money at all times.<hr> \
<b>The %1 process works like this:</b>\
<ol type=\"1\"> \
<li>%1 begins by breaking your transaction inputs down into standard denominations. \
These denominations are 0.001 BBC, 0.01 BBC, 0.1 BBC, 1 BBC and 10 BBC -- sort of like the paper money you use every day.</li> \
<li>Your wallet then sends requests to specially configured software nodes on the network, called \"smartnodes.\" \
These smartnodes are informed then that you are interested in mixing a certain denomination. \
No identifiable information is sent to the smartnodes, so they never know \"who\" you are.</li> \
<li>When two or more other people send similar messages, indicating that they wish to mix the same denomination, a mixing session begins. \
The smartnode mixes up the inputs and instructs all three users' wallets to pay the now-transformed input back to themselves. \
Your wallet pays that denomination directly to itself, but in a different address (called a change address).</li> \
<li>In order to fully obscure your funds, your wallet must repeat this process a number of times with each denomination. \
Each time the process is completed, it's called a \"round.\" Each round of %1 makes it exponentially more difficult to determine where your funds originated.</li> \
<li>This mixing process happens in the background without any intervention on your part. When you wish to make a transaction, \
your funds will already be mixed. No additional waiting is required.</li> \
</ol> <hr>\
<b>IMPORTANT:</b> Your wallet only contains 1000 of these \"change addresses.\" Every time a mixing event happens, up to 9 of your addresses are used up. \
This means those 1000 addresses last for about 100 mixing events. When 900 of them are used, your wallet must create more addresses. \
It can only do this, however, if you have automatic backups enabled.<br> \
Consequently, users who have backups disabled will also have %1 disabled. <hr>\
For more information, see the <a style=\"%2\" href=\"%3\">%1 documentation</a>."
        )
        .arg("CoinJoin")
        .arg(GUIUtil::getThemedStyleQString(GUIUtil::ThemedStyle::TS_COMMAND))
        .arg("https://docs.dash.org/en/stable/wallets/dashcore/coinjoin-instantsend.html")
        );
        ui->aboutMessage->setWordWrap(true);
        ui->helpMessage->setVisible(false);
    }
}

HelpMessageDialog::~HelpMessageDialog()
{
    delete ui;
}

void HelpMessageDialog::printToConsole()
{
    // On other operating systems, the expected action is to print the message to the console.
    fprintf(stdout, "%s\n", qPrintable(text));
}

void HelpMessageDialog::showOrPrint()
{
#if defined(WIN32)
    // On Windows, show a message box, as there is no stderr/stdout in windowed applications
    exec();
#else
    // On other operating systems, print help text to console
    printToConsole();
#endif
}

void HelpMessageDialog::on_okButton_accepted()
{
    close();
}


/** "Shutdown" window */
ShutdownWindow::ShutdownWindow(interfaces::Node& node, QWidget *parent, Qt::WindowFlags f):
    QWidget(parent, f)
{
    setObjectName("ShutdownWindow");

    QVBoxLayout *layout = new QVBoxLayout();
    layout->addWidget(new QLabel(
        tr("%1 is shutting down...").arg(tr(PACKAGE_NAME)) + "<br /><br />" +
        tr("Do not shut down the computer until this window disappears.")));
    setLayout(layout);

    GUIUtil::updateFonts();
}

QWidget *ShutdownWindow::showShutdownWindow(interfaces::Node& node, BitcoinGUI *window)
{
    if (!window)
        return nullptr;

    // Show a simple window indicating shutdown status
    QWidget *shutdownWindow = new ShutdownWindow(node);
    shutdownWindow->setWindowTitle(window->windowTitle());

    // Center shutdown window at where main window was
    const QPoint global = window->mapToGlobal(window->rect().center());
    shutdownWindow->move(global.x() - shutdownWindow->width() / 2, global.y() - shutdownWindow->height() / 2);
    shutdownWindow->show();
    return shutdownWindow;
}

void ShutdownWindow::closeEvent(QCloseEvent *event)
{
    event->ignore();
}
