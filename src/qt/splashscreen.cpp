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
    QPixmap logo = networkStyle->getAppIcon().pixmap(88 * scale, 88 * scale);
    logo.setDevicePixelRatio(scale);
    painter.drawPixmap(30, 40, logo);
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
    painter.drawText(QRect(144, 95, 320, 24), Qt::AlignLeft, QString::fromStdString(FormatFullVersion()));
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
    InitMessage(splash, title + std::string("\n") +
            (resume_possible ? _("(press q to shutdown and continue later)")
                                : _("press q to shutdown")) +
            strprintf("\n%d", nProgress) + "%");
}
#ifdef ENABLE_WALLET
void SplashScreen::ConnectWallet(std::unique_ptr<interfaces::Wallet> wallet)
{
    m_connected_wallet_handlers.emplace_back(wallet->handleShowProgress(boost::bind(ShowProgress, this, _1, _2, false)));
    m_connected_wallets.emplace_back(std::move(wallet));
}
#endif

void SplashScreen::subscribeToCoreSignals()
{
    // Connect signals to client
    m_handler_init_message = m_node.handleInitMessage(boost::bind(InitMessage, this, _1));
    m_handler_show_progress = m_node.handleShowProgress(boost::bind(ShowProgress, this, _1, _2, _3));
#ifdef ENABLE_WALLET
    m_handler_load_wallet = m_node.handleLoadWallet([this](std::unique_ptr<interfaces::Wallet> wallet) { ConnectWallet(std::move(wallet)); });
#endif
}

void SplashScreen::unsubscribeFromCoreSignals()
{
    // Disconnect signals from client
    m_handler_init_message->disconnect();
    m_handler_show_progress->disconnect();
#ifdef ENABLE_WALLET
    m_handler_load_wallet->disconnect();
#endif // ENABLE_WALLET
    for (auto& handler : m_connected_wallet_handlers) {
        handler->disconnect();
    }
    m_connected_wallet_handlers.clear();
    m_connected_wallets.clear();
}

void SplashScreen::showMessage(const QString &message, int alignment, const QColor &color)
{
    curMessage = message;
    curAlignment = alignment;
    curColor = color;
    update();
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
