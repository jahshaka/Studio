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
    /*
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
    */
}

/**
 * sets active scene node and determines which property ui should be shown
 * @param sceneNode
 */
void SceneNodePropertiesWidget::setSceneNode(QSharedPointer<jah3d::SceneNode> sceneNode)
{
    //todo: properly cleanup layout

    if(!!sceneNode)
    {
        if(sceneNode->isRootNode())
        {

        }
        else
        {
            //gotta recreate them each time
            transformPropView = new AccordianBladeWidget();
            transformPropView->setContentTitle("Transformation");
            transformWidget = transformPropView->addTransform();
            //transformPropView->expand();

            //light blade
            lightPropView = new LightPropertyWidget();
            lightPropView->setContentTitle("Light");

            //material blade
            materialPropView = new MaterialPropertyWidget();
            materialPropView->setContentTitle("Material");
            materialPropView->setMaxHeight(700);


            this->sceneNode = sceneNode;
            lightPropView->setSceneNode(sceneNode);
            materialPropView->setSceneNode(sceneNode);
            transformWidget->setSceneNode(sceneNode);

            //delete this->layout();

            auto layout = new QVBoxLayout();
            layout->addWidget(transformPropView);
            transformPropView->expand();

            switch(sceneNode->getSceneNodeType())
            {
            case jah3d::SceneNodeType::Light:
                layout->addWidget(lightPropView);
                lightPropView->expand();
                break;
            case jah3d::SceneNodeType::Mesh:
                layout->addWidget(materialPropView);
                materialPropView->expand();
                break;

            default:
                break;
            }

            layout->addStretch();
            layout->setMargin(0);

            auto oldLayout = this->layout();
            clearLayout(oldLayout);

            this->setLayout(layout);
        }
    }
    else
    {
        auto layout = new QVBoxLayout();
        auto oldLayout = this->layout();
        clearLayout(oldLayout);

        this->setLayout(layout);
    }
}

/**
 * clears layout and child layouts and deletes child widget
 * @param layout
 */
void SceneNodePropertiesWidget::clearLayout(QLayout* layout)
{
    if(layout==nullptr)
        return;

    while(auto item = layout->takeAt(0))
    {
        if(auto widget = item->widget())
        {
            delete widget;
        }

        if(auto childLayout = item->layout())
        {
            this->clearLayout(childLayout);
        }

        delete item;
    }

    delete layout;
}
