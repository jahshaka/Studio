/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "worldpropertywidget.h"
#include "../texturepickerwidget.h"
#include "../../irisgl/src/core/scene.h"
#include "../../irisgl/src/core/irisutils.h"
#include "../../irisgl/src/materials/defaultskymaterial.h"

#include "../colorvaluewidget.h"
#include "../colorpickerwidget.h"
#include "../hfloatsliderwidget.h"
#include "../checkboxwidget.h"
#include "../comboboxwidget.h"

void WorldPropertyWidget::setupViewSelector()
{
    viewSelector = this->addComboBox("Skybox View");
    viewSelector->addItem("Front");
    viewSelector->addItem("Back");
    viewSelector->addItem("Top");
    viewSelector->addItem("Bottom");
    viewSelector->addItem("Left");
    viewSelector->addItem("Right");

    connect(viewSelector,               SIGNAL(currentIndexChanged(QString)),
            this,                       SLOT(viewTextureSlotChanged(QString)));
}

WorldPropertyWidget::WorldPropertyWidget()
{
    this->setPanelTitle("Sky and Lighting");

    skyColor = this->addColorPicker("Sky Color");
    ambientColor = this->addColorPicker("Ambient Color");

    setupViewSelector();

    skyTexture = this->addTexturePicker("Skybox Texture");

    connect(skyTexture,                 SIGNAL(valueChanged(QString)),
            this,                       SLOT(onSkyTextureChanged(QString)));

    connect(skyColor->getPicker(),      SIGNAL(onColorChanged(QColor)),
            this,                       SLOT(onSkyColorChanged(QColor)));

    connect(ambientColor->getPicker(),  SIGNAL(onColorChanged(QColor)),
            this,                       SLOT(onAmbientColorChanged(QColor)));
}

void WorldPropertyWidget::viewTextureSlotChanged(const QString &text)
{
    if (text == "Front") {
        if (!skyBoxTextures[0].isEmpty()) {
            skyTexture->setTexture(skyBoxTextures[0]);
        } else {
            skyTexture->setTexture("");
        }
    } else if (text == "Back") {
        if (!skyBoxTextures[1].isEmpty()) {
            skyTexture->setTexture(skyBoxTextures[1]);
        } else {
            skyTexture->setTexture("");
        }
    } else if (text == "Top") {
        if (!skyBoxTextures[2].isEmpty()) {
            skyTexture->setTexture(skyBoxTextures[2]);
        } else {
            skyTexture->setTexture("");
        }
    } else if (text == "Bottom") {
        if (!skyBoxTextures[3].isEmpty()) {
            skyTexture->setTexture(skyBoxTextures[3]);
        } else {
            skyTexture->setTexture("");
        }
    } else if (text == "Left") {
        if (!skyBoxTextures[4].isEmpty()) {
            skyTexture->setTexture(skyBoxTextures[4]);
        } else {
            skyTexture->setTexture("");
        }
    } else if (text == "Right") {
        if (!skyBoxTextures[5].isEmpty()) {
            skyTexture->setTexture(skyBoxTextures[5]);
        } else {
            skyTexture->setTexture("");
        }
    }
}

void WorldPropertyWidget::setScene(QSharedPointer<iris::Scene> scene)
{
    if (!!scene) {
        this->scene = scene;
//        auto skyTex = scene->skyTexture;
//        if (!!skyTex) {
//            skyTexture->setTexture(skyTex->getSource());
//        }

        skyColor->setColorValue(scene->skyColor);
        ambientColor->setColorValue(scene->ambientColor);
    } else {
        this->scene.clear();
        //return;
        //todo: clear ui
    }
}

void WorldPropertyWidget::onSkyTextureChanged(QString texPath)
{
    if (viewSelector->getCurrentItem() == "Front") {
        skyBoxTextures[0] = texPath;
    } else if (viewSelector->getCurrentItem() == "Back") {
        skyBoxTextures[1] = texPath;
    } else if (viewSelector->getCurrentItem() == "Top") {
        skyBoxTextures[2] = texPath;
    } else if (viewSelector->getCurrentItem() == "Bottom") {
        skyBoxTextures[3] = texPath;
    } else if (viewSelector->getCurrentItem() == "Left") {
        skyBoxTextures[4] = texPath;
    } else if (viewSelector->getCurrentItem() == "Right") {
        skyBoxTextures[5] = texPath;
    }

    if (texPath.isEmpty()) {
//        scene->setSkyTexture(iris::Texture2DPtr());
    } else {
//        scene->setSkyTexture(iris::Texture2D::load(texPath,false));

        auto pixel = IrisUtils::getAbsoluteAssetPath("app/content/textures/bottom.jpg");

        QImage *info = new QImage(texPath);

        auto y1 = !skyBoxTextures[2].isEmpty() ? skyBoxTextures[2] : pixel;
        auto y2 = !skyBoxTextures[3].isEmpty() ? skyBoxTextures[3] : pixel;
        auto z1 = !skyBoxTextures[0].isEmpty() ? skyBoxTextures[0] : pixel;
        auto z2 = !skyBoxTextures[1].isEmpty() ? skyBoxTextures[1] : pixel;
        auto x1 = !skyBoxTextures[4].isEmpty() ? skyBoxTextures[4] : pixel;
        auto x2 = !skyBoxTextures[5].isEmpty() ? skyBoxTextures[5] : pixel;

        scene->setSkyTexture(iris::Texture2D::createCubeMap(x1, x1, y1, y2, z1, z2, info));
    }
}

void WorldPropertyWidget::onSkyColorChanged(QColor color)
{
    scene->setSkyColor(color);
}

void WorldPropertyWidget::onAmbientColorChanged(QColor color)
{
    scene->setAmbientColor(color);
}

