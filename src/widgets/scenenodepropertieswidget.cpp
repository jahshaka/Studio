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

#include "irisgl/src/scenegraph/scenenode.h"

#include "sceneviewwidget.h"

#include "uimanager.h"

#include "accordianbladewidget.h"
#include "scenenodepropertieswidget.h"
#include "transformeditor.h"

#include "core/database/database.h"
#include "propertywidgets/demopane.h"
#include "propertywidgets/emitterpropertywidget.h"
#include "propertywidgets/fogpropertywidget.h"
#include "propertywidgets/lightpropertywidget.h"
#include "propertywidgets/materialpropertywidget.h"
#include "propertywidgets/meshpropertywidget.h"
#include "propertywidgets/nodepropertywidget.h"
#include "propertywidgets/shaderpropertywidget.h"
#include "propertywidgets/worldpropertywidget.h"
#include "propertywidgets/physicspropertywidget.h"

SceneNodePropertiesWidget::SceneNodePropertiesWidget(QWidget *parent) : QWidget(parent)
{
    widgetPropertyLayout = new QVBoxLayout(this);
    widgetPropertyLayout->setMargin(0);

    fogPropView = new FogPropertyWidget();
    fogPropView->setPanelTitle("Fog");
    fogPropView->expand();

    worldPropView = new WorldPropertyWidget();
    worldPropView->setPanelTitle("World");
    worldPropView->expand();

    transformPropView = new AccordianBladeWidget();
    transformPropView->setPanelTitle("Transformation");
    transformWidget = transformPropView->addTransformControls();
    transformPropView->expand();

    physicsPropView = new PhysicsPropertyWidget();
    physicsPropView->setPanelTitle("Physics Properties");
    physicsPropView->expand();

    meshPropView = new MeshPropertyWidget();
    meshPropView->setPanelTitle("Mesh Properties");
    meshPropView->expand();

    lightPropView = new LightPropertyWidget();
    lightPropView->setPanelTitle("Light");
    lightPropView->expand();

    emitterPropView = new EmitterPropertyWidget();
    emitterPropView->setPanelTitle("Emitter");
    emitterPropView->setDatabase(db);
    emitterPropView->expand();

    shaderPropView = new ShaderPropertyWidget();
    shaderPropView->setPanelTitle("Shader Definitions");
    shaderPropView->setDatabase(db);
    shaderPropView->expand();

    setLayout(widgetPropertyLayout);
}

/**
 * sets active scene node and determines which property ui should be shown
 * @param sceneNode
 */
void SceneNodePropertiesWidget::setSceneNode(QSharedPointer<iris::SceneNode> sceneNode)
{
    if (!!sceneNode) {
        this->sceneNode = sceneNode;

        widgetPropertyLayout->setMargin(0);
        clearLayout(this->layout());

        if (sceneNode->isRootNode()) {
            fogPropView->setParent(this);
            fogPropView->setScene(sceneNode->scene);
            worldPropView->setParent(this);
            worldPropView->setScene(sceneNode->scene);
            widgetPropertyLayout->addWidget(fogPropView);
            widgetPropertyLayout->addWidget(worldPropView);
        }
        else {
            transformPropView->setParent(this);
            transformWidget->setSceneNode(sceneNode);
            widgetPropertyLayout->addWidget(transformPropView);

            switch (sceneNode->getSceneNodeType()) {
                case iris::SceneNodeType::Light: {
                    lightPropView->setParent(this);
                    lightPropView->setSceneNode(sceneNode);
                    widgetPropertyLayout->addWidget(lightPropView);
                    break;
                }

                case iris::SceneNodeType::Empty: {
                    physicsPropView->setParent(this);
                    physicsPropView->setSceneNode(sceneNode);
                    physicsPropView->setSceneView(sceneView);
                    widgetPropertyLayout->addWidget(physicsPropView);
                    break;
                }

                case iris::SceneNodeType::Mesh: {
                    materialPropView = new MaterialPropertyWidget();
                    materialPropView->setPanelTitle("Material");
                    materialPropView->setDatabase(db);
                    materialPropView->expand();

                    physicsPropView->setParent(this);
                    meshPropView->setParent(this);
                    materialPropView->setParent(this);
                    physicsPropView->setSceneNode(sceneNode);
                    physicsPropView->setSceneView(sceneView);
                    meshPropView->setSceneNode(sceneNode);
                    materialPropView->setSceneNode(sceneNode);

                    if (!UiManager::isSimulationRunning) {
                        widgetPropertyLayout->addWidget(physicsPropView);
                    }

                    widgetPropertyLayout->addWidget(meshPropView);
                    widgetPropertyLayout->addWidget(materialPropView);
                    break;
                }

                case iris::SceneNodeType::ParticleSystem: {
                    emitterPropView->setParent(this);
                    emitterPropView->setSceneNode(sceneNode);
                    widgetPropertyLayout->addWidget(emitterPropView);
                    break;
                }

                default: break;
            }
        }

        widgetPropertyLayout->addStretch();
    }
    else {
        clearLayout(this->layout());
    }
}

void SceneNodePropertiesWidget::setAssetItem(QListWidgetItem *item)
{
    if (item) {
		clearLayout(this->layout());
        shaderPropView->setParent(this);
        shaderPropView->setShaderGuid(item->data(MODEL_GUID_ROLE).toString());
        widgetPropertyLayout->addWidget(shaderPropView);
        widgetPropertyLayout->addStretch();
    }
    else {
        clearLayout(this->layout());
    }
}

void SceneNodePropertiesWidget::refreshMaterial(const QString &matName)
{
    if (!!sceneNode && sceneNode->sceneNodeType == iris::SceneNodeType::Mesh) {
        materialPropView->forceShaderRefresh(matName);
    }
}

void SceneNodePropertiesWidget::refreshTransform()
{
	if (transformWidget) {
		transformWidget->refreshUi();
	}
}

void SceneNodePropertiesWidget::setSceneView(SceneViewWidget *sceneView)
{
    this->sceneView = sceneView;
}

void SceneNodePropertiesWidget::setDatabase(Database *db)
{
    this->db = db;
}

/**
 * clears layout and child layouts and deletes child widget
 * @param layout
 */
void SceneNodePropertiesWidget::clearLayout(QLayout *layout)
{
    if (layout == nullptr) return;

    while (auto item = layout->takeAt(0)) {
        if (auto widget = item->widget()) {
            //delete widget;
            widget->setParent(0);
        }

        if (auto childLayout = item->layout()) this->clearLayout(childLayout);
        delete item;
    }

    //delete layout;
}
