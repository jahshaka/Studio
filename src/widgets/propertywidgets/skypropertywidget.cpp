/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "skypropertywidget.h"
#include "../texturepickerwidget.h"
#include "../../irisgl/src/materials/defaultskymaterial.h"

#include "../colorvaluewidget.h"
#include "../colorpickerwidget.h"
#include "../hfloatsliderwidget.h"
#include "../comboboxwidget.h"

#include "../checkboxwidget.h"

#include "io/assetmanager.h"

SkyPropertyWidget::SkyPropertyWidget()
{
}

void SkyPropertyWidget::skyTypeChanged(int index)
{
    // Decide whether of not to update the panel
    if (index == static_cast<int>(currentSky)) {
        return;
    } else {
        clearPanel(this->layout());

        skySelector = this->addComboBox("Sky Type");
        skySelector->addItem("Single Color");
        skySelector->addItem("Cubemap");
        skySelector->addItem("Equirectangular");
        skySelector->addItem("Gradient");
        skySelector->addItem("Material");
        skySelector->addItem("Realistic");
        skySelector->setCurrentIndex(index);

        connect(skySelector, SIGNAL(currentIndexChanged(int)), this, SLOT(skyTypeChanged(int)));
    }

    // Draw the widgets depending on the sky type
    if (index == static_cast<int>(iris::SkyType::SINGLE_COLOR))
    {
        scene->skyType = iris::SkyType::SINGLE_COLOR;

        singleColor = this->addColorPicker("Sky Color");
        singleColor->setColor(scene->skyColor);

        connect(singleColor->getPicker(), SIGNAL(onColorChanged(QColor)),
            this, SLOT(onSingleSkyColorChanged(QColor)));
    }
    else if (index == static_cast<int>(iris::SkyType::CUBEMAP))
    {
        scene->skyType = iris::SkyType::CUBEMAP;
        
        cubemapFront = this->addTexturePicker("Front");
        cubemapBack = this->addTexturePicker("Back");
        cubemapLeft = this->addTexturePicker("Left");
        cubemapRight = this->addTexturePicker("Right");
        cubemapTop = this->addTexturePicker("Top");
        cubemapBottom = this->addTexturePicker("Bottom");

        // remember the image on the tiles... TODO
        auto changeFunc = [this](QString value) {
            // Use the metadata from any valid image
            if (value.isEmpty()) return;
            scene->setSkyTexture(iris::Texture2D::createCubeMap(cubemapFront->filePath,
                                                                cubemapBack->filePath,
                                                                cubemapTop->filePath,
                                                                cubemapBottom->filePath,
                                                                cubemapLeft->filePath,
                                                                cubemapRight->filePath,
                                                                new QImage(value)));
        };

        connect(cubemapFront, &TexturePickerWidget::valueChanged, [=](QString value) {
            changeFunc(value);
        });

        connect(cubemapBack, &TexturePickerWidget::valueChanged, this, [=](QString value) {
            changeFunc(value);
        });

        connect(cubemapLeft, &TexturePickerWidget::valueChanged, this, [=](QString value) {
            changeFunc(value);
        });

        connect(cubemapRight, &TexturePickerWidget::valueChanged, this, [=](QString value) {
            changeFunc(value);
        });

        connect(cubemapTop, &TexturePickerWidget::valueChanged, this, [=](QString value) {
            changeFunc(value);
        });

        connect(cubemapBottom, &TexturePickerWidget::valueChanged, this, [=](QString value) {
            changeFunc(value);
        });
    }
    else if (index == static_cast<int>(iris::SkyType::EQUIRECTANGULAR)) {
        scene->skyType = iris::SkyType::EQUIRECTANGULAR;
        // one string
        equiTexture = this->addTexturePicker("Equi Map");
    }
    else if (index == static_cast<int>(iris::SkyType::GRADIENT)) {
        scene->skyType = iris::SkyType::GRADIENT;
        // two strings
        // one float
        gradientType = this->addComboBox("Type");
        gradientType->addItem("Linear");
        gradientType->addItem("Radial");
        color1 = this->addColorPicker("Top Color");
        color2 = this->addColorPicker("Bottom Color");
        scale = this->addFloatValueSlider("Scale", 0, 1.f);
        angle = this->addFloatValueSlider("Angle", -360.f, 360.f);
    }
    else if (index == static_cast<int>(iris::SkyType::MATERIAL)) {
        scene->skyType = iris::SkyType::MATERIAL;
        // material asset
        shaderSelector = this->addComboBox("Material");

        for (const auto material : AssetManager::getAssets()) {
            shaderSelector->addItem(material->fileName);
        }
    }
    else if (index == static_cast<int>(iris::SkyType::REALISTIC)) {
        scene->skyType = iris::SkyType::REALISTIC;
        // floats
        // height
        luminance = this->addFloatValueSlider("Luminance", 0.01, 1.f);
        reileigh = this->addFloatValueSlider("Reileigh", 0, 10.f);
        mieCoefficient = this->addFloatValueSlider("Mie Coeff", 0, 100.f);
        mieDirectionalG = this->addFloatValueSlider("Mie Dir.", 0, 100.f);
        turbidity = this->addFloatValueSlider("Turbidity", 0, 1.f);
        sunPosX = this->addFloatValueSlider("X", 0, 10.f);
        sunPosY = this->addFloatValueSlider("Y", 0, 10.f);
        sunPosZ = this->addFloatValueSlider("Z", 0, 10.f);

        connect(luminance, SIGNAL(valueChanged(float)), SLOT(onLuminanceChanged(float)));
        connect(reileigh, SIGNAL(valueChanged(float)), SLOT(onReileighChanged(float)));
        connect(mieCoefficient, SIGNAL(valueChanged(float)), SLOT(onMieCoeffGChanged(float)));
        connect(mieDirectionalG, SIGNAL(valueChanged(float)), SLOT(onMieDireChanged(float)));
        connect(turbidity, SIGNAL(valueChanged(float)), SLOT(onTurbidityChanged(float)));
        connect(sunPosX, SIGNAL(valueChanged(float)), SLOT(onSunPosXChanged(float)));
        connect(sunPosY, SIGNAL(valueChanged(float)), SLOT(onSunPosYChanged(float)));
        connect(sunPosZ, SIGNAL(valueChanged(float)), SLOT(onSunPosZChanged(float)));
    }

    currentSky = static_cast<iris::SkyType>(index);

    scene->skyType = currentSky;
    scene->switchSkyTexture(currentSky);
}

void SkyPropertyWidget::setScene(QSharedPointer<iris::Scene> scene)
{
    if (!!scene) {
        this->scene = scene;

        skyTypeChanged(static_cast<int>(scene->skyType));

        switch (scene->skyType)
        {
            case iris::SkyType::REALISTIC: {
                reileigh->setValue(scene->skyRealistic.reileigh);
                luminance->setValue(scene->skyRealistic.luminance);
                mieCoefficient->setValue(scene->skyRealistic.mieCoefficient);
                mieDirectionalG->setValue(scene->skyRealistic.mieDirectionalG);
                turbidity->setValue(scene->skyRealistic.turbidity);
                sunPosX->setValue(scene->skyRealistic.sunPosX);
                sunPosY->setValue(scene->skyRealistic.sunPosY);
                sunPosZ->setValue(scene->skyRealistic.sunPosZ);
                break;
            }

            case iris::SkyType::SINGLE_COLOR: {
                singleColor->setColorValue(scene->skyColor);
            }

            default:
                break;
        }

    } else {
        this->scene.clear();
    }
}

void SkyPropertyWidget::onSingleSkyColorChanged(QColor color)
{
    if (!!scene) scene->skyColor = color;
}

void SkyPropertyWidget::onReileighChanged(float val)
{
    if (!!scene) scene->skyRealistic.reileigh = val;
}

void SkyPropertyWidget::onLuminanceChanged(float val)
{
    if (!!scene) scene->skyRealistic.luminance = val;
}

void SkyPropertyWidget::onTurbidityChanged(float val)
{
    if (!!scene) scene->skyRealistic.turbidity = val;
}

void SkyPropertyWidget::onMieCoeffGChanged(float val)
{
    if (!!scene) scene->skyRealistic.mieCoefficient = val;
}

void SkyPropertyWidget::onMieDireChanged(float val)
{
    if (!!scene) scene->skyRealistic.mieDirectionalG = val;
}

void SkyPropertyWidget::onSunPosXChanged(float val)
{
    if (!!scene)  scene->skyRealistic.sunPosX = val;
}

void SkyPropertyWidget::onSunPosYChanged(float val)
{
    if (!!scene) scene->skyRealistic.sunPosY = val;
}

void SkyPropertyWidget::onSunPosZChanged(float val)
{
    if (!!scene) scene->skyRealistic.sunPosZ = val;
}

