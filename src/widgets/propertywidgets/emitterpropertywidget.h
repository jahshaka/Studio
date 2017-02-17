#ifndef EMITTERPROPERTY_H
#define EMITTERPROPERTY_H

#include <QWidget>
#include <QSharedPointer>
#include "../../irisgl/src/irisglfwd.h"

#include "../accordianbladewidget.h"

namespace iris {
    class SceneNode;
    class MeshNode;
    class DefaultMaterial;
}

namespace Ui {
    class EmitterPropertyWidget;
}

class ComboBox;

class EmitterPropertyWidget : public AccordianBladeWidget
{
    Q_OBJECT

public:
    EmitterPropertyWidget();
    ~EmitterPropertyWidget();

    void setSceneNode(iris::SceneNodePtr sceneNode);

protected slots:
    void onEmissionRateChanged(float val);
    void onParticleLifeChanged(float val);
    void onParticleScaleChanged(float val);
    void onLifeFactorChanged(float val);
    void onSpeedFactorChanged(float val);
    void onScaleFactorChanged(float val);
    void onGravityFactorChanged(float val);
    void onVelocityFactorChanged(float val);
    void onRandomRotation(bool val);
    void onSortOrderChanged(bool val);
    void onDissipateChanged(bool val);
    void onDissipateInvChanged(bool val);
    void onAdditiveChanged(bool val);
    void onBillboardImageChanged(QString);

private:
    iris::ParticleSystemNodePtr ps;

    ComboBox* preset;
    TexturePicker* billboardImage;
    HFloatSlider* emissionRate;
    HFloatSlider* particleLife;
    HFloatSlider* particleScale;
    HFloatSlider* lifeFactor;
    HFloatSlider* speedFactor;
    HFloatSlider* scaleFactor;
    HFloatSlider* gravityFactor;
    HFloatSlider* velocityFactor;
    CheckBoxProperty* randomRotation;
    CheckBoxProperty* sortOrder;
    CheckBoxProperty* dissipate;
    CheckBoxProperty* dissipateInv;
    CheckBoxProperty* useAdditive;
    ComboBox* blendMode;
};

#endif // EMITTER_H
