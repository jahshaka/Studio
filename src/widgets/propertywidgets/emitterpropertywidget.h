/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef EMITTERPROPERTYWIDGET_H
#define EMITTERPROPERTYWIDGET_H

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

    ComboBoxWidget* preset;
    TexturePickerWidget* billboardImage;
    HFloatSliderWidget* emissionRate;
    HFloatSliderWidget* particleLife;
    HFloatSliderWidget* particleScale;
    HFloatSliderWidget* lifeFactor;
    HFloatSliderWidget* speedFactor;
    HFloatSliderWidget* scaleFactor;
    HFloatSliderWidget* gravityFactor;
    HFloatSliderWidget* velocityFactor;
    CheckBoxWidget* randomRotation;
    CheckBoxWidget* sortOrder;
    CheckBoxWidget* dissipate;
    CheckBoxWidget* dissipateInv;
    CheckBoxWidget* useAdditive;
    ComboBoxWidget* blendMode;
};

#endif // EMITTERPROPERTYWIDGET_H
