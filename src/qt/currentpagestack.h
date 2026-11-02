// Copyright (c) 2026 The Babacoin developers
// Distributed under the MIT software license, see the accompanying COPYING file.
#ifndef BABACOIN_QT_CURRENTPAGESTACK_H
#define BABACOIN_QT_CURRENTPAGESTACK_H

#include <QStackedWidget>
#include <QLayout>

/** Hidden desktop forms must not impose their minimum size on other pages. */
class CurrentPageStack : public QStackedWidget
{
public:
    explicit CurrentPageStack(QWidget* parent = nullptr) : QStackedWidget(parent)
    {
#if defined(Q_OS_WIN) || defined(Q_OS_MAC)
        // QStackedLayout otherwise also writes the maximum hidden-page
        // minimum directly onto this widget, bypassing minimumSizeHint().
        layout()->setSizeConstraint(QLayout::SetNoConstraint);
#endif
        connect(this, &QStackedWidget::currentChanged, this, [this] { updateGeometry(); });
    }
    QSize minimumSizeHint() const override
    {
#if defined(Q_OS_WIN) || defined(Q_OS_MAC)
        if (auto* page = currentWidget()) return page->minimumSizeHint().expandedTo(page->minimumSize());
#endif
        return QStackedWidget::minimumSizeHint();
    }
    QSize sizeHint() const override
    {
#if defined(Q_OS_WIN) || defined(Q_OS_MAC)
        if (auto* page = currentWidget()) return page->sizeHint();
#endif
        return QStackedWidget::sizeHint();
    }
};
#endif
