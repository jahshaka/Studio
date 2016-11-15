#include "../accordianbladewidget.h"

#include "lightpropertywidget.h"
#include "../hfloatslider.h"
#include "../colorpickerwidget.h"

#include "../../jah3d/core/scene.h"


LightPropertyWidget::LightPropertyWidget(QWidget* parent):
    AccordianBladeWidget(parent)
{
    lightColor = this->addColorPicker("Color");
    radius = this->addFloatValueSlider("Radius",0,100);

    spotCutOff = this->addFloatValueSlider("Spotlight CutOff",0,90);
}


void LightPropertyWidget::setSceneNode(QSharedPointer<jah3d::SceneNode> sceneNode)
{
    this->sceneNode = sceneNode;
}
