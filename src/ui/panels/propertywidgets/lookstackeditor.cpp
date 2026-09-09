/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/panels/propertywidgets/lookstackeditor.h"

#include <QHBoxLayout>
#include <QJsonObject>
#include <QPushButton>
#include <QWidget>

#include "services/looks.h"
#include "ui/controls/accordionbladewidget.h"
#include "ui/controls/checkboxwidget.h"
#include "ui/controls/comboboxwidget.h"
#include "ui/controls/dragvaluewidgets.h"
#include "ui/controls/labelwidget.h"

namespace lookstack
{

void build(AccordianBladeWidget *blade, const QJsonArray &raw,
           const std::function<void(const QJsonArray &, bool)> &write)
{
    if (!blade || !write) return;
    const QJsonArray stack = iris::normalizeLookStack(raw);

    for (int i = 0; i < stack.size(); ++i) {
        const QJsonObject entry = stack.at(i).toObject();
        const QString id = entry.value(QStringLiteral("id")).toString();
        const iris::LookDef *def = iris::lookDef(id);
        const looks::LookUi *ui = looks::lookUi(id);
        if (!def || !ui) continue;

        const bool enabled = entry.value(QStringLiteral("enabled")).toBool(true);

        // THE HEADER ROW. The number is not decoration: the stack order IS the
        // frame order, and a list whose order matters has to show it.
        auto *box = blade->addCheckBox(QStringLiteral("%1. %2").arg(i + 1).arg(ui->label),
                                       enabled);
        // addCheckBox ignores its value argument (the same upstream quirk the
        // World Mode section documents) — set it explicitly.
        box->setValue(enabled);
        box->setToolTip(ui->doc);
        QObject::connect(box, &CheckBoxWidget::valueChanged, blade,
                         [stack, i, write](bool on) {
                             QJsonArray next = stack;
                             QJsonObject e = next.at(i).toObject();
                             e.insert(QStringLiteral("enabled"), on);
                             next.replace(i, e);
                             write(next, true);
                         });

        // The three gestures an ordered list needs, on one compact row.
        auto *row = new QWidget();
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        auto *up = new QPushButton(QStringLiteral("▲"));
        auto *down = new QPushButton(QStringLiteral("▼"));
        auto *remove = new QPushButton(QObject::tr("Remove"));
        up->setToolTip(QObject::tr("Run this look earlier — the stack order is the order the "
                                   "filters are applied in."));
        down->setToolTip(QObject::tr("Run this look later."));
        remove->setToolTip(QObject::tr("Take this look out of the stack. To keep its settings "
                                       "and stop it drawing, switch it off instead."));
        up->setEnabled(i > 0);
        down->setEnabled(i + 1 < stack.size());
        rowLayout->addWidget(up);
        rowLayout->addWidget(down);
        rowLayout->addWidget(remove);
        blade->addWidgetToContent(row);

        const auto move = [stack, i, write](int delta) {
            QJsonArray next = stack;
            const int to = qBound(0, i + delta, next.size() - 1);
            if (to == i) return;
            const QJsonValue e = next.at(i);
            next.removeAt(i);
            next.insert(to, e);
            write(next, true);
        };
        QObject::connect(up, &QPushButton::clicked, blade, [move]() { move(-1); });
        QObject::connect(down, &QPushButton::clicked, blade, [move]() { move(+1); });
        QObject::connect(remove, &QPushButton::clicked, blade, [stack, i, write]() {
            QJsonArray next = stack;
            next.removeAt(i);
            write(next, true);
        });

        // The look's own parameters, scrubbable, exactly like the post-fx
        // parameters above them.
        const QJsonObject params = entry.value(QStringLiteral("params")).toObject();
        for (int j = 0; j < def->paramCount && j < ui->params.size(); ++j) {
            const iris::LookParamDef &d = def->params[j];
            const looks::ParamUi &pui = ui->params[j];
            const QString paramId = QString::fromLatin1(d.id);
            const double value = params.value(paramId).toDouble(double(d.defaultValue));
            auto *field = blade->addDragFloat(pui.label, value, double(d.minValue),
                                              double(d.maxValue), pui.perPixelStep,
                                              pui.decimals);
            field->setToolTip(pui.doc);
            // Greyed while the look is off, for the same reason a parameter of a
            // disabled effect is: hiding it makes "where did it go" the
            // question, which is the worse one.
            field->setEnabled(enabled);
            QObject::connect(field, &DragFloatWidget::valueChanged, blade,
                             [stack, i, paramId, write](double v) {
                                 QJsonArray next = stack;
                                 QJsonObject e = next.at(i).toObject();
                                 QJsonObject p = e.value(QStringLiteral("params")).toObject();
                                 p.insert(paramId, v);
                                 e.insert(QStringLiteral("params"), p);
                                 next.replace(i, e);
                                 // NO rebuild: this fires on every scrubbed
                                 // pixel and would destroy the widget under the
                                 // cursor. normalizeLookStack clamps it anyway.
                                 write(next, false);
                             });
        }
    }

    // ADDING. The combo offers what is NOT already in the stack, because a look
    // may appear only once — its parameters live on one shared renderer
    // material, so a second copy would be handed the first one's numbers. The
    // rule is enforced by the document; this is the UI's honest expression of
    // it, offering nothing that would be refused.
    int count = 0;
    const iris::LookDef *cat = iris::lookCatalogue(count);
    QStringList addable;
    for (int i = 0; i < count; ++i) {
        const QString id = QString::fromLatin1(cat[i].id);
        bool present = false;
        for (const QJsonValue &v : stack)
            if (v.toObject().value(QStringLiteral("id")).toString() == id) { present = true; break; }
        if (!present) addable << id;
    }
    if (addable.isEmpty()) return;

    auto *combo = blade->addComboBox(QObject::tr("Add Look"));
    combo->addItem(QObject::tr("Choose..."), QString());
    for (const QString &id : addable) {
        const looks::LookUi *ui = looks::lookUi(id);
        combo->addItem(ui ? ui->label : id, id);
    }
    combo->setCurrentIndex(0);
    QObject::connect(combo, QOverload<int>::of(&ComboBoxWidget::currentIndexChanged), blade,
                     [stack, combo, write](int row) {
                         if (row <= 0) return;
                         const QString id = combo->getItemData(row).toString();
                         const iris::LookDef *def = iris::lookDef(id);
                         if (!def) return;
                         QJsonArray next = stack;
                         next.append(iris::makeLookEntry(*def));
                         write(next, true);
                     });
}

}   // namespace lookstack
