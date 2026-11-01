// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2014-2021 The Dash Core developers
// Copyright (c) 2020-2022 The Babacoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/overviewpage.h>
#include <qt/forms/ui_overviewpage.h>

#include <qt/bitcoinunits.h>
#include <qt/clientmodel.h>
#include <qt/guiutil.h>
#include <qt/optionsmodel.h>
#include <qt/transactionfilterproxy.h>
#include <qt/transactiontablemodel.h>
#include <qt/utilitydialog.h>
#include <qt/walletmodel.h>

#include <coinjoin/coinjoin-client-options.h>

#include <QAbstractItemDelegate>
#include <QPainter>
#include <QSettings>
#include <QTimer>
#include <QScrollArea>
#include <QCoreApplication>
#include <QFrame>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPainterPath>
#include <QLinearGradient>
#include <QLocale>
#include <QStyle>
#include <cmath>
#include <algorithm>

#define ITEM_HEIGHT 68
#define NUM_ITEMS_DISABLED 5
#define NUM_ITEMS_ENABLED_NORMAL 6
#define NUM_ITEMS_ENABLED_ADVANCED 8

Q_DECLARE_METATYPE(interfaces::WalletBalances)

// Decorative only: fixed geometry, no timers, balances or network operations.
class WalletBalanceHero : public QFrame
{
public:
    explicit WalletBalanceHero(QWidget* parent) : QFrame(parent) { setMinimumHeight(176); }
protected:
    void paintEvent(QPaintEvent* event) override
    {
        QFrame::paintEvent(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QPainterPath clip;
        clip.addRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 20, 20);
        painter.setClipPath(clip);
        painter.setClipRect(QRectF(width() * 0.62, 0, width() * 0.38, height()), Qt::IntersectClip);
        const QPointF center(width() - 72, height() * 0.52);
        painter.setBrush(Qt::NoBrush);
        for (int radius : {76, 110, 148, 192}) {
            painter.setPen(QPen(QColor(228, 190, 96, radius == 110 ? 65 : 25), 1));
            painter.drawEllipse(center, radius, radius);
        }
        painter.setPen(QPen(QColor(228, 190, 96, 45), 1));
        const QPointF a = center + QPointF(-114, -64), b = center + QPointF(-158, 46);
        painter.drawLine(a, b);
        painter.drawLine(b, center + QPointF(-22, 88));
        painter.setBrush(QColor("#e4be60"));
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(a, 3, 3);
        painter.drawEllipse(b, 4, 4);
    }
};

// Bounded presentation of the entries already displayed in the recent list.
// It never scans wallet history or computes balances, prices or investment returns.
class WalletActivityGraph : public QWidget
{
public:
    explicit WalletActivityGraph(QWidget* parent = nullptr) : QWidget(parent) { setMinimumHeight(110); }
    void setValues(const QVector<qint64>& entries) { values = entries; update(); }
protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF plot = QRectF(rect()).adjusted(12, 12, -12, -18);
        const qreal middle = plot.center().y();
        QColor muted = GUIUtil::getThemedQColor(GUIUtil::ThemedColor::BAREADDRESS);
        painter.setPen(QPen(muted, 1));
        if (values.isEmpty()) {
            painter.drawText(plot, Qt::AlignCenter, QCoreApplication::translate("OverviewPage", "No recent activity"));
            return;
        }
        QColor grid = muted;
        grid.setAlpha(35);
        painter.setPen(QPen(grid, 1, Qt::DashLine));
        for (qreal y : {plot.top(), plot.bottom()})
            painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        painter.setPen(QPen(muted, 1));
        painter.drawLine(QPointF(plot.left(), middle), QPointF(plot.right(), middle));
        long double maximum = 1;
        for (qint64 value : values) maximum = std::max(maximum, std::fabs(static_cast<long double>(value)));
        const qreal slot = plot.width() / values.size();
        for (int i = 0; i < values.size(); ++i) {
            const qreal height = static_cast<qreal>(std::fabs(static_cast<long double>(values[i])) / maximum) * (plot.height() / 2 - 6);
            const QColor color = GUIUtil::getThemedQColor(values[i] >= 0 ? GUIUtil::ThemedColor::GREEN : GUIUtil::ThemedColor::RED);
            painter.setPen(Qt::NoPen);
            QLinearGradient fill(0, middle - height, 0, middle + height);
            fill.setColorAt(0, color.lighter(125));
            fill.setColorAt(1, color.darker(115));
            painter.setBrush(fill);
            const qreal width = std::min(qreal(36), slot * 0.55);
            const qreal x = plot.left() + slot * i + (slot - width) / 2;
            painter.drawRoundedRect(QRectF(x, values[i] >= 0 ? middle - height : middle, width, height), 3, 3);
        }
    }
private:
    QVector<qint64> values;
};

class TxViewDelegate : public QAbstractItemDelegate
{
    Q_OBJECT
public:
    explicit TxViewDelegate(QObject* parent = nullptr) :
        QAbstractItemDelegate(), unit(BitcoinUnits::BBC)
    {

    }

    inline void paint(QPainter *painter, const QStyleOptionViewItem &option,
                      const QModelIndex &index ) const
    {
        painter->save();

        QRect mainRect = option.rect;
        if (option.state & (QStyle::State_Selected | QStyle::State_MouseOver)) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(228, 190, 96, option.state & QStyle::State_Selected ? 38 : 18));
            painter->drawRoundedRect(QRectF(mainRect).adjusted(1, 2, -1, -2), 10, 10);
        }
        int xspace = 56;
        int ypad = 8;
        int halfheight = (mainRect.height() - 2*ypad)/2;
        QRect rectTopHalf(mainRect.left() + xspace, mainRect.top() + ypad, mainRect.width() - xspace, halfheight);
        QRect rectBottomHalf(mainRect.left() + xspace, mainRect.top() + ypad + halfheight + 5, mainRect.width() - xspace, halfheight);
        QRect rectBounding;
        QColor colorForeground;
        qreal initialFontSize = painter->font().pointSizeF();

        // Grab model indexes for desired data from TransactionTableModel
        QModelIndex indexDate = index.sibling(index.row(), TransactionTableModel::Date);
        QModelIndex indexAmount = index.sibling(index.row(), TransactionTableModel::Amount);
        QModelIndex indexAddress = index.sibling(index.row(), TransactionTableModel::ToAddress);

        // Draw first line (with slightly bigger font than the second line will get)
        // Content: Date/Time, Optional IS indicator, Amount
        painter->setFont(GUIUtil::getFont(GUIUtil::FontWeight::Normal, false, GUIUtil::getScaledFontSize(initialFontSize * 1.17)));
        // Date/Time
        colorForeground = qvariant_cast<QColor>(indexDate.data(Qt::ForegroundRole));
        QString strDate = indexDate.data(Qt::DisplayRole).toString();
        painter->setPen(colorForeground);
        // Amount
        colorForeground = qvariant_cast<QColor>(indexAmount.data(Qt::ForegroundRole));
        // Note: do NOT use Qt::DisplayRole, have format properly here
        qint64 nAmount = index.data(TransactionTableModel::AmountRole).toLongLong();
        QString strAmount = BitcoinUnits::floorLocalizedWithUnit(unit, nAmount, true, BitcoinUnits::separatorAlways);
        // Reserve the amount column before drawing the date. Long localized
        // amounts and dates must not paint over one another at large font sizes.
        const int amountWidth = painter->fontMetrics().width(strAmount) + 16;
        const QRect dateRect = rectTopHalf.adjusted(0, 0, -amountWidth, 0);
        painter->setPen(qvariant_cast<QColor>(indexDate.data(Qt::ForegroundRole)));
        painter->drawText(dateRect, Qt::AlignLeft | Qt::AlignVCenter,
            painter->fontMetrics().elidedText(strDate, Qt::ElideRight, std::max(0, dateRect.width() - 22)), &rectBounding);
        QIcon iconInstantSend = qvariant_cast<QIcon>(indexAddress.data(TransactionTableModel::RawDecorationRole));
        if (rectBounding.right() + 21 < dateRect.right())
            iconInstantSend.paint(painter, QRect(rectBounding.right() + 5, dateRect.top(), 16, halfheight));
        painter->setPen(colorForeground);
        painter->drawText(rectTopHalf, Qt::AlignRight | Qt::AlignVCenter, strAmount);

        // Draw second line (with the initial font)
        // Content: Address/label, Optional Watchonly indicator
        painter->setFont(GUIUtil::getFont(GUIUtil::FontWeight::Normal, false, GUIUtil::getScaledFontSize(initialFontSize)));
        // Address/Label
        colorForeground = qvariant_cast<QColor>(indexAddress.data(Qt::ForegroundRole));
        QString address = indexAddress.data(Qt::DisplayRole).toString();
        painter->setPen(colorForeground);
        painter->drawText(rectBottomHalf, Qt::AlignLeft | Qt::AlignVCenter,
            painter->fontMetrics().elidedText(address, Qt::ElideMiddle, std::max(0, rectBottomHalf.width() - 24)), &rectBounding);
        // Optional Watchonly indicator
        if (index.data(TransactionTableModel::WatchonlyRole).toBool())
        {
            QIcon iconWatchonly = qvariant_cast<QIcon>(index.data(TransactionTableModel::WatchonlyDecorationRole));
            QRect rectWatchonly(rectBounding.right() + 5, rectBottomHalf.top(), 16, halfheight);
            iconWatchonly.paint(painter, rectWatchonly);
        }

        const QColor direction = GUIUtil::getThemedQColor(nAmount >= 0 ? GUIUtil::ThemedColor::GREEN : GUIUtil::ThemedColor::RED);
        QColor badge = direction;
        badge.setAlpha(28);
        painter->setPen(Qt::NoPen);
        painter->setBrush(badge);
        const QRectF icon(mainRect.left() + 6, mainRect.center().y() - 18, 36, 36);
        painter->drawRoundedRect(icon, 10, 10);
        painter->setPen(QPen(direction, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        const QPointF c = icon.center();
        const int d = nAmount < 0 ? -1 : 1;
        painter->drawLine(c + QPointF(0, -8), c + QPointF(0, 8));
        painter->drawLine(c + QPointF(0, d * 8), c + QPointF(-5, d * 3));
        painter->drawLine(c + QPointF(0, d * 8), c + QPointF(5, d * 3));
        painter->restore();
    }

    inline QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
    {
        return QSize(ITEM_HEIGHT, ITEM_HEIGHT);
    }

    int unit;

};
#include <qt/overviewpage.moc>

OverviewPage::OverviewPage(QWidget* parent) :
    QWidget(parent),
    timer(nullptr),
    ui(new Ui::OverviewPage),
    clientModel(0),
    walletModel(0),
    cachedNumISLocks(-1),
    txdelegate(new TxViewDelegate(this)),
    activityGraph(nullptr),
    emptyTransactions(nullptr),
    historyLoadingNotice(nullptr),
    heroBalance(nullptr)
{
    ui->setupUi(this);
    setMinimumWidth(640);
    ui->topLayout->setContentsMargins(22, 8, 22, 22);
    ui->topLayout->setSpacing(20);
    ui->horizontalLayout->setSpacing(18);
    ui->label_5->setText(tr("Balance details"));
    ui->label_4->setText(tr("Recent transactions"));

    QFrame* hero = new WalletBalanceHero(this);
    hero->setObjectName("balanceHero");
    QHBoxLayout* heroLayout = new QHBoxLayout(hero);
    heroLayout->setContentsMargins(30, 26, 30, 26);
    QVBoxLayout* totalLayout = new QVBoxLayout;
    totalLayout->setSpacing(8);
    QLabel* totalCaption = new QLabel(tr("YOUR BABACOIN"), hero);
    totalCaption->setObjectName("heroCaption");
    totalLayout->addWidget(totalCaption);
    heroBalance = new QLabel(ui->labelTotal->text(), hero);
    heroBalance->setObjectName("heroTotal");
    heroBalance->setTextInteractionFlags(Qt::TextSelectableByMouse);
    totalLayout->addWidget(heroBalance);
    QLabel* balanceHint = new QLabel(tr("Total balance · Spendable and pending amounts are shown below."), hero);
    balanceHint->setObjectName("balanceHint");
    balanceHint->setWordWrap(true);
    totalLayout->addWidget(balanceHint);
    heroLayout->addLayout(totalLayout, 1);
    QLabel* coin = new QLabel(hero);
    const QPixmap brand(":/images/babacoin_brand_gold");
    const qreal ratio = devicePixelRatioF();
    QPixmap coinImage = (brand.isNull() ? QPixmap(":/icons/babacoin") : brand.copy(0, 0, brand.height(), brand.height()))
        .scaled(qRound(104 * ratio), qRound(104 * ratio), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    coinImage.setDevicePixelRatio(ratio);
    coin->setPixmap(coinImage);
    coin->setFixedSize(104, 104);
    heroLayout->addWidget(coin);
    ui->topLayout->insertWidget(1, hero);

    auto* networkStrip = new QWidget(this);
    auto* networkLayout = new QHBoxLayout(networkStrip);
    networkLayout->setContentsMargins(0, 0, 0, 0);
    networkLayout->setSpacing(14);
    auto addMetric = [&](const QString& title, const QString& name) {
        auto* card = new QFrame(networkStrip);
        card->setObjectName("networkMetricCard");
        auto* layout = new QVBoxLayout(card);
        layout->setContentsMargins(18, 14, 18, 14);
        auto* caption = new QLabel(title, card);
        caption->setObjectName("metricCaption");
        auto* value = new QLabel(tr("Waiting..."), card);
        value->setObjectName(name);
        value->setWordWrap(true);
        value->setTextFormat(Qt::PlainText);
        layout->addWidget(caption);
        layout->addWidget(value);
        GUIUtil::setFont({caption}, GUIUtil::FontWeight::Normal, 9);
        GUIUtil::setFont({value}, GUIUtil::FontWeight::Bold, 13);
        networkLayout->addWidget(card, 1);
        return value;
    };
    connectionValue = addMetric(tr("CONNECTION"), "connectionValue");
    blockHeightValue = addMetric(tr("BLOCK HEIGHT"), "blockHeightValue");
    syncValue = addMetric(tr("SYNCHRONIZATION"), "syncValue");
    ui->topLayout->insertWidget(2, networkStrip);

    QFrame* activity = new QFrame(this);
    activity->setObjectName("activityCard");
    QVBoxLayout* activityLayout = new QVBoxLayout(activity);
    activityLayout->setContentsMargins(18, 16, 18, 14);
    QLabel* activityTitle = new QLabel(tr("Recent movements"), activity);
    activityTitle->setObjectName("activityTitle");
    activityLayout->addWidget(activityTitle);
    QLabel* activityHint = new QLabel(tr("Relative sizes of the entries shown here"), activity);
    activityHint->setObjectName("activityHint");
    activityHint->setWordWrap(true);
    activityLayout->addWidget(activityHint);
    activityGraph = new WalletActivityGraph(activity);
    activityGraph->setToolTip(tr("Credits appear above the line; debits below. Exact amounts are shown in recent transactions."));
    activityLayout->addWidget(activityGraph);
    auto* legend = new QHBoxLayout;
    auto* incoming = new QLabel(tr("↓ Incoming"), activity);
    incoming->setObjectName("activityCreditLegend");
    auto* outgoing = new QLabel(tr("↑ Outgoing"), activity);
    outgoing->setObjectName("activityDebitLegend");
    legend->addWidget(incoming);
    legend->addWidget(outgoing);
    legend->addStretch();
    activityLayout->addLayout(legend);
    GUIUtil::setFont({incoming, outgoing}, GUIUtil::FontWeight::Normal, 9);
    // Retire the old expanding spacer between the two dashboard cards.
    ui->verticalLayout_2->removeItem(ui->verticalSpacer);
    delete ui->verticalSpacer;
    ui->verticalSpacer = nullptr;
    ui->verticalLayout_2->addWidget(activity);
    ui->verticalLayout_2->setSpacing(18);

    historyLoadingNotice = new QLabel(tr("Loading transaction history..."), this);
    historyLoadingNotice->setObjectName("historyLoadingNotice");
    historyLoadingNotice->setWordWrap(true);
    historyLoadingNotice->hide();
    ui->verticalLayout->insertWidget(1, historyLoadingNotice);
    emptyTransactions = new QLabel(tr("No recent transactions") + "\n\n" + tr("Your wallet activity will appear here."), this);
    emptyTransactions->setObjectName("emptyTransactions");
    emptyTransactions->setAlignment(Qt::AlignCenter);
    emptyTransactions->setWordWrap(true);
    ui->verticalLayout->addWidget(emptyTransactions, 1);
    ui->listTransactions->hide();

    GUIUtil::setFont({heroBalance}, GUIUtil::FontWeight::Bold, 30);
    GUIUtil::setFont({totalCaption}, GUIUtil::FontWeight::Normal, 10);
    GUIUtil::setFont({balanceHint}, GUIUtil::FontWeight::Normal, 9);
    GUIUtil::setFont({activityTitle}, GUIUtil::FontWeight::Bold, 12);
    GUIUtil::setFont({activityHint}, GUIUtil::FontWeight::Normal, 8);
    GUIUtil::setFont({emptyTransactions}, GUIUtil::FontWeight::Normal, 11);
    GUIUtil::setFont({historyLoadingNotice}, GUIUtil::FontWeight::Normal, 9);

    // Scroll the content instead of clipping balances on smaller screens or
    // when the user's font scale is large. Existing wallet widgets are reused.
    QWidget* body = new QWidget;
    body->setObjectName("walletDashboardBody");
    body->setLayout(ui->topLayout);
    QScrollArea* scroll = new QScrollArea(this);
    scroll->setObjectName("walletDashboardScroll");
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setWidget(body);
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->addWidget(scroll);

    GUIUtil::setFont({ui->label_4,
                      ui->label_5,
                      ui->labelCoinJoinHeader
                     }, GUIUtil::FontWeight::Bold, 12);

    GUIUtil::setFont({ui->labelTotalText,
                      ui->labelWatchTotal,
                      ui->labelTotal
                     }, GUIUtil::FontWeight::Bold, 14);

    GUIUtil::setFont({ui->labelBalanceText,
                      ui->labelPendingText,
                      ui->labelImmatureText,
                      ui->labelWatchonly,
                      ui->labelSpendable
                     }, GUIUtil::FontWeight::Bold);

    GUIUtil::updateFonts();

    m_balances.balance = -1;

    // Recent transactions
    ui->listTransactions->setItemDelegate(txdelegate);
    ui->listTransactions->setResizeMode(QListView::Adjust);
    ui->listTransactions->setMouseTracking(true);
    // Note: minimum height of listTransactions will be set later in updateAdvancedCJUI() to reflect actual settings
    ui->listTransactions->setAttribute(Qt::WA_MacShowFocusRect, false);

    connect(ui->listTransactions, SIGNAL(clicked(QModelIndex)), this, SLOT(handleTransactionClicked(QModelIndex)));

    // init "out of sync" warning labels
    ui->labelWalletStatus->setText("(" + tr("out of sync") + ")");
    ui->labelCoinJoinSyncStatus->setText("(" + tr("out of sync") + ")");
    ui->labelTransactionsStatus->setText("(" + tr("out of sync") + ")");

    ui->labelAnonymizedText->setText(tr("%1 Balance").arg("CoinJoin"));

    // hide PS frame (helps to preserve saved size)
    // we'll setup and make it visible in coinJoinStatus() later
    ui->frameCoinJoin->setVisible(false);

    // start with displaying the "out of sync" warnings
    showOutOfSyncWarning(true);

    timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(coinJoinStatus()));
}

void OverviewPage::handleTransactionClicked(const QModelIndex &index)
{
    if(filter)
        Q_EMIT transactionClicked(filter->mapToSource(index));
}

void OverviewPage::handleOutOfSyncWarningClicks()
{
    Q_EMIT outOfSyncWarningClicked();
}

OverviewPage::~OverviewPage()
{
    if(timer) disconnect(timer, SIGNAL(timeout()), this, SLOT(coinJoinStatus()));
    delete ui;
}

void OverviewPage::setBalance(const interfaces::WalletBalances& balances)
{
    int unit = walletModel->getOptionsModel()->getDisplayUnit();
    m_balances = balances;
    ui->labelBalance->setText(BitcoinUnits::floorLocalizedHtmlWithUnit(unit, balances.balance, false, BitcoinUnits::separatorAlways));
    ui->labelUnconfirmed->setText(BitcoinUnits::floorLocalizedHtmlWithUnit(unit, balances.unconfirmed_balance, false, BitcoinUnits::separatorAlways));
    ui->labelImmature->setText(BitcoinUnits::floorLocalizedHtmlWithUnit(unit, balances.immature_balance, false, BitcoinUnits::separatorAlways));
    ui->labelAnonymized->setText(BitcoinUnits::floorLocalizedHtmlWithUnit(unit, balances.anonymized_balance, false, BitcoinUnits::separatorAlways));
    ui->labelTotal->setText(BitcoinUnits::floorLocalizedHtmlWithUnit(unit, balances.balance + balances.unconfirmed_balance + balances.immature_balance, false, BitcoinUnits::separatorAlways));
    heroBalance->setText(ui->labelTotal->text());
    ui->labelWatchAvailable->setText(BitcoinUnits::floorLocalizedHtmlWithUnit(unit, balances.watch_only_balance, false, BitcoinUnits::separatorAlways));
    ui->labelWatchPending->setText(BitcoinUnits::floorLocalizedHtmlWithUnit(unit, balances.unconfirmed_watch_only_balance, false, BitcoinUnits::separatorAlways));
    ui->labelWatchImmature->setText(BitcoinUnits::floorLocalizedHtmlWithUnit(unit, balances.immature_watch_only_balance, false, BitcoinUnits::separatorAlways));
    ui->labelWatchTotal->setText(BitcoinUnits::floorLocalizedHtmlWithUnit(unit, balances.watch_only_balance + balances.unconfirmed_watch_only_balance + balances.immature_watch_only_balance, false, BitcoinUnits::separatorAlways));

    // only show immature (newly mined) balance if it's non-zero, so as not to complicate things
    // for the non-mining users
    bool fDebugUI = gArgs.GetBoolArg("-debug-ui", false);
    bool showImmature = fDebugUI || balances.immature_balance != 0;
    bool showWatchOnlyImmature = fDebugUI || balances.immature_watch_only_balance != 0;

    // for symmetry reasons also show immature label when the watch-only one is shown
    ui->labelImmature->setVisible(showImmature || showWatchOnlyImmature);
    ui->labelImmatureText->setVisible(showImmature || showWatchOnlyImmature);
    ui->labelWatchImmature->setVisible(showWatchOnlyImmature); // show watch-only immature balance

    updateCoinJoinProgress();

    if (walletModel) {
        int numISLocks = walletModel->getNumISLocks();
        if(cachedNumISLocks != numISLocks) {
            cachedNumISLocks = numISLocks;
            ui->listTransactions->update();
        }
    }
}

// show/hide watch-only labels
void OverviewPage::updateWatchOnlyLabels(bool showWatchOnly)
{
    ui->labelSpendable->setVisible(showWatchOnly);      // show spendable label (only when watch-only is active)
    ui->labelWatchonly->setVisible(showWatchOnly);      // show watch-only label
    ui->lineWatchBalance->setVisible(showWatchOnly);    // show watch-only balance separator line
    ui->labelWatchAvailable->setVisible(showWatchOnly); // show watch-only available balance
    ui->labelWatchPending->setVisible(showWatchOnly);   // show watch-only pending balance
    ui->labelWatchTotal->setVisible(showWatchOnly);     // show watch-only total balance

    if (!showWatchOnly){
        ui->labelWatchImmature->hide();
    }
    else{
        ui->labelBalance->setIndent(20);
        ui->labelUnconfirmed->setIndent(20);
        ui->labelImmature->setIndent(20);
        ui->labelTotal->setIndent(20);
    }
}

void OverviewPage::setClientModel(ClientModel *model)
{
    if (clientModel) disconnect(clientModel, nullptr, this, nullptr);
    this->clientModel = model;
    dashboardHeight = model ? model->getCachedBlockTip().height : -1;
    if (model) {
        connect(model, &ClientModel::numConnectionsChanged, this, [this](int) { updateNetworkOverview(); });
        connect(model, &ClientModel::networkActiveChanged, this, [this](bool) { updateNetworkOverview(); });
        connect(model, &ClientModel::numBlocksChanged, this,
            [this](int count, const QDateTime&, const QString&, double, bool header) {
                if (!header) { dashboardHeight = count; updateNetworkOverview(); }
            });
    }
    updateNetworkOverview();
    if(model)
    {
        // Show warning if this is a prerelease version
        connect(model, SIGNAL(alertsChanged(QString)), this, SLOT(updateAlerts(QString)));
        updateAlerts(model->getStatusBarWarnings());
    }
}

void OverviewPage::setWalletModel(WalletModel *model)
{
    this->walletModel = model;
    if(model && model->getOptionsModel())
    {
        // update the display unit, to not use the default ("BBC")
        updateDisplayUnit();
        // Keep up to date with wallet
        interfaces::Wallet& wallet = model->wallet();
        if (model->hasBalanceSnapshot()) {
            setBalance(model->getCachedBalances());
        } else {
            // Initial verification may hold cs_main. Display an explicit
            // pending state and wait for the nonblocking balance poll.
            heroBalance->setText(tr("Checking balance..."));
            for (auto* label : {ui->labelBalance, ui->labelUnconfirmed, ui->labelImmature,
                               ui->labelTotal, ui->labelWatchAvailable, ui->labelWatchPending,
                               ui->labelWatchImmature, ui->labelWatchTotal}) {
                label->setText(QString::fromUtf8("—"));
            }
        }
        connect(model, SIGNAL(balanceChanged(interfaces::WalletBalances)), this, SLOT(setBalance(interfaces::WalletBalances)));
        connect(model->getTransactionTableModel(), &TransactionTableModel::historyLoadProgress, this, [this](int, int) { updateHistoryLoading(); });
        connect(model->getTransactionTableModel(), &TransactionTableModel::historyLoadFinished, this, &OverviewPage::updateHistoryLoading);
        updateHistoryLoading();

        connect(model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));
        updateWatchOnlyLabels(wallet.haveWatchOnly() || gArgs.GetBoolArg("-debug-ui", false));
        connect(model, SIGNAL(notifyWatchonlyChanged(bool)), this, SLOT(updateWatchOnlyLabels(bool)));

        // explicitly update PS frame and transaction list to reflect actual settings
        updateAdvancedCJUI(model->getOptionsModel()->getShowAdvancedCJUI());

        connect(model->getOptionsModel(), SIGNAL(coinJoinRoundsChanged()), this, SLOT(updateCoinJoinProgress()));
        connect(model->getOptionsModel(), SIGNAL(coinJoinAmountChanged()), this, SLOT(updateCoinJoinProgress()));
        connect(model->getOptionsModel(), SIGNAL(AdvancedCJUIChanged(bool)), this, SLOT(updateAdvancedCJUI(bool)));
        connect(model->getOptionsModel(), &OptionsModel::coinJoinEnabledChanged, [=]() {
            coinJoinStatus(true);
        });

        // Disable coinJoinClient builtin support for automatic backups while we are in GUI,
        // we'll handle automatic backups and user warnings in coinJoinStatus()
        walletModel->coinJoin().disableAutobackups();

        connect(ui->toggleCoinJoin, SIGNAL(clicked()), this, SLOT(toggleCoinJoin()));

        // coinjoin buttons will not react to spacebar must be clicked on
        ui->toggleCoinJoin->setFocusPolicy(Qt::NoFocus);
    }
}

void OverviewPage::updateDisplayUnit()
{
    if(walletModel && walletModel->getOptionsModel())
    {
        nDisplayUnit = walletModel->getOptionsModel()->getDisplayUnit();
        if (m_balances.balance != -1) {
            setBalance(m_balances);
        }

        // Update txdelegate->unit with the current unit
        txdelegate->unit = nDisplayUnit;

        ui->listTransactions->update();
    }
}

void OverviewPage::updateAlerts(const QString &warnings)
{
    this->ui->labelAlerts->setVisible(!warnings.isEmpty());
    this->ui->labelAlerts->setText(warnings);
}

void OverviewPage::showOutOfSyncWarning(bool fShow)
{
    outOfSync = fShow;
    updateNetworkOverview();
    ui->labelWalletStatus->setVisible(fShow);
    ui->labelCoinJoinSyncStatus->setVisible(fShow);
    ui->labelTransactionsStatus->setVisible(fShow);
}

void OverviewPage::updateNetworkOverview()
{
    if (!clientModel) {
        connectionValue->setText(tr("Initializing"));
        blockHeightValue->setText(QString::fromUtf8("—"));
        syncValue->setText(tr("Checking..."));
        return;
    }
    const bool active = clientModel->node().getNetworkActive();
    const int peers = clientModel->getNumConnections();
    connectionValue->setText(!active ? tr("Network paused") : peers == 0 ? tr("Connecting...") : tr("%n peer(s)", "", peers));
    blockHeightValue->setText(dashboardHeight >= 0
        ? QLocale::system().toString(dashboardHeight) : QString::fromUtf8("—"));
    syncValue->setText(!active ? tr("Offline view") : peers == 0 ? tr("Waiting for peers") : outOfSync ? tr("Catching up") : tr("Up to date"));
}

void OverviewPage::updateCoinJoinProgress()
{
    if (!walletModel || !clientModel || clientModel->node().shutdownRequested() || !clientModel->smartnodeSync().isBlockchainSynced()) return;

    QString strAmountAndRounds;
    QString strCoinJoinAmount = BitcoinUnits::formatHtmlWithUnit(nDisplayUnit, clientModel->coinJoinOptions().getAmount() * COIN, false, BitcoinUnits::separatorAlways);

    if(m_balances.balance == 0)
    {
        ui->coinJoinProgress->setValue(0);
        ui->coinJoinProgress->setToolTip(tr("No inputs detected"));

        // when balance is zero just show info from settings
        strCoinJoinAmount = strCoinJoinAmount.remove(strCoinJoinAmount.indexOf("."), BitcoinUnits::decimals(nDisplayUnit) + 1);
        strAmountAndRounds = strCoinJoinAmount + " / " + tr("%n Rounds", "", clientModel->coinJoinOptions().getRounds());

        ui->labelAmountRounds->setToolTip(tr("No inputs detected"));
        ui->labelAmountRounds->setText(strAmountAndRounds);
        return;
    }

    CAmount nAnonymizableBalance = walletModel->wallet().getAnonymizableBalance(false, false);

    CAmount nMaxToAnonymize = nAnonymizableBalance + m_balances.anonymized_balance;

    // If it's more than the anon threshold, limit to that.
    if (nMaxToAnonymize > clientModel->coinJoinOptions().getAmount() * COIN) nMaxToAnonymize = clientModel->coinJoinOptions().getAmount() * COIN;

    if(nMaxToAnonymize == 0) return;

    if (nMaxToAnonymize >= clientModel->coinJoinOptions().getAmount() * COIN) {
        ui->labelAmountRounds->setToolTip(tr("Found enough compatible inputs to mix %1")
                                          .arg(strCoinJoinAmount));
        strCoinJoinAmount = strCoinJoinAmount.remove(strCoinJoinAmount.indexOf("."), BitcoinUnits::decimals(nDisplayUnit) + 1);
        strAmountAndRounds = strCoinJoinAmount + " / " + tr("%n Rounds", "", clientModel->coinJoinOptions().getRounds());
    } else {
        QString strMaxToAnonymize = BitcoinUnits::formatHtmlWithUnit(nDisplayUnit, nMaxToAnonymize, false, BitcoinUnits::separatorAlways);
        ui->labelAmountRounds->setToolTip(tr("Not enough compatible inputs to mix <span style='%1'>%2</span>,<br>"
                                             "will mix <span style='%1'>%3</span> instead")
                                          .arg(GUIUtil::getThemedStyleQString(GUIUtil::ThemedStyle::TS_ERROR))
                                          .arg(strCoinJoinAmount)
                                          .arg(strMaxToAnonymize));
        strMaxToAnonymize = strMaxToAnonymize.remove(strMaxToAnonymize.indexOf("."), BitcoinUnits::decimals(nDisplayUnit) + 1);
        strAmountAndRounds = "<span style='" + GUIUtil::getThemedStyleQString(GUIUtil::ThemedStyle::TS_ERROR) + "'>" +
                QString(BitcoinUnits::factor(nDisplayUnit) == 1 ? "" : "~") + strMaxToAnonymize +
                " / " + tr("%n Rounds", "", clientModel->coinJoinOptions().getRounds()) + "</span>";
    }
    ui->labelAmountRounds->setText(strAmountAndRounds);

    if (!fShowAdvancedCJUI) return;

    CAmount nDenominatedConfirmedBalance;
    CAmount nDenominatedUnconfirmedBalance;
    CAmount nNormalizedAnonymizedBalance;
    float nAverageAnonymizedRounds;

    nDenominatedConfirmedBalance = walletModel->wallet().getDenominatedBalance(false);
    nDenominatedUnconfirmedBalance = walletModel->wallet().getDenominatedBalance(true);
    nNormalizedAnonymizedBalance = walletModel->wallet().getNormalizedAnonymizedBalance();
    nAverageAnonymizedRounds = walletModel->wallet().getAverageAnonymizedRounds();

    // calculate parts of the progress, each of them shouldn't be higher than 1
    // progress of denominating
    float denomPart = 0;
    // mixing progress of denominated balance
    float anonNormPart = 0;
    // completeness of full amount anonymization
    float anonFullPart = 0;

    CAmount denominatedBalance = nDenominatedConfirmedBalance + nDenominatedUnconfirmedBalance;
    denomPart = (float)denominatedBalance / nMaxToAnonymize;
    denomPart = denomPart > 1 ? 1 : denomPart;
    denomPart *= 100;

    anonNormPart = (float)nNormalizedAnonymizedBalance / nMaxToAnonymize;
    anonNormPart = anonNormPart > 1 ? 1 : anonNormPart;
    anonNormPart *= 100;

    anonFullPart = (float)m_balances.anonymized_balance / nMaxToAnonymize;
    anonFullPart = anonFullPart > 1 ? 1 : anonFullPart;
    anonFullPart *= 100;

    // apply some weights to them ...
    float denomWeight = 1;
    float anonNormWeight = clientModel->coinJoinOptions().getRounds();
    float anonFullWeight = 2;
    float fullWeight = denomWeight + anonNormWeight + anonFullWeight;
    // ... and calculate the whole progress
    float denomPartCalc = ceilf((denomPart * denomWeight / fullWeight) * 100) / 100;
    float anonNormPartCalc = ceilf((anonNormPart * anonNormWeight / fullWeight) * 100) / 100;
    float anonFullPartCalc = ceilf((anonFullPart * anonFullWeight / fullWeight) * 100) / 100;
    float progress = denomPartCalc + anonNormPartCalc + anonFullPartCalc;
    if(progress >= 100) progress = 100;

    ui->coinJoinProgress->setValue(progress);

    QString strToolPip = ("<b>" + tr("Overall progress") + ": %1%</b><br/>" +
                          tr("Denominated") + ": %2%<br/>" +
                          tr("Partially mixed") + ": %3%<br/>" +
                          tr("Mixed") + ": %4%<br/>" +
                          tr("Denominated inputs have %5 of %n rounds on average", "", clientModel->coinJoinOptions().getRounds()))
            .arg(progress).arg(denomPart).arg(anonNormPart).arg(anonFullPart)
            .arg(nAverageAnonymizedRounds);
    ui->coinJoinProgress->setToolTip(strToolPip);
}

void OverviewPage::updateAdvancedCJUI(bool fShowAdvancedCJUI)
{
    if (!walletModel || !clientModel) return;

    this->fShowAdvancedCJUI = fShowAdvancedCJUI;
    coinJoinStatus(true);
}

void OverviewPage::coinJoinStatus(bool fForce)
{
    if (!walletModel || !clientModel) return;

    if (!fForce && (clientModel->node().shutdownRequested() || !clientModel->smartnodeSync().isBlockchainSynced())) return;

    // Disable any PS UI for smartnode or when autobackup is disabled or failed for whatever reason
    if (fSmartnodeMode || nWalletBackups <= 0) {
        DisableCoinJoinCompletely();
        if (nWalletBackups <= 0) {
            ui->labelCoinJoinEnabled->setToolTip(tr("Automatic backups are disabled, no mixing available!"));
        }
        return;
    }

    bool fIsEnabled = clientModel->coinJoinOptions().isEnabled();
    ui->frameCoinJoin->setVisible(fIsEnabled);
    if (!fIsEnabled) {
        SetupTransactionList(NUM_ITEMS_DISABLED);
        if (timer != nullptr) {
            timer->stop();
        }
        return;
    }

    if (timer != nullptr && !timer->isActive()) {
        timer->start(1000);
    }

    // Wrap all coinjoin related widgets we want to show/hide state based.
    // Value of the map contains a flag if this widget belongs to the advanced
    // CoinJoin UI option or not. True if it does, false if not.
    std::map<QWidget*, bool> coinJoinWidgets = {
        {ui->labelCompletitionText, true},
        {ui->coinJoinProgress, true},
        {ui->labelSubmittedDenomText, true},
        {ui->labelSubmittedDenom, true},
        {ui->labelAmountAndRoundsText, false},
        {ui->labelAmountRounds, false}
    };

    auto setWidgetsVisible = [&](bool fVisible) {
        static bool fInitial{true};
        static bool fLastVisible{false};
        static bool fLastShowAdvanced{false};
        // Only update the widget's visibility if something changed since the last call of setWidgetsVisible
        if (fLastShowAdvanced == fShowAdvancedCJUI && fLastVisible == fVisible) {
            if (fInitial) {
                fInitial = false;
            } else {
                return;
            }
        }
        // Set visible if: fVisible and not advanced UI element or advanced ui element and advanced ui active
        for (const auto& it : coinJoinWidgets) {
            it.first->setVisible(fVisible && (!it.second || it.second == fShowAdvancedCJUI));
        }
        fLastVisible = fVisible;
        fLastShowAdvanced = fShowAdvancedCJUI;
    };

    static int64_t nLastDSProgressBlockTime = 0;
    int nBestHeight = clientModel->getCachedBlockTip().height;
    if (nBestHeight < 0) return;

    // We are processing more than 1 block per second, we'll just leave
    if (nBestHeight > walletModel->coinJoin().getCachedBlocks() && GetTime() - nLastDSProgressBlockTime <= 1) return;
    nLastDSProgressBlockTime = GetTime();

    QString strKeysLeftText(tr("keys left: %1").arg(walletModel->getKeysLeftSinceAutoBackup()));
    if(walletModel->getKeysLeftSinceAutoBackup() < COINJOIN_KEYS_THRESHOLD_WARNING) {
        strKeysLeftText = "<span style='" + GUIUtil::getThemedStyleQString(GUIUtil::ThemedStyle::TS_ERROR) + "'>" + strKeysLeftText + "</span>";
    }
    ui->labelCoinJoinEnabled->setToolTip(strKeysLeftText);

    if (!walletModel->coinJoin().isMixing()) {
        if (nBestHeight != walletModel->coinJoin().getCachedBlocks()) {
            walletModel->coinJoin().setCachedBlocks(nBestHeight);
            updateCoinJoinProgress();
        }

        setWidgetsVisible(false);
        ui->toggleCoinJoin->setText(tr("Start %1").arg("CoinJoin"));

        QString strEnabled = tr("Disabled");
        // Show how many keys left in advanced PS UI mode only
        if (fShowAdvancedCJUI) strEnabled += ", " + strKeysLeftText;
        ui->labelCoinJoinEnabled->setText(strEnabled);

        // If mixing isn't active always show the lower number of txes because there are
        // anyway the most PS widgets hidden.
        SetupTransactionList(NUM_ITEMS_ENABLED_NORMAL);

        return;
    } else {
        SetupTransactionList(fShowAdvancedCJUI ? NUM_ITEMS_ENABLED_ADVANCED : NUM_ITEMS_ENABLED_NORMAL);
    }

    // Warn user that wallet is running out of keys
    // NOTE: we do NOT warn user and do NOT create autobackups if mixing is not running
    if (nWalletBackups > 0 && walletModel->getKeysLeftSinceAutoBackup() < COINJOIN_KEYS_THRESHOLD_WARNING) {
        QSettings settings;
        if(settings.value("fLowKeysWarning").toBool()) {
            QString strWarn =   tr("Very low number of keys left since last automatic backup!") + "<br><br>" +
                                tr("We are about to create a new automatic backup for you, however "
                                   "<span style='%1'> you should always make sure you have backups "
                                   "saved in some safe place</span>!").arg(GUIUtil::getThemedStyleQString(GUIUtil::ThemedStyle::TS_COMMAND)) + "<br><br>" +
                                tr("Note: You can turn this message off in options.");
            ui->labelCoinJoinEnabled->setToolTip(strWarn);
            LogPrint(BCLog::COINJOIN, "OverviewPage::coinJoinStatus -- Very low number of keys left since last automatic backup, warning user and trying to create new backup...\n");
            QMessageBox::warning(this, "CoinJoin", strWarn, QMessageBox::Ok, QMessageBox::Ok);
        } else {
            LogPrint(BCLog::COINJOIN, "OverviewPage::coinJoinStatus -- Very low number of keys left since last automatic backup, skipping warning and trying to create new backup...\n");
        }

        QString strBackupWarning;
        QString strBackupError;
        if(!walletModel->autoBackupWallet(strBackupWarning, strBackupError)) {
            if (!strBackupWarning.isEmpty()) {
                // It's still more or less safe to continue but warn user anyway
                LogPrint(BCLog::COINJOIN, "OverviewPage::coinJoinStatus -- WARNING! Something went wrong on automatic backup: %s\n", strBackupWarning.toStdString());

                QMessageBox::warning(this, "CoinJoin",
                    tr("WARNING! Something went wrong on automatic backup") + ":<br><br>" + strBackupWarning,
                    QMessageBox::Ok, QMessageBox::Ok);
            }
            if (!strBackupError.isEmpty()) {
                // Things are really broken, warn user and stop mixing immediately
                LogPrint(BCLog::COINJOIN, "OverviewPage::coinJoinStatus -- ERROR! Failed to create automatic backup: %s\n", strBackupError.toStdString());

                QMessageBox::warning(this, "CoinJoin",
                    tr("ERROR! Failed to create automatic backup") + ":<br><br>" + strBackupError + "<br>" +
                    tr("Mixing is disabled, please close your wallet and fix the issue!"),
                    QMessageBox::Ok, QMessageBox::Ok);
            }
        }
    }

    QString strEnabled = walletModel->coinJoin().isMixing() ? tr("Enabled") : tr("Disabled");
    // Show how many keys left in advanced PS UI mode only
    if(fShowAdvancedCJUI) strEnabled += ", " + strKeysLeftText;
    ui->labelCoinJoinEnabled->setText(strEnabled);

    if(nWalletBackups == -1) {
        // Automatic backup failed, nothing else we can do until user fixes the issue manually
        DisableCoinJoinCompletely();

        QString strError =  tr("ERROR! Failed to create automatic backup") + ", " +
                            tr("see debug.log for details.") + "<br><br>" +
                            tr("Mixing is disabled, please close your wallet and fix the issue!");
        ui->labelCoinJoinEnabled->setToolTip(strError);

        return;
    } else if(nWalletBackups == -2) {
        // We were able to create automatic backup but keypool was not replenished because wallet is locked.
        QString strWarning = tr("WARNING! Failed to replenish keypool, please unlock your wallet to do so.");
        ui->labelCoinJoinEnabled->setToolTip(strWarning);
    }

    // check coinjoin status and unlock if needed
    if(nBestHeight != walletModel->coinJoin().getCachedBlocks()) {
        // Balance and number of transactions might have changed
        walletModel->coinJoin().setCachedBlocks(nBestHeight);
        updateCoinJoinProgress();
    }

    setWidgetsVisible(true);

    ui->labelSubmittedDenom->setText(QString(walletModel->coinJoin().getSessionDenoms().c_str()));
}

void OverviewPage::toggleCoinJoin(){
    QSettings settings;
    // Popup some information on first mixing
    QString hasMixed = settings.value("hasMixed").toString();
    if(hasMixed.isEmpty()){
        QMessageBox::information(this, "CoinJoin",
                tr("If you don't want to see internal %1 fees/transactions select \"Most Common\" as Type on the \"Transactions\" tab.").arg("CoinJoin"),
                QMessageBox::Ok, QMessageBox::Ok);
        settings.setValue("hasMixed", "hasMixed");
    }

    if (!walletModel->coinJoin().isMixing()) {
        auto& options = walletModel->node().coinJoinOptions();
        const CAmount nMinAmount = options.getSmallestDenomination() + options.getMaxCollateralAmount();
        if(m_balances.balance < nMinAmount) {
            QString strMinAmount(BitcoinUnits::formatWithUnit(nDisplayUnit, nMinAmount));
            QMessageBox::warning(this, "CoinJoin",
                tr("%1 requires at least %2 to use.").arg("CoinJoin").arg(strMinAmount),
                QMessageBox::Ok, QMessageBox::Ok);
            return;
        }

        // if wallet is locked, ask for a passphrase
        if (walletModel && walletModel->getEncryptionStatus() == WalletModel::Locked)
        {
            WalletModel::UnlockContext ctx(walletModel->requestUnlock(true));
            if(!ctx.isValid())
            {
                //unlock was cancelled
                walletModel->coinJoin().resetCachedBlocks();
                QMessageBox::warning(this, "CoinJoin",
                    tr("Wallet is locked and user declined to unlock. Disabling %1.").arg("CoinJoin"),
                    QMessageBox::Ok, QMessageBox::Ok);
                LogPrint(BCLog::COINJOIN, "OverviewPage::toggleCoinJoin -- Wallet is locked and user declined to unlock. Disabling CoinJoin.\n");
                return;
            }
        }

    }

    walletModel->coinJoin().resetCachedBlocks();

    if (walletModel->coinJoin().isMixing()) {
        ui->toggleCoinJoin->setText(tr("Start %1").arg("CoinJoin"));
        walletModel->coinJoin().resetPool();
        walletModel->coinJoin().stopMixing();
    } else {
        ui->toggleCoinJoin->setText(tr("Stop %1").arg("CoinJoin"));
        walletModel->coinJoin().startMixing();
    }
}

void OverviewPage::SetupTransactionList(int nNumItems)
{
    if (walletModel == nullptr || walletModel->getTransactionTableModel() == nullptr) {
        return;
    }

    // Set up transaction list
    if (filter == nullptr) {
        filter.reset(new TransactionFilterProxy());
        filter->setSourceModel(walletModel->getTransactionTableModel());
        filter->setDynamicSortFilter(true);
        filter->setSortRole(Qt::EditRole);
        filter->setShowInactive(false);
        filter->sort(TransactionTableModel::Date, Qt::DescendingOrder);
        ui->listTransactions->setModel(filter.get());
        connect(filter.get(), &QAbstractItemModel::rowsInserted, this, &OverviewPage::updateRecentActivity);
        connect(filter.get(), &QAbstractItemModel::rowsRemoved, this, &OverviewPage::updateRecentActivity);
        connect(filter.get(), &QAbstractItemModel::modelReset, this, &OverviewPage::updateRecentActivity);
        connect(filter.get(), &QAbstractItemModel::dataChanged, this, &OverviewPage::updateRecentActivity);
        connect(filter.get(), &QAbstractItemModel::layoutChanged, this, &OverviewPage::updateRecentActivity);
    }
    updateRecentActivity();

    filter->setLimit(nNumItems);
    ui->listTransactions->setMinimumHeight(nNumItems * ITEM_HEIGHT);
    updateRecentActivity();
}

void OverviewPage::updateRecentActivity()
{
    QVector<qint64> values;
    // The proxy is bounded; never walk an entire wallet just to draw a chart.
    const int count = filter ? std::min(filter->rowCount(), NUM_ITEMS_ENABLED_ADVANCED) : 0;
    for (int row = count - 1; row >= 0; --row) {
        values.push_back(filter->index(row, 0).data(TransactionTableModel::AmountRole).toLongLong());
    }
    activityGraph->setValues(values);
    const bool loading = walletModel && walletModel->getTransactionTableModel()->isLoadingHistory();
    emptyTransactions->setVisible(count == 0 && !loading);
    ui->listTransactions->setVisible(count != 0);
}

void OverviewPage::updateHistoryLoading()
{
    const auto* history = walletModel ? walletModel->getTransactionTableModel() : nullptr;
    const bool loading = history && history->isLoadingHistory();
    historyLoadingNotice->setVisible(loading);
    if (loading) {
        historyLoadingNotice->setText(history->totalHistoryTransactions() > 0 ?
            tr("Loading transaction history: %1 of %2 transactions").arg(history->loadedHistoryTransactions()).arg(history->totalHistoryTransactions()) :
            tr("Loading transaction history..."));
    }
    updateRecentActivity();
}

void OverviewPage::DisableCoinJoinCompletely()
{
    if (walletModel == nullptr) {
        return;
    }

    ui->toggleCoinJoin->setText("(" + tr("Disabled") + ")");
    ui->frameCoinJoin->setEnabled(false);
    if (nWalletBackups <= 0) {
        ui->labelCoinJoinEnabled->setText("<span style='" + GUIUtil::getThemedStyleQString(GUIUtil::ThemedStyle::TS_ERROR) + "'>(" + tr("Disabled") + ")</span>");
    }
    walletModel->coinJoin().stopMixing();
}
