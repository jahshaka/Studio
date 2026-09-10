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
#include "ui/panels/propertywidgets/panelundo.h"
#include "viewport/ieditorviewport.h"

#include <QComboBox>
#include <QPointer>
#include <QSignalBlocker>

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
        build();
        refreshRows();
    } else {
        this->scene.clear();
    }
}

void WorldModesPropertyWidget::setSceneView(IEditorViewport *sceneView)
{
    this->sceneView = sceneView;
}

void WorldModesPropertyWidget::build()
{
    if (modeSelector) return;   // built once; the registry does not change
    if (!scene) return;

    const auto &rows = worldmodes::rows();
    rowControls.clear();

    // The tier. "Custom" is only ever shown, never chosen: it is what a scene
    // is before anyone picks a mode, and what the reader gives a document
    // written before World Modes existed. The entry is added and removed by
    // refreshRows(), which is also where it is selected.
    modeSelector = this->addComboBox("World Mode");
    for (const QString &n : worldmodes::modeNames()) modeSelector->addItem(titled(n));
    modeSelector->setToolTip(
        QStringLiteral("One scalability tier for the whole scene. Picking a mode sets every row "
                       "below to that tier's value, except rows you have changed yourself — those "
                       "stay pinned until you reset them."));
    connect(modeSelector, QOverload<int>::of(&ComboBoxWidget::currentIndexChanged),
            this, &WorldModesPropertyWidget::onModeChanged);

    // One control per registry row, in registry order (which groups them).
    for (int i = 0; i < rows.size(); ++i) {
        const worldmodes::Row &r = rows[i];

        if (!r.available) {
            // Declared, not yet implemented (POST_CHAIN_SPEC §9.2). Shown so the
            // tier table is honest about what a mode WILL mean, disabled so it
            // cannot be set to something the renderer would ignore.
            auto *lbl = this->addLabel(r.label, QStringLiteral("not available yet"));
            if (lbl) lbl->setToolTip(r.cost);
            rowControls.append(nullptr);
            continue;
        }

        if (r.type == worldmodes::RowType::Bool) {
            auto *box = this->addCheckBox(r.label, false);
            box->setToolTip(r.cost);
            const int index = i;
            connect(box, &CheckBoxWidget::valueChanged, this, [this, index](bool on) {
                const auto &table = worldmodes::rows();
                if (loading || !scene || index >= table.size()) return;
                const QString id = table[index].id;
                runUndoable(tr("Set %1").arg(table[index].label),
                            [this, id, on]() { worldmodes::setRowValue(scene, id, on ? 1 : 0); });
            });
            rowControls.append(box);
            continue;
        }

        // Enum and Int rows both present as a combo: every Int row we have is a
        // small budget (0..8 planes), and a combo makes the tier values legible.
        auto *combo = this->addComboBox(r.label);
        if (r.type == worldmodes::RowType::Enum) {
            for (const worldmodes::EnumOption &o : r.options) combo->addItem(o.label, o.value);
        } else {
            for (int v = r.minValue; v <= r.maxValue; ++v)
                combo->addItem(QString::number(v), v);
        }
        combo->setToolTip(r.cost);
        connect(combo, QOverload<int>::of(&ComboBoxWidget::currentIndexChanged),
                this, &WorldModesPropertyWidget::onRowChanged);
        rowControls.append(combo);
    }

    // Only worth offering when there is something to reset — hidden, not
    // absent, so an edit never changes which rows exist.
    resetRow = this->addCheckBox(QStringLiteral("Reset All Pinned Rows"), false);
    resetRow->setValue(false);
    resetRow->setToolTip(QStringLiteral("Drops every pinned row (*) and re-applies the mode."));
    connect(resetRow, &CheckBoxWidget::valueChanged, this, [this](bool on) {
        if (loading || !on || !scene) return;
        runUndoable(tr("Reset Pinned Quality Rows"),
                    [this]() { worldmodes::clearOverrides(scene); });
    });
}

void WorldModesPropertyWidget::refreshRows()
{
    if (!scene || !modeSelector) return;
    loading = true;

    const worldmodes::Mode currentMode = worldmodes::mode(scene);
    const QStringList names = worldmodes::modeNames();
    if (QComboBox *box = modeSelector->getWidget()) {
        const QSignalBlocker quiet(box);
        if (currentMode == worldmodes::Mode::Custom) {
            if (box->count() == names.size()) box->addItem(QStringLiteral("Custom"));
            box->setCurrentIndex(names.size());
        } else {
            if (box->count() > names.size()) box->removeItem(names.size());
            box->setCurrentIndex(int(currentMode));
        }
    }

    const auto &rows = worldmodes::rows();
    for (int i = 0; i < rows.size() && i < rowControls.size(); ++i) {
        const worldmodes::Row &r = rows[i];
        QWidget *control = rowControls[i];
        if (!control) continue;
        const int value = worldmodes::resolved(scene, r);
        const bool pinned = worldmodes::source(scene, r) == QLatin1String("override");
        // A pinned row says so in its label: without the marker "why did Epic
        // not change my MSAA" is unanswerable from the panel.
        const QString label = pinned ? r.label + QStringLiteral(" *") : r.label;

        if (auto *box = qobject_cast<CheckBoxWidget *>(control)) {
            box->setLabel(label);
            box->setValue(value != 0);           // does not emit
        } else if (auto *combo = qobject_cast<ComboBoxWidget *>(control)) {
            combo->setLabel(label);
            const QSignalBlocker quiet(combo->getWidget());
            const int index = combo->findData(value);
            combo->setCurrentIndex(index >= 0 ? index : 0);
        }
    }

    if (resetRow) {
        resetRow->setVisible(!scene->worldOverrides.isEmpty());
        resetRow->setValue(false);
    }
    loading = false;
}

void WorldModesPropertyWidget::applied()
{
    // Shadow-atlas and MSAA changes are applied by SceneMirror at the next sync;
    // step two frames so the readbacks the sibling sections show are the truth.
    if (sceneView && sceneView->isInitialized()) sceneView->renderFrames(2);
    refreshRows();
    // The sibling World sections (Anti-Aliasing, Shadows, Global Illumination,
    // Sky) display the very backing fields a tier writes through to, and they
    // only read them when they are built: without this they would keep showing
    // the pre-mode-switch values until the user reselected the node.
    emit worldSettingsChanged();
}

void WorldModesPropertyWidget::runUndoable(const QString &text,
                                           const std::function<void()> &edit)
{
    // The shared implementation (debt L6: the aa/shadow/postfx/sky sections
    // write registry rows too, and each had grown — or lacked — its own copy of
    // this). An undo has to repaint the panel it came from, exactly like the
    // edit did: the rows ARE the state.
    QPointer<WorldModesPropertyWidget> self(this);
    panelundo::runWorldModeEdit(services, scene, text, edit,
                                [self]() { if (self) self->applied(); });
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
