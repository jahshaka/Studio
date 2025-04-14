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
#include "propertywidgets/handpropertywidget.h"
#include "propertywidgets/skypropertywidget.h"
#include "propertywidgets/worldskypropertywidget.h"

SceneNodePropertiesWidget::SceneNodePropertiesWidget(QWidget *parent) : QWidget(parent)
{
    widgetPropertyLayout = new QVBoxLayout(this);
    widgetPropertyLayout->setContentsMargins(0, 0, 0, 0);

    fogPropView = new FogPropertyWidget();
    fogPropView->setPanelTitle("Fog");

    worldPropView = new WorldPropertyWidget();
    worldPropView->setPanelTitle("World");
    worldPropView->expand();

	skyPropView = new SkyPropertyWidget();
	skyPropView->setPanelTitle("Sky");
	skyPropView->setDatabase(db);
	skyPropView->expand();

	worldSkyPropView = new WorldSkyPropertyWidget();
	worldSkyPropView->setPanelTitle("Sky");
	worldSkyPropView->setDatabase(db);
	worldSkyPropView->expand();

    transformPropView = new AccordianBladeWidget();
    transformPropView->setPanelTitle("Transformation");
    transformWidget = transformPropView->addTransformControls();
    transformPropView->expand();

    physicsPropView = new PhysicsPropertyWidget();
    physicsPropView->setPanelTitle("Physics Properties");

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

    handPropView = new HandPropertyWidget();
	handPropView->setPanelTitle("Hand");
	//handPropView->setDatabase(db);
	handPropView->expand();

    setLayout(widgetPropertyLayout);
}

void SceneNodePropertiesWidget::setScene(QSharedPointer<iris::Scene> scene)
{
    if (!!scene) {
        this->scene = scene;
        skyPropView->setScene(this->scene);
		worldSkyPropView->setScene(this->scene);
    }
}

/**
 * sets active scene node and determines which property ui should be shown
 * @param sceneNode
 */
void SceneNodePropertiesWidget::setSceneNode(QSharedPointer<iris::SceneNode> sceneNode)
{
    if (!!sceneNode) {
        this->sceneNode = sceneNode;

        widgetPropertyLayout->setContentsMargins(0, 0, 0, 0);
        clearLayout(this->layout());

        if (sceneNode->isRootNode()) {
            fogPropView->setParent(this);
            fogPropView->setScene(sceneNode->scene);
            worldPropView->setParent(this);
            worldPropView->setScene(sceneNode->scene);
            widgetPropertyLayout->addWidget(worldPropView);
            widgetPropertyLayout->addWidget(worldSkyPropView);
            widgetPropertyLayout->addWidget(fogPropView);
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
				
				case iris::SceneNodeType::Grab:
					handPropView->setParent(this);
					handPropView->setSceneNode(sceneNode);
					widgetPropertyLayout->addWidget(handPropView);
					break;

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
    if (!item) return;

    if (item->data(MODEL_TYPE_ROLE) == static_cast<int>(ModelTypes::Shader)) {
        clearLayout(this->layout());
        shaderPropView->setParent(this);
        shaderPropView->setShaderGuid(item->data(MODEL_GUID_ROLE).toString());
        widgetPropertyLayout->addWidget(shaderPropView);
        widgetPropertyLayout->addStretch();
    }
    else if (item->data(MODEL_TYPE_ROLE) == static_cast<int>(ModelTypes::Sky))
    {
        clearLayout(this->layout());
		skyPropView->setParent(this);
		skyPropView->setSkyAlongWithProperties(item->data(MODEL_GUID_ROLE).toString(),
											   static_cast<iris::SkyType>(item->data(SKY_TYPE_ROLE).toInt()));
		widgetPropertyLayout->addWidget(skyPropView);
		widgetPropertyLayout->addStretch();
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

void SceneNodePropertiesWidget::acceptCubemapTexturesFromSkyPresets(QStringList guids)
{
	QStringList fileNames;

	for (auto guid : guids) {
		fileNames.append(db->fetchAsset(guid).name);
	}

	db->removeDependenciesByType(scene->skyGuid, ModelTypes::Texture);

	worldSkyPropView->skyTypeChanged(static_cast<int>(iris::SkyType::CUBEMAP));
	for (int i = 0; i < 6; i++) {
		worldSkyPropView->onSlotChanged(fileNames[i], guids[i], i);
	}
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
