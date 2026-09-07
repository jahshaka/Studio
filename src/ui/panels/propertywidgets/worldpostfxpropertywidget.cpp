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

#include "services/worldmodes.h"
#include "ui/controls/checkboxwidget.h"
#include "ui/controls/comboboxwidget.h"
#include "ui/controls/dragvaluewidgets.h"
#include "ui/controls/labelwidget.h"
#include "viewport/ieditorviewport.h"

WorldPostFxPropertyWidget::WorldPostFxPropertyWidget()
{
}

void WorldPostFxPropertyWidget::setScene(QSharedPointer<iris::Scene> scene)
{
    if (!!scene) {
        this->scene = scene;
        rebuild();
    } else {
        this->scene.clear();
    }
}

void WorldPostFxPropertyWidget::setSceneView(IEditorViewport *sceneView)
{
    this->sceneView = sceneView;
}

void WorldPostFxPropertyWidget::rebuild()
{
    clearPanel(this->layout());
    if (!scene) return;

    for (const QString &rowId : worldmodes::postFxRowIds()) {
        const worldmodes::Row *r = worldmodes::row(rowId);
        if (!r) continue;                       // a table entry was renamed; say nothing

        const bool pinned = worldmodes::source(scene, *r) == QLatin1String("override");
        // The World Mode section marks a pinned row with a star and this section
        // writes through the same setRowValue, so it marks them the same way —
        // two views of one value must not describe it differently.
        const QString label = pinned ? r->label + QStringLiteral(" *") : r->label;

        if (!r->available) {
            // Declared, not served (POST_CHAIN_SPEC §9.2 — SSR today). Shown so
            // the section is a complete account of the chain, disabled so it
            // cannot be set to something the renderer would ignore.
            auto *lbl = this->addLabel(label, QStringLiteral("not available yet"));
            if (lbl) lbl->setToolTip(r->cost);
        } else if (r->type == worldmodes::RowType::Bool) {
            auto *box = this->addCheckBox(label, worldmodes::resolved(scene, *r) != 0);
            // addCheckBox ignores its value argument (see the same note in
            // worldmodespropertywidget.cpp) — set it explicitly.
            box->setValue(worldmodes::resolved(scene, *r) != 0);
            box->setToolTip(r->cost);
            const QString id = r->id;
            connect(box, &CheckBoxWidget::valueChanged, this, [this, id](bool on) {
                if (!scene) return;
                worldmodes::setRowValue(scene, id, on ? 1 : 0);
                // Turning an effect on or off changes which parameter rows are
                // meaningful, so this one rebuilds.
                applied(true);
            });
        } else {
            auto *combo = this->addComboBox(label);
            const int value = worldmodes::resolved(scene, *r);
            int current = 0;
            for (int o = 0; o < r->options.size(); ++o) {
                combo->addItem(r->options[o].label, r->options[o].value);
                if (r->options[o].value == value) current = o;
            }
            combo->setCurrentIndex(current);
            combo->setToolTip(r->cost);
            const QString id = r->id;
            connect(combo, QOverload<int>::of(&ComboBoxWidget::currentIndexChanged),
                    this, [this, id, combo](int row) {
                        if (!scene || row < 0) return;
                        worldmodes::setRowValue(scene, id, combo->getItemData(row).toInt());
                        applied(true);
                    });
        }

        // The effect's own parameters, immediately under it. Compact scrubbable
        // rows (dragvaluewidgets.h): the number IS the control, dragged left or
        // right, exactly like a transform field.
        for (const worldmodes::ParamRow &p : worldmodes::postFxParams()) {
            if (p.ownerRowId != r->id) continue;
            auto *field = this->addDragFloat(p.label, p.get(scene), p.minValue, p.maxValue,
                                             p.perPixelStep, p.decimals);
            field->setToolTip(p.doc);
            // A parameter is dead weight while its effect is off; showing it
            // greyed is more honest than hiding it, because "where did the
            // exposure slider go" is a worse question than "why is it grey".
            field->setEnabled(worldmodes::resolved(scene, *r) != 0);
            const QString id = p.id;
            connect(field, &DragFloatWidget::valueChanged, this, [this, id](double v) {
                if (!scene) return;
                const worldmodes::ParamRow *param = worldmodes::postFxParam(id);
                if (!param) return;
                param->set(scene, qBound(param->minValue, v, param->maxValue));
                // The auto-exposure window must stay ordered — the one rule
                // that spans two rows, and the same one world.postFx enforces.
                if (scene->exposureMax < scene->exposureMin)
                    std::swap(scene->exposureMin, scene->exposureMax);
                // NO rebuild: this fires on every scrubbed pixel, and rebuilding
                // the panel under the cursor would destroy the widget being
                // dragged. The value is live in the viewport either way.
                applied(false);
            });
        }
    }
}

void WorldPostFxPropertyWidget::applied(bool rebuildPanel)
{
    // Two frames, like the sibling sections: the chain's shape changes are
    // applied by SceneMirror at the next sync and a single frame can be the one
    // that rebuilds the workspace rather than the one that draws with it.
    if (sceneView && sceneView->isInitialized()) sceneView->renderFrames(2);
    if (rebuildPanel) {
        rebuild();
        // The World Mode section shows the very rows this one just wrote (and
        // owns the pin markers), so it has to re-read them.
        emit worldSettingsChanged();
    }
}
