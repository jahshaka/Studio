/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/panels/propertywidgets/worldvrpropertywidget.h"

#include <QSignalBlocker>

#include "irisgl/document/scenegraph/scene.h"

#include "commands/scenepropertycommand.h"
#include "services/vrworld.h"
#include "ui/controls/checkboxwidget.h"
#include "ui/controls/comboboxwidget.h"
#include "ui/controls/dragvaluewidgets.h"
#include "ui/panels/propertyrows.h"
#include "ui/panels/propertywidgets/panelundo.h"
#include "ui/panels/propertywidgets/rowundo.h"

WorldVrPropertyWidget::WorldVrPropertyWidget()
{
}

void WorldVrPropertyWidget::setScene(QSharedPointer<iris::Scene> scene)
{
    if (!!scene) {
        this->scene = scene;
        build();
        refreshRows();
    } else {
        this->scene.clear();
    }
}

void WorldVrPropertyWidget::build()
{
    if (!rows.isEmpty() || !scene) return;

    for (const vrworld::Row &r : vrworld::rows()) {
        RowWidgets row;
        row.id = r.id;
        // THE FILTER KEY IS THE UNDO KEY (PROPERTY_FILTER_SPEC §3.2): one name
        // for one setting, wherever it is reached from.
        const QString key = vrworld::propsKey(r.id);

        if (r.kind == vrworld::RowKind::Flag) {
            // A TRUE/FALSE IS A SWITCH, not a two-item combo: it is the control
            // this theme draws for a flag everywhere else in the World panel
            // (the Photon row, the fog rows), and a flag drawn as a list reads
            // as a choice between two things nobody named.
            row.flag = this->addCheckBox(r.label);
            row.flag->setValue(r.get ? r.get(scene) != 0.0 : false);
            row.flag->setToolTip(r.doc);
            PropertyRows::identify(row.flag, key,
                                   { QStringLiteral("vr"), QStringLiteral("headset"), r.id });
            const QString id = r.id;
            const QString label = r.label;
            connect(row.flag, &CheckBoxWidget::valueChanged, this,
                    [this, id, label](bool on) {
                        if (loading || !scene) return;
                        const QVariant before = sceneprops::get(scene, vrworld::propsKey(id));
                        const double value = on ? 1.0 : 0.0;
                        sceneprops::set(scene, vrworld::propsKey(id), value);
                        panelundo::pushSceneEdit(services, scene, vrworld::propsKey(id),
                                                 tr("Set %1").arg(label), before, QVariant(value),
                                                 [this]() { refreshRows(); });
                        refreshRows();
                    });
            rows.append(row);
            continue;
        }

        if (r.kind == vrworld::RowKind::Enum) {
            row.combo = this->addComboBox(r.label);
            for (const vrworld::EnumOption &o : r.options)
                row.combo->addItem(o.label, o.value);
            row.combo->setToolTip(r.doc);
            PropertyRows::identify(row.combo, key,
                                   { QStringLiteral("vr"), QStringLiteral("headset"), r.id });
            const QString id = r.id;
            const QString label = r.label;
            ComboBoxWidget *combo = row.combo;
            connect(combo, QOverload<int>::of(&ComboBoxWidget::currentIndexChanged), this,
                    [this, id, label, combo](int index) {
                        if (loading || !scene || index < 0) return;
                        const QVariant before = sceneprops::get(scene, vrworld::propsKey(id));
                        const double value = combo->getItemData(index).toDouble();
                        sceneprops::set(scene, vrworld::propsKey(id), value);
                        panelundo::pushSceneEdit(services, scene, vrworld::propsKey(id),
                                                 tr("Set %1").arg(label), before, QVariant(value),
                                                 [this]() { refreshRows(); });
                        // The turn's step and its rate are live or dead by the
                        // turn mode, so a mode change re-reads every row.
                        refreshRows();
                    });
        } else {
            row.field = this->addDragFloat(r.label, r.get ? r.get(scene) : 0.0, r.minValue,
                                           r.maxValue, r.perPixelStep, r.decimals);
            if (!r.unit.isEmpty())
                row.field->setSuffix(QStringLiteral(" ") + r.unit);
            row.field->setToolTip(r.doc);
            PropertyRows::identify(row.field, key,
                                   { QStringLiteral("vr"), QStringLiteral("headset"), r.id });
            // ONE UNDO STEP PER SCRUB: rowundo brackets the gesture (first tick
            // to editingDone) and the write goes through the sceneprops table —
            // the identical function `world.vr` calls.
            panelundo::SceneRows sceneRows([this]() { return scene; },
                                           [this]() { return services; },
                                           [this]() { refreshRows(); },
                                           [this]() { return !loading; });
            rowundo::bind(row.field, sceneRows(key, r.label));
        }
        rows.append(row);
    }
}

void WorldVrPropertyWidget::refreshRows()
{
    if (!scene) return;
    loading = true;
    for (const RowWidgets &w : rows) {
        const vrworld::Row *r = vrworld::row(w.id);
        if (!r) continue;
        const double value = r->get ? r->get(scene) : 0.0;
        if (w.flag) {
            const QSignalBlocker quiet(w.flag.get());
            w.flag->setValue(value != 0.0);
            w.flag->setEnabled(r->enabled ? r->enabled(scene) : true);
        } else if (w.combo) {
            const QSignalBlocker quiet(w.combo->getWidget());
            const int index = w.combo->findData(int(qRound(value)));
            w.combo->setCurrentIndex(index >= 0 ? index : 0);
        } else if (w.field) {
            w.field->setValue(value);            // quiet: does not emit
            // A setting that means nothing in the current mode is GREYED, never
            // hidden: "why is it grey" beats "where did it go" (the sibling
            // sections' rule). The snap step is dead while the turn is smooth,
            // and the smooth rate while it snaps.
            w.field->setEnabled(r->enabled ? r->enabled(scene) : true);
        }
    }
    loading = false;
}
