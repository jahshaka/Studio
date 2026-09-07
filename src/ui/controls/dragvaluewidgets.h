/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef DRAGVALUEWIDGETS_H
#define DRAGVALUEWIDGETS_H

// The COMPACT, SCRUBBABLE number rows for the property panels (owner report
// 2026-09-07: "the World > GI number rows are too wide and you cannot drag
// them").
//
// The transform editor has had the right shape since it was written: a
// fixed-width right-aligned title, fields that shrink with the panel
// (QSizePolicy::Ignored, so a 3-field row never forces the dock wider), a
// coloured left edge per axis instead of an X/Y/Z chip, and DragSpinBox — which
// scrubs on a horizontal click-drag and only enters text edit on a clean click.
// Everything else in the panels used Widget3D/HFloatSliderWidget, which are
// plain QDoubleSpinBoxes at their natural width with no label at all
// (AccordianBladeWidget::addVector3Widget literally ignores its name argument).
//
// These two are that shape, extracted so any panel can have it. They are
// deliberately NOT a change to Widget3D: that class is the materials module's
// node-property row and is laid out inside a different container.

#include <QString>
#include <QWidget>

#include "irisgl/core/math/vec.h"

class DragSpinBox;

/// One labelled, scrubbable scalar. `valueChanged` carries the new value;
/// `editingDone` fires once per gesture (a finished scrub or a typed commit),
/// which is what a caller that pushes an undo command or rebuilds a panel
/// should listen to instead.
class DragFloatWidget : public QWidget
{
    Q_OBJECT
public:
    explicit DragFloatWidget(const QString &title, QWidget *parent = nullptr);

    void setRange(double min, double max);
    void setDecimals(int decimals);
    void setPerPixelStep(double step);
    void setValue(double value);          ///< quiet: does not emit valueChanged
    double value() const;
    void setSuffix(const QString &suffix);

signals:
    void valueChanged(double value);
    void editingDone();

private:
    DragSpinBox *box = nullptr;
};

/// Three of the above on one row, with the transform editor's axis colours.
class DragVector3Widget : public QWidget
{
    Q_OBJECT
public:
    explicit DragVector3Widget(const QString &title, QWidget *parent = nullptr);

    void setRange(double min, double max);
    void setDecimals(int decimals);
    void setPerPixelStep(double step);
    void setValues(const iris::Vec3 &v);   ///< quiet: does not emit valueChanged
    iris::Vec3 values() const;

signals:
    void valueChanged(iris::Vec3 value);
    void editingDone();

private:
    DragSpinBox *x = nullptr, *y = nullptr, *z = nullptr;
    bool quiet = false;
};

#endif // DRAGVALUEWIDGETS_H
