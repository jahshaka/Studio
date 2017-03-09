/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "emitterpropertywidget.h"

#include "materialpropertywidget.h"
#include "../texturepickerwidget.h"
#include "../hfloatsliderwidget.h"
#include "../checkboxwidget.h"
#include "../comboboxwidget.h"

#include "../../irisgl/src/graphics/texture2d.h"
#include "../../irisgl/src/scenegraph/meshnode.h"
#include "../../irisgl/src/scenegraph/particlesystemnode.h"
#include "../../irisgl/src/materials/defaultmaterial.h"

EmitterPropertyWidget::EmitterPropertyWidget()
{
    // @TODO: constants file...
    billboardImage  = this->addTexturePicker("Particle Image");
    emissionRate    = this->addFloatValueSlider("Emission Rate", 0, 512);
    particleLife    = this->addFloatValueSlider("Lifetime", 0, 32);
    particleScale   = this->addFloatValueSlider("Particle Scale", 0, 8);
    lifeFactor      = this->addFloatValueSlider("Random Lifetime", 0, 1);
    speedFactor     = this->addFloatValueSlider("Random Speed", 0, 1);
    scaleFactor     = this->addFloatValueSlider("Random Scale", 0, 1);
    gravityFactor   = this->addFloatValueSlider("Gravity Effect", 0, 1);
    velocityFactor  = this->addFloatValueSlider("Velocity", 0, 32);
    randomRotation  = this->addCheckBox("Random Rotation", true);
    sortOrder       = this->addCheckBox("Flip Sort Order", false);
    dissipate       = this->addCheckBox("Dissipate Over Time", false);
    dissipateInv    = this->addCheckBox("Invert dissipation", false);
    useAdditive     = this->addCheckBox("Use Additive Blending", false);
    blendMode       = this->addComboBox("Col Box");
    preset          = this->addComboBox("Particle Preset");

    // @TODO
    blendMode->addItem("Premultiplied Alpha");
    blendMode->addItem("Additive");

    preset->addItem("Fire");
    preset->addItem("Smoke");
    preset->addItem("Rain");
    preset->addItem("Snow");
    preset->addItem("Steady Flow");
    preset->addItem("Chaos");
    preset->addItem("Sparks");

    connect(emissionRate,   SIGNAL(valueChanged(float)), SLOT(onEmissionRateChanged(float)));
    connect(particleLife,   SIGNAL(valueChanged(float)), SLOT(onParticleLifeChanged(float)));
    connect(particleScale,  SIGNAL(valueChanged(float)), SLOT(onParticleScaleChanged(float)));
    connect(lifeFactor,     SIGNAL(valueChanged(float)), SLOT(onLifeFactorChanged(float)));
    connect(speedFactor,    SIGNAL(valueChanged(float)), SLOT(onSpeedFactorChanged(float)));
    connect(scaleFactor,    SIGNAL(valueChanged(float)), SLOT(onScaleFactorChanged(float)));
    connect(gravityFactor,  SIGNAL(valueChanged(float)), SLOT(onGravityFactorChanged(float)));
    connect(velocityFactor, SIGNAL(valueChanged(float)), SLOT(onVelocityFactorChanged(float)));
    connect(randomRotation, SIGNAL(valueChanged(bool)),  SLOT(onRandomRotation(bool)));
    connect(sortOrder,      SIGNAL(valueChanged(bool)),  SLOT(onSortOrderChanged(bool)));
    connect(dissipate,      SIGNAL(valueChanged(bool)),  SLOT(onDissipateChanged(bool)));
    connect(dissipateInv,   SIGNAL(valueChanged(bool)),  SLOT(onDissipateInvChanged(bool)));
    connect(useAdditive,    SIGNAL(valueChanged(bool)),  SLOT(onAdditiveChanged(bool)));
    connect(billboardImage, SIGNAL(valueChanged(QString)),
            this,           SLOT(onBillboardImageChanged(QString)));
}

EmitterPropertyWidget::~EmitterPropertyWidget()
{

}

void EmitterPropertyWidget::onBillboardImageChanged(QString image)
{
    if (!image.isEmpty() || !image.isNull()) {
        ps->texture = iris::Texture2D::load(image);
    }
}

void EmitterPropertyWidget::setSceneNode(QSharedPointer<iris::SceneNode> sceneNode)
{
    if (!!sceneNode && sceneNode->getSceneNodeType() == iris::SceneNodeType::ParticleSystem) {
        this->ps = sceneNode.staticCast<iris::ParticleSystemNode>();

        emissionRate->setValue(ps->particlesPerSecond);
        particleLife->setValue(ps->lifeLength);
        gravityFactor->setValue(ps->gravityComplement);
        velocityFactor->setValue(ps->speed);
        speedFactor->setValue(ps->speedFactor);
        lifeFactor->setValue(ps->lifeFactor);
        particleScale->setValue(ps->particleScale);
        scaleFactor->setValue(ps->scaleFactor);
        randomRotation->setValue(ps->randomRotation);
        useAdditive->setValue(ps->useAdditive);
        dissipate->setValue(ps->dissipate);
        dissipateInv->setValue(ps->dissipateInv);

        if (ps->texture) {
            billboardImage->setTexture(ps->texture->getSource());
        }

    } else {
        this->ps.clear();
    }
}

void EmitterPropertyWidget::onEmissionRateChanged(float val)
{
    if (!!this->ps) {
        ps->particlesPerSecond = val;
    }
}

void EmitterPropertyWidget::onParticleLifeChanged(float val)
{
    if (!!this->ps) {
        ps->lifeLength = val;
    }
}

void EmitterPropertyWidget::onParticleScaleChanged(float val)
{
    if (!!this->ps) {
        ps->particleScale = val;
    }
}

void EmitterPropertyWidget::onLifeFactorChanged(float val)
{
    if (!!this->ps) {
        ps->setLifeError(val);
    }
}

void EmitterPropertyWidget::onSpeedFactorChanged(float val)
{
    if (!!this->ps) {
        ps->setSpeedError(val);
    }
}

void EmitterPropertyWidget::onScaleFactorChanged(float val)
{
    if (!!this->ps) {
        ps->setScaleError(val);
    }
}

void EmitterPropertyWidget::onGravityFactorChanged(float val)
{
    if (!!this->ps) {
        ps->gravityComplement = val;
    }
}

void EmitterPropertyWidget::onVelocityFactorChanged(float val)
{
    if (!!this->ps) {
        ps->speed = val;
    }
}

void EmitterPropertyWidget::onRandomRotation(bool val)
{
    if (!!this->ps) {
        ps->randomRotation = val;
    }
}

void EmitterPropertyWidget::onSortOrderChanged(bool val)
{

}

void EmitterPropertyWidget::onDissipateChanged(bool val)
{
    if (!!this->ps) {
        ps->dissipate = val;
    }
}

void EmitterPropertyWidget::onDissipateInvChanged(bool val)
{
    if (!!this->ps) {
        ps->dissipateInv = val;
    }
}

void EmitterPropertyWidget::onAdditiveChanged(bool val)
{
    if (!!this->ps) {
        ps->setBlendMode(val);
    }
}


