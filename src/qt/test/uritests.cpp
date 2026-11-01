// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/test/uritests.h>

#include <qt/guiutil.h>
#include <qt/walletmodel.h>

#include <QUrl>

void URITests::uriTests()
{
    SendCoinsRecipient rv;
    QUrl uri;
    uri.setUrl(QString("babacoin:BRYaKcFxuNExx2Zw1w3Qy7H2U3DdgPAszN?req-dontexist="));
    QVERIFY(!GUIUtil::parseBitcoinURI(uri, &rv));

    uri.setUrl(QString("babacoin:BRYaKcFxuNExx2Zw1w3Qy7H2U3DdgPAszN?dontexist="));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == QString("BRYaKcFxuNExx2Zw1w3Qy7H2U3DdgPAszN"));
    QVERIFY(rv.label == QString());
    QVERIFY(rv.amount == 0);

    uri.setUrl(QString("babacoin:BRYaKcFxuNExx2Zw1w3Qy7H2U3DdgPAszN?label=Some Example Address"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == QString("BRYaKcFxuNExx2Zw1w3Qy7H2U3DdgPAszN"));
    QVERIFY(rv.label == QString("Some Example Address"));
    QVERIFY(rv.amount == 0);

    uri.setUrl(QString("babacoin:BRYaKcFxuNExx2Zw1w3Qy7H2U3DdgPAszN?amount=0.001"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == QString("BRYaKcFxuNExx2Zw1w3Qy7H2U3DdgPAszN"));
    QVERIFY(rv.label == QString());
    QVERIFY(rv.amount == 100000);

    uri.setUrl(QString("babacoin:BRYaKcFxuNExx2Zw1w3Qy7H2U3DdgPAszN?amount=1.001"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == QString("BRYaKcFxuNExx2Zw1w3Qy7H2U3DdgPAszN"));
    QVERIFY(rv.label == QString());
    QVERIFY(rv.amount == 100100000);

    uri.setUrl(QString("babacoin:BRYaKcFxuNExx2Zw1w3Qy7H2U3DdgPAszN?amount=100&label=Some Example"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == QString("BRYaKcFxuNExx2Zw1w3Qy7H2U3DdgPAszN"));
    QVERIFY(rv.amount == 10000000000LL);
    QVERIFY(rv.label == QString("Some Example"));

    uri.setUrl(QString("babacoin:BRYaKcFxuNExx2Zw1w3Qy7H2U3DdgPAszN?message=Some Example Address"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == QString("BRYaKcFxuNExx2Zw1w3Qy7H2U3DdgPAszN"));
    QVERIFY(rv.label == QString());

    QVERIFY(GUIUtil::parseBitcoinURI("babacoin:BRYaKcFxuNExx2Zw1w3Qy7H2U3DdgPAszN?message=Some Example Address", &rv));
    QVERIFY(rv.address == QString("BRYaKcFxuNExx2Zw1w3Qy7H2U3DdgPAszN"));
    QVERIFY(rv.label == QString());

    uri.setUrl(QString("babacoin:BRYaKcFxuNExx2Zw1w3Qy7H2U3DdgPAszN?req-message=Some Example Address"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));

    uri.setUrl(QString("babacoin:BRYaKcFxuNExx2Zw1w3Qy7H2U3DdgPAszN?amount=1,000&label=Some Example"));
    QVERIFY(!GUIUtil::parseBitcoinURI(uri, &rv));

    uri.setUrl(QString("babacoin:BRYaKcFxuNExx2Zw1w3Qy7H2U3DdgPAszN?amount=1,000.0&label=Some Example"));
    QVERIFY(!GUIUtil::parseBitcoinURI(uri, &rv));

    uri.setUrl(QString("babacoin:BRYaKcFxuNExx2Zw1w3Qy7H2U3DdgPAszN?amount=100&label=Some Example&message=Some Example Message"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == QString("BRYaKcFxuNExx2Zw1w3Qy7H2U3DdgPAszN"));
    QVERIFY(rv.amount == 10000000000LL);
    QVERIFY(rv.label == QString("Some Example"));
    QVERIFY(rv.message == QString("Some Example Message"));

    // Verify that IS=xxx does not lead to an error (we ignore the field)
    uri.setUrl(QString("babacoin:BRYaKcFxuNExx2Zw1w3Qy7H2U3DdgPAszN?IS=1"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));

    uri.setUrl(QString("babacoin:BRYaKcFxuNExx2Zw1w3Qy7H2U3DdgPAszN?req-IS=1"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));

    uri.setUrl(QString("babacoin:BRYaKcFxuNExx2Zw1w3Qy7H2U3DdgPAszN"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
}
