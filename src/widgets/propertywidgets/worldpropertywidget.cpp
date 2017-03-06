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
#include "../../irisgl/src/materials/defaultskymaterial.h"

#include "../colorvaluewidget.h"
#include "../colorpickerwidget.h"
#include "../hfloatsliderwidget.h"
#include "../checkboxwidget.h"

WorldPropertyWidget::WorldPropertyWidget()
{
    this->setPanelTitle("Sky and Lighting");

    skyColor = this->addColorPicker("Sky Color");
    ambientColor = this->addColorPicker("Ambient Color");
    skyTexture = this->addTexturePicker("Sky Texture");

    connect(skyTexture,SIGNAL(valueChanged(QString)),SLOT(onSkyTextureChanged(QString)));
    connect(skyColor->getPicker(),SIGNAL(onColorChanged(QColor)),SLOT(onSkyColorChanged(QColor)));
    connect(ambientColor->getPicker(),SIGNAL(onColorChanged(QColor)),SLOT(onAmbientColorChanged(QColor)));
}

void WorldPropertyWidget::setScene(QSharedPointer<iris::Scene> scene)
{
    if(!!scene)
    {
        this->scene = scene;
        auto skyTex = scene->skyTexture;
        if(!!skyTex)
            skyTexture->setTexture(skyTex->getSource());

        skyColor->setColorValue(scene->skyColor);
        ambientColor->setColorValue(scene->ambientColor);
    }
    else
    {
        this->scene.clear();
        //return;
        //todo: clear ui
    }


}

void WorldPropertyWidget::onSkyTextureChanged(QString texPath)
{
    if(texPath.isEmpty())
    {
        scene->setSkyTexture(iris::Texture2DPtr());

    }
    else
    {
        scene->setSkyTexture(iris::Texture2D::load(texPath,false));
    }
}

void WorldPropertyWidget::onSkyColorChanged(QColor color)
{
    scene->setSkyColor(color);
}

void WorldPropertyWidget::onAmbientColorChanged(QColor color)
{
    //scene->set
    scene->setAmbientColor(color);
}

