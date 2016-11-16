#include <QWidget>
#include <QLayout>

#include "scenenodepropertieswidget.h"
#include "accordianbladewidget.h"
#include "transformeditor.h"

#include "propertywidgets/lightpropertywidget.h"
#include "propertywidgets/materialpropertywidget.h"

#include "../jah3d/core/scenenode.h"


SceneNodePropertiesWidget::SceneNodePropertiesWidget(QWidget* parent):
    QWidget(parent)
{
    //transform blade and widget
    transformPropView = new AccordianBladeWidget();
    transformPropView->setContentTitle("Transformation");
    transformWidget = transformPropView->addTransform();

    //light blade
    lightPropView = new LightPropertyWidget();
    lightPropView->setContentTitle("Light");

    //material blade
    materialPropView = new MaterialPropertyWidget();
    materialPropView->setContentTitle("Material");
    //materialPropView->setMaxHeight(materialPropView->minimum_height);

    //mesh blade

    //scene blade
}

/**
 * sets active scene node and determines which property ui should be shown
 * @param sceneNode
 */
void SceneNodePropertiesWidget::setSceneNode(QSharedPointer<jah3d::SceneNode> sceneNode)
{
    this->sceneNode = sceneNode;
    lightPropView->setSceneNode(sceneNode);
    materialPropView->setSceneNode(sceneNode);
    transformWidget->setSceneNode(sceneNode);

    //delete this->layout();

    auto layout = new QVBoxLayout(this);
    layout->addWidget(transformPropView);
    //materialPropView->expand();

    switch(sceneNode->getSceneNodeType())
    {
    case jah3d::SceneNodeType::Light:
        layout->addWidget(lightPropView);
        //lightPropView->expand();
        break;
    case jah3d::SceneNodeType::Mesh:
        layout->addWidget(materialPropView);
        //materialPropView->expand();
        break;

    default:
        break;
    }

    layout->addStretch();
    layout->setMargin(0);

    this->setLayout(layout);
}
