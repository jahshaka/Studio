/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/panels/propertywidgets/worldmodespropertywidget.h"

#include "irisgl/document/scenegraph/scene.h"

#include "commands/worldmodecommand.h"
#include "services/services.h"
#include "services/undoservice.h"
#include "services/worldmodes.h"
#include "ui/controls/checkboxwidget.h"
#include "ui/controls/comboboxwidget.h"
#include "ui/controls/labelwidget.h"
#include "viewport/ieditorviewport.h"

#include <QComboBox>
#include <QPointer>

namespace {
/// "Epic" reads better than "epic" in a combo.
QString titled(const QString &s)
{
    return s.isEmpty() ? s : s.left(1).toUpper() + s.mid(1);
}
}

WorldModesPropertyWidget::WorldModesPropertyWidget()
{
}

void WorldModesPropertyWidget::setScene(QSharedPointer<iris::Scene> scene)
{
    if (!!scene) {
        this->scene = scene;
        rebuild();
    } else {
        this->scene.clear();
    }
}

void WorldModesPropertyWidget::setSceneView(IEditorViewport *sceneView)
{
    this->sceneView = sceneView;
}

void WorldModesPropertyWidget::rebuild()
{
    clearPanel(this->layout());
    rowControls.clear();
    if (!scene) return;

    const auto &rows = worldmodes::rows();
    const worldmodes::Mode currentMode = worldmodes::mode(scene);

    // The tier. "Custom" is only ever shown, never chosen: it is what a scene
    // is before anyone picks a mode, and what the reader gives a document
    // written before World Modes existed.
    modeSelector = this->addComboBox("World Mode");
    const QStringList names = worldmodes::modeNames();
    for (const QString &n : names) modeSelector->addItem(titled(n));
    if (currentMode == worldmodes::Mode::Custom) {
        modeSelector->addItem(QStringLiteral("Custom"));
        modeSelector->setCurrentIndex(names.size());
    } else {
        modeSelector->setCurrentIndex(int(currentMode));
    }
    modeSelector->setToolTip(
        QStringLiteral("One scalability tier for the whole scene. Picking a mode sets every row "
                       "below to that tier's value, except rows you have changed yourself — those "
                       "stay pinned until you reset them."));
    connect(modeSelector, QOverload<int>::of(&ComboBoxWidget::currentIndexChanged),
            this, &WorldModesPropertyWidget::onModeChanged);

    // One control per registry row, in registry order (which groups them).
    for (int i = 0; i < rows.size(); ++i) {
        const worldmodes::Row &r = rows[i];
        const int value = worldmodes::resolved(scene, r);
        const bool pinned = worldmodes::source(scene, r) == QLatin1String("override");
        // A pinned row says so in its label: without the marker "why did Epic
        // not change my MSAA" is unanswerable from the panel.
        const QString label = pinned ? r.label + QStringLiteral(" *") : r.label;

        if (!r.available) {
            // Declared, not yet implemented (POST_CHAIN_SPEC §9.2). Shown so the
            // tier table is honest about what a mode WILL mean, disabled so it
            // cannot be set to something the renderer would ignore.
            auto *lbl = this->addLabel(label, QStringLiteral("not available yet"));
            if (lbl) lbl->setToolTip(r.cost);
            rowControls.append(nullptr);
            continue;
        }

        if (r.type == worldmodes::RowType::Bool) {
            auto *box = this->addCheckBox(label, value != 0);
            // AccordianBladeWidget::addCheckBox IGNORES its `value` argument
            // (accordionbladewidget.cpp: it never calls setValue) — every
            // checkbox it builds starts unchecked. Set it here rather than fix
            // the shared helper, which would silently flip the initial state of
            // every other panel that has quietly worked around the same thing.
            box->setValue(value != 0);
            box->setToolTip(r.cost);
            const int index = i;
            connect(box, &CheckBoxWidget::valueChanged, this, [this, index](bool on) {
                const auto &table = worldmodes::rows();
                if (!scene || index >= table.size()) return;
                const QString id = table[index].id;
                runUndoable(tr("Set %1").arg(table[index].label),
                            [this, id, on]() { worldmodes::setRowValue(scene, id, on ? 1 : 0); });
            });
            rowControls.append(box);
            continue;
        }

        // Enum and Int rows both present as a combo: every Int row we have is a
        // small budget (0..8 planes), and a combo makes the tier values legible.
        auto *combo = this->addComboBox(label);
        int current = 0;
        if (r.type == worldmodes::RowType::Enum) {
            for (int o = 0; o < r.options.size(); ++o) {
                combo->addItem(r.options[o].label, r.options[o].value);
                if (r.options[o].value == value) current = o;
            }
        } else {
            for (int v = r.minValue; v <= r.maxValue; ++v) {
                combo->addItem(QString::number(v), v);
                if (v == value) current = v - r.minValue;
            }
        }
        combo->setCurrentIndex(current);
        combo->setToolTip(r.cost);
        connect(combo, QOverload<int>::of(&ComboBoxWidget::currentIndexChanged),
                this, &WorldModesPropertyWidget::onRowChanged);
        rowControls.append(combo);
    }

    // Only worth offering when there is something to reset.
    if (!scene->worldOverrides.isEmpty()) {
        auto *reset = this->addCheckBox(QStringLiteral("Reset All Pinned Rows"), false);
        reset->setValue(false);
        reset->setToolTip(QStringLiteral("Drops every pinned row (*) and re-applies the mode."));
        connect(reset, &CheckBoxWidget::valueChanged, this, [this](bool on) {
            if (!on || !scene) return;
            runUndoable(tr("Reset Pinned Quality Rows"),
                        [this]() { worldmodes::clearOverrides(scene); });
        });
    }
}

void WorldModesPropertyWidget::applied()
{
    // Shadow-atlas and MSAA changes are applied by SceneMirror at the next sync;
    // step two frames so the readbacks the sibling sections show are the truth.
    if (sceneView && sceneView->isInitialized()) sceneView->renderFrames(2);
    rebuild();
    // The sibling World sections (Anti-Aliasing, Shadows, Global Illumination,
    // Sky) display the very backing fields a tier writes through to, and they
    // only read them when they are built: without this they would keep showing
    // the pre-mode-switch values until the user reselected the node.
    emit worldSettingsChanged();
}

void WorldModesPropertyWidget::runUndoable(const QString &text,
                                           const std::function<void()> &edit)
{
    if (!scene || !edit) return;
    const auto before = WorldModeCommand::capture(scene);
    edit();
    applied();
    if (services && services->undo) {
        auto *cmd = new WorldModeCommand(text, scene, before);
        // An undo has to repaint the panel it came from, exactly like the edit
        // did — the rows ARE the state, so restoring the state without
        // rebuilding them would leave the panel lying a second time.
        QPointer<WorldModesPropertyWidget> self(this);
        cmd->setRefresh([self]() { if (self) self->applied(); });
        services->undo->push(cmd);
    }
}

void WorldModesPropertyWidget::onModeChanged(int row)
{
    if (!scene) return;
    const QStringList names = worldmodes::modeNames();
    if (row < 0 || row >= names.size()) return;   // the "Custom" entry is not pickable
    bool ok = false;
    const worldmodes::Mode m = worldmodes::modeFromName(names[row], &ok);
    if (!ok) return;
    runUndoable(tr("World Mode: %1").arg(titled(names[row])),
                [this, m]() { worldmodes::setMode(scene, m); });
}

void WorldModesPropertyWidget::onRowChanged(int)
{
    if (!scene) return;
    auto *combo = qobject_cast<ComboBoxWidget *>(sender());
    if (!combo) return;
    const int index = rowControls.indexOf(combo);
    if (index < 0) return;
    const auto &table = worldmodes::rows();
    if (index >= table.size()) return;
    QComboBox *box = combo->getWidget();
    if (!box) return;
    bool ok = false;
    const int value = box->currentData().toInt(&ok);
    if (!ok) return;
    const QString id = table[index].id;
    runUndoable(tr("Set %1").arg(table[index].label),
                [this, id, value]() { worldmodes::setRowValue(scene, id, value); });
}
