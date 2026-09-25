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
#include "ui/panels/propertyrows.h"
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

bool WorldModesPropertyWidget::sceneTracesRays() const
{
    return sceneView && sceneView->isInitialized() && sceneView->sceneTracesRays();
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
    PropertyRows::identify(modeSelector, QStringLiteral("world.mode"),
                           { QStringLiteral("quality"), QStringLiteral("tier"),
                             QStringLiteral("scalability") });
    modeSelector->setToolTip(
        QStringLiteral("One scalability tier for the whole scene. Picking a mode sets every row "
                       "below to that tier's value, except rows you have changed yourself — those "
                       "stay pinned until you reset them."));
    connect(modeSelector, QOverload<int>::of(&ComboBoxWidget::currentIndexChanged),
            this, &WorldModesPropertyWidget::onModeChanged);

    // One control per registry row, in registry order (which groups them).
    const bool rays = sceneTracesRays();
    for (int i = 0; i < rows.size(); ++i) {
        const worldmodes::Row &r = rows[i];

        // A ROW NO TIER RESOLVES IS NOT A SCALABILITY ROW (EXPOSURE-1). This
        // blade IS the tier table — every control in it has four columns behind
        // it and a mode switch writes them — so a row with no columns at all
        // would be a control the blade cannot explain. It lives in the section
        // it belongs to (exposure under Post Process) and nowhere else.
        if (r.tierSpace == worldmodes::TierSpace::None) {
            rowControls.append(nullptr);
            continue;
        }

        if (!r.available) {
            // Declared, not yet implemented (POST_CHAIN_SPEC §9.2). Shown so the
            // tier table is honest about what a mode WILL mean, disabled so it
            // cannot be set to something the renderer would ignore.
            auto *lbl = this->addLabel(r.label, QStringLiteral("not available yet"));
            if (lbl) lbl->setToolTip(worldmodes::rowCost(r, rays));
            identifyRow(lbl, r);
            rowControls.append(nullptr);
            continue;
        }

        if (r.type == worldmodes::RowType::Bool) {
            auto *box = this->addCheckBox(r.label, false);
            box->setToolTip(worldmodes::rowCost(r, rays));
            identifyRow(box, r);
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
            for (const worldmodes::EnumOption &o : r.options)
                combo->addItem(worldmodes::optionLabel(r, o, scene, rays), o.value);
        } else {
            for (int v = r.minValue; v <= r.maxValue; ++v)
                combo->addItem(QString::number(v), v);
        }
        combo->setToolTip(worldmodes::rowCost(r, rays));
        identifyRow(combo, r);
        connect(combo, QOverload<int>::of(&ComboBoxWidget::currentIndexChanged),
                this, &WorldModesPropertyWidget::onRowChanged);
        rowControls.append(combo);
    }

    // Only worth offering when there is something to reset — hidden, not
    // absent, so an edit never changes which rows exist.
    resetRow = this->addCheckBox(QStringLiteral("Reset All Pinned Rows"), false);
    resetRow->setValue(false);
    resetRow->setToolTip(QStringLiteral("Drops every pinned row (*) and re-applies the mode."));
    PropertyRows::identify(resetRow, QStringLiteral("world.resetOverrides"),
                           { QStringLiteral("pinned"), QStringLiteral("reset") });
    connect(resetRow, &CheckBoxWidget::valueChanged, this, [this](bool on) {
        if (loading || !on || !scene) return;
        runUndoable(tr("Reset Pinned Quality Rows"),
                    [this]() { worldmodes::clearOverrides(scene); });
    });
}

/// THE TIER TABLE'S ROWS ARE THE SAME ROWS THE POST-PROCESS SECTION SHOWS, so
/// they carry the same key (PROPERTY_FILTER_SPEC §3.2): "ssr" finds
/// Screen-Space Reflections in both places, which is exactly what the owner
/// asked the box for.
void WorldModesPropertyWidget::identifyRow(QWidget *row, const worldmodes::Row &r)
{
    if (!row) return;
    QStringList keywords{ r.group };
    // CURATED SYNONYMS (PROPERTY_FILTER_SPEC D5), beside the row, deleted with
    // it: the short names people actually type for rows whose label spells the
    // thing out in full.
    if (r.id == QLatin1String("ssr"))
        keywords << QStringLiteral("screen space reflections") << QStringLiteral("reflections");
    else if (r.id == QLatin1String("ssao"))
        keywords << QStringLiteral("ao") << QStringLiteral("ambient occlusion");
    else if (r.id == QLatin1String("hdr"))
        keywords << QStringLiteral("tonemap") << QStringLiteral("tone mapping");
    // "exposure" belongs to the EXPOSURE rows, not to HDR: it used to be a
    // synonym here because HDR owned the exposure parameters, and since
    // EXPOSURE-1 it would send a search for "exposure" to the wrong row.
    else if (r.id == QLatin1String("exposureMode"))
        keywords << QStringLiteral("exposure") << QStringLiteral("ev")
                 << QStringLiteral("stops") << QStringLiteral("auto exposure");
    else if (r.id == QLatin1String("exposureMetering"))
        keywords << QStringLiteral("metering") << QStringLiteral("meter")
                 << QStringLiteral("spot") << QStringLiteral("centre weighted")
                 << QStringLiteral("center weighted");
    else if (r.id == QLatin1String("photon"))
        keywords << QStringLiteral("gi") << QStringLiteral("global illumination");
    PropertyRows::identify(row, QStringLiteral("world.override:") + r.id, keywords);
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
    // The texts follow the machine too: the scene's Ray Tracing row can move
    // after the blade was built (worldmodes::rowCost).
    const bool rays = sceneTracesRays();
    for (int i = 0; i < rows.size() && i < rowControls.size(); ++i) {
        const worldmodes::Row &r = rows[i];
        QWidget *control = rowControls[i];
        if (!control) continue;
        control->setToolTip(worldmodes::rowCost(r, rays));
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
            if (r.type == worldmodes::RowType::Enum)
                for (int o = 0; o < r.options.size() && o < combo->getWidget()->count(); ++o)
                    combo->getWidget()->setItemText(o, worldmodes::optionLabel(r, r.options[o], scene, rays));
            const int index = combo->findData(value);
            combo->setCurrentIndex(index >= 0 ? index : 0);
        }
        // A row that would be a LIE in this mode is not shown at all
        // (Row::visible; the Metering pattern under Manual exposure, which
        // takes no measurement). The Post Process section applies the same
        // predicate to the same row, so the two views agree.
        if (r.visible) control->setVisible(r.visible(scene));
    }

    if (resetRow) {
        PropertyRows::setPanelVisible(resetRow, !scene->worldOverrides.isEmpty());
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
