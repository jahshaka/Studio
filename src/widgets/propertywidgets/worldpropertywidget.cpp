#include "worldpropertywidget.h"
#include "../texturepicker.h"

WorldPropertyWidget::WorldPropertyWidget()
{
    skyTexture = this->addTexturePicker("Sky Texture");
    connect(skyTexture,SIGNAL(valueChanged(QString)),this,SLOT(onSkyTextureChanged(QString)));
}

void WorldPropertyWidget::setScene(QSharedPointer<jah3d::Scene> scene)
{
    if(!!scene)
    {
        this->scene = scene;
    }
    else
    {
        this->scene.clear();
        //return;
    }


}

void WorldPropertyWidget::onSkyTextureChanged(QString texPath)
{
    //scene->setSkyTexture(texPath);
    //scene->skyMaterial->setSkyTexture();
}
