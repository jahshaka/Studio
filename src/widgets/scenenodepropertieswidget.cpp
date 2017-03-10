/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include <QWidget>
#include <QLayout>

#include "scenenodepropertieswidget.h"
#include "accordianbladewidget.h"
#include "transformeditor.h"

#include "propertywidgets/lightpropertywidget.h"
#include "propertywidgets/materialpropertywidget.h"
#include "propertywidgets/worldpropertywidget.h"
#include "propertywidgets/fogpropertywidget.h"
#include "propertywidgets/emitterpropertywidget.h"
#include "propertywidgets/nodepropertywidget.h"
#include "propertywidgets/demopane.h"

#include "../irisgl/src/core/scenenode.h"


SceneNodePropertiesWidget::SceneNodePropertiesWidget(QWidget* parent) : QWidget(parent)
{

}

/**
 * sets active scene node and determines which property ui should be shown
 * @param sceneNode
 */
void SceneNodePropertiesWidget::setSceneNode(QSharedPointer<iris::SceneNode> sceneNode)
{
    if (!!sceneNode) {
        if (sceneNode->isRootNode()) {
            /* remember this is used to test new widgets. Do NOT push to prod enabled! */
            // demoPane = new DemoPane();
            // demoPane->setPanelTitle("Demo Pane");
            // demoPane->expand();

            fogPropView = new FogPropertyWidget();
            fogPropView->setPanelTitle("Fog");
            fogPropView->setScene(sceneNode->scene);
            fogPropView->expand();

            worldPropView = new WorldPropertyWidget();
            worldPropView->setPanelTitle("World");
            worldPropView->setScene(sceneNode->scene);
            worldPropView->expand();

            auto layout = new QVBoxLayout();
            // layout->addWidget(demoPane);

            layout->addWidget(fogPropView);
            layout->addWidget(worldPropView);

            layout->addStretch();
            layout->setMargin(0);

            clearLayout(this->layout());
            this->setLayout(layout);
        } else {
            transformPropView = new AccordianBladeWidget();
            transformPropView->setPanelTitle("Transformation");
            transformWidget = transformPropView->addTransformControls();

            nodePropView = new NodePropertyWidget();
            nodePropView->setPanelTitle("Node Properties");

            lightPropView = new LightPropertyWidget();
            lightPropView->setPanelTitle("Light");

            materialPropView = new MaterialPropertyWidget(sceneNode->materialType);
            materialPropView->setPanelTitle("Material");

            emitterPropView = new EmitterPropertyWidget();
            emitterPropView->setPanelTitle("Emitter");

            this->sceneNode = sceneNode;

            nodePropView->setSceneNode(sceneNode);
            lightPropView->setSceneNode(sceneNode);
            materialPropView->setSceneNode(sceneNode);
            emitterPropView->setSceneNode(sceneNode);
            transformWidget->setSceneNode(sceneNode);

            auto layout = new QVBoxLayout();
            layout->addWidget(transformPropView);
            transformPropView->expand();

            switch (sceneNode->getSceneNodeType()) {
                case iris::SceneNodeType::Light:
                    layout->addWidget(lightPropView);
                    lightPropView->expand();
                    break;
                case iris::SceneNodeType::Mesh:
                    layout->addWidget(nodePropView);
                    layout->addWidget(materialPropView);
                    materialPropView->expand();
                    nodePropView->expand();
                    break;
                case iris::SceneNodeType::ParticleSystem:
                    layout->addWidget(emitterPropView);
                    emitterPropView->expand();
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
    } else {
        auto layout = new QVBoxLayout();
        clearLayout(this->layout());
        this->setLayout(layout);
    }
}

void SceneNodePropertiesWidget::refreshMaterial()
{
    if (!!sceneNode && sceneNode->sceneNodeType == iris::SceneNodeType::Mesh) {
        materialPropView->setSceneNode(sceneNode);
    }
}

/**
 * clears layout and child layouts and deletes child widget
 * @param layout
 */
void SceneNodePropertiesWidget::clearLayout(QLayout* layout)
{
    if (layout == nullptr) return;

    while (auto item = layout->takeAt(0)) {
        if (auto widget = item->widget()) delete widget;

        if (auto childLayout = item->layout()) {
            this->clearLayout(childLayout);
        }

        delete item;
    }

    delete layout;
}
