/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/controls/dragvaluewidgets.h"

#include <QGridLayout>
#include <QLabel>
#include <QSignalBlocker>

#include "ui/controls/dragspinbox.h"
#include "ui/style/stylesheet.h"

namespace {
// The transform editor's numbers, verbatim (ui/panels/transformeditor.cpp):
// the point of these widgets is that a GI bounds row and a Position row LOOK
// like the same control, because they are the same control.
const int kTitleWidth = 56;

// The row sheet (also the transform editor's) is Classic's
// (StyleSheet::DragValueRowPanel); under Qlementine the fields are the style's
// own and the X/Y/Z identity is a painted strip (DragSpinBox::setAxisColor).
void colourAxes(DragSpinBox *x, DragSpinBox *y, DragSpinBox *z)
{
    if (StyleSheet::classicThemeActive()) return;
    if (x) x->setAxisColor(QColor(0xc0, 0x39, 0x2b));
    if (y) y->setAxisColor(QColor(0x27, 0xae, 0x60));
    if (z) z->setAxisColor(QColor(0x29, 0x80, 0xb9));
}

DragSpinBox *makeField(QWidget *parent, const QString &objectName, double perPixelStep)
{
    auto *box = new DragSpinBox(parent);
    box->setObjectName(objectName);
    box->setDecimals(3);
    box->setRange(-100000.0, 100000.0);
    box->setPerPixelStep(perPixelStep);
    // Ignored, exactly as the transform editor's fields: three number fields at
    // their NATURAL width is what made the GI rows push the dock wider than the
    // panel it lives in.
    box->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    return box;
}

QLabel *makeTitle(QWidget *parent, const QString &title)
{
    auto *label = new QLabel(title, parent);
    label->setFixedWidth(kTitleWidth);
    label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    label->setToolTip(title);   // the width elides long names
    return label;
}
}   // namespace

// ---------------------------------------------------------------------------

DragFloatWidget::DragFloatWidget(const QString &title, QWidget *parent)
    : QWidget(parent)
{
    setObjectName("DragValueRow");
    setStyleSheet(StyleSheet::DragValueRowPanel());

    auto *grid = new QGridLayout(this);
    grid->setContentsMargins(14, 2, 14, 2);
    grid->setHorizontalSpacing(4);
    grid->setVerticalSpacing(2);
    grid->addWidget(makeTitle(this, title), 0, 0);

    box = makeField(this, QStringLiteral("dragvalue"), 0.02);
    grid->addWidget(box, 0, 1);
    grid->setColumnStretch(1, 1);

    connect(box, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &DragFloatWidget::valueChanged);
    connect(box, &DragSpinBox::scrubFinished, this, [this](bool) { emit editingDone(); });
    connect(box, &QAbstractSpinBox::editingFinished, this, &DragFloatWidget::editingDone);
    adjustSize();   // AccordianBladeWidget sizes the blade from height()
}

void DragFloatWidget::setRange(double min, double max) { box->setRange(min, max); }
void DragFloatWidget::setDecimals(int decimals)        { box->setDecimals(decimals); }
void DragFloatWidget::setPerPixelStep(double step)     { box->setPerPixelStep(step); }
void DragFloatWidget::setSuffix(const QString &suffix) { box->setSuffix(suffix); }
double DragFloatWidget::value() const                  { return box->value(); }

void DragFloatWidget::setValue(double value)
{
    const QSignalBlocker blocked(box);
    box->setValue(value);
}

// ---------------------------------------------------------------------------

DragVector3Widget::DragVector3Widget(const QString &title, QWidget *parent)
    : QWidget(parent)
{
    setObjectName("DragValueRow");
    setStyleSheet(StyleSheet::DragValueRowPanel());

    auto *grid = new QGridLayout(this);
    grid->setContentsMargins(14, 2, 14, 2);
    grid->setHorizontalSpacing(4);
    grid->setVerticalSpacing(2);
    grid->addWidget(makeTitle(this, title), 0, 0);

    x = makeField(this, QStringLiteral("dragx"), 0.02);
    y = makeField(this, QStringLiteral("dragy"), 0.02);
    z = makeField(this, QStringLiteral("dragz"), 0.02);
    colourAxes(x, y, z);
    grid->addWidget(x, 0, 1);
    grid->addWidget(y, 0, 2);
    grid->addWidget(z, 0, 3);
    for (int c = 1; c <= 3; ++c) grid->setColumnStretch(c, 1);

    for (DragSpinBox *box : { x, y, z }) {
        connect(box, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) {
            if (quiet) return;
            emit valueChanged(values());
        });
        connect(box, &DragSpinBox::scrubFinished, this, [this](bool) { emit editingDone(); });
        connect(box, &QAbstractSpinBox::editingFinished, this, &DragVector3Widget::editingDone);
    }
    adjustSize();
}

void DragVector3Widget::setRange(double min, double max)
{
    for (DragSpinBox *box : { x, y, z }) box->setRange(min, max);
}

void DragVector3Widget::setDecimals(int decimals)
{
    for (DragSpinBox *box : { x, y, z }) box->setDecimals(decimals);
}

void DragVector3Widget::setPerPixelStep(double step)
{
    for (DragSpinBox *box : { x, y, z }) box->setPerPixelStep(step);
}

void DragVector3Widget::setValues(const iris::Vec3 &v)
{
    // ONE quiet flag rather than three QSignalBlockers: the three fields feed
    // ONE valueChanged(Vec3), so blocking them individually would still emit
    // two intermediate vectors made of old and new components.
    quiet = true;
    x->setValue(double(v.x()));
    y->setValue(double(v.y()));
    z->setValue(double(v.z()));
    quiet = false;
}

iris::Vec3 DragVector3Widget::values() const
{
    return iris::Vec3(float(x->value()), float(y->value()), float(z->value()));
}
