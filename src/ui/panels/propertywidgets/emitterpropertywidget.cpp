/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/panels/propertywidgets/emitterpropertywidget.h"
#include "data/project.h"

#include "ui/controls/texturepickerwidget.h"
#include "ui/controls/hfloatsliderwidget.h"
#include "ui/controls/checkboxwidget.h"
#include "ui/controls/comboboxwidget.h"
#include "ui/controls/labelwidget.h"
#include "ui/controls/particlerampwidget.h"

#include "irisgl/document/assets/texture2d.h"
#include "irisgl/document/scenegraph/particlesystemnode.h"

#include "data/database/database.h"

#include <QJsonObject>
#include <QVariant>
#include <algorithm>

#include "io/scenewriter.h"

namespace {

/// Splits a linear HDR channel triple into a displayable 0-255 swatch plus the
/// factor it was scaled by. A 4.0/1.6/0.35 fire core comes back as an orange
/// swatch with intensity 4 — which is editable, where a clamped white swatch
/// would not be.
ParticleRampStop toStop(const iris::ParticleColourKey &k)
{
    ParticleRampStop s;
    s.time = k.time;
    const float peak = std::max({ k.r, k.g, k.b, 1.0f });
    s.intensity = peak;
    s.colour = QColor::fromRgbF(qBound(0.0f, k.r / peak, 1.0f),
                                qBound(0.0f, k.g / peak, 1.0f),
                                qBound(0.0f, k.b / peak, 1.0f),
                                qBound(0.0f, k.a, 1.0f));
    return s;
}

iris::ParticleColourKey fromStop(const ParticleRampStop &s)
{
    iris::ParticleColourKey k;
    k.time = s.time;
    k.r = float(s.colour.redF()) * s.intensity;
    k.g = float(s.colour.greenF()) * s.intensity;
    k.b = float(s.colour.blueF()) * s.intensity;
    k.a = float(s.colour.alphaF());
    return k;
}

QString prettyPreset(const QString &id)
{
    if (id == "steadyFlow") return QStringLiteral("Steady Flow");
    QString s = id;
    if (!s.isEmpty()) s[0] = s[0].toUpper();
    return s;
}

}  // namespace

EmitterPropertyWidget::EmitterPropertyWidget()
{
    // ---- group 1: EMITTER — what is born, and where ------------------------
    addLabel(QStringLiteral("Emitter"),
             QStringLiteral("What is born, and where."));

    preset = addComboBox("Preset");
    for (const QString &name : iris::ParticleSystemNode::presetNames())
        preset->addItem(prettyPreset(name), name);
    preset->getWidget()->setToolTip(QStringLiteral(
        "A whole recipe in one click — rate, velocity, lifetime, size, cone, forces, "
        "turbulence, blend mode, quota and both over-life ramps. It leaves the node's "
        "name, transform and image alone. Emissive recipes (Fire, Embers, Sparks) carry "
        "HDR colours and only look right with HDR and Bloom on in World settings."));

    shape = addComboBox("Shape");
    shape->addItem("Point", "point");
    shape->addItem("Box", "box");
    shape->addItem("Cylinder", "cylinder");
    shape->addItem("Ellipsoid", "ellipsoid");
    shape->addItem("Hollow Ellipsoid", "hollowEllipsoid");
    shape->addItem("Ring", "ring");
    shape->getWidget()->setToolTip(QStringLiteral(
        "The volume particles spawn inside. NOTE the node's SCALE does not resize it — "
        "the extents below are the spawn volume, in world units."));

    emissionRate   = addFloatValueSlider("Emission Rate", 0, 1024);
    particleLife   = addFloatValueSlider("Lifetime", 0.05f, 32);
    lifeFactor     = addFloatValueSlider("Random Lifetime", 0, 1);
    velocityFactor = addFloatValueSlider("Velocity", 0, 32);
    speedFactor    = addFloatValueSlider("Random Velocity", 0, 1);
    particleScale  = addFloatValueSlider("Particle Scale", 0, 8);
    coneAngle      = addFloatValueSlider("Cone Angle", 0, 180);
    quota          = addFloatValueSlider("Max Particles", 0, 16000);
    quota->setDecimals(0);

    coneAngle->setToolTip(QStringLiteral(
        "How wide the emission cone opens around the emitter's axis, in degrees. "
        "0 is a beam; 180 sprays in every direction."));
    quota->setToolTip(QStringLiteral(
        "The hard cap on live particles. It is REAL now — the renderer allocates a pool "
        "of exactly this size and refuses to exceed it. 0 lets the renderer pick (1024). "
        "Changing it rebuilds the emitter's definition, so nudge it, do not sweep it."));
    particleScale->setToolTip(QStringLiteral(
        "The size a particle is born at, in world units. The node's scale does not "
        "multiply it any more."));

    extentX = addFloatValueSlider("Extent X", 0, 32);
    extentY = addFloatValueSlider("Extent Y", 0, 32);
    extentZ = addFloatValueSlider("Extent Z", 0, 32);
    innerX  = addFloatValueSlider("Inner X", 0, 0.99f);
    innerY  = addFloatValueSlider("Inner Y", 0, 0.99f);
    innerX->setToolTip(QStringLiteral(
        "Ring and Hollow Ellipsoid only: the hole, as a fraction of the extent."));

    burstDuration = addFloatValueSlider("Burst Duration", 0, 30);
    burstRepeat   = addFloatValueSlider("Burst Repeat", 0, 30);
    startDelay    = addFloatValueSlider("Start Delay", 0, 30);
    burstDuration->setToolTip(QStringLiteral(
        "0 emits forever. Above 0, the emitter runs for this many seconds, then waits "
        "Burst Repeat seconds and runs again."));

    // ---- group 2: OVER LIFE — what happens to it ---------------------------
    addLabel(QStringLiteral("Over Life"),
             QStringLiteral("Per particle, by life fraction — this is what makes fire."));

    colourRamp = new ParticleColourRampWidget();
    addWidgetToContent(colourRamp);
    scaleRamp = new ParticleScaleRampWidget();
    addWidgetToContent(scaleRamp);

    dissipate    = addCheckBox("Shrink Over Time", false);
    dissipateInv = addCheckBox("Grow Over Time", false);
    dissipate->setToolTip(QStringLiteral(
        "A shortcut for a scale ramp 1 -> 0. An explicit scale ramp above wins."));

    // ---- group 3: FORCES & LOOK -------------------------------------------
    addLabel(QStringLiteral("Forces & Look"),
             QStringLiteral("What pushes a particle around, and how it draws."));

    gravityFactor = addFloatValueSlider("Gravity Effect", 0, 1);
    windX = addFloatValueSlider("Wind X", -20, 20);
    windY = addFloatValueSlider("Wind Y", -20, 20);
    windZ = addFloatValueSlider("Wind Z", -20, 20);
    windY->setToolTip(QStringLiteral(
        "A constant force on top of gravity. Positive Y is buoyancy — what makes flame "
        "and smoke rise instead of fall."));
    turbulence = addFloatValueSlider("Turbulence", 0, 16);
    turbulence->setToolTip(QStringLiteral(
        "Random velocity perturbation each frame: the flicker. Costs simulation time "
        "even at low values, so 0 removes it entirely rather than running it neutral."));

    randomRotation = addCheckBox("Random Start Angle", true);
    spinMin = addFloatValueSlider("Spin Min", -360, 360);
    spinMax = addFloatValueSlider("Spin Max", -360, 360);
    spinMin->setToolTip(QStringLiteral(
        "Degrees per second, picked per particle between Min and Max."));

    orientation = addComboBox("Orientation");
    orientation->addItem("Billboard", "billboard");
    orientation->addItem("Stretched (common)", "stretchedCommon");
    orientation->addItem("Stretched (velocity)", "stretchedVelocity");
    orientation->addItem("Perpendicular (common)", "perpendicularCommon");
    orientation->addItem("Perpendicular (velocity)", "perpendicularVelocity");
    orientation->getWidget()->setToolTip(QStringLiteral(
        "Billboard always faces the camera. Stretched (velocity) streaks the quad along "
        "the particle's own direction — sparks, rain, embers."));

    billboardImage = addTexturePicker("Particle Image");
    useAdditive = addCheckBox("Additive Blending", false);
    alphaHash   = addCheckBox("Order-independent Alpha", true);
    useAdditive->setToolTip(QStringLiteral(
        "Additive is for anything that GLOWS — fire, sparks, magic. It is also "
        "order-independent, which matters because particles are never sorted."));
    alphaHash->setToolTip(QStringLiteral(
        "Alpha-blended particles only. Stochastic transparency, so unsorted smoke stops "
        "showing draw-order artefacts. Resolves cleanly with anti-aliasing on; without "
        "it the dither is visible."));

    // ---- wiring -----------------------------------------------------------
    // Every scalar row goes through setPropertyValue, which is the exact call
    // node.setProperty makes. One code path, two front ends.
    connect(preset, SIGNAL(currentIndexChanged(QString)), SLOT(onPresetChanged(QString)));
    connect(billboardImage, SIGNAL(valueChanged(QString)), SLOT(onBillboardImageChanged(QString)));
    // currentIndexChanged is OVERLOADED on ComboBoxWidget (int and QString), so
    // the pointer-to-member form is ambiguous — qOverload picks the int one.
    connect(shape, qOverload<int>(&ComboBoxWidget::currentIndexChanged), this, [this](int) {
        set("shape", shape->getCurrentItemData());
        updateShapeRows();
    });
    connect(orientation, qOverload<int>(&ComboBoxWidget::currentIndexChanged), this,
            [this](int) { set("orientation", orientation->getCurrentItemData()); });

    auto bindFloat = [this](HFloatSliderWidget *w, const char *field) {
        connect(w, &HFloatSliderWidget::valueChanged, this,
                [this, field](float v) { set(field, v); });
    };
    bindFloat(emissionRate,  "particlesPerSecond");
    bindFloat(particleLife,  "lifeLength");
    bindFloat(velocityFactor,"speed");
    bindFloat(particleScale, "particleScale");
    bindFloat(coneAngle,     "coneAngle");
    bindFloat(gravityFactor, "gravityComplement");
    bindFloat(turbulence,    "turbulence");
    bindFloat(spinMin,       "rotationSpeedMin");
    bindFloat(spinMax,       "rotationSpeedMax");
    bindFloat(burstDuration, "burstDuration");
    bindFloat(burstRepeat,   "burstRepeatDelay");
    bindFloat(startDelay,    "startDelay");
    // The three "Random ..." sliders are FRACTIONS of the mean; the node holds
    // the absolute spread, and its setters do the conversion.
    connect(lifeFactor, &HFloatSliderWidget::valueChanged, this, [this](float v) {
        if (!mLoading && ps) ps->setLifeError(v);
    });
    connect(speedFactor, &HFloatSliderWidget::valueChanged, this, [this](float v) {
        if (!mLoading && ps) ps->setSpeedError(v);
    });
    connect(quota, &HFloatSliderWidget::valueChanged, this,
            [this](float v) { set("maxParticles", int(v)); });

    auto bindVec = [this](HFloatSliderWidget *x, HFloatSliderWidget *y,
                          HFloatSliderWidget *z, const char *field) {
        auto push = [this, x, y, z, field]() {
            if (mLoading || !ps) return;
            set(field, QVariant::fromValue(QVector3D(x->getValue(), y->getValue(),
                                                     z ? z->getValue() : 0.0f)));
        };
        connect(x, &HFloatSliderWidget::valueChanged, this, [push](float) { push(); });
        connect(y, &HFloatSliderWidget::valueChanged, this, [push](float) { push(); });
        if (z) connect(z, &HFloatSliderWidget::valueChanged, this, [push](float) { push(); });
    };
    bindVec(extentX, extentY, extentZ, "extents");
    bindVec(innerX, innerY, nullptr, "innerExtents");
    bindVec(windX, windY, windZ, "wind");

    auto bindBool = [this](CheckBoxWidget *w, const char *field) {
        connect(w, &CheckBoxWidget::valueChanged, this,
                [this, field](bool v) { set(field, v); });
    };
    bindBool(randomRotation, "randomRotation");
    bindBool(dissipate,      "dissipate");
    bindBool(dissipateInv,   "dissipateInv");
    bindBool(useAdditive,    "blendMode");
    bindBool(alphaHash,      "alphaHash");

    connect(colourRamp, &ParticleColourRampWidget::changed,
            this, &EmitterPropertyWidget::pushColourKeys);
    connect(scaleRamp, &ParticleScaleRampWidget::changed,
            this, &EmitterPropertyWidget::pushScaleKeys);
}

EmitterPropertyWidget::~EmitterPropertyWidget() = default;

void EmitterPropertyWidget::set(const char *field, const QVariant &value)
{
    if (mLoading || !ps) return;
    ps->setPropertyValue(QString::fromLatin1(field), value);
}

void EmitterPropertyWidget::onPresetChanged(const QString &name)
{
    if (mLoading || !ps) return;
    // A whole recipe, in one step — then re-read every control, because the
    // recipe just overwrote almost all of them.
    ps->applyPreset(iris::ParticleSystemNode::presetFromName(preset->getCurrentItemData()));
    refresh();
}

void EmitterPropertyWidget::pushColourKeys()
{
    if (mLoading || !ps) return;
    QVector<ParticleRampStop> stops = colourRamp->stops();
    std::stable_sort(stops.begin(), stops.end(),
                     [](const ParticleRampStop &a, const ParticleRampStop &b) {
                         return a.time < b.time;
                     });
    QVector<iris::ParticleColourKey> keys;
    for (const ParticleRampStop &s : stops) keys.append(fromStop(s));
    ps->colourKeys = keys;
}

void EmitterPropertyWidget::pushScaleKeys()
{
    if (mLoading || !ps) return;
    QVector<ParticleScaleStop> stops = scaleRamp->stops();
    std::stable_sort(stops.begin(), stops.end(),
                     [](const ParticleScaleStop &a, const ParticleScaleStop &b) {
                         return a.time < b.time;
                     });
    QVector<iris::ParticleScaleKey> keys;
    for (const ParticleScaleStop &s : stops) {
        iris::ParticleScaleKey k; k.time = s.time; k.scale = s.scale;
        keys.append(k);
    }
    ps->scaleKeys = keys;
}

void EmitterPropertyWidget::updateShapeRows()
{
    const bool area = ps && ps->shape != iris::ParticleEmitterShape::Point;
    const bool hollow = ps && (ps->shape == iris::ParticleEmitterShape::Ring ||
                               ps->shape == iris::ParticleEmitterShape::HollowEllipsoid);
    for (auto *w : { extentX, extentY, extentZ }) w->setVisible(area);
    for (auto *w : { innerX, innerY }) w->setVisible(hollow);
}

void EmitterPropertyWidget::refresh()
{
    if (!ps) return;
    mLoading = true;

    preset->setCurrentItemData(iris::ParticleSystemNode::presetName(ps->preset));
    shape->setCurrentItemData(iris::ParticleSystemNode::shapeName(ps->shape));
    orientation->setCurrentItemData(iris::ParticleSystemNode::orientationName(ps->orientation));

    emissionRate->setValue(ps->particlesPerSecond);
    particleLife->setValue(ps->lifeLength);
    velocityFactor->setValue(ps->speed);
    particleScale->setValue(ps->particleScale);
    coneAngle->setValue(ps->coneAngle);
    quota->setValue(float(ps->maxParticles));
    // Absolute spread in, fraction out — the sliders have always spoken
    // fractions and the model has always stored absolutes.
    lifeFactor->setValue(ps->lifeErrorFraction());
    speedFactor->setValue(ps->speedErrorFraction());

    extentX->setValue(ps->extents.x());
    extentY->setValue(ps->extents.y());
    extentZ->setValue(ps->extents.z());
    innerX->setValue(ps->innerExtents.x());
    innerY->setValue(ps->innerExtents.y());

    burstDuration->setValue(ps->burstDuration);
    burstRepeat->setValue(ps->burstRepeatDelay);
    startDelay->setValue(ps->startDelay);

    gravityFactor->setValue(ps->gravityComplement);
    windX->setValue(ps->wind.x());
    windY->setValue(ps->wind.y());
    windZ->setValue(ps->wind.z());
    turbulence->setValue(ps->turbulence);
    spinMin->setValue(ps->rotationSpeedMin);
    spinMax->setValue(ps->rotationSpeedMax);

    randomRotation->setValue(ps->randomRotation);
    dissipate->setValue(ps->dissipate);
    dissipateInv->setValue(ps->dissipateInv);
    useAdditive->setValue(ps->useAdditive);
    alphaHash->setValue(ps->alphaHash);

    QVector<ParticleRampStop> colourStops;
    for (const iris::ParticleColourKey &k : ps->colourKeys) colourStops.append(toStop(k));
    colourRamp->setStops(colourStops);

    QVector<ParticleScaleStop> scaleStops;
    for (const iris::ParticleScaleKey &k : ps->scaleKeys)
        scaleStops.append(ParticleScaleStop{ k.time, k.scale });
    scaleRamp->setStops(scaleStops);

    if (ps->texture) billboardImage->setTexture(ps->texture->getSource());

    updateShapeRows();
    mLoading = false;
}

void EmitterPropertyWidget::setSceneNode(QSharedPointer<iris::SceneNode> sceneNode)
{
    if (!!sceneNode && sceneNode->getSceneNodeType() == iris::SceneNodeType::ParticleSystem) {
        ps = sceneNode.staticCast<iris::ParticleSystemNode>();
        refresh();
    } else {
        ps.clear();
    }
}

void EmitterPropertyWidget::onBillboardImageChanged(QString image)
{
    if (mLoading || !ps || image.isEmpty()) return;
    ps->texture = iris::Texture2D::load(image);

    QJsonObject particleDef;
    SceneWriter::writeParticleData(particleDef, ps);

    auto textureGuid = particleDef.value("texture").toString();
    if (!textureGuid.isEmpty()) {
        particleDef["texture"] = textureGuid;

        db->updateAssetAsset(ps->getGUID(), QJsonDocument(particleDef).toJson());
        db->removeDependenciesByType(ps->getGUID(), ModelTypes::Texture);
        db->createDependency(
            static_cast<int>(ModelTypes::ParticleSystem),
            static_cast<int>(ModelTypes::Texture),
            ps->getGUID(), textureGuid,
            project->getProjectGuid()
        );
    }
}
