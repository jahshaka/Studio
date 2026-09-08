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
#include "irisgl/document/scenegraph/scenenode.h"

#include <QPushButton>

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
           "voxelizes the lit volume and cone-traces the bounce out of it. High adds a grid of "
           "reflection probes, captured in HDR with shadows. Epic adds the irradiance field, "
           "which replaces the cone-traced diffuse with probe-stored bounce that cannot leak "
           "through walls.\n\n"
           "New scenes start at Epic.\n\n"
           "Known gap at Epic: with the irradiance field on, ambient light inside the lit volume "
           "has no source, so an OPEN scene reads 15-25% darker in the mid-ground (a sealed room "
           "is unaffected). Drop to High, or raise the field's Intensity under Advanced.");
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
        this->addLabel(tr("GI Update Budget"),
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
                          "so rough metal reflects the probes."));
        updateBudget = this->addFloatValueSlider(QString(), 0.0f, 8.0f,
                                                 float(qBound(0, scene->giUpdateBudget, 8)));
        // Shown even with Rayon off — it is one of the three rows this section
        // promises — but there is nothing to budget then, so it greys out the
        // same way the tier does.
        updateBudget->setEnabled(on);
        connect(updateBudget, SIGNAL(valueChanged(float)), SLOT(onUpdateBudgetChanged(float)));
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
        connect(lightSelector, QOverload<int>::of(&ComboBoxWidget::currentIndexChanged),
                this, &WorldGiPropertyWidget::onLightChanged);

        bounces = this->addFloatValueSlider(tr("Light Bounces"), 1.0f, 4.0f,
                                            float(scene->giNumBounces));
        connect(bounces, SIGNAL(valueChanged(float)), SLOT(onBouncesChanged(float)));
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

        bounces = this->addFloatValueSlider(tr("Light Bounces"), 1.0f, 4.0f,
                                            float(scene->giNumBounces));
        connect(bounces, SIGNAL(valueChanged(float)), SLOT(onBouncesChanged(float)));

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
        connect(boundsMin, &DragVector3Widget::valueChanged, this, &WorldGiPropertyWidget::onBoundsMinChanged);
        connect(boundsMax, &DragVector3Widget::valueChanged, this, &WorldGiPropertyWidget::onBoundsMaxChanged);

        if (scene->giMode == iris::GiMode::VCT_PCC_HYBRID) {
            this->addLabel(tr("Reflection Probes"), tr("Probe counts along each axis of the bounds"));
            // Counts, not lengths: whole numbers, a coarse scrub, and a range
            // that cannot ask for a probe grid nobody could afford.
            pccGrid = this->addDragVector3(tr("Grid"), scene->giPccGrid, 1.0, 16.0, 0.05, 0);
            connect(pccGrid, &DragVector3Widget::valueChanged, this, &WorldGiPropertyWidget::onPccGridChanged);
        }

        // THE IRRADIANCE FIELD (GI_UNIFIED_SPEC P1). Epic's defining feature,
        // and the reason the tier row carries a warning: it REPLACES the
        // cone-traced diffuse rather than adding to it.
        ddgiToggle = this->addCheckBox(tr("Irradiance Field (DDGI)") + pinMark(scene, "giDdgi"),
                                       scene->giDdgi > 0);
        ddgiToggle->setValue(scene->giDdgi > 0);
        ddgiToggle->setToolTip(
            tr("A grid of probes over the lit volume storing the bounced light arriving from "
               "every direction, plus a depth map that decides what each probe can see. It is "
               "the leak fix: a cone cannot tell a wall from empty space, and this can.\n\n"
               "Turning it on turns the voxel-cone diffuse OFF — it replaces that term rather "
               "than adding to it. Reflections, probes and planar are untouched. Epic turns it "
               "on; the other tiers leave it off."));
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
            connect(ddgiIntensity, SIGNAL(valueChanged(float)), SLOT(onDdgiIntensityChanged(float)));
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
    if (!deviations.isEmpty() || !pinMark(scene, "giMode").isEmpty() ||
        !pinMark(scene, "giQuality").isEmpty() || !pinMark(scene, "giDdgi").isEmpty()) {
        resetAdvancedButton = new QPushButton(tr("Reset Advanced Settings"));
        resetAdvancedButton->setToolTip(tr("Drops the settings you pinned here (*) and lets the "
                                           "quality tier decide them again."));
        connect(resetAdvancedButton, &QPushButton::clicked,
                this, &WorldGiPropertyWidget::onResetAdvancedClicked);
        this->addWidgetToContent(resetAdvancedButton);
    }
}

void WorldGiPropertyWidget::onRayonToggled(bool on)
{
    if (!scene) return;
    worldmodes::setRayon(scene, on, worldmodes::rayonTier(scene));
    // The switch changed which rows exist (the budget row, the whole Advanced
    // block), so the panel has to be rebuilt rather than merely refreshed.
    rebuild();
}

void WorldGiPropertyWidget::onTierChanged(int row)
{
    if (!scene) return;
    const QStringList names = worldmodes::rayonTierNames();
    if (row < 0 || row >= names.size()) return;   // the "Custom" entry is not pickable
    bool ok = false;
    const worldmodes::RayonTier t = worldmodes::rayonTierFromName(names[row], &ok);
    if (!ok) return;
    worldmodes::setRayon(scene, worldmodes::rayonEnabled(scene), t);
    rebuild();
}

void WorldGiPropertyWidget::onAdvancedToggled(bool on)
{
    advancedOpen = on;
    rebuild();
}

void WorldGiPropertyWidget::modeChanged(int row)
{
    if (!scene || row < 0 || row >= kGiRowCount) return;
    scene->giMode = kGiRows[row];
    // A direct edit of a backing field is a PIN (POST_CHAIN_SPEC §9.1) — here
    // it is the Rayon technique pin: it survives tier switches until reset.
    worldmodes::pinRowValue(scene, QStringLiteral("giMode"), int(scene->giMode));
    rebuild();
}

void WorldGiPropertyWidget::onQualityChanged(int row)
{
    if (!scene) return;
    scene->giQuality = static_cast<iris::GiQuality>(qBound(0, row, 2));
    worldmodes::pinRowValue(scene, QStringLiteral("giQuality"), int(scene->giQuality));
    rebuild();
}

void WorldGiPropertyWidget::onLightChanged(int row)
{
    if (!scene || !lightSelector) return;
    scene->giLightGuid = lightSelector->getItemData(row).toString();
}

void WorldGiPropertyWidget::onBouncesChanged(float value)
{
    if (!!scene) scene->giNumBounces = qBound(1, qRound(value), 4);
}

void WorldGiPropertyWidget::onBoundsMinChanged(iris::Vec3 value)
{
    if (!!scene) scene->giBoundsMin = value;
}

void WorldGiPropertyWidget::onBoundsMaxChanged(iris::Vec3 value)
{
    if (!!scene) scene->giBoundsMax = value;
}

void WorldGiPropertyWidget::onPccGridChanged(iris::Vec3 value)
{
    if (!!scene)
        scene->giPccGrid = iris::Vec3(qBound(1, qRound(value.x()), 8),
                                     qBound(1, qRound(value.y()), 8),
                                     qBound(1, qRound(value.z()), 8));
}

void WorldGiPropertyWidget::onUpdateBudgetChanged(float value)
{
    // The slider tops out at 8 because a row is a row; the VERB takes 0..512 for
    // scripts that want a whole grid live for a capture.
    if (!!scene) scene->giUpdateBudget = qBound(0, qRound(value), 8);
}

void WorldGiPropertyWidget::onDdgiToggled(bool on)
{
    if (!scene) return;
    scene->giDdgi = on ? 1 : 0;
    worldmodes::pinRowValue(scene, QStringLiteral("giDdgi"), scene->giDdgi);
    rebuild();   // the intensity row only exists while the field is on
}

void WorldGiPropertyWidget::onDdgiIntensityChanged(float value)
{
    if (!!scene) scene->giDdgiIntensity = qBound(0.0f, value, 64.0f);
}

void WorldGiPropertyWidget::onFitBoundsClicked()
{
    if (!scene || scene->getRootNode().isNull()) return;
    iris::Vec3 mn, mx;
    if (!gibounds::fit(scene->getRootNode()->children(), 0.0f, mn, mx)) return;
    scene->giBoundsMin = mn;
    scene->giBoundsMax = mx;
    // The rows are spin boxes holding the OLD numbers; rebuild so the panel
    // shows what it just wrote.
    rebuild();
}

void WorldGiPropertyWidget::onResetAdvancedClicked()
{
    if (!scene) return;
    worldmodes::clearRayonOverrides(scene);
    rebuild();
}
