#include "worldpropertywidget.h"
#include "../texturepicker.h"
#include "../../jah3d/core/scene.h"
#include "../../jah3d/materials/defaultskymaterial.h"

#include "../colorvaluewidget.h"
#include "../hfloatslider.h"

WorldPropertyWidget::WorldPropertyWidget()
{
    skyTexture = this->addTexturePicker("Sky Texture");

    connect(skyTexture,SIGNAL(valueChanged(QString)),SLOT(onSkyTextureChanged(QString)));
}

void WorldPropertyWidget::setScene(QSharedPointer<jah3d::Scene> scene)
{
    if(!!scene)
    {
        this->scene = scene;
        auto skyTex = scene->skyMaterial->getSkyTexture();
        if(!!skyTex)
            skyTexture->setTexture(skyTex->getSource());

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
