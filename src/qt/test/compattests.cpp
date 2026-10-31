// Copyright (c) 2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/paymentrequestplus.h> // this includes protobuf's port.h which defines its own bswap macos

#include <qt/test/compattests.h>

#include <compat/byteswap.h>
#include <qt/guiutil.h>
#include <QLabel>
#include <QApplication>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>

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
