/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef WORLDGIPROPERTYWIDGET_H
#define WORLDGIPROPERTYWIDGET_H

#include "irisgl/core/math/vec.h"
#include <QWidget>
#include "commands/worldmodecommand.h"
#include "ui/controls/accordionbladewidget.h"
#include "irisgl/irisglfwd.h"

class ComboBoxWidget;
class HFloatSliderWidget;
class CheckBoxWidget;
class DragVector3Widget;
class QPushButton;
struct StudioServices;

/**
 * World-panel "Global Illumination" section — RAYON (GI_UNIFIED_SPEC.md §2).
 *
 * THREE VISIBLE ROWS and nothing else: the Rayon switch, the quality tier, and
 * the GI update budget. Everything the tier consumes — the technique picker,
 * the voxel/probe quality, bounces, the lit volume, the probe grid, the
 * irradiance field and its intensity — moves under an "Advanced" disclosure,
 * where each row still works exactly as it did and each edit still PINS itself
 * (worldmodes' override model: a pin survives tier switches, and the tier row
 * says "Custom" while one deviates).
 *
 * Nothing was deleted to get here. The tier is a registry row like every other
 * quality row, so this panel writes through worldmodes and the same state is
 * reachable from world.rayon, world.gi, world.settings and world.override.
 *
 * UNDO (rayontiers review follow-up, 2026-09-10): the two Epic-column sliders
 * (Light Bounces, Dynamic Probes) are undoable through WorldModeCommand, ONE
 * step per gesture — the snapshot is taken on the slider's valueChangeStart
 * (press, or the first typed step), every tick writes through live so the
 * viewport follows the drag, and the command is pushed on valueChangeEnd
 * (release / editingFinished). Those ticks refresh the pin mark, the reset
 * button and the tier row's "Custom" entry IN PLACE (refreshPins) rather than
 * rebuilding the panel, which would destroy the slider mid-drag. The rest of
 * this panel's slots are the older shape (write, then rebuild, no undo step);
 * converting them is debt L6's job, not this widget's.
 */
class WorldGiPropertyWidget : public AccordianBladeWidget
{
    Q_OBJECT

public:
    WorldGiPropertyWidget();
    void setScene(QSharedPointer<iris::Scene> scene);
    /// The services aggregate, for the undo stack. Nullable (headless hosts,
    /// the panel suite without a stack): the edits still apply, just without
    /// an undo step.
    void setServices(StudioServices *services) { this->services = services; }

protected slots:
    void onRayonToggled(bool on);
    void onTierChanged(int row);
    void onAdvancedToggled(bool on);
    void modeChanged(int row);
    void onQualityChanged(int row);
    void onLightChanged(int row);
    void onBouncesChanged(float value);
    void onDynamicProbesChanged(float value);
    void onBoundsMinChanged(iris::Vec3 value);
    void onBoundsMaxChanged(iris::Vec3 value);
    void onPccGridChanged(iris::Vec3 value);
    void onUpdateBudgetChanged(float value);
    void onDdgiToggled(bool on);
    void onDdgiIntensityChanged(float value);
    void onDdgiAmbientChanged(float value);
    void onDdgiSourceChanged(int index);
    void onFitBoundsClicked();
    void onResetAdvancedClicked();

private:
    void rebuild();

    /// One Rayon-tiered Int row edit: write-through + pin, in-place refresh,
    /// and — when no drag/typing session brackets it — its own undo step.
    void editRayonRow(const QString &id, int value, const QString &text);
    void beginRayonEdit();
    void endRayonEdit(const QString &text);
    /// Wires a rayonTiered slider: start/tick/end -> snapshot/write/push.
    void wireRayonSlider(HFloatSliderWidget *slider,
                         void (WorldGiPropertyWidget::*tick)(float), const QString &text);
    /// Re-reads the pin marks, the tier row's "Custom" entry and the reset
    /// button from the document WITHOUT rebuilding the rows.
    void refreshPins();
    bool advancedResettable() const;
    void addResetAdvancedButton();

    QSharedPointer<iris::Scene> scene;
    StudioServices *services = nullptr;
    WorldModeCommand::Snapshot editBefore;
    bool editing = false;
    CheckBoxWidget *rayonSwitch = nullptr;
    ComboBoxWidget *tierSelector = nullptr;
    ComboBoxWidget *modeSelector = nullptr;
    ComboBoxWidget *quality = nullptr;
    ComboBoxWidget *lightSelector = nullptr;
    HFloatSliderWidget *bounces = nullptr;
    HFloatSliderWidget *dynamicProbes = nullptr;
    DragVector3Widget *boundsMin = nullptr;
    DragVector3Widget *boundsMax = nullptr;
    DragVector3Widget *pccGrid = nullptr;
    HFloatSliderWidget *updateBudget = nullptr;
    CheckBoxWidget *ddgiToggle = nullptr;
    HFloatSliderWidget *ddgiIntensity = nullptr;
    HFloatSliderWidget *ddgiAmbient = nullptr;
    ComboBoxWidget *ddgiSource = nullptr;
    QPushButton *fitBoundsButton = nullptr;
    QPushButton *advancedButton = nullptr;
    QPushButton *resetAdvancedButton = nullptr;
    /// Disclosure state, per session (the panel is rebuilt on every edit).
    bool advancedOpen = false;
};

#endif // WORLDGIPROPERTYWIDGET_H
