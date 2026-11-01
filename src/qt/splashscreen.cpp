// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2014-2020 The Dash Core developers
// Copyright (c) 2020-2022 The Babacoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include <config/babacoin-config.h>
// Boost 1.73+: _1, _2 placeholders moved to boost::placeholders namespace
#include <boost/bind/bind.hpp>
using namespace boost::placeholders;

#endif

#include <qt/splashscreen.h>

#include <qt/guiutil.h>
#include <qt/networkstyle.h>

#include <chainparams.h>
#include <clientversion.h>
#include <init.h>
#include <interfaces/handler.h>
#include <interfaces/node.h>
#include <interfaces/wallet.h>
#include <util.h>
#include <ui_interface.h>
#include <version.h>

#include <QApplication>
#include <QCloseEvent>
#include <QDesktopWidget>
#include <QPainter>
#include <QLinearGradient>

SplashScreen::SplashScreen(interfaces::Node& node, Qt::WindowFlags f, const NetworkStyle *networkStyle) :
    QWidget(0, f), curAlignment(0), m_node(node)
{

    setAttribute(Qt::WA_TranslucentBackground);
    setWindowFlags(Qt::FramelessWindowHint);
    setAccessibleName(tr("Babacoin is starting"));
    const int width = 500, height = 300;
    const qreal scale = qApp->devicePixelRatio();
    pixmap = QPixmap(width * scale, height * scale);
    pixmap.setDevicePixelRatio(scale);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    QLinearGradient background(0, 0, width, height);
    background.setColorAt(0, QColor("#0e1c24"));
    background.setColorAt(1, QColor("#203d43"));
    painter.setBrush(background);
    painter.setPen(QPen(QColor("#39545e"), 1));
    painter.drawRoundedRect(QRectF(1, 1, width - 2, height - 2), 18, 18);
    // QIcon handles device-pixel scaling itself. A fixed logical destination
    // avoids applying the application and screen scale factors twice.
    networkStyle->getAppIcon().paint(&painter, QRect(30, 40, 88, 88), Qt::AlignCenter);
    QFont font = QApplication::font();
    font.setPointSize(27);
    font.setWeight(QFont::DemiBold);
    painter.setFont(font);
    painter.setPen(QColor("#f3f7f7"));
    painter.drawText(QRect(142, 47, 330, 45), Qt::AlignLeft | Qt::AlignVCenter, tr("Babacoin"));
    font.setPointSize(11);
    font.setWeight(QFont::Normal);
    painter.setFont(font);
    painter.setPen(QColor("#b1c4cb"));
    const QString version = QString::fromStdString(FormatFullVersion());
    painter.drawText(QRect(144, 95, 320, 24), Qt::AlignLeft,
        painter.fontMetrics().elidedText(version, Qt::ElideRight, 320));
    setToolTip(version);
    const QString network = networkStyle->getTitleAddText();
    painter.setPen(QColor("#e4be60"));
    painter.drawText(QRect(32, 158, 435, 24), Qt::AlignLeft, network.isEmpty() ? tr("BBC CORE") : network);
    painter.setPen(QColor("#38525c"));
    painter.drawLine(30, 198, width - 30, 198);
    painter.end();
    setFixedSize(width, height);
    move(QApplication::desktop()->availableGeometry().center() - rect().center());

    subscribeToCoreSignals();
    installEventFilter(this);
}

SplashScreen::~SplashScreen()
{
    unsubscribeFromCoreSignals();
}

bool SplashScreen::eventFilter(QObject * obj, QEvent * ev) {
    if (ev->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(ev);
        if (!keyEvent->text().isEmpty() && keyEvent->text().at(0).toLower() == QChar('q')) {
            m_node.startShutdown();
        }
    }
    return QObject::eventFilter(obj, ev);
}

void SplashScreen::slotFinish(QWidget *mainWin)
{
    Q_UNUSED(mainWin);

    /* If the window is minimized, hide() will be ignored. */
    /* Make sure we de-minimize the splashscreen window before hiding */
    if (isMinimized())
        showNormal();
    hide();
    deleteLater(); // No more need for this
}

static void InitMessage(SplashScreen *splash, const std::string &message)
{
    QMetaObject::invokeMethod(splash, "showMessage",
        Qt::QueuedConnection,
        Q_ARG(QString, QString::fromStdString(message)),
        Q_ARG(int, Qt::AlignBottom | Qt::AlignHCenter),
        Q_ARG(QColor, Qt::white));
//      Q_ARG(QColor, GUIUtil::getThemedQColor(GUIUtil::ThemedColor::DEFAULT)));
}

static void ShowProgress(SplashScreen *splash, const std::string &title, int nProgress, bool resume_possible)
{
    QMetaObject::invokeMethod(splash, "showProgress", Qt::QueuedConnection,
        Q_ARG(QString, QString::fromStdString(title)), Q_ARG(int, nProgress), Q_ARG(bool, resume_possible));
}

void SplashScreen::subscribeToCoreSignals()
{
    // Connect signals to client
    m_handler_init_message = m_node.handleInitMessage(boost::bind(InitMessage, this, _1));
    m_handler_show_progress = m_node.handleShowProgress(boost::bind(ShowProgress, this, _1, _2, _3));
    m_handler_wallet_scan = m_node.handleWalletScanProgress([this](const WalletScanProgress& scan) {
        QMetaObject::invokeMethod(this, "showWalletScanProgress", Qt::QueuedConnection,
            Q_ARG(QString, QString::fromStdString(scan.wallet_name)), Q_ARG(int, scan.start_height),
            Q_ARG(int, scan.current_height), Q_ARG(int, scan.target_height), Q_ARG(bool, scan.complete));
    });
}

void SplashScreen::unsubscribeFromCoreSignals()
{
    // Disconnect signals from client
    m_handler_init_message->disconnect();
    m_handler_show_progress->disconnect();
    m_handler_wallet_scan->disconnect();
}

void SplashScreen::showWalletScanProgress(const QString& wallet, int start, int current, int target, bool complete)
{
    Q_EMIT startupWalletScanProgress(wallet, start, current, target, complete);
}

void SplashScreen::showMessage(const QString &message, int alignment, const QColor &color)
{
    curMessage = message;
    curAlignment = alignment;
    curColor = color;
    update();
    Q_EMIT startupMessage(message);
}

void SplashScreen::showProgress(const QString& title, int progress, bool resumePossible)
{
    showMessage(title + "\n" + (resumePossible ? tr("press q to shutdown and continue later") : tr("press q to shutdown")) +
        QString("\n%1%").arg(progress), Qt::AlignBottom | Qt::AlignHCenter, Qt::white);
    Q_EMIT startupProgress(title, progress);
}

void SplashScreen::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    // Startup feedback is painted before the application's custom fonts load.
    QFont messageFont = QApplication::font();
    messageFont.setPointSize(11);
    painter.setFont(messageFont);
    painter.drawPixmap(0, 0, pixmap);
    QRect r(30, 212, width() - 60, height() - 228);
    painter.setPen(curColor);
    painter.drawText(r, curAlignment | Qt::TextWordWrap, curMessage);
}

void SplashScreen::closeEvent(QCloseEvent *event)
{
    m_node.startShutdown(); // allows an "emergency" shutdown during startup
    event->ignore();
}
