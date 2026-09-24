/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef WORLDSHADOWPROPERTYWIDGET_H
#define WORLDSHADOWPROPERTYWIDGET_H

#include <QWidget>
#include <functional>
#include "ui/controls/accordionbladewidget.h"
#include "ui/panels/propertywidgets/panelundo.h"
#include "irisgl/irisglfwd.h"

namespace iris { struct SunContact; }
class CheckBoxWidget;
class ComboBoxWidget;
class HFloatSliderWidget;
class LabelWidget;
class IEditorViewport;
struct StudioServices;

/**
 * World-panel "Shadows" section (VISUAL_PARITY_SPEC item 2, option A).
 *
 * The renderer keeps ONE shadow atlas for every light in the scene, sized from
 * a single base resolution (PSSM split 0 and the two focused maps at RxR, the
 * remaining splits at R/2 — an RxR*3.5 D32_FLOAT allocation). The Light panel's
 * per-light "Shadow Size" is therefore only a request: whichever light asks for
 * the most wins. This row makes that truth settable and visible.
 *
 * "Auto" (scene->shadowResolution == 0) keeps the historical derive-from-lights
 * behaviour and shows what it derived; an explicit choice overrides it. The
 * VRAM cost of the choice is spelled out beside it, because 4096 is a quarter
 * of a gigabyte and the difference is not otherwise visible until a machine
 * with less memory tries to open the scene.
 *
 * The document field is the API: SceneMirror pushes it to the engine each
 * frame, exactly the path world.setShadowResolution() takes.
 *
 * SUN CONTACT (PHOTON-RAYS-1's Studio slice, SMALL-FIXES-3): three rows —
 * the switch, the range in metres, the resolution — over the ONE document
 * block `iris::Scene::sunContact`, written through the sceneprops key
 * "sunContact" that world.sunContact writes, with the verb's clamp
 * (iris::SunContact::clamped) and the verb's band on the range control, so
 * the panel and the verb are one model and every gesture is one undo step.
 * What the renderer did with it is world.sunContact().live, read here from the
 * same engine calls: a scene that is not ray traced on this machine greys the
 * rows that cannot act and says why.
 */
class WorldShadowPropertyWidget : public AccordianBladeWidget
{
    Q_OBJECT

public:
    WorldShadowPropertyWidget();
    void setScene(QSharedPointer<iris::Scene> scene);
    /// The live viewport, for the applied-resolution readback. Nullable
    /// (headless): without it the widget shows what the document asked for.
    void setSceneView(IEditorViewport *sceneView);

    /// VRAM the atlas costs at a given base resolution, in MB: the engine
    /// allocates R wide x 3.5R tall at 32-bit depth. Static so the verb docs and
    /// the tests can quote the same number.
    static int atlasMegabytes(int resolution);
    /// The undo stack: shadow resolution is a quality-registry row, so an edit
    /// is one WorldModeCommand (value + pin). Nullable.
    void setServices(StudioServices *s) { services = s; }

protected slots:
    void onQualityChanged(int row);

private:
    /// Built ONCE; selection and undo refresh the same rows in place (debt L6).
    void build();
    void refreshRows();
    /// The Sun Contact rows' half of refreshRows: the document block, then the
    /// renderer's answer (world.sunContact().live's source).
    void refreshSunContact();
    /// Two frames, then refreshRows — after a Sun Contact gesture and after
    /// every undo/redo of one.
    void contactEdited();
    /// The whole block with ONE field changed, as the "sunContact" key stores
    /// it — the verb's clamp applied.
    QVariant withContact(const std::function<void(iris::SunContact &)> &edit) const;
    /// What Auto would derive right now: the largest Shadow Size among the
    /// scene's shadow-casting lights (the mirror's own policy), or 0 if none.
    int derivedFromLights() const;

    QSharedPointer<iris::Scene> scene;
    IEditorViewport *sceneView = nullptr;
    StudioServices *services = nullptr;
    bool loading = false;
    ComboBoxWidget *qualitySelector = nullptr;
    /// The three read-back rows: what Auto resolved to, what the atlas costs,
    /// and how many casters actually got a map. Present from the start, hidden
    /// when they have nothing to say.
    LabelWidget *autoRow = nullptr;
    LabelWidget *memoryRow = nullptr;
    LabelWidget *mapsRow = nullptr;
    /// THE SUN (SUN_AND_LIGHT_DEFAULTS Q1/Q1d): which directional light is this
    /// scene's sun and why, and — when there is more than one — that the others
    /// cast nothing. "No sun" is a legal, common answer and is said plainly,
    /// never as a warning: an interior lit by lamps is an ordinary scene.
    LabelWidget *sunRow = nullptr;
    LabelWidget *secondaryRow = nullptr;

    /// SUN CONTACT: the switch, the range (metres, the verb's band) and the
    /// resolution (auto / full / half — the verb's words), plus the renderer's
    /// answer in words. Row keys `sunContact.enabled` / `.range` /
    /// `.resolution` / `.status` (the property filter and editor.propertyRow).
    panelundo::SceneRows contactRows;
    CheckBoxWidget *contactEnabled = nullptr;
    HFloatSliderWidget *contactRange = nullptr;
    ComboBoxWidget *contactResolution = nullptr;
    LabelWidget *contactStatus = nullptr;
};

#endif // WORLDSHADOWPROPERTYWIDGET_H
