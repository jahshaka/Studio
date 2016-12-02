#include "worldpropertywidget.h"
#include "../texturepicker.h"
#include "../../jah3d/core/scene.h"
#include "../../jah3d/materials/defaultskymaterial.h"

#include "../colorvaluewidget.h"
#include "../hfloatslider.h"

WorldPropertyWidget::WorldPropertyWidget()
{
    skyTexture = this->addTexturePicker("Sky Texture");
    fogColor = this->addColorPicker("Fog Color");
    fogStart = this->addFloatValueSlider("Fog Start",0,1000);
    fogEnd = this->addFloatValueSlider("Fog End",0,1000);


    connect(skyTexture,SIGNAL(valueChanged(QString)),SLOT(onSkyTextureChanged(QString)));

    connect(fogColor,SIGNAL(colorChanged(QColor)),SLOT(onFogColorChanged(QColor)));
    connect(fogStart,SIGNAL(valueChanged(float)),SLOT(onFogStartChanged(float)));
    connect(fogEnd,SIGNAL(valueChanged(float)),SLOT(onFogEndChanged(float)));
}

void WorldPropertyWidget::setScene(QSharedPointer<jah3d::Scene> scene)
{
    if(!!scene)
    {
        this->scene = scene;
        auto skyTex = scene->skyMaterial->getSkyTexture();
        if(!!skyTex)
            skyTexture->setTexture(skyTex->getSource());

        fogColor->setColorValue(scene->fogColor);
        fogStart->setValue(scene->fogStart);
        fogEnd->setValue(scene->fogEnd);

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
        scene->skyMaterial->setSkyTexture(jah3d::Texture2DPtr());

    }
    else
    {
        scene->skyMaterial->setSkyTexture(jah3d::Texture2D::load(texPath,false));
    }
}

void WorldPropertyWidget::onFogColorChanged(QColor color)
{
    if(!!scene)
        scene->fogColor = color;
}

void WorldPropertyWidget::onFogStartChanged(float val)
{
    if(!!scene)
        scene->fogStart = val;
}

void WorldPropertyWidget::onFogEndChanged(float val)
{
    if(!!scene)
        scene->fogEnd = val;
}
