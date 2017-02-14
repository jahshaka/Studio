#include "emitterpropertywidget.h"

#include "materialpropertywidget.h"
#include "../texturepicker.h"
#include "../hfloatslider.h"
#include "../checkboxproperty.h"
#include "../combobox.h"

#include "../../irisgl/src/graphics/texture2d.h"
#include "../../irisgl/src/scenegraph/meshnode.h"
#include "../../irisgl/src/materials/defaultmaterial.h"

EmitterPropertyWidget::EmitterPropertyWidget()
{
    // @TODO: constants file...
    billboardImage  = this->addTexturePicker("Particle Image");
    emissionRate    = this->addFloatValueSlider("Emission Rate", 0, 512);
    particleLife    = this->addFloatValueSlider("Lifetime", 0, 32);
    lifeFactor      = this->addFloatValueSlider("Random Lifetime", 0, 1);
    speedFactor     = this->addFloatValueSlider("Random Speed", 0, 1);
    scaleFactor     = this->addFloatValueSlider("Random Scale", 0, 1);
    gravityFactor   = this->addFloatValueSlider("Gravity Effect", 0, 1);
    velocityFactor  = this->addFloatValueSlider("Velocity", 0, 32);
    randomRotation  = this->addCheckBox("Random Rotation", true);
    sortOrder       = this->addCheckBox("Flip Sort Order", false);
    dissipate       = this->addCheckBox("Dissipate Over Time", false);
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
    connect(lifeFactor,     SIGNAL(valueChanged(float)), SLOT(onLifeFactorChanged(float)));
    connect(speedFactor,    SIGNAL(valueChanged(float)), SLOT(onSpeedFactorChanged(float)));
    connect(scaleFactor,    SIGNAL(valueChanged(float)), SLOT(onScaleFactorChanged(float)));
    connect(gravityFactor,  SIGNAL(valueChanged(float)), SLOT(onGravityFactorChanged(float)));
    connect(velocityFactor, SIGNAL(valueChanged(float)), SLOT(onVelocityFactorChanged(float)));
    connect(randomRotation, SIGNAL(valueChanged(bool)),  SLOT(onRandomRotation(bool)));
    connect(sortOrder,      SIGNAL(valueChanged(bool)),  SLOT(onSortOrderChanged(bool)));
    connect(dissipate,      SIGNAL(valueChanged(bool)),  SLOT(onDissipateChanged(bool)));
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
        meshNode->texture = iris::Texture2D::load(image);
    }
}

void EmitterPropertyWidget::setSceneNode(QSharedPointer<iris::SceneNode> sceneNode)
{
    if (!!sceneNode && sceneNode->getSceneNodeType() == iris::SceneNodeType::Emitter) {
        this->meshNode = sceneNode.staticCast<iris::MeshNode>();

        emissionRate->setValue(meshNode->pps);
        particleLife->setValue(meshNode->particleLife);
        gravityFactor->setValue(meshNode->gravity);
        velocityFactor->setValue(meshNode->speed);
        speedFactor->setValue(meshNode->speedFac);
        lifeFactor->setValue(meshNode->lifeFac);
        scaleFactor->setValue(meshNode->scaleFac);
        randomRotation->setValue(meshNode->randomRotation);

        if (meshNode->texture) {
            billboardImage->setTexture(meshNode->texture->getSource());
        }

    } else {
        this->meshNode.clear();
    }
}

void EmitterPropertyWidget::onEmissionRateChanged(float val)
{
    if (!!this->meshNode) {
        meshNode->pps = val;
    }
}

void EmitterPropertyWidget::onParticleLifeChanged(float val)
{
    if (!!this->meshNode) {
        meshNode->particleLife = val;
    }
}

void EmitterPropertyWidget::onLifeFactorChanged(float val)
{
    if (!!this->meshNode) {
        meshNode->lifeFac = val;
    }
}

void EmitterPropertyWidget::onSpeedFactorChanged(float val)
{
    if (!!this->meshNode) {
        meshNode->speedFac = val;
    }
}

void EmitterPropertyWidget::onScaleFactorChanged(float val)
{
    if (!!this->meshNode) {
        meshNode->scaleFac = val;
    }
}

void EmitterPropertyWidget::onGravityFactorChanged(float val)
{
    if (!!this->meshNode) {
        meshNode->gravity = val;
    }
}

void EmitterPropertyWidget::onVelocityFactorChanged(float val)
{
    if (!!this->meshNode) {
        meshNode->speed = val;
    }
}

void EmitterPropertyWidget::onRandomRotation(bool val)
{
    if (!!this->meshNode) {
        meshNode->randomRotation = val;
    }
}

void EmitterPropertyWidget::onSortOrderChanged(bool val)
{

}

void EmitterPropertyWidget::onDissipateChanged(bool val)
{
    if (!!this->meshNode) {
        meshNode->dissipate = val;
    }
}

void EmitterPropertyWidget::onAdditiveChanged(bool val)
{
    if (!!this->meshNode) {
        meshNode->useAdditive = val;
    }
}


