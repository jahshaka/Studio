#include "worldpropertywidget.h"
#include "../texturepicker.h"
#include "../../irisgl/src/core/scene.h"
#include "../../irisgl/src/materials/defaultskymaterial.h"

#include "../colorvaluewidget.h"
#include "../hfloatslider.h"

WorldPropertyWidget::WorldPropertyWidget()
{
    skyTexture = this->addTexturePicker("Sky Texture");

    connect(skyTexture,SIGNAL(valueChanged(QString)),SLOT(onSkyTextureChanged(QString)));
}

void WorldPropertyWidget::setScene(QSharedPointer<iris::Scene> scene)
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
        scene->skyMaterial->setSkyTexture(iris::Texture2DPtr());

    }
    else
    {
        scene->skyMaterial->setSkyTexture(iris::Texture2D::load(texPath,false));
    }
}
