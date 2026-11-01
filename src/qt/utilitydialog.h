// Copyright (c) 2011-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_UTILITYDIALOG_H
#define BITCOIN_QT_UTILITYDIALOG_H

#include <QDialog>
#include <QObject>
#include <QElapsedTimer>

class QLabel;
class QProgressBar;
class QPushButton;
class QTimer;

/** Read-only startup UI. It has no wallet/backend access. */
class StartupPanel : public QWidget
{
    Q_OBJECT
public:
    StartupPanel(const QString& version, const QString& network, const QString& dataDirectory, QWidget* parent = nullptr);
    QString diagnostics() const;
public Q_SLOTS:
    void setPhase(const QString& message);
    void setProgress(const QString& title, int progress);
    void setWalletScanProgress(const QString& wallet, int start, int current, int target, bool complete);
    void requestStop();
Q_SIGNALS:
    void stopRequested();
private:
    QString m_version, m_network, m_directory;
    QLabel* m_phase;
    QLabel* m_elapsed;
    QLabel* m_scanSummary;
    QProgressBar* m_progress;
    QPushButton* m_stop;
    QElapsedTimer m_clock;
    bool m_stopping{false};
};

class BitcoinGUI;

namespace interfaces {
    class Node;
}

namespace Ui {
    class HelpMessageDialog;
}

/** "Help message" dialog box */
class HelpMessageDialog : public QDialog
{
    Q_OBJECT

public:
    enum HelpMode {
        about,
        cmdline,
        pshelp
    };

    explicit HelpMessageDialog(interfaces::Node& node, QWidget *parent, HelpMode helpMode);
    ~HelpMessageDialog();

    void printToConsole();
    void showOrPrint();

private:
    Ui::HelpMessageDialog *ui;
    QString text;

private Q_SLOTS:
    void on_okButton_accepted();
};


/** "Shutdown" window */
class ShutdownWindow : public QWidget
{
    Q_OBJECT

public:
    explicit ShutdownWindow(interfaces::Node& node, QWidget *parent=0, Qt::WindowFlags f=0);
    static QWidget *showShutdownWindow(interfaces::Node& node, BitcoinGUI *window);

protected:
    void closeEvent(QCloseEvent *event);
};

#endif // BITCOIN_QT_UTILITYDIALOG_H
