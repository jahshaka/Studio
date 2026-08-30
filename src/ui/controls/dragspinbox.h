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

signals:
    void scrubStarted();
    // cancelled = true when the drag was aborted with Esc
    // (the value has already been restored to its pre-drag state)
    void scrubFinished(bool cancelled);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void beginScrub();
    void endScrub(bool cancelled);

    double perPixelStep_ = 0.02;
    bool pressed_ = false;
    bool scrubbing_ = false;
    QPoint pressGlobalPos_;
    int lastGlobalX_ = 0;
    double startValue_ = 0;
    double scrubValue_ = 0;
};

#endif // DRAGSPINBOX_H
