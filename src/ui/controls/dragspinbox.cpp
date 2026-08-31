/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/controls/dragspinbox.h"

#include <QGuiApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>

namespace {
// pixels of horizontal travel before a press turns into a scrub
const int kDragThresholdPx = 3;
const double kFineFactor = 0.1;   // Ctrl held
const double kCoarseFactor = 10.0; // Shift held
}

DragSpinBox::DragSpinBox(QWidget* parent) : QDoubleSpinBox(parent)
{
    setButtonSymbols(QAbstractSpinBox::NoButtons);
    setKeyboardTracking(false);
    setFocusPolicy(Qt::StrongFocus);

    // scrub gestures land on the child line edit, watch it
    lineEdit()->installEventFilter(this);
    lineEdit()->setCursor(Qt::SizeHorCursor);
}

void DragSpinBox::setPerPixelStep(double step)
{
    perPixelStep_ = step;
}

double DragSpinBox::perPixelStep() const
{
    return perPixelStep_;
}

bool DragSpinBox::isScrubbing() const
{
    return scrubbing_;
}

bool DragSpinBox::eventFilter(QObject* watched, QEvent* event)
{
    if (watched != lineEdit())
        return QDoubleSpinBox::eventFilter(watched, event);

    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        auto mouse = static_cast<QMouseEvent*>(event);
        // when already editing (typing), leave the mouse to the line edit
        // (caret, selection). editing_ — not hasFocus(): see the header note.
        if (mouse->button() == Qt::LeftButton && !editing_) {
            pressed_ = true;
            scrubbing_ = false;
            pressGlobalPos_ = mouse->globalPosition().toPoint();
            lastGlobalX_ = pressGlobalPos_.x();
            startValue_ = value();
            scrubValue_ = startValue_;
            return true; // no focus yet — decided on release
        }
        break;
    }
    case QEvent::MouseMove: {
        if (!pressed_)
            break;
        auto mouse = static_cast<QMouseEvent*>(event);
        const int gx = mouse->globalPosition().toPoint().x();

        if (!scrubbing_) {
            if (qAbs(gx - pressGlobalPos_.x()) < kDragThresholdPx)
                return true;
            beginScrub();
        }

        double factor = 1.0;
        const auto mods = mouse->modifiers();
        if (mods & Qt::ControlModifier) factor *= kFineFactor;
        if (mods & Qt::ShiftModifier) factor *= kCoarseFactor;

        // accumulate incrementally so toggling a modifier mid-drag
        // changes the rate, not the value reached so far
        scrubValue_ = qBound(minimum(),
                             scrubValue_ + (gx - lastGlobalX_) * perPixelStep_ * factor,
                             maximum());
        lastGlobalX_ = gx;
        setValue(scrubValue_);
        return true;
    }
    case QEvent::MouseButtonRelease: {
        auto mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() != Qt::LeftButton || !pressed_)
            break;
        pressed_ = false;
        if (scrubbing_) {
            endScrub(false);
        } else {
            // plain click: enter editing mode, focus for typing
            editing_ = true;
            lineEdit()->setCursor(Qt::IBeamCursor);
            lineEdit()->setFocus(Qt::MouseFocusReason);
            lineEdit()->selectAll();
        }
        return true;
    }
    case QEvent::FocusIn: {
        // Tab / programmatic focus for typing also counts as editing; the
        // click-focus Qt grants during a press does not (reason = Mouse, and
        // our release decides whether it was a click or a scrub).
        auto focus = static_cast<QFocusEvent*>(event);
        if (focus->reason() == Qt::TabFocusReason ||
            focus->reason() == Qt::BacktabFocusReason) {
            editing_ = true;
            lineEdit()->setCursor(Qt::IBeamCursor);
        }
        break;
    }
    case QEvent::FocusOut:
        // back to scrub affordance once typing ends
        editing_ = false;
        lineEdit()->setCursor(Qt::SizeHorCursor);
        break;
    default:
        break;
    }

    return QDoubleSpinBox::eventFilter(watched, event);
}

void DragSpinBox::keyPressEvent(QKeyEvent* event)
{
    if (scrubbing_ && event->key() == Qt::Key_Escape) {
        setValue(startValue_);
        pressed_ = false;
        endScrub(true);
        event->accept();
        return;
    }
    QDoubleSpinBox::keyPressEvent(event);
}

void DragSpinBox::beginScrub()
{
    scrubbing_ = true;
    grabKeyboard(); // so Esc reaches us mid-drag
    emit scrubStarted();
}

void DragSpinBox::endScrub(bool cancelled)
{
    scrubbing_ = false;
    releaseKeyboard();
    emit scrubFinished(cancelled);
}
