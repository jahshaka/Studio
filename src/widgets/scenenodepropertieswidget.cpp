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
    transformPropView = new AccordianBladeWidget(this);
    transformWidget = transformPropView->addTransform();

    //light blade
    lightPropView = new LightPropertyWidget(this);

    //material blade
    materialPropView = new MaterialPropertyWidget(this);

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

    delete this->layout();

    auto layout = new QVBoxLayout(this);
    layout->addWidget(materialPropView);

    switch(sceneNode->getSceneNodeType())
    {
    case jah3d::SceneNodeType::Light:
        layout->addWidget(lightPropView);
        break;
    case jah3d::SceneNodeType::Mesh:
        layout->addWidget(materialPropView);
        break;

    default:
        break;
    }

    lightPropView->setSceneNode(sceneNode);
    materialPropView->setSceneNode(sceneNode);

    this->setLayout(layout);
}
