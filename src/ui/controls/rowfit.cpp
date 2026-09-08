/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/controls/rowfit.h"

#include <QAbstractSpinBox>
#include <QComboBox>
#include <QEvent>
#include <QFontMetrics>
#include <QLabel>
#include <QLineEdit>
#include <QWidget>

namespace {

const char *kFitted = "jahRowFitted";

/// Keeps a QLabel's text elided to whatever width the layout gives it.
///
/// The label's own text is the elided string — that is what makes the label
/// stop demanding its natural width — so the filter has to remember the FULL
/// text itself. It also has to notice when somebody else calls setText() (every
/// panel rebuild does): the test is "the text on screen is not the one I last
/// wrote", which is true exactly when a new full text has arrived.
class ElideFilter : public QObject
{
public:
    explicit ElideFilter(QLabel *label) : QObject(label), mLabel(label)
    {
        mFull = label->text();
        label->installEventFilter(this);
        apply();
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        switch (event->type()) {
        case QEvent::FontChange:
        case QEvent::StyleChange:
            mLastWidth = -1;   // the metrics changed: the cached decision is stale
            apply();
            break;
        case QEvent::Resize:
        case QEvent::LayoutRequest:
        case QEvent::Polish:
        case QEvent::Paint:
            apply();
            break;
        default:
            break;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    void apply()
    {
        if (mInside) return;                    // setText() re-enters through Paint
        const QString current = mLabel->text();
        if (current != mWrote) mFull = current; // a new value arrived from the panel

        const QMargins m = mLabel->contentsMargins();
        const int avail = mLabel->width() - m.left() - m.right() - 2 * mLabel->margin();
        if (avail <= 0) return;

        // Paint is one of the events this listens to, so the common case has to
        // be free: same width, same text, nothing to do. (Selection cost is a
        // watched number here — ui.selection_cost.)
        if (avail == mLastWidth && current == mWrote) return;
        mLastWidth = avail;

        const QString elided = mLabel->fontMetrics().elidedText(mFull, Qt::ElideRight, avail);
        if (elided == current) return;

        mInside = true;
        mWrote = elided;
        mLabel->setText(elided);
        // The name stays reachable when it does not fit — but never at the cost
        // of a tooltip the PANEL set (those explain the setting; this one only
        // repeats the name).
        if (elided != mFull && (mLabel->toolTip().isEmpty() || mLabel->toolTip() == mOwnTip)) {
            mOwnTip = mFull;
            mLabel->setToolTip(mFull);
        }
        mInside = false;
    }

    QLabel *mLabel;
    QString mFull;    ///< the text the panel set
    QString mWrote;   ///< the elided text this filter put on screen
    QString mOwnTip;  ///< the tooltip this filter set (so it never eats a panel's)
    int mLastWidth = -1;
    bool mInside = false;
};

bool markFitted(QWidget *w)
{
    if (!w || w->property(kFitted).toBool()) return false;
    w->setProperty(kFitted, true);
    return true;
}

void shrinkable(QWidget *w, int minimum)
{
    // AN EXPLICIT MINIMUM WIDTH IS THE MECHANISM, not the size policy.
    // qSmartMinSize (qlayoutengine.cpp) takes a widget's minimum from its
    // minimumSizeHint — which for a QLabel is the full width of its text, and
    // for a QComboBox its widest item — UNLESS an explicit minimumWidth is set,
    // which overrides it outright. So this sets one: the control still PREFERS
    // its natural width and keeps it whenever the dock has room, and only
    // shrinks (and elides) when the dock does not.
    //
    // The first attempt used QSizePolicy::Ignored instead, and it was wrong in
    // a way worth recording: a control whose size hint is ignored gets nothing
    // but its minimum whenever it shares a layout with a stretch — the light
    // channels row's "Channels" name and the transform editor's axis names
    // both VANISHED (measured on the rig, before/after screenshots). Ignored is
    // right for the drag-value fields, which set it themselves and are meant to
    // divide a row equally; it is wrong as a blanket rule.
    QSizePolicy policy = w->sizePolicy();
    if (policy.horizontalPolicy() != QSizePolicy::Ignored) {
        policy.setHorizontalPolicy(QSizePolicy::Preferred);
        w->setSizePolicy(policy);
    }
    w->setMinimumWidth(minimum);
}

}   // namespace

namespace RowFit {

void fitLabel(QLabel *label)
{
    if (!markFitted(label)) return;

    if (label->wordWrap()) {
        // A WRAPPING label already has a small minimum (one word, not one
        // paragraph) — it is the shape prose should have, and LabelWidget's
        // value label asks for it deliberately. Leave its text alone; it only
        // needs to be allowed to grow taller as it narrows.
        QSizePolicy policy = label->sizePolicy();
        policy.setHorizontalPolicy(QSizePolicy::Preferred);
        policy.setVerticalPolicy(QSizePolicy::Minimum);
        policy.setHeightForWidth(true);
        label->setSizePolicy(policy);
        label->setMinimumWidth(kMinControlWidth);
        return;
    }

    // A NON-wrapping label would otherwise demand the full width of its text,
    // and growing taller instead is not an option for a row whose height the
    // accordion computed by hand — so it elides.
    shrinkable(label, kMinControlWidth);
    new ElideFilter(label);
}

void fitCombo(QComboBox *combo)
{
    if (!markFitted(combo)) return;
    // AdjustToContentsOnFirstShow (the default) makes the minimum width the
    // WIDEST ITEM in the list — one long preset name and the row is wider than
    // the dock forever. With a minimum contents length the combo sizes from
    // that instead, and elides its current text like it always could.
    combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    combo->setMinimumContentsLength(4);
    shrinkable(combo, kMinControlWidth);
}

void fitSpin(QAbstractSpinBox *spin)
{
    if (!markFitted(spin)) return;
    shrinkable(spin, kMinControlWidth);
}

void fitLineEdit(QLineEdit *edit)
{
    if (!markFitted(edit)) return;
    shrinkable(edit, kMinControlWidth);
}

void fitRow(QWidget *row)
{
    if (!row) return;

    // A combo box and a spin box own their internals (a view, a line edit, two
    // buttons). Fit the control and stop: walking into it would hand its own
    // line edit a policy Qt never gives it.
    if (auto *combo = qobject_cast<QComboBox *>(row))       { fitCombo(combo); return; }
    if (auto *spin = qobject_cast<QAbstractSpinBox *>(row)) { fitSpin(spin); return; }

    if (auto *label = qobject_cast<QLabel *>(row))          fitLabel(label);
    else if (auto *edit = qobject_cast<QLineEdit *>(row))   fitLineEdit(edit);
    else if (markFitted(row)) {
        // A container: it must not carry a minimum of its own either, or the
        // shrinking of everything inside it buys nothing.
        QSizePolicy policy = row->sizePolicy();
        if (policy.horizontalPolicy() != QSizePolicy::Ignored)
            policy.setHorizontalPolicy(QSizePolicy::Preferred);
        row->setSizePolicy(policy);
        if (row->minimumWidth() > kMinControlWidth) row->setMinimumWidth(kMinControlWidth);
    }

    for (QObject *child : row->children()) {
        if (auto *w = qobject_cast<QWidget *>(child)) {
            if (!w->isWindow()) fitRow(w);
        }
    }
}

}   // namespace RowFit
