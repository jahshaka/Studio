/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/panels/propertywidgets/worldpostfxpropertywidget.h"

#include "irisgl/document/scenegraph/scene.h"

#include "services/looks.h"
#include "ui/panels/propertywidgets/lookstackeditor.h"
#include "ui/panels/propertywidgets/panelundo.h"
#include "ui/panels/propertywidgets/rowundo.h"
#include "services/worldmodes.h"
#include "ui/controls/checkboxwidget.h"
#include "ui/controls/comboboxwidget.h"
#include "ui/controls/dragvaluewidgets.h"
#include "ui/controls/labelwidget.h"
#include "viewport/ieditorviewport.h"

#include <QJsonArray>
#include <QSignalBlocker>

WorldPostFxPropertyWidget::WorldPostFxPropertyWidget()
{
}

void WorldPostFxPropertyWidget::setScene(QSharedPointer<iris::Scene> scene)
{
    if (!!scene) {
        this->scene = scene;
        build();
        refreshRows();
        rebuildLooks();
    } else {
        this->scene.clear();
    }
}

void WorldPostFxPropertyWidget::setSceneView(IEditorViewport *sceneView)
{
    this->sceneView = sceneView;
}

// THE ROWS, ONCE. Which rows exist is decided by the registry and never by an
// edit — an effect being switched off greys its parameters, it does not remove
// them — so this blade is built on the first scene and refreshed from then on
// (debt L6: the rebuild-per-edit was what destroyed the control that raised the
// edit, and what made a page switch repaint half-built rows).
void WorldPostFxPropertyWidget::build()
{
    if (!effectRows.isEmpty() || !scene) return;

    for (const QString &rowId : worldmodes::postFxRowIds()) {
        const worldmodes::Row *r = worldmodes::row(rowId);
        if (!r) continue;                       // a table entry was renamed; say nothing

        EffectRow row;
        row.id = r->id;

        if (!r->available) {
            // Declared, not served (POST_CHAIN_SPEC §9.2 — SSR today). Shown so
            // the section is a complete account of the chain, disabled so it
            // cannot be set to something the renderer would ignore.
            row.unavailable = this->addLabel(r->label, QStringLiteral("not available yet"));
            if (row.unavailable) row.unavailable->setToolTip(r->cost);
        } else if (r->type == worldmodes::RowType::Bool) {
            row.box = this->addCheckBox(r->label, false);
            row.box->setToolTip(r->cost);
            const QString id = r->id;
            const QString label = r->label;
            connect(row.box, &CheckBoxWidget::valueChanged, this, [this, id, label](bool on) {
                if (loading || !scene) return;
                panelundo::runWorldModeEdit(services, scene, tr("Set %1").arg(label),
                    [this, id, on]() { worldmodes::setRowValue(scene, id, on ? 1 : 0); },
                    [this]() { applied(); emit worldSettingsChanged(); });
            });
        } else {
            row.combo = this->addComboBox(r->label);
            for (const worldmodes::EnumOption &o : r->options)
                row.combo->addItem(o.label, o.value);
            row.combo->setToolTip(r->cost);
            const QString id = r->id;
            const QString label = r->label;
            ComboBoxWidget *combo = row.combo;
            connect(combo, QOverload<int>::of(&ComboBoxWidget::currentIndexChanged), this,
                    [this, id, label, combo](int index) {
                        if (loading || !scene || index < 0) return;
                        const int value = combo->getItemData(index).toInt();
                        panelundo::runWorldModeEdit(services, scene, tr("Set %1").arg(label),
                            [this, id, value]() { worldmodes::setRowValue(scene, id, value); },
                            [this]() { applied(); emit worldSettingsChanged(); });
                    });
        }
        effectRows.append(row);

        // The effect's own parameters, immediately under it. Compact scrubbable
        // rows (dragvaluewidgets.h): the number IS the control, dragged left or
        // right, exactly like a transform field.
        for (const worldmodes::ParamRow &p : worldmodes::postFxParams()) {
            if (p.ownerRowId != r->id) continue;
            ParamField pf;
            pf.id = p.id;
            pf.ownerRowId = p.ownerRowId;
            pf.field = this->addDragFloat(p.label, p.get ? p.get(scene) : 0.0, p.minValue,
                                          p.maxValue, p.perPixelStep, p.decimals);
            pf.field->setToolTip(p.doc);

            // ONE UNDO STEP PER SCRUB: rowundo brackets the gesture (first tick
            // to editingDone) and the write goes through the sceneprops table,
            // which clamps through the registry's own setter — the identical
            // function world.postFx calls.
            const QString key = QStringLiteral("postFx.") + p.id;
            panelundo::SceneRows rows([this]() { return scene; }, [this]() { return services; },
                                      [this]() { refreshRows(); }, [this]() { return !loading; });
            rowundo::Binding binding = rows(key, p.label);
            auto write = binding.write;
            binding.write = [this, write](const QVariant &v) {
                write(v);
                // The auto-exposure window must stay ordered — the one rule
                // that spans two rows, and the same one world.postFx enforces.
                if (scene && scene->exposureMax < scene->exposureMin)
                    std::swap(scene->exposureMin, scene->exposureMax);
                // Live in the viewport, no rebuild: this fires on every scrubbed
                // pixel and rebuilding under the cursor would destroy the widget
                // being dragged.
                applied();
            };
            rowundo::bind(pf.field, binding);
            paramFields.append(pf);
        }
    }

    // THE LOOKS SUB-SECTION (SPECS/POST_LOOKS_SPEC.md §4.1, phase 5). The stack
    // editor itself is shared with the camera panel's whole-stack override
    // (lookstackeditor.h): the same editor over two different arrays, written
    // once. It lives in a sub-blade because it is the one part of this section
    // whose ROWS change — adding, removing or reordering a look changes what
    // exists, so that part is rebuilt while the rest of the panel is not.
    looksHeading = this->addLabel(tr("Looks"), QString());
    if (looksHeading)
        looksHeading->setToolTip(tr("Image filters applied to the finished picture, in order — "
                                    "entry 1 runs first, and the order is part of the look. They "
                                    "are an ART choice, not a quality tier: a World Mode switch "
                                    "never adds or removes one."));
    looksSection = this->addSection(tr("Looks"));
    if (looksSection) looksSection->expand();
}

void WorldPostFxPropertyWidget::refreshRows()
{
    if (!scene) return;
    loading = true;

    for (const EffectRow &row : effectRows) {
        const worldmodes::Row *r = worldmodes::row(row.id);
        if (!r) continue;
        const bool pinned = worldmodes::source(scene, *r) == QLatin1String("override");
        // The World Mode section marks a pinned row with a star and this section
        // writes through the same setRowValue, so it marks them the same way —
        // two views of one value must not describe it differently.
        const QString label = pinned ? r->label + QStringLiteral(" *") : r->label;
        const int value = worldmodes::resolved(scene, *r);

        if (row.box) {
            row.box->setLabel(label);
            row.box->setValue(value != 0);
        } else if (row.combo) {
            row.combo->setLabel(label);
            const QSignalBlocker quiet(row.combo->getWidget());
            const int index = row.combo->findData(value);
            row.combo->setCurrentIndex(index >= 0 ? index : 0);
        }
    }

    for (const ParamField &pf : paramFields) {
        const worldmodes::ParamRow *p = worldmodes::postFxParam(pf.id);
        const worldmodes::Row *owner = worldmodes::row(pf.ownerRowId);
        if (!pf.field || !p) continue;
        pf.field->setValue(p->get ? p->get(scene) : 0.0);   // quiet: does not emit
        // A parameter is dead weight while its effect is off; showing it greyed
        // is more honest than hiding it, because "where did the exposure slider
        // go" is a worse question than "why is it grey".
        pf.field->setEnabled(owner ? worldmodes::resolved(scene, *owner) != 0 : true);
    }

    if (looksHeading) {
        const QJsonArray stack = iris::normalizeLookStack(scene->looks);
        looksHeading->setText(stack.isEmpty() ? tr("none")
                                              : tr("%1 in the stack").arg(stack.size()));
    }
    loading = false;
}

void WorldPostFxPropertyWidget::rebuildLooks()
{
    if (!scene || !looksSection) return;
    looksSection->clearPanel(looksSection->layout());

    lookstack::build(
        looksSection, iris::normalizeLookStack(scene->looks),
        [this](const QJsonArray &next, bool structural) {
            if (!scene) return;
            const QJsonArray before = looksScrubbing ? looksBefore : scene->looks;
            if (!structural && !looksScrubbing) {
                // A scrub has started: remember where it started, and let the
                // editor stream values until it says the gesture ended.
                looksScrubbing = true;
                looksBefore = scene->looks;
            }
            scene->looks = iris::normalizeLookStack(next);
            applied();
            if (structural) {
                // Adding, removing, reordering or switching a look changes which
                // rows exist: one undo step, and the sub-section is rebuilt.
                looksScrubbing = false;
                commitLooks(before, tr("Looks"));
                refreshRows();
                rebuildLooks();
            }
        },
        [this]() {
            // The end of a parameter scrub: one step for the whole drag.
            if (!looksScrubbing) return;
            looksScrubbing = false;
            commitLooks(looksBefore, tr("Look Settings"));
        });
}

void WorldPostFxPropertyWidget::commitLooks(const QJsonArray &before, const QString &text)
{
    if (!scene) return;
    panelundo::pushSceneEdit(services, scene, QStringLiteral("looks"), text, QVariant(before),
                             QVariant(scene->looks), [this]() {
                                 refreshRows();
                                 rebuildLooks();
                             });
}

void WorldPostFxPropertyWidget::applied()
{
    // Two frames, like the sibling sections: the chain's shape changes are
    // applied by SceneMirror at the next sync and a single frame can be the one
    // that rebuilds the workspace rather than the one that draws with it.
    if (sceneView && sceneView->isInitialized()) sceneView->renderFrames(2);
    if (loading) return;
    refreshRows();
    // NOTE the sibling notification (worldSettingsChanged) is NOT raised here:
    // it is raised by the rows that write a World-Mode-backed field, because
    // those are the only edits the World Mode section can be showing. A looks
    // gesture or a parameter scrub concerns nobody else.
}
