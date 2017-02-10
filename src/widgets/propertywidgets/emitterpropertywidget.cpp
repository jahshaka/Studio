#include "emitterpropertywidget.h"
#include "ui_emitterproperty.h"

#include "materialpropertywidget.h"
#include "../texturepicker.h"
#include "../hfloatslider.h"
#include "../checkboxproperty.h"

#include "../../irisgl/src/graphics/texture2d.h"
#include "../../irisgl/src/scenegraph/meshnode.h"
#include "../../irisgl/src/materials/defaultmaterial.h"

EmitterPropertyWidget::EmitterPropertyWidget()
{
    // @TODO: constants file...
    billboardImage  = this->addTexturePicker("Particle Image");
    emissionRate    = this->addFloatValueSlider("Emission Rate", 0, 128);
    particleLife    = this->addFloatValueSlider("Lifetime", 0, 24);
    rotationFactor  = this->addFloatValueSlider("Random Rotation", 0, 1);
    scaleFactor     = this->addFloatValueSlider("Random Scale", 0, 1);
    gravityFactor   = this->addFloatValueSlider("Gravity Effect", 0, 1);
    velocityFactor  = this->addFloatValueSlider("Velocity", 0, 32);
    sortOrder       = this->addCheckBox("Flip Sort Order", false);

    connect(emissionRate,   SIGNAL(valueChanged(float)), SLOT(onEmissionRateChanged(float)));
    connect(particleLife,   SIGNAL(valueChanged(float)), SLOT(onParticleLifeChanged(float)));
    connect(rotationFactor, SIGNAL(valueChanged(float)), SLOT(onRotationFactorChanged(float)));
    connect(scaleFactor,    SIGNAL(valueChanged(float)), SLOT(onScaleFactorChanged(float)));
    connect(gravityFactor,  SIGNAL(valueChanged(float)), SLOT(onGravityFactorChanged(float)));
    connect(velocityFactor, SIGNAL(valueChanged(float)), SLOT(onVelocityFactorChanged(float)));
    connect(sortOrder,      SIGNAL(valueChanged(bool)),  SLOT(onSortOrderChanged(bool)));
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

void EmitterPropertyWidget::onRotationFactorChanged(float val)
{

}

void EmitterPropertyWidget::onScaleFactorChanged(float val)
{

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

void EmitterPropertyWidget::onSortOrderChanged(bool val)
{

}

