// Copyright (c) 2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/modaloverlay.h>
#include <qt/forms/ui_modaloverlay.h>

#include <qt/guiutil.h>

#include <chainparams.h>
#include <ui_interface.h>

#include <QResizeEvent>
#include <QPropertyAnimation>
#include <algorithm>
#include <cmath>

ModalOverlay::ModalOverlay(QWidget *parent) :
QWidget(parent),
ui(new Ui::ModalOverlay),
bestHeaderHeight(0),
bestHeaderDate(QDateTime()),
layerIsVisible(false),
userClosed(false),
foreverHidden(false)
{
    ui->setupUi(this);
    normalInfoText = ui->infoTextStrong->text();

    GUIUtil::setFont({ui->infoTextStrong,
                      ui->labelNumberOfBlocksLeft,
                      ui->labelLastBlockTime,
                      ui->labelSyncDone,
                      ui->labelProgressIncrease,
                      ui->labelEstimatedTimeLeft,
                     }, GUIUtil::FontWeight::Bold);

    ui->warningIcon->setPixmap(GUIUtil::getIcon("warning", GUIUtil::ThemedColor::ORANGE).pixmap(48, 48));

    connect(ui->closeButton, SIGNAL(clicked()), this, SLOT(closeClicked()));
    if (parent) {
        parent->installEventFilter(this);
        raise();
    }

    blockProcessTime.clear();
    setVisible(false);

    GUIUtil::updateFonts();
}

ModalOverlay::~ModalOverlay()
{
    delete ui;
}

bool ModalOverlay::eventFilter(QObject * obj, QEvent * ev) {
    if (obj == parent()) {
        if (ev->type() == QEvent::Resize) {
            QResizeEvent * rev = static_cast<QResizeEvent*>(ev);
            resize(rev->size());
            if (!layerIsVisible)
                setGeometry(0, height(), width(), height());

        }
        else if (ev->type() == QEvent::ChildAdded) {
            raise();
        }
    }
    return QWidget::eventFilter(obj, ev);
}

//! Tracks parent widget changes
bool ModalOverlay::event(QEvent* ev) {
    if (ev->type() == QEvent::ParentAboutToChange) {
        if (parent()) parent()->removeEventFilter(this);
    }
    else if (ev->type() == QEvent::ParentChange) {
        if (parent()) {
            parent()->installEventFilter(this);
            raise();
        }
    }
    return QWidget::event(ev);
}

void ModalOverlay::setKnownBestHeight(int count, const QDateTime& blockDate)
{
    if (count > bestHeaderHeight) {
        bestHeaderHeight = count;
        bestHeaderDate = blockDate;
        updateProgress();
    }
}

void ModalOverlay::setImporting(bool importing)
{
    if (importingFromDisk == importing) return;
    importingFromDisk = importing;
    ui->infoTextStrong->setText(normalInfoText + (importing
        ? "\n" + tr("Importing blocks from disk. Block verification continues before balances are ready.")
        : QString()));
}

void ModalOverlay::tipUpdate(int count, const QDateTime& blockDate, double nVerificationProgress)
{
    lastTipHeight = count;
    lastTipDate = blockDate;
    lastVerificationProgress = nVerificationProgress;
    updateProgress();
}

void ModalOverlay::setImportProgress(int phase, qulonglong processed, qulonglong total, int newBlocks)
{
    importPhase = phase;
    importProcessedBytes = total > 0 ? std::min(processed, total) : processed;
    importTotalBytes = total;
    importNewBlocks = std::max(0, newBlocks);
    setImporting(phase != BlockImportProgress::IDLE);
    updateProgress();
    if (phase != BlockImportProgress::IDLE) showHide(false);
}

void ModalOverlay::updateProgress()
{
    const QDateTime now = QDateTime::currentDateTime();
    if (importPhase == BlockImportProgress::READING) {
        ui->labelSyncDone->setText(tr("Reading blockchain file"));
        ui->labelNumberOfBlocksLeft->setText(tr("New blocks indexed"));
        ui->numberOfBlocksLeft->setText(QString::number(importNewBlocks));
        ui->labelLastBlockTime->setText(tr("File bytes processed"));
        ui->newestBlockDate->setText(QString("%1 / %2").arg(importProcessedBytes).arg(importTotalBytes));
        ui->progressIncreasePerH->setText(tr("Unknown"));
        ui->expectedTimeLeft->setText(tr("Unknown..."));
        if (importTotalBytes > 0) {
            const double progress = double(importProcessedBytes) / importTotalBytes;
            ui->percentageProgress->setText(QString::number(progress * 100, 'f', 2) + "%");
            ui->progressBar->setRange(0, 10000);
            ui->progressBar->setValue(int(progress * 10000 + 0.5));
        } else {
            ui->percentageProgress->setText(tr("Unknown"));
            ui->progressBar->setRange(0, 0);
        }
        blockProcessTime.clear();
        haveProgressStage = false;
        return;
    }
    if (!bestHeaderDate.isValid()) {
        ui->labelSyncDone->setText(importPhase == BlockImportProgress::VALIDATING
            ? tr("Validating imported blocks") : tr("Waiting for headers"));
        ui->percentageProgress->setText(tr("Unknown"));
        ui->progressBar->setRange(0, 0);
        ui->expectedTimeLeft->setText(tr("Unknown..."));
        return;
    }
    const qint64 remainingHeaders = std::max<qint64>(0,
        bestHeaderDate.secsTo(now) / Params().GetConsensus().nPowTargetSpacing);
    const bool headers = importPhase != BlockImportProgress::VALIDATING && remainingHeaders > HEADER_HEIGHT_DELTA_SYNC;
    double progress = std::isfinite(lastVerificationProgress)
        ? std::max(0.0, std::min(1.0, lastVerificationProgress)) : 0.0;
    if (headers) {
        // Header-time estimates must never be presented as completed block
        // validation. Historical transaction-count estimates can already be 1.
        const double target = double(bestHeaderHeight) + remainingHeaders;
        progress = target > 0 ? bestHeaderHeight / target : 0;
        progress = std::max(0.0, std::min(0.9999, progress));
        ui->labelSyncDone->setText(tr("Header synchronization (estimate)"));
        ui->labelNumberOfBlocksLeft->setText(tr("Headers remaining (estimate)"));
        ui->numberOfBlocksLeft->setText(QString::number(remainingHeaders));
        ui->labelLastBlockTime->setText(tr("Last header time"));
        ui->newestBlockDate->setText(bestHeaderDate.toString());
    } else {
        if (bestHeaderHeight > 0) {
            progress = std::min(progress, std::max(0, lastTipHeight) / double(bestHeaderHeight));
        }
        if (lastTipHeight < bestHeaderHeight) progress = std::min(progress, 0.9999);
        ui->labelSyncDone->setText(importPhase == BlockImportProgress::VALIDATING
            ? tr("Validating imported blocks (estimate)") : tr("Block verification (estimate)"));
        ui->labelNumberOfBlocksLeft->setText(tr("Blocks remaining"));
        ui->numberOfBlocksLeft->setText(QString::number(std::max(0, bestHeaderHeight - std::max(0, lastTipHeight))));
        ui->labelLastBlockTime->setText(tr("Last block time"));
        ui->newestBlockDate->setText(lastTipDate.isValid() ? lastTipDate.toString() : tr("Unknown"));
    }
    const qint64 timestamp = now.toMSecsSinceEpoch();
    if (!haveProgressStage || headers != lastProgressWasHeaders ||
        (!blockProcessTime.isEmpty() && (progress < blockProcessTime.front().second || timestamp <= blockProcessTime.front().first))) {
        blockProcessTime.clear();
    }
    lastProgressWasHeaders = headers;
    haveProgressStage = true;
    blockProcessTime.push_front(qMakePair(timestamp, progress));
    if (blockProcessTime.size() > 5000) blockProcessTime.resize(5000);
    double perHour = 0;
    qint64 remainingMSecs = -1;
    for (int i = 1; i < blockProcessTime.size(); ++i) {
        const auto& sample = blockProcessTime[i];
        if (sample.first < timestamp - 500000 || i == blockProcessTime.size() - 1) {
            const qint64 elapsed = timestamp - sample.first;
            const double delta = progress - sample.second;
            if (elapsed > 0 && delta > 0) {
                perHour = delta / elapsed * 1000 * 3600;
                remainingMSecs = (1.0 - progress) / delta * elapsed;
            }
            break;
        }
    }
    ui->progressIncreasePerH->setText(QString::number(perHour * 100, 'f', 2) + "%");
    ui->expectedTimeLeft->setText(!headers && remainingMSecs >= 0
        ? GUIUtil::formatNiceTimeOffset(remainingMSecs / 1000.0) : tr("Unknown..."));
    ui->percentageProgress->setText(QString::number(progress * 100, 'f', 2) + "%");
    ui->progressBar->setRange(0, 10000);
    ui->progressBar->setValue(int(progress * 10000 + 0.5));
}

void ModalOverlay::toggleVisibility()
{
    showHide(layerIsVisible, true);
    if (!layerIsVisible)
        userClosed = true;
}

void ModalOverlay::showHide(bool hide, bool userRequested)
{
    if ( (layerIsVisible && !hide) || (!layerIsVisible && hide) || (!hide && userClosed && !userRequested))
        return;

    if (!hide && foreverHidden)
        return;

    if (!isVisible() && !hide)
        setVisible(true);

    setGeometry(0, hide ? 0 : height(), width(), height());

    QPropertyAnimation* animation = new QPropertyAnimation(this, "pos");
    animation->setDuration(300);
    animation->setStartValue(QPoint(0, hide ? 0 : this->height()));
    animation->setEndValue(QPoint(0, hide ? this->height() : 0));
    animation->setEasingCurve(QEasingCurve::OutQuad);
    animation->start(QAbstractAnimation::DeleteWhenStopped);
    layerIsVisible = !hide;
}

void ModalOverlay::closeClicked()
{
    showHide(true);
    userClosed = true;
}

void ModalOverlay::hideForever()
{
    foreverHidden = true;
}
