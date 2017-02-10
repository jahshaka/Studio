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
    void onRotationFactorChanged(float val);
    void onScaleFactorChanged(float val);
    void onGravityFactorChanged(float val);
    void onVelocityFactorChanged(float val);
    void onSortOrderChanged(bool val);
    void onBillboardImageChanged(QString);

private:
    QSharedPointer<iris::MeshNode> meshNode;

    TexturePicker* billboardImage;
    HFloatSlider* emissionRate;
    HFloatSlider* particleLife;
    HFloatSlider* rotationFactor;
    HFloatSlider* scaleFactor;
    HFloatSlider* gravityFactor;
    HFloatSlider* velocityFactor;
    CheckBoxProperty* sortOrder;
};

#endif // EMITTER_H
