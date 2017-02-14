#ifndef EMITTERPROPERTY_H
#define EMITTERPROPERTY_H

#include <QWidget>
#include <QSharedPointer>

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

    void setSceneNode(QSharedPointer<iris::SceneNode> sceneNode);

protected slots:
    void onEmissionRateChanged(float val);
    void onParticleLifeChanged(float val);
    void onLifeFactorChanged(float val);
    void onSpeedFactorChanged(float val);
    void onScaleFactorChanged(float val);
    void onGravityFactorChanged(float val);
    void onVelocityFactorChanged(float val);
    void onRandomRotation(bool val);
    void onSortOrderChanged(bool val);
    void onDissipateChanged(bool val);
    void onAdditiveChanged(bool val);
    void onBillboardImageChanged(QString);

private:
    QSharedPointer<iris::MeshNode> meshNode;

    ComboBox* preset;
    TexturePicker* billboardImage;
    HFloatSlider* emissionRate;
    HFloatSlider* particleLife;
    HFloatSlider* lifeFactor;
    HFloatSlider* speedFactor;
    HFloatSlider* scaleFactor;
    HFloatSlider* gravityFactor;
    HFloatSlider* velocityFactor;
    CheckBoxProperty* randomRotation;
    CheckBoxProperty* sortOrder;
    CheckBoxProperty* dissipate;
    CheckBoxProperty* useAdditive;
    ComboBox* blendMode;
};

#endif // EMITTER_H
