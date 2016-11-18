#include "worldpropertywidget.h"

WorldPropertyWidget::WorldPropertyWidget()
{
    skyTexture = this->addTexturePicker("Sky");
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
        return;
    }


}

void WorldPropertyWidget::onSkyTextureSet(QString texPath)
{

}
