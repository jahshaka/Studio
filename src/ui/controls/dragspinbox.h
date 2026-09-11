/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef DRAGSPINBOX_H
#define DRAGSPINBOX_H

#include <QColor>
#include <QDoubleSpinBox>
#include <QPoint>

/**
 * A QDoubleSpinBox that can be scrubbed like Blender/Unreal drag-sliders.
 *
 * - Click-and-drag horizontally on the field changes the value
 *   (perPixelStep units per pixel; Ctrl = fine x0.1, Shift = coarse x10).
 * - Click without dragging focuses the field for normal typing.
 * - Esc during a drag cancels it and restores the value at drag start.
 * - The cursor shows SizeHor while hovering (I-beam while editing).
 *
 * Reusable by any panel: listen to scrubStarted()/scrubFinished(bool)
 * to batch the whole drag into one undoable change; valueChanged(double)
 * still fires continuously for live preview.
 */
class DragSpinBox : public QDoubleSpinBox
{
    Q_OBJECT

public:
    explicit DragSpinBox(QWidget* parent = nullptr);

    // value change per horizontal pixel dragged (before modifiers)
    void setPerPixelStep(double step);
    double perPixelStep() const;

    bool isScrubbing() const;

    // AXIS IDENTITY (X red / Y green / Z blue): a 3 px strip painted on the
    // field's left edge. The style draws the field itself — this is the
    // sheet-free replacement for the Classic theme's `border-left: 3px solid`
    // rule, which under Qlementine would hand the whole field to
    // QStyleSheetStyle. Invalid colour (the default) = no strip.
    void setAxisColor(const QColor &color);

signals:
    void scrubStarted();
    // cancelled = true when the drag was aborted with Esc
    // (the value has already been restored to its pre-drag state)
    void scrubFinished(bool cancelled);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void beginScrub();
    void endScrub(bool cancelled);

    double perPixelStep_ = 0.02;
    bool pressed_ = false;
    bool scrubbing_ = false;
    // True only while the user is actually TYPING in the box (entered via our
    // own click-release or Tab). NOT the same as hasFocus(): Qt hands the box
    // click-focus BEFORE the press reaches the event filter (and the line
    // edit's focus proxy is the spinbox), so a hasFocus() guard at press time
    // is always true and would kill the scrub gesture.
    bool editing_ = false;
    QPoint pressGlobalPos_;
    int lastGlobalX_ = 0;
    double startValue_ = 0;
    double scrubValue_ = 0;
    QColor axisColor_;
    QWidget *axisStrip_ = nullptr;
    void placeAxisStrip();
};

#endif // DRAGSPINBOX_H
