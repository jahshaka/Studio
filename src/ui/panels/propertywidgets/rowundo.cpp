/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/panels/propertywidgets/rowundo.h"

#include <memory>

#include <QColor>
#include <QVector3D>

#include "ui/controls/checkboxwidget.h"
#include "ui/controls/colorpickerwidget.h"
#include "ui/controls/comboboxwidget.h"
#include "ui/controls/dragvaluewidgets.h"
#include "ui/controls/hfloatsliderwidget.h"

namespace {

/// One row's gesture: the value the document held when it started, and whether
/// it is open. Held by shared_ptr so the three lambdas of one binding share it
/// and it dies with the last of them (i.e. with the row).
struct Session
{
    bool open = false;
    QVariant before;
};

using SessionPtr = std::shared_ptr<Session>;

bool live(const rowundo::Binding &b)
{
    return !b.guard || b.guard();
}

void begin(const SessionPtr &s, const rowundo::Binding &b)
{
    if (s->open || !b.read || !live(b)) return;
    s->open = true;
    s->before = b.read();
}

void end(const SessionPtr &s, const rowundo::Binding &b)
{
    if (!s->open) return;
    s->open = false;
    if (!b.read || !b.commit) return;
    const QVariant after = b.read();
    // A gesture that ended where it started is not an edit.
    if (after == s->before) return;
    b.commit(s->before, after);
}

/// A live tick: write through, and — if nothing bracketed it — make the tick
/// its own one-step edit. (A panel populating its rows must not reach here;
/// that is what each panel's loading guard is for. If it does, before == after
/// and the commit is dropped.)
void tick(const SessionPtr &s, const rowundo::Binding &b, const QVariant &value)
{
    if (!live(b)) return;
    const bool atomic = !s->open;
    if (atomic) begin(s, b);
    if (b.write) b.write(value);
    if (atomic) end(s, b);
}

}   // namespace

namespace rowundo {

void bind(HFloatSliderWidget *slider, const Binding &binding)
{
    if (!slider) return;
    auto session = std::make_shared<Session>();
    QObject::connect(slider, &HFloatSliderWidget::valueChangeStart, slider,
                     [session, binding](float) { begin(session, binding); });
    QObject::connect(slider, &HFloatSliderWidget::valueChanged, slider,
                     [session, binding](float v) { tick(session, binding, QVariant(v)); });
    QObject::connect(slider, &HFloatSliderWidget::valueChangeEnd, slider,
                     [session, binding](float) { end(session, binding); });
}

void bind(DragFloatWidget *field, const Binding &binding)
{
    if (!field) return;
    auto session = std::make_shared<Session>();
    QObject::connect(field, &DragFloatWidget::valueChanged, field,
                     [session, binding](double v) {
                         // The first tick opens the session; editingDone closes
                         // it. There is no start signal, so "atomic" is decided
                         // by the close, not the open.
                         if (!live(binding)) return;
                         begin(session, binding);
                         if (binding.write) binding.write(QVariant(v));
                     });
    QObject::connect(field, &DragFloatWidget::editingDone, field,
                     [session, binding]() { end(session, binding); });
}

void bind(DragVector3Widget *field, const Binding &binding)
{
    if (!field) return;
    auto session = std::make_shared<Session>();
    QObject::connect(field, &DragVector3Widget::valueChanged, field,
                     [session, binding](const iris::Vec3 &v) {
                         if (!live(binding)) return;
                         begin(session, binding);
                         if (binding.write)
                             binding.write(QVariant::fromValue(QVector3D(v.x(), v.y(), v.z())));
                     });
    QObject::connect(field, &DragVector3Widget::editingDone, field,
                     [session, binding]() { end(session, binding); });
}

void bind(ColorPickerWidget *picker, const Binding &binding)
{
    if (!picker) return;
    auto session = std::make_shared<Session>();
    QObject::connect(picker, &ColorPickerWidget::pickingStarted, picker,
                     [session, binding]() { begin(session, binding); });
    QObject::connect(picker, &ColorPickerWidget::onColorChanged, picker,
                     [session, binding](QColor c) { tick(session, binding, QVariant(c)); });
    QObject::connect(picker, &ColorPickerWidget::pickingEnded, picker,
                     [session, binding]() { end(session, binding); });
    // setColor() — a direct set, from a preset or another panel — is one edit.
    QObject::connect(picker, &ColorPickerWidget::onSetColor, picker,
                     [session, binding](QColor c) { tick(session, binding, QVariant(c)); });
}

void bind(CheckBoxWidget *box, const Binding &binding)
{
    if (!box) return;
    auto session = std::make_shared<Session>();
    QObject::connect(box, &CheckBoxWidget::valueChanged, box,
                     [session, binding](bool v) { tick(session, binding, QVariant(v)); });
}

void bind(ComboBoxWidget *combo, const Binding &binding)
{
    if (!combo) return;
    auto session = std::make_shared<Session>();
    QObject::connect(combo, QOverload<int>::of(&ComboBoxWidget::currentIndexChanged), combo,
                     [session, binding](int row) {
                         if (row < 0) return;
                         tick(session, binding, QVariant(row));
                     });
}

}   // namespace rowundo
