#include "../accordianbladewidget.h"
#include <QWidget>

#include "lightpropertywidget.h"
#include "../hfloatslider.h"
#include "../colorpickerwidget.h"
#include "../colorvaluewidget.h"

#include "../../jah3d/core/scene.h"
#include "../../jah3d/core/scenenode.h"
#include "../../jah3d/scenegraph/lightnode.h"


LightPropertyWidget::LightPropertyWidget(QWidget* parent):
    AccordianBladeWidget(parent)
{
    lightColor = this->addColorPicker("Color");
    radius = this->addFloatValueSlider("Radius",0,100);
    spotCutOff = this->addFloatValueSlider("Spotlight CutOff",0,90);

    connect(lightColor->getPicker(),SIGNAL(onColorChanged(QColor)),this,SLOT(lightColorChanged(QColor)));
    connect(lightColor->getPicker(),SIGNAL(onSetColor(QColor)),this,SLOT(lightColorChanged(QColor)));
}

void LightPropertyWidget::lightColorChanged(QColor color)
{
    if(!!lightNode)
        lightNode->color = color;
}


void LightPropertyWidget::setSceneNode(QSharedPointer<jah3d::SceneNode> sceneNode)
{
    //this->sceneNode = sceneNode;
    if(sceneNode->getSceneNodeType()==jah3d::SceneNodeType::Light)
    {
        lightNode = sceneNode.staticCast<jah3d::LightNode>();
    }
    else
    {
        lightNode.clear();
    }


}
