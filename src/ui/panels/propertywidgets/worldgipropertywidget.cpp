/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "irisgl/core/math/vec.h"
#include "ui/panels/propertywidgets/worldgipropertywidget.h"

#include "irisgl/document/scenegraph/scene.h"
#include "services/worldmodes.h"
#include "irisgl/document/scenegraph/lightnode.h"

#include "ui/controls/checkboxwidget.h"
#include "ui/controls/comboboxwidget.h"
#include "ui/controls/hfloatsliderwidget.h"
#include "ui/controls/labelwidget.h"
#include "ui/controls/dragvaluewidgets.h"
#include "services/gibounds.h"
#include "services/services.h"
#include "services/undoservice.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "ui_hfloatsliderwidget.h"

#include <QPointer>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVector3D>

namespace {
// Combo rows in display order -> document modes (rows are NOT the enum values).
const iris::GiMode kGiRows[] = {
    iris::GiMode::OFF, iris::GiMode::INSTANT_RADIOSITY,
    iris::GiMode::VCT, iris::GiMode::VCT_PCC_HYBRID,
};
const int kGiRowCount = int(sizeof(kGiRows) / sizeof(kGiRows[0]));
int giRowFor(iris::GiMode m)
{
    for (int i = 0; i < kGiRowCount; ++i)
        if (kGiRows[i] == m) return i;
    return 0;
}

/// A row the tier would have set differently wears the same marker the World
/// Mode panel uses for a pinned row.
QString pinMark(const iris::ScenePtr &scene, const char *rowId)
{
    return scene && scene->worldOverrides.contains(QLatin1String(rowId))
        ? QStringLiteral(" *") : QString();
}
}

WorldGiPropertyWidget::WorldGiPropertyWidget()
{
}

void WorldGiPropertyWidget::setScene(QSharedPointer<iris::Scene> scene)
{
    if (!!scene) {
        this->scene = scene;
        rebuild();
    } else {
        this->scene.clear();
    }
}

// RAYON (GI_UNIFIED_SPEC.md §2): three rows, then everything else behind a
// disclosure. The panel is rebuilt on every edit (like the sky panel), which is
// what lets the visible rows change shape — the tier row gains a "Custom" entry
// while a pin deviates, the Advanced rows follow the chosen technique.
void WorldGiPropertyWidget::rebuild()
{
    clearPanel(this->layout());
    // The rows just retired (deleteLater) — every pointer below is to one of
    // them, and refreshPins() dereferences whichever this build leaves set.
    rayonSwitch = nullptr; tierSelector = nullptr; modeSelector = nullptr;
    quality = nullptr; lightSelector = nullptr; bounces = nullptr;
    dynamicProbes = nullptr; boundsMin = nullptr; boundsMax = nullptr;
    pccGrid = nullptr; updateBudget = nullptr; ddgiToggle = nullptr;
    ddgiIntensity = nullptr; ddgiAmbient = nullptr; ddgiSource = nullptr;
    fitBoundsButton = nullptr; advancedButton = nullptr; resetAdvancedButton = nullptr;
    editing = false;   // a build mid-gesture ends the gesture (the slider is gone)
    if (!scene) return;

    const bool on = worldmodes::rayonEnabled(scene);
    const worldmodes::RayonTier tier = worldmodes::rayonTier(scene);
    const QStringList deviations = worldmodes::rayonDeviations(scene);

    // ---- 1. THE SWITCH ------------------------------------------------------
    rayonSwitch = this->addCheckBox(QStringLiteral("Rayon"), on);
    // addCheckBox ignores its `value` argument (accordionbladewidget.cpp never
    // calls setValue) — every panel that cares sets it itself.
    rayonSwitch->setValue(on);
    rayonSwitch->setToolTip(
        tr("Rayon — realtime global illumination.\n\n"
           "Light that bounces off surfaces and colours everything it lands on, recomputed "
           "live rather than baked. Off is the renderer doing no indirect light at all; the "
           "quality below is remembered, so switching back on brings it back."));
    connect(rayonSwitch, &CheckBoxWidget::valueChanged,
            this, &WorldGiPropertyWidget::onRayonToggled);

    // ---- 2. THE QUALITY TIER ------------------------------------------------
    tierSelector = this->addComboBox(QStringLiteral("Quality"));
    const QStringList tierNames = worldmodes::rayonTierNames();
    tierSelector->addItem(QStringLiteral("Low"));
    tierSelector->addItem(QStringLiteral("Medium"));
    tierSelector->addItem(QStringLiteral("High"));
    tierSelector->addItem(QStringLiteral("Epic"));
    if (!deviations.isEmpty()) {
        // The registry's own pattern (the World Mode combo shows "Custom" the
        // same way): shown, never chosen. Advanced > Reset is the way back.
        tierSelector->addItem(QStringLiteral("Custom"));
        tierSelector->setCurrentIndex(tierNames.size());
    } else {
        tierSelector->setCurrentIndex(int(tier));
    }
    QString tierTip =
        tr("How much machinery Rayon uses. Low bounces one light off the scene (cheapest — and "
           "the one tier where emissive surfaces and area lights contribute nothing). Medium "
           "voxelizes the lit volume and feeds an irradiance field from it: probe-stored bounce "
           "that cannot leak through walls. High adds a grid of reflection probes, captured in "
           "HDR with shadows. Epic adds three light bounces and dynamic reflection probes — the "
           "probes covering whatever moves re-capture every frame instead of waiting their turn "
           "in the update budget.\n\n"
           "New scenes start at Epic.");
    if (!deviations.isEmpty())
        tierTip += tr("\n\nCUSTOM: %1 %2 been set by hand and no longer follow the tier. They stay "
                      "that way through tier switches; Advanced > Reset Advanced Settings hands "
                      "them back.")
                       .arg(deviations.join(QStringLiteral(", ")),
                            deviations.size() == 1 ? tr("has") : tr("have"));
    tierSelector->setToolTip(tierTip);
    tierSelector->setEnabled(on);
    connect(tierSelector, QOverload<int>::of(&ComboBoxWidget::currentIndexChanged),
            this, &WorldGiPropertyWidget::onTierChanged);

    // ---- 3. THE UPDATE BUDGET ----------------------------------------------
    // NOT tier-driven, on purpose (owner decision D5): the tier answers "how
    // much machinery", the budget answers "how fast may it keep up", and a
    // scene that wants GI paused wants it paused at every quality.
    {
        // THE EXPLANATION IS A TOOLTIP, NOT A ROW VALUE (the World Mode rows'
        // pattern, worldmodespropertywidget.cpp). It used to be passed as the
        // label row's VALUE, and a 700-character value in a non-wrapping QLabel
        // demanded 3674 px of a 315 px dock — which stretched every row in the
        // panel and pushed the World section's controls right off the visible
        // dock. That is the defect the owner reported as "the World blades show
        // no controls"; LabelWidget wraps now as well, so neither half can come
        // back.
        auto *budgetRow = this->addLabel(tr("GI Update Budget"),
                                         tr("0 pauses · 1 is realtime"));
        const QString budgetTip =
            tr("How much work global illumination may spend per frame keeping "
               "up with the scene. 0 PAUSES it: nothing re-solves and no "
               "reflection probe re-captures until world.refreshGi() asks. "
               "1 (the default) is a realtime editor — at High and Epic it "
               "re-captures one reflection probe per frame, so the whole "
               "grid refreshes over as many frames as it has probes, the ones "
               "nearest you and the ones around whatever just moved going "
               "first, for about 2 ms a frame. Higher costs that again per "
               "unit. Note that any budget above 0 makes the renderer prefer "
               "the probes to cone-traced reflections inside the probe region, "
               "so rough metal reflects the probes.");
        if (budgetRow) budgetRow->setToolTip(budgetTip);
        updateBudget = this->addFloatValueSlider(QString(), 0.0f, 8.0f,
                                                 float(qBound(0, scene->giUpdateBudget, 8)));
        updateBudget->setToolTip(budgetTip);
        // Shown even with Rayon off — it is one of the three rows this section
        // promises — but there is nothing to budget then, so it greys out the
        // same way the tier does.
        updateBudget->setEnabled(on);
        // The slider tops out at 8 because a row is a row; the VERB takes 0..512
        // for scripts that want a whole grid live for a capture.
        wirePlainRow(updateBudget, QStringLiteral("giUpdateBudget"), tr("Rayon Update Budget"),
                     [](const QVariant &v) { return QVariant(qBound(0, qRound(v.toFloat()), 8)); });
    }

    // ---- THE ADVANCED DISCLOSURE -------------------------------------------
    // Everything the tier consumes lives here (owner decision D3, "as Unreal
    // does for Lumen"). Nothing is deleted and nothing is read-only: every row
    // writes the same backing field it always did and PINS itself, so the tier
    // stops overwriting it.
    advancedButton = new QPushButton(advancedOpen ? tr("▾  Advanced") : tr("▸  Advanced"));
    advancedButton->setCheckable(true);
    advancedButton->setChecked(advancedOpen);
    advancedButton->setToolTip(tr("Everything the quality tier sets for you, one setting at a "
                                  "time. Changing one PINS it: it keeps its value through tier "
                                  "switches until you reset it."));
    connect(advancedButton, &QPushButton::toggled, this, &WorldGiPropertyWidget::onAdvancedToggled);
    this->addWidgetToContent(advancedButton);
    if (!advancedOpen) return;

    // The technique. Off is reachable here too — it is the same field the
    // switch above writes, and that is deliberate: there is no second flag.
    modeSelector = this->addComboBox(tr("Technique") + pinMark(scene, "giMode"));
    modeSelector->addItem(tr("Off"));
    modeSelector->addItem(tr("Bounced Light"));
    modeSelector->addItem(tr("Voxel Lighting"));
    modeSelector->addItem(tr("Voxel + Reflections"));
    modeSelector->setCurrentIndex(giRowFor(scene->giMode));
    modeSelector->setToolTip(tr("Which technique Rayon uses, if you want to choose it yourself. "
                                "Bounced Light is Instant Radiosity; Voxel Lighting cone-traces "
                                "the bounce out of a voxelization of the lit volume; Voxel + "
                                "Reflections adds the parallax-corrected probe grid."));
    connect(modeSelector, QOverload<int>::of(&ComboBoxWidget::currentIndexChanged),
            this, &WorldGiPropertyWidget::modeChanged);

    switch (scene->giMode) {
    case iris::GiMode::OFF:
        break;

    case iris::GiMode::INSTANT_RADIOSITY: {
        // Bounced Light: sunlight (or any light) bounces once off surfaces and
        // spills its colour into the shadows. Live in the engine viewport.
        quality = this->addComboBox(tr("Quality") + pinMark(scene, "giQuality"));
        quality->addItem(tr("Low"));
        quality->addItem(tr("Medium"));
        quality->addItem(tr("High"));
        quality->setCurrentIndex(qBound(0, static_cast<int>(scene->giQuality), 2));
        connect(quality, QOverload<int>::of(&ComboBoxWidget::currentIndexChanged),
                this, &WorldGiPropertyWidget::onQualityChanged);

        lightSelector = this->addComboBox(tr("Bounce From"));
        lightSelector->addItem(tr("Automatic"), QString());
        int row = 0, current = 0;
        for (const auto &light : scene->lights) {
            if (light.isNull()) continue;
            lightSelector->addItem(light->getName(), light->getGUID());
            ++row;
            if (!scene->giLightGuid.isEmpty() && light->getGUID() == scene->giLightGuid)
                current = row;
        }
        lightSelector->setCurrentIndex(current);
        // "Bounce From" carries the light's GUID as item data (the row index is
        // meaningless to the document), so the row maps its own value.
        {
            ComboBoxWidget *picker = lightSelector;
            wirePlainRow(lightSelector, QStringLiteral("giLightGuid"), tr("Rayon Bounce Light"),
                         [picker](const QVariant &row) {
                             return QVariant(picker->getItemData(row.toInt()).toString());
                         });
        }

        bounces = this->addFloatValueSlider(tr("Light Bounces") + pinMark(scene, "giBounces"),
                                            1.0f, 4.0f, float(scene->giNumBounces));
        wireRayonSlider(bounces, &WorldGiPropertyWidget::onBouncesChanged, tr("Rayon Light Bounces"));
        break;
    }

    case iris::GiMode::VCT:
    case iris::GiMode::VCT_PCC_HYBRID: {
        // Voxel Lighting: the scene is voxelized over the bounds and every
        // surface cone-traces bounced light and reflections out of the volume.
        // Voxel + Reflections adds a grid of reflection probes blended with the
        // cone-traced reflections by distance. Live in the engine viewport.
        quality = this->addComboBox(tr("Quality") + pinMark(scene, "giQuality"));
        quality->addItem(tr("Low"));        // 32^3 voxels, 128px probes
        quality->addItem(tr("Medium"));     // 64^3, 256px
        quality->addItem(tr("High"));       // 128^3, 512px
        quality->setCurrentIndex(qBound(0, static_cast<int>(scene->giQuality), 2));
        connect(quality, QOverload<int>::of(&ComboBoxWidget::currentIndexChanged),
                this, &WorldGiPropertyWidget::onQualityChanged);

        // A Rayon tier row (Epic's column: 3): an edit here PINS it, like the
        // technique and quality above, and the mark says so.
        bounces = this->addFloatValueSlider(tr("Light Bounces") + pinMark(scene, "giBounces"),
                                            1.0f, 4.0f, float(scene->giNumBounces));
        bounces->setToolTip(tr("Total light bounces, 1-4. Each bounce past the first is another "
                               "light-propagation pass over the whole voxel volume on every "
                               "re-solve, and the irradiance field is fed from that volume so it "
                               "sees them too. Epic sets 3; the other tiers 1."));
        wireRayonSlider(bounces, &WorldGiPropertyWidget::onBouncesChanged, tr("Rayon Light Bounces"));

        // COMPACT, SCRUBBABLE ROWS (owner report 2026-09-07). These were
        // addVector3Widget — three full-width QDoubleSpinBoxes and NO label at
        // all (that helper ignores its name argument), which is why the section
        // pushed the dock wider than the panel and why a caption row above each
        // pair was needed to say what they were. DragVector3Widget is the
        // transform editor's row: named, shrinkable, and scrubbed by dragging
        // left/right (ui/controls/dragvaluewidgets.h).
        this->addLabel(tr("Bounds"), tr("Corners of the lit area; zeros = fit the scene"));
        boundsMin = this->addDragVector3(tr("Min"), scene->giBoundsMin);
        boundsMax = this->addDragVector3(tr("Max"), scene->giBoundsMax);
        wirePlainRow(boundsMin, QStringLiteral("giBoundsMin"), tr("Rayon Bounds"));
        wirePlainRow(boundsMax, QStringLiteral("giBoundsMax"), tr("Rayon Bounds"));

        if (scene->giMode == iris::GiMode::VCT_PCC_HYBRID) {
            this->addLabel(tr("Reflection Probes"), tr("Probe counts along each axis of the bounds"));
            // Counts, not lengths: whole numbers, a coarse scrub, and a range
            // that cannot ask for a probe grid nobody could afford.
            pccGrid = this->addDragVector3(tr("Grid"), scene->giPccGrid, 1.0, 16.0, 0.05, 0);
            // Counts: whole numbers in 1..8, clamped on the way to the document.
            wirePlainRow(pccGrid, QStringLiteral("giPccGrid"), tr("Rayon Probe Grid"),
                         [](const QVariant &v) {
                             const QVector3D g = v.value<QVector3D>();
                             return QVariant::fromValue(QVector3D(qBound(1, qRound(g.x()), 8),
                                                                  qBound(1, qRound(g.y()), 8),
                                                                  qBound(1, qRound(g.z()), 8)));
                         });
            // DYNAMIC PROBES (Epic's other column). A Rayon tier row; pins.
            dynamicProbes = this->addFloatValueSlider(
                tr("Dynamic Probes") + pinMark(scene, "giDynamicProbes"), 0.0f, 8.0f,
                float(qBound(0, scene->giDynamicProbes, 8)));
            dynamicProbes->setToolTip(
                tr("Extra reflection-probe re-captures per frame, on top of the GI Update "
                   "Budget, reserved for the probes covering whatever MOVED this frame — so a "
                   "moving object's reflection follows it frame by frame instead of waiting "
                   "its turn in the budget's sweep. Costs nothing while the scene is still. "
                   "Epic sets 2; the other tiers 0 (the sweep alone)."));
            wireRayonSlider(dynamicProbes, &WorldGiPropertyWidget::onDynamicProbesChanged,
                            tr("Rayon Dynamic Probes"));
        }

        // THE IRRADIANCE FIELD (GI_UNIFIED_SPEC P1). On at every voxel tier
        // since option (b); it REPLACES the cone-traced diffuse rather than
        // adding to it.
        ddgiToggle = this->addCheckBox(tr("Irradiance Field (DDGI)") + pinMark(scene, "giDdgi"),
                                       scene->giDdgi > 0);
        ddgiToggle->setValue(scene->giDdgi > 0);
        ddgiToggle->setToolTip(
            tr("A grid of probes over the lit volume storing the bounced light arriving from "
               "every direction, plus a depth map that decides what each probe can see. It is "
               "the leak fix: a cone cannot tell a wall from empty space, and this can.\n\n"
               "Turning it on turns the voxel-cone diffuse OFF — it replaces that term rather "
               "than adding to it. Reflections, probes and planar are untouched. Every voxel "
               "tier (Medium, High, Epic) turns it on; Low has no volume to feed it from."));
        connect(ddgiToggle, &CheckBoxWidget::valueChanged,
                this, &WorldGiPropertyWidget::onDdgiToggled);
        if (scene->giDdgi > 0) {
            ddgiIntensity = this->addFloatValueSlider(tr("Field Intensity"), 0.0f, 4.0f,
                                                      qBound(0.0f, scene->giDdgiIntensity, 4.0f));
            ddgiIntensity->setToolTip(
                tr("How brightly the field's diffuse is applied. 1.0 is the renderer's raw value "
                   "and the calibrated default (measured at 86% of the cone-traced diffuse it "
                   "replaces). Raise it to trim a room brighter; 0 leaves the field bound and "
                   "contributing nothing."));
            wirePlainRow(ddgiIntensity, QStringLiteral("giDdgiIntensity"), tr("Field Intensity"),
                         [](const QVariant &v) { return QVariant(qBound(0.0f, v.toFloat(), 64.0f)); });
            ddgiAmbient = this->addFloatValueSlider(tr("Ambient Fill"), 0.0f, 4.0f,
                                                    qBound(0.0f, scene->giDdgiAmbient, 4.0f));
            ddgiAmbient->setToolTip(
                tr("How strongly the ambient the field would otherwise swallow is rebuilt. "
                   "Inside the lit volume the ordinary ambient term is switched off — the "
                   "cone-traced bounce carried it instead, weighted by how much sky each "
                   "surface could see — and the field replacing that bounce used to take the "
                   "ambient with it, flattening open scenes.\n\n"
                   "1.0 rebuilds it from the field's own depth probes and is the default; 0 "
                   "leaves it out, which is how this behaved before the fix. A sealed room "
                   "sees no difference either way: it has no sky to see."));
            wirePlainRow(ddgiAmbient, QStringLiteral("giDdgiAmbient"), tr("Ambient Fill"),
                         [](const QVariant &v) { return QVariant(qBound(0.0f, v.toFloat(), 8.0f)); });
            // THE PROBE SOURCE (GI_UNIFIED_SPEC P3 "A2", rayon2 S3). Advanced
            // only, by decree: no tier writes it, epic stays voxel-fed, so it
            // carries no pin mark and no registry row.
            ddgiSource = this->addComboBox(tr("Probe Source"));
            ddgiSource->addItem(tr("Automatic (voxel)"));
            ddgiSource->addItem(tr("Voxel cone tracing"));
            ddgiSource->addItem(tr("Rasterised captures (sees animation)"));
            ddgiSource->setCurrentIndex(qBound(0, scene->giDdgiSource + 1, 2));
            ddgiSource->setToolTip(
                tr("Where the field's probes get their light.\n\n"
                   "Voxel cone tracing reads the voxel volume the field sits over: cheap, and "
                   "blind to anything the voxelizer did not bake — a skinned character "
                   "animates inside a static voxel of itself.\n\n"
                   "Rasterised captures render six small faces per probe from the live scene "
                   "instead and re-capture while rigs move, under the same GI Update Budget. "
                   "The field is never born dark: it starts from the voxel answer."));
            // Row 0 is "Automatic" = -1 on the document.
            wirePlainRow(ddgiSource, QStringLiteral("giDdgiSource"), tr("Probe Source"),
                         [](const QVariant &row) { return QVariant(qBound(-1, row.toInt() - 1, 1)); });
        }

        // P1a.3, adapted: the spec asked for "fit to SELECTION", but this panel
        // only ever appears while the WORLD is the selection (the properties
        // panel swaps the whole stack, scenenodepropertieswidget.cpp), so a
        // selection-driven button here would be permanently empty. What is
        // useful at this moment is pinning the volume to the scene's contents
        // and then nudging the rows. The selection-driven form lives where it
        // can work: the world.fitGiBounds({nodes}) verb.
        fitBoundsButton = new QPushButton(tr("Fit Bounds To Scene"));
        fitBoundsButton->setToolTip(tr("Pin the bounds above to everything in the scene. Leaving "
                                       "them at zero lets the renderer fit them automatically, "
                                       "which also ignores outsized objects like a ground plane; "
                                       "pin them when you want a volume of your own."));
        connect(fitBoundsButton, &QPushButton::clicked, this, &WorldGiPropertyWidget::onFitBoundsClicked);
        this->addWidgetToContent(fitBoundsButton);
        break;
    }
    }

    // Only worth offering when there is something to hand back — the same rule
    // the World Mode panel's "Reset All Pinned Rows" follows.
    if (advancedResettable()) addResetAdvancedButton();
}

bool WorldGiPropertyWidget::advancedResettable() const
{
    if (!scene) return false;
    bool anyPin = !worldmodes::rayonDeviations(scene).isEmpty();
    for (const QString &id : worldmodes::rayonRowIds())
        anyPin = anyPin || scene->worldOverrides.contains(id);
    return anyPin;
}

void WorldGiPropertyWidget::addResetAdvancedButton()
{
    if (resetAdvancedButton) return;
    resetAdvancedButton = new QPushButton(tr("Reset Advanced Settings"));
    resetAdvancedButton->setToolTip(tr("Drops the settings you pinned here (*) and lets the "
                                       "quality tier decide them again."));
    connect(resetAdvancedButton, &QPushButton::clicked,
            this, &WorldGiPropertyWidget::onResetAdvancedClicked);
    // The button is the LAST thing rebuild() adds, so appending it later lands
    // it in the same place.
    this->addWidgetToContent(resetAdvancedButton);
}

// What a slider tick may change besides its own backing field: the row's pin
// mark, whether the reset button is offered, and whether the tier row reads
// "Custom". All three are re-read from the document here, in place — rebuild()
// would deleteLater the slider that is emitting.
void WorldGiPropertyWidget::refreshPins()
{
    if (!scene) return;
    if (bounces)
        bounces->ui->label->setText(tr("Light Bounces") + pinMark(scene, "giBounces"));
    if (dynamicProbes)
        dynamicProbes->ui->label->setText(tr("Dynamic Probes") + pinMark(scene, "giDynamicProbes"));

    if (tierSelector && tierSelector->getWidget()) {
        QComboBox *combo = tierSelector->getWidget();
        const int tiers = worldmodes::rayonTierNames().size();
        const bool custom = !worldmodes::rayonDeviations(scene).isEmpty();
        const QSignalBlocker quiet(combo);   // a display change, not a pick
        if (custom) {
            if (combo->count() == tiers) combo->addItem(QStringLiteral("Custom"));
            combo->setCurrentIndex(tiers);
        } else {
            if (combo->count() > tiers) combo->removeItem(tiers);
            combo->setCurrentIndex(int(worldmodes::rayonTier(scene)));
        }
    }

    const bool want = advancedResettable();
    if (want && !resetAdvancedButton) {
        addResetAdvancedButton();
    } else if (!want && resetAdvancedButton) {
        delete resetAdvancedButton;      // a live delete: the layout drops it itself
        resetAdvancedButton = nullptr;
    }
}

void WorldGiPropertyWidget::wireRayonSlider(HFloatSliderWidget *slider,
                                            void (WorldGiPropertyWidget::*tick)(float),
                                            const QString &text)
{
    if (!slider) return;
    connect(slider, &HFloatSliderWidget::valueChangeStart, this,
            [this](float) { beginRayonEdit(); });
    connect(slider, &HFloatSliderWidget::valueChanged, this, tick);
    connect(slider, &HFloatSliderWidget::valueChangeEnd, this,
            [this, text](float) { endRayonEdit(text); });
}

void WorldGiPropertyWidget::beginRayonEdit()
{
    if (!scene || editing) return;
    editing = true;
    editBefore = WorldModeCommand::capture(scene);
}

void WorldGiPropertyWidget::endRayonEdit(const QString &text)
{
    if (!editing) return;
    editing = false;
    if (!scene || !services || !services->undo) return;
    const WorldModeCommand::Snapshot after = WorldModeCommand::capture(scene);
    // A press-and-release that moved nothing is not an edit.
    if (after.rowValues == editBefore.rowValues && after.overrides == editBefore.overrides &&
        after.worldMode == editBefore.worldMode && after.rayonTier == editBefore.rayonTier)
        return;
    auto *cmd = new WorldModeCommand(text, scene, editBefore);
    // An undo repaints the panel it came from (the rows ARE the state); by then
    // no slider of ours is mid-gesture, so a full rebuild is the right refresh.
    QPointer<WorldGiPropertyWidget> self(this);
    cmd->setRefresh([self]() { if (self) self->rebuild(); });
    services->undo->push(cmd);
}

void WorldGiPropertyWidget::editRayonRow(const QString &id, int value, const QString &text)
{
    if (!scene) return;
    // A tick outside a start/end bracket (nothing in this panel emits one, but
    // the slider's contract does not forbid it) is its own one-step edit.
    const bool atomic = !editing;
    if (atomic) beginRayonEdit();
    // Through the registry: the write AND the pin, so a later tier switch
    // does not silently undo the edit (worldmodes.h's one invariant).
    worldmodes::setRowValue(scene, id, value);
    refreshPins();
    if (atomic) endRayonEdit(text);
}

// A REGISTRY edit: through worldmodes (the write AND the pin), as ONE
// WorldModeCommand, and then a rebuild — every row here decides which OTHER
// rows exist, so the panel's shape follows the document.
void WorldGiPropertyWidget::editRegistry(const QString &text, const std::function<void()> &edit)
{
    if (!scene) return;
    QPointer<WorldGiPropertyWidget> self(this);
    panelundo::runWorldModeEdit(services, scene, text, edit, [self]() {
        if (!self) return;
        self->rebuild();
        // The sibling sections (World Mode above all, which lists every one of
        // these rows with its pin mark) display what was just written.
        emit self->worldSettingsChanged();
    });
}

void WorldGiPropertyWidget::wirePlainRow(QWidget *row, const QString &key, const QString &text,
                                         std::function<QVariant(const QVariant &)> toDocument)
{
    if (!row) return;
    QPointer<WorldGiPropertyWidget> self(this);
    panelundo::SceneRows rows([this]() { return scene; }, [this]() { return services; },
                              [self]() { if (self) self->rebuild(); });
    const rowundo::Binding binding = rows(key, text, std::move(toDocument));
    if (auto *slider = qobject_cast<HFloatSliderWidget *>(row))      rowundo::bind(slider, binding);
    else if (auto *vec = qobject_cast<DragVector3Widget *>(row))     rowundo::bind(vec, binding);
    else if (auto *combo = qobject_cast<ComboBoxWidget *>(row))      rowundo::bind(combo, binding);
}

void WorldGiPropertyWidget::onRayonToggled(bool on)
{
    if (!scene) return;
    // The switch changed which rows exist (the budget row, the whole Advanced
    // block), so editRegistry's rebuild is not optional here.
    editRegistry(on ? tr("Rayon On") : tr("Rayon Off"), [this, on]() {
        worldmodes::setRayon(scene, on, worldmodes::rayonTier(scene));
    });
}

void WorldGiPropertyWidget::onTierChanged(int row)
{
    if (!scene) return;
    const QStringList names = worldmodes::rayonTierNames();
    if (row < 0 || row >= names.size()) return;   // the "Custom" entry is not pickable
    bool ok = false;
    const worldmodes::RayonTier t = worldmodes::rayonTierFromName(names[row], &ok);
    if (!ok) return;
    editRegistry(tr("Rayon Quality: %1").arg(names[row]), [this, t]() {
        worldmodes::setRayon(scene, worldmodes::rayonEnabled(scene), t);
    });
}

void WorldGiPropertyWidget::onAdvancedToggled(bool on)
{
    // A disclosure is not a document edit: nothing to record, everything to
    // rebuild.
    advancedOpen = on;
    rebuild();
}

void WorldGiPropertyWidget::modeChanged(int row)
{
    if (!scene || row < 0 || row >= kGiRowCount) return;
    const iris::GiMode mode = kGiRows[row];
    // A direct edit of a backing field is a PIN (POST_CHAIN_SPEC §9.1) — here
    // it is the Rayon technique pin: it survives tier switches until reset.
    editRegistry(tr("Rayon Technique"), [this, mode]() {
        scene->giMode = mode;
        worldmodes::pinRowValue(scene, QStringLiteral("giMode"), int(scene->giMode));
    });
}

void WorldGiPropertyWidget::onQualityChanged(int row)
{
    if (!scene) return;
    const int quality = qBound(0, row, 2);
    editRegistry(tr("Rayon Quality Detail"), [this, quality]() {
        scene->giQuality = static_cast<iris::GiQuality>(quality);
        worldmodes::pinRowValue(scene, QStringLiteral("giQuality"), int(scene->giQuality));
    });
}

void WorldGiPropertyWidget::onLightChanged(int row)
{
    Q_UNUSED(row)   // the row is wired through wirePlainRow (giLightGuid)
}

void WorldGiPropertyWidget::onBouncesChanged(float value)
{
    editRayonRow(QStringLiteral("giBounces"), qBound(1, qRound(value), 4), tr("Rayon Light Bounces"));
}

void WorldGiPropertyWidget::onDynamicProbesChanged(float value)
{
    editRayonRow(QStringLiteral("giDynamicProbes"), qBound(0, qRound(value), 8),
                 tr("Rayon Dynamic Probes"));
}

void WorldGiPropertyWidget::onDdgiToggled(bool on)
{
    if (!scene) return;
    editRegistry(on ? tr("Irradiance Field On") : tr("Irradiance Field Off"), [this, on]() {
        scene->giDdgi = on ? 1 : 0;
        worldmodes::pinRowValue(scene, QStringLiteral("giDdgi"), scene->giDdgi);
    });   // the intensity rows only exist while the field is on
}

void WorldGiPropertyWidget::onDdgiSourceChanged(int index)
{
    Q_UNUSED(index)   // wired through wirePlainRow (giDdgiSource)
}

void WorldGiPropertyWidget::onFitBoundsClicked()
{
    if (!scene || scene->getRootNode().isNull()) return;
    iris::Vec3 mn, mx;
    if (!gibounds::fit(scene->getRootNode()->children(), 0.0f, mn, mx)) return;
    // TWO fields, one gesture: the pair is the volume, and an undo that put
    // back one corner would leave a box nobody asked for. Recorded as one step
    // whose apply half is the same assignment the button just made.
    const iris::Vec3 oldMin = scene->giBoundsMin;
    const iris::Vec3 oldMax = scene->giBoundsMax;
    auto apply = [this](const iris::Vec3 &lo, const iris::Vec3 &hi) {
        if (!scene) return;
        scene->giBoundsMin = lo;
        scene->giBoundsMax = hi;
    };
    apply(mn, mx);
    QPointer<WorldGiPropertyWidget> self(this);
    panelundo::pushEdit(services, tr("Fit Rayon Bounds"),
                        [apply, mn, mx]() { apply(mn, mx); },
                        [self, apply, oldMin, oldMax]() {
                            apply(oldMin, oldMax);
                            if (self) self->rebuild();
                        });
    // The rows are spin boxes holding the OLD numbers; rebuild so the panel
    // shows what it just wrote.
    rebuild();
}

void WorldGiPropertyWidget::onResetAdvancedClicked()
{
    if (!scene) return;
    // Dropping every Rayon pin is the widest edit this section makes — and the
    // one most worth being able to take back.
    editRegistry(tr("Reset Rayon Advanced Settings"),
                 [this]() { worldmodes::clearRayonOverrides(scene); });
}
