// Copyright (c) 2026 The Babacoin developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#include <qt/webwalletpage.h>
#include <qt/guiutil.h>
#include <chainparams.h>
#include <QDesktopServices>
#include <QFrame>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

WebWalletPage::WebWalletPage(QWidget* parent) : QWidget(parent)
{
    setObjectName("webWalletPage");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    auto* card = new QFrame(this);
    card->setObjectName("webWalletCard");
    auto* content = new QVBoxLayout(card);
    content->setContentsMargins(28, 28, 28, 28);
    content->setSpacing(20);
    auto* title = new QLabel(tr("Babacoin Web Wallet"));
    GUIUtil::setFont({title}, GUIUtil::FontWeight::Bold, 24);
    content->addWidget(title);
    auto* address = new QLabel("https://web.babacoin.network/");
    address->setObjectName("webWalletAddress");
    address->setTextInteractionFlags(Qt::TextSelectableByMouse);
    content->addWidget(address);
    auto* description = new QLabel(tr("Use the Web Wallet in your default browser, then return here to continue with the local full-node wallet. Your local and web wallet keys, recovery backups and balances are managed separately. No keys or wallet files are transferred automatically."));
    description->setWordWrap(true);
    content->addWidget(description);
    auto* open = new QPushButton(tr("Open Web Wallet in browser"));
    open->setObjectName("webWalletOpenButton");
    const bool mainnet = Params().NetworkIDString() == "main";
    open->setEnabled(mainnet);
    content->addWidget(open, 0, Qt::AlignLeft);
    auto* status = new QLabel(mainnet ?
        tr("The website opens only when you choose this button. Check the exact HTTPS address in your browser before using the web wallet.") :
        tr("This local wallet is on a test network. The Web Wallet is a separate mainnet service; launching it is disabled in this preview."));
    status->setWordWrap(true);
    content->addWidget(status);
    content->addStretch();
    layout->addWidget(card);
    layout->addStretch();
    connect(open, &QPushButton::clicked, this, [this] {
        if (Params().NetworkIDString() != "main") return;
        if (!QDesktopServices::openUrl(QUrl("https://web.babacoin.network/")))
            QMessageBox::warning(this, tr("Web Wallet"), tr("Could not open the default browser. You can open the displayed HTTPS address manually."));
    });
}
