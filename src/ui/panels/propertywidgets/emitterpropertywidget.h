/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef EMITTERPROPERTYWIDGET_H
#define EMITTERPROPERTYWIDGET_H

// The emitter panel (PARTICLES_FX2_SPEC.md §6). One blade, three groups by
// reading order: EMITTER (what is born and where), OVER LIFE (what happens to
// it), FORCES & LOOK (what pushes it and how it draws).
//
// Every scalar row writes through ParticleSystemNode::setPropertyValue — the
// exact call `node.setProperty` makes — so the panel and the scripting verb are
// provably one code path, not two implementations of one idea. The two ramps
// and the preset go through the same methods particles.setColourKeys /
// particles.preset call.
//
// Three rows are GONE, deliberately (§5):
//   * "Flip Sort Order" — the renderer never sorts particles, and the slot was
//     empty for ten years;
//   * "Col Box" — a dead combo superseded by the colour ramp;
//   * "Random Scale" — the renderer's emitters carry ONE fixed dimension pair;
//     there is no per-particle random birth size to drive. The field is still
//     read and written so old scenes round-trip, it simply has no row.

#include <QWidget>
#include <QSharedPointer>
#include "irisgl/irisglfwd.h"

#include "ui/controls/accordionbladewidget.h"

namespace iris {
    class SceneNode;
}

class Database;
class ParticleColourRampWidget;
class ParticleScaleRampWidget;

class EmitterPropertyWidget : public AccordianBladeWidget
{
    Q_OBJECT

public:
    EmitterPropertyWidget();
    ~EmitterPropertyWidget();

    void setDatabase(Database *db) { this->db = db; }
    void setSceneNode(iris::SceneNodePtr sceneNode);

protected slots:
    void onPresetChanged(const QString &name);
    void onBillboardImageChanged(QString);

private:
    /// Pushes one reflected field onto the node — the same call the scripting
    /// verb makes. `mLoading` suppresses it while setSceneNode fills controls.
    void set(const char *field, const QVariant &value);
    /// Re-reads every control from the node. Used by setSceneNode and after a
    /// preset stamps a whole recipe.
    void refresh();
    void pushColourKeys();
    void pushScaleKeys();
    /// Shows only the rows the current emitter shape actually uses.
    void updateShapeRows();

    iris::ParticleSystemNodePtr ps;
    bool mLoading = false;

    ComboBoxWidget      *preset = nullptr;
    ComboBoxWidget      *shape = nullptr;
    ComboBoxWidget      *orientation = nullptr;
    TexturePickerWidget *billboardImage = nullptr;

    HFloatSliderWidget *emissionRate = nullptr;
    HFloatSliderWidget *particleLife = nullptr;
    HFloatSliderWidget *lifeFactor = nullptr;
    HFloatSliderWidget *velocityFactor = nullptr;
    HFloatSliderWidget *speedFactor = nullptr;
    HFloatSliderWidget *particleScale = nullptr;
    HFloatSliderWidget *coneAngle = nullptr;
    HFloatSliderWidget *quota = nullptr;

    HFloatSliderWidget *extentX = nullptr;
    HFloatSliderWidget *extentY = nullptr;
    HFloatSliderWidget *extentZ = nullptr;
    HFloatSliderWidget *innerX = nullptr;
    HFloatSliderWidget *innerY = nullptr;

    HFloatSliderWidget *burstDuration = nullptr;
    HFloatSliderWidget *burstRepeat = nullptr;
    HFloatSliderWidget *startDelay = nullptr;

    ParticleColourRampWidget *colourRamp = nullptr;
    ParticleScaleRampWidget  *scaleRamp = nullptr;

    HFloatSliderWidget *gravityFactor = nullptr;
    HFloatSliderWidget *windX = nullptr;
    HFloatSliderWidget *windY = nullptr;
    HFloatSliderWidget *windZ = nullptr;
    HFloatSliderWidget *turbulence = nullptr;
    HFloatSliderWidget *spinMin = nullptr;
    HFloatSliderWidget *spinMax = nullptr;

    CheckBoxWidget *randomRotation = nullptr;
    CheckBoxWidget *dissipate = nullptr;
    CheckBoxWidget *dissipateInv = nullptr;
    CheckBoxWidget *useAdditive = nullptr;
    CheckBoxWidget *alphaHash = nullptr;

    Database *db = nullptr;
};

#endif // EMITTERPROPERTYWIDGET_H
