// Copyright (c) 2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/paymentrequestplus.h> // this includes protobuf's port.h which defines its own bswap macos

#include <qt/test/compattests.h>

#include <compat/byteswap.h>
#include <qt/guiutil.h>
#include <qt/explorerpeers.h>
#include <qt/bitcoinunits.h>
#include <qt/utilitydialog.h>
#include <qt/intro.h>
#include <interfaces/node.h>
#include <util.h>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QTimer>
#include <QDir>
#include <QFileInfo>
#include <QSignalSpy>
#include <QProgressBar>
#include <QPushButton>
#include <QLabel>
#include <QLabel>
#include <QApplication>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

void CompatTests::bswapTests()
{
	// Sibling in bitcoin/src/test/bswap_tests.cpp
	uint16_t u1 = 0x1234;
	uint32_t u2 = 0x56789abc;
	uint64_t u3 = 0xdef0123456789abc;
	uint16_t e1 = 0x3412;
	uint32_t e2 = 0xbc9a7856;
	uint64_t e3 = 0xbc9a78563412f0de;
	QVERIFY(bswap_16(u1) == e1);
	QVERIFY(bswap_32(u2) == e2);
	QVERIFY(bswap_64(u3) == e3);
}

void CompatTests::movedDataDirectoryRecovery()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QSettings settings;
    struct Restore {
        std::map<QString, QVariant> values;
        std::string dataDir{gArgs.GetArg("-datadir", "")};
        std::string choose{gArgs.GetArg("-choosedatadir", "")};
        bool hadData{gArgs.IsArgSet("-datadir")}, hadChoose{gArgs.IsArgSet("-choosedatadir")};
        ~Restore() {
            QSettings settings;
            settings.remove("strDataDir"); settings.remove("strDataDirDefault");
            settings.remove("DataDirectoryRelocations");
            for (const auto& value : values) settings.setValue(value.first, value.second);
            if (hadData) gArgs.ForceSetArg("-datadir", dataDir); else gArgs.ForceRemoveArg("-datadir");
            if (hadChoose) gArgs.ForceSetArg("-choosedatadir", choose); else gArgs.ForceRemoveArg("-choosedatadir");
            ClearDatadirCache();
        }
    } restore;
    for (const auto& key : settings.allKeys()) {
        if (key == "strDataDir" || key == "strDataDirDefault" || key.startsWith("DataDirectoryRelocations/")) {
            restore.values.emplace(key, settings.value(key));
        }
    }
    settings.remove("strDataDir"); settings.remove("strDataDirDefault"); settings.remove("DataDirectoryRelocations");
    gArgs.ForceSetArg("-choosedatadir", "0");
    const QString missing = root.filePath("old disconnected drive");
    const QString moved = root.filePath(QString::fromUtf8("moved wallet \xc3\x96"));
    const QString empty = root.filePath("empty folder");
    QVERIFY(QDir().mkpath(moved)); QVERIFY(QDir().mkpath(empty));
    const QByteArray marker("private test marker, not an actual wallet\n");
    auto writeMarker = [&](const QString& directory) {
        QFile file(directory + "/wallet.dat");
        return file.open(QIODevice::WriteOnly) && file.write(marker) == marker.size();
    };
    QVERIFY(writeMarker(moved));
    auto node = interfaces::MakeNode();
    auto forceDirectory = [&](const QString& path) {
        gArgs.ForceSetArg("-datadir", GUIUtil::qstringToBoostPath(path).string());
        ClearDatadirCache();
    };
    auto currentDirectory = [&] {
        return QDir::fromNativeSeparators(GUIUtil::boostPathToQString(GetDataDir(false)));
    };
    struct PickResult { bool accepted; bool shown; bool recovery; bool timedOut; bool okEnabled; };
    auto pick = [&](const QString& destination, bool cancel, bool reconnect = false) {
        PickResult result{false, false, false, false, false};
        bool reconnected = false;
        QElapsedTimer elapsed; elapsed.start();
        QTimer driver;
        QObject::connect(&driver, &QTimer::timeout, [&] {
            for (auto* widget : QApplication::topLevelWidgets()) {
                auto* intro = qobject_cast<Intro*>(widget);
                if (!intro || !intro->isVisible()) continue;
                result.shown = true;
                result.recovery = intro->property("dataDirectoryRecovery").toBool();
                if (intro->getDataDirectory() != destination) intro->setDataDirectory(destination);
                auto* buttons = intro->findChild<QDialogButtonBox*>("buttonBox");
                if (!buttons) { intro->reject(); driver.stop(); return; }
                if (reconnect && !reconnected && elapsed.elapsed() > 200) {
                    reconnected = true;
                    if (!QDir().mkpath(destination) || !writeMarker(destination)) { intro->reject(); driver.stop(); return; }
                    auto* retry = intro->findChild<QPushButton*>("recheckDataDirectory");
                    if (!retry) { intro->reject(); driver.stop(); return; }
                    retry->click();
                }
                result.okEnabled = buttons->button(QDialogButtonBox::Ok)->isEnabled();
                if (elapsed.elapsed() > 5000) {
                    result.timedOut = true; intro->reject(); driver.stop();
                } else if (cancel && elapsed.elapsed() > 200) {
                    intro->reject(); driver.stop();
                } else if (!cancel && result.okEnabled) {
                    buttons->button(QDialogButtonBox::Ok)->click(); driver.stop();
                }
            }
        });
        driver.start(10);
        result.accepted = Intro::pickDataDirectory(*node);
        driver.stop();
        return result;
    };

    forceDirectory(missing);
    const auto cancelled = pick(empty, true);
    QVERIFY(cancelled.shown && cancelled.recovery && !cancelled.accepted && !cancelled.timedOut);
    QVERIFY(!cancelled.okEnabled);
    QVERIFY(!QFileInfo::exists(missing));
    QVERIFY(!settings.contains("strDataDir"));
    QVERIFY(QDir(empty).entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot).isEmpty());

    const auto selected = pick(moved, false);
    QVERIFY(selected.shown && selected.recovery && selected.accepted && !selected.timedOut);
    QCOMPARE(QString::fromStdString(gArgs.GetArg("-datadir", "")), QString::fromStdString(GUIUtil::qstringToBoostPath(moved).string()));
    QCOMPARE(settings.value("strDataDir").toString(), moved);
    QCOMPARE(currentDirectory(), moved);
    QVERIFY(!QFileInfo::exists(missing));

    // The same stale installed shortcut reuses the consented relocation.
    forceDirectory(missing);
    QVERIFY(Intro::pickDataDirectory(*node));
    QCOMPARE(currentDirectory(), moved);

    // Reconnecting the old disk must not silently switch back to an old wallet.
    QVERIFY(QDir().mkpath(missing)); QVERIFY(writeMarker(missing));
    forceDirectory(missing);
    const auto reconnected = pick(missing, false);
    QVERIFY(reconnected.shown && reconnected.recovery && reconnected.accepted && !reconnected.timedOut);
    forceDirectory(missing);
    QVERIFY(Intro::pickDataDirectory(*node)); // valid explicit path: no dialog
    QCOMPARE(currentDirectory(), missing);

    // Missing saved GUI settings use the same recovery screen without a CLI path.
    gArgs.ForceRemoveArg("-datadir"); ClearDatadirCache();
    settings.setValue("strDataDir", root.filePath("another missing location"));
    settings.setValue("strDataDirDefault", Intro::getDefaultDataDirectory());
    const auto saved = pick(moved, false);
    QVERIFY(saved.shown && saved.recovery && saved.accepted && !saved.timedOut);
    QCOMPARE(currentDirectory(), moved);

    const QString reattached = root.filePath("reattached drive");
    forceDirectory(reattached);
    const auto retried = pick(reattached, false, true);
    QVERIFY(retried.shown && retried.recovery && retried.accepted && !retried.timedOut);
    QCOMPARE(currentDirectory(), reattached);

    // Use the same default-wallet base as the backend. Unrelated files in
    // wallets/ must not allow a new empty default wallet to be created.
    const QString nested = root.filePath("nested data");
    QVERIFY(QDir().mkpath(nested + "/wallets"));
    QVERIFY(writeMarker(nested)); // ignored by the backend when wallets/ exists
    QFile unrelated(nested + "/wallets/README.txt");
    QVERIFY(unrelated.open(QIODevice::WriteOnly)); unrelated.write("not a wallet"); unrelated.close();
    forceDirectory(root.filePath("missing nested source"));
    const auto rejectedNested = pick(nested, true);
    QVERIFY(rejectedNested.shown && !rejectedNested.accepted && !rejectedNested.okEnabled);
    QVERIFY(writeMarker(nested + "/wallets"));
    const auto acceptedNested = pick(nested, false);
    QVERIFY(acceptedNested.shown && acceptedNested.accepted && !acceptedNested.timedOut);
    QCOMPARE(currentDirectory(), nested);

    QFile file(moved + "/wallet.dat"); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), marker);
}

void CompatTests::pixelSizedFont()
{
    QVERIFY(GUIUtil::loadFonts());
    QLabel label;
    QFont font = label.font();
    font.setPixelSize(18);
    label.setFont(font);
    GUIUtil::updateFonts();
    QVERIFY(label.font().pointSizeF() > 0);
}

void CompatTests::stylesheetLineEndings()
{
    // Exercise the actual loader: Windows checkouts and custom CSS may use CRLF.
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings;
    struct RestoreStyle {
        QVariant theme;
        bool hadTheme;
        ~RestoreStyle()
        {
            QSettings settings;
            if (hadTheme) settings.setValue("theme", theme);
            else settings.remove("theme");
            GUIUtil::setStyleSheetDirectory(":css");
            GUIUtil::loadStyleSheet(true);
        }
    } restore{settings.value("theme"), settings.contains("theme")};
    settings.setValue("theme", "Light");
    GUIUtil::setStyleSheetDirectory(directory.path());
    const QByteArray general =
        "QLabel#base { padding: 1px; }\n"
        "<os=\"windows,macosx,other\">\n"
        "QLabel#included { color: #123456; }\n"
        "</os>\n"
        "<os=\"not-a-real-platform\">\n"
        "QLabel#excluded { color: #abcdef; }\n"
        "</os>\n";
    auto writeStyles = [&](bool crlf) {
        for (const auto& name : {"general.css", "light.css"}) {
            QFile file(directory.filePath(name));
            if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
            QByteArray content = QString(name) == "general.css" ? general : QByteArray("QLabel#theme { padding: 2px; }\n");
            if (crlf) content.replace("\n", "\r\n");
            if (file.write(content) != content.size()) return false;
        }
        return true;
    };
    QVERIFY(writeStyles(false));
    GUIUtil::loadStyleSheet(true);
    const QString lf = qApp->styleSheet();
    QVERIFY(lf.contains("QLabel#included"));
    QVERIFY2(!lf.contains("QLabel#excluded"), qPrintable(lf));
    QVERIFY(!lf.contains("<os="));
    QVERIFY(writeStyles(true));
    GUIUtil::loadStyleSheet(true);
    QCOMPARE(qApp->styleSheet(), lf);
}

void CompatTests::explorerPeerSafety()
{
    ExplorerPeers::Policy policy;
    QVERIFY(!ExplorerPeers::Allowed(policy));
    policy.enabled = true;
    QVERIFY(ExplorerPeers::Allowed(policy));
    for (int condition = 0; condition != 9; ++condition) {
        auto blocked = policy;
        switch (condition) {
        case 0: blocked.network = "test"; break;
        case 1: blocked.network = "regtest"; break;
        case 2: blocked.active = false; break;
        case 3: blocked.shuttingDown = true; break;
        case 4: blocked.explicitPeers = true; break;
        case 5: blocked.restrictedNetworks = true; break;
        case 6: blocked.proxy = true; break;
        case 7: blocked.dnsSeed = false; break;
        case 8: blocked.maxConnections = 0; break;
        }
        QVERIFY(!ExplorerPeers::Allowed(blocked));
    }
    const int64_t now = 1700000000;
    auto encode = [](const QJsonArray& peers) {
        QJsonObject root;
        root.insert("addnode_peers", peers);
        return QJsonDocument(root).toJson(QJsonDocument::Compact);
    };
    auto entry = [](QString host, QJsonValue port = QJsonValue(QStringLiteral("6678"))) {
        QJsonObject object;
        object.insert("address", host);
        object.insert("port", port);
        return object;
    };
    QJsonArray valid;
    valid.append(entry("8.8.8.8"));
    valid.append(entry("8.8.4.4")); // Same network group, no second hint.
    valid.append(entry("1.1.1.1", QJsonValue(6678)));
    valid.append(entry("[2606:4700:4700::1111]"));
    auto parsed = ExplorerPeers::Parse(encode(valid), now);
    QCOMPARE(parsed.size(), size_t(3));
    for (const auto& address : parsed) {
        QVERIFY(address.IsRoutable());
        QCOMPARE(address.GetPort(), uint16_t(6678));
        QCOMPARE(int64_t(address.nTime), now - 3 * 24 * 60 * 60);
    }
    QJsonArray hostile;
    for (const auto& host : {"127.0.0.1", "192.168.1.1", "10.1.2.3", "172.16.0.1", "169.254.1.1",
                             "0.0.0.0", "::1", "fe80::1", "fc00::1", "::ffff:127.0.0.1",
                             "localhost", "example.com", "https://8.8.8.8", "8.8.8.8:22", "8.8.8.8\n"}) {
        hostile.append(entry(host));
    }
    hostile.append(entry("8.8.8.8", QJsonValue(22)));
    hostile.append(entry("8.8.8.8", QJsonValue("6678;anything")));
    hostile.append(QJsonValue("addnode=8.8.8.8"));
    QVERIFY(ExplorerPeers::Parse(encode(hostile), now).empty());
    for (const auto& malformed : {QByteArray("[]"), QByteArray("{}"), QByteArray("not json"),
                                  QByteArray(ExplorerPeers::MAX_RESPONSE_BYTES + 1, ' ')}) {
        QVERIFY(ExplorerPeers::Parse(malformed, now).empty());
    }
    QJsonArray many;
    for (int group = 1; group < 40; ++group) many.append(entry(QString("8.%1.1.1").arg(group)));
    QCOMPARE(ExplorerPeers::Parse(encode(many), now).size(), ExplorerPeers::MAX_PEERS);
}

void CompatTests::localizedAmounts()
{
    QSettings settings;
    struct RestoreDigits {
        QSettings& settings;
        bool existed;
        QVariant value;
        ~RestoreDigits() { if (existed) settings.setValue("digits", value); else settings.remove("digits"); }
    } restore{settings, settings.contains("digits"), settings.value("digits")};
    settings.setValue("digits", 2);
    settings.sync();
    QCOMPARE(settings.status(), QSettings::NoError);
    const QLocale tr(QLocale::Turkish, QLocale::Turkey);
    const QLocale en(QLocale::English, QLocale::UnitedStates);
    const CAmount amount = 1234567 * COIN + 89123456;
    const QString unit = " " + BitcoinUnits::name(BitcoinUnits::BBC);
    QCOMPARE(BitcoinUnits::formatLocalized(BitcoinUnits::BBC, amount, false, BitcoinUnits::separatorAlways, tr), QString("1.234.567,89123456"));
    QCOMPARE(BitcoinUnits::floorLocalizedWithUnit(BitcoinUnits::BBC, amount, false, BitcoinUnits::separatorAlways, tr), QString("1.234.567,89") + unit);
    QCOMPARE(BitcoinUnits::floorLocalizedWithUnit(BitcoinUnits::BBC, amount, false, BitcoinUnits::separatorAlways, en), QString("1,234,567.89") + unit);
    QCOMPARE(BitcoinUnits::formatLocalized(BitcoinUnits::BBC, -amount, false, BitcoinUnits::separatorAlways, tr), QString("-1.234.567,89123456"));
    QCOMPARE(BitcoinUnits::formatLocalized(BitcoinUnits::BBC, 1000 * COIN, true, BitcoinUnits::separatorAlways, tr), QString("+1.000,00000000"));
    QCOMPARE(BitcoinUnits::formatLocalized(BitcoinUnits::BBC, 1, false, BitcoinUnits::separatorAlways, tr), QString("0,00000001"));
    // Canonical amount text remains exact and unambiguous for URI/input/export.
    QCOMPARE(BitcoinUnits::format(BitcoinUnits::BBC, amount, false, BitcoinUnits::separatorNever), QString("1234567.89123456"));
    CAmount parsed = 0;
    QVERIFY(BitcoinUnits::parse(BitcoinUnits::BBC, "1234567.89123456", &parsed));
    QCOMPARE(parsed, amount);
    QVERIFY(!BitcoinUnits::parse(BitcoinUnits::BBC, "1.234.567,89123456", &parsed));
    settings.setValue("digits", 0);
    settings.sync();
    QCOMPARE(BitcoinUnits::floorLocalizedWithUnit(BitcoinUnits::BBC, amount, false, BitcoinUnits::separatorAlways, tr), QString("1.234.567") + unit);
    settings.setValue("digits", 8);
    settings.sync();
    QCOMPARE(BitcoinUnits::floorLocalizedWithUnit(BitcoinUnits::BBC, amount, false, BitcoinUnits::separatorAlways, tr), QString("1.234.567,89123456") + unit);
}

void CompatTests::startupPanel()
{
    StartupPanel panel("test-version", "regtest", "/synthetic-wallet-data");
    auto* progress = panel.findChild<QProgressBar*>("startupProgress");
    auto* phase = panel.findChild<QLabel*>("startupPhase");
    auto* stop = panel.findChild<QPushButton*>("stopStartup");
    QVERIFY(progress && phase && stop);
    panel.setPhase("Rescanning...");
    QCOMPARE(progress->maximum(), 0);
    panel.setProgress("Rescanning...", 37);
    QCOMPARE(progress->maximum(), 100);
    QCOMPARE(progress->value(), 37);
    QVERIFY(panel.diagnostics().contains("/synthetic-wallet-data"));
    QVERIFY(panel.diagnostics().contains("startup checks incomplete"));
    panel.setWalletScanProgress("test", 100, 119, 199, false);
    QCOMPARE(progress->maximum(), 10000);
    QCOMPARE(progress->value(), 2000);
    QVERIFY(panel.diagnostics().contains("20"));
    auto* summary = panel.findChild<QLabel*>("startupScanSummary");
    QVERIFY(summary && !summary->isHidden());
    QVERIFY(summary->text().contains("119"));
    QVERIFY(summary->text().contains(progress->format()));
    panel.setWalletScanProgress("test", 100, 199, 199, false);
    QCOMPARE(progress->value(), 9999);
    panel.setWalletScanProgress("test", 100, 199, 199, true);
    QCOMPARE(progress->value(), 10000);
    panel.setPhase("Final checks");
    QCOMPARE(progress->maximum(), 0);
    QVERIFY(summary->isHidden());
    QSignalSpy stopping(&panel, &StartupPanel::stopRequested);
    stop->click();
    QCOMPARE(stopping.count(), 1);
    QVERIFY(!stop->isEnabled());
    const QString stoppingText = phase->text();
    panel.setProgress("Late progress", 80);
    panel.setWalletScanProgress("Late scan", 0, 100, 100, true);
    panel.requestStop();
    QCOMPARE(phase->text(), stoppingText);
    QCOMPARE(stopping.count(), 1);
}
