/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include <QWidget>
#include <QLayout>

#include "irisgl/document/scenegraph/scenenode.h"


#include "services/services.h"
#include "services/playbackservice.h"

#include "ui/controls/accordionbladewidget.h"
#include "ui/panels/scenenodepropertieswidget.h"
#include "ui/panels/transformeditor.h"

#include "data/database/database.h"
#include "ui/panels/propertywidgets/emitterpropertywidget.h"
#include "ui/panels/propertywidgets/fogpropertywidget.h"
#include "ui/panels/propertywidgets/lightpropertywidget.h"
#include "ui/panels/propertywidgets/materialpropertywidget.h"
#include "ui/panels/propertywidgets/meshpropertywidget.h"
#include "ui/panels/propertywidgets/nodepropertywidget.h"
#include "ui/panels/propertywidgets/shaderpropertywidget.h"
#include "ui/panels/propertywidgets/worldpropertywidget.h"
#include "ui/panels/propertywidgets/physicspropertywidget.h"
#include "ui/panels/propertywidgets/skypropertywidget.h"
#include "ui/panels/propertywidgets/worldskypropertywidget.h"
#include "ui/panels/propertywidgets/worldgipropertywidget.h"
#include "ui/panels/propertywidgets/worldaapropertywidget.h"
#include "ui/panels/propertywidgets/worldmodespropertywidget.h"
#include "ui/panels/propertywidgets/worldshadowpropertywidget.h"

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

	// World Modes (POST_CHAIN_SPEC §9.6) sits FIRST among the quality sections:
	// it is the tier every one of them resolves through.
	worldModesPropView = new WorldModesPropertyWidget();
	worldModesPropView->setPanelTitle("World Mode");
	worldModesPropView->expand();
	// A tier writes THROUGH to the very fields the Anti-Aliasing, Shadows, GI and
	// Sky sections display, and those sections read their fields only when they
	// are built. Rebuild them whenever a World Mode edit lands, or they keep
	// showing the pre-switch values until the node is reselected.
	connect(worldModesPropView, &WorldModesPropertyWidget::worldSettingsChanged,
	        this, [this]() {
		auto sc = scene;
		if (!sc && !!sceneNode) sc = sceneNode->scene;
		if (!sc) return;
		worldAaPropView->setScene(sc);
		worldShadowPropView->setScene(sc);
		worldGiPropView->setScene(sc);
	});

	worldGiPropView = new WorldGiPropertyWidget();
	worldGiPropView->setPanelTitle("Global Illumination");
	worldGiPropView->expand();

	worldAaPropView = new WorldAaPropertyWidget();
	worldAaPropView->setPanelTitle("Anti-Aliasing");
	worldAaPropView->expand();

	worldShadowPropView = new WorldShadowPropertyWidget();
	worldShadowPropView->setPanelTitle("Shadows");
	worldShadowPropView->expand();

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
            worldModesPropView->setParent(this);
            worldModesPropView->setSceneView(sceneView);
            worldModesPropView->setScene(sceneNode->scene);
            worldGiPropView->setParent(this);
            worldGiPropView->setScene(sceneNode->scene);
            worldAaPropView->setParent(this);
            worldAaPropView->setSceneView(sceneView);
            worldAaPropView->setScene(sceneNode->scene);
            worldShadowPropView->setParent(this);
            worldShadowPropView->setSceneView(sceneView);
            worldShadowPropView->setScene(sceneNode->scene);
            widgetPropertyLayout->addWidget(worldPropView);
            widgetPropertyLayout->addWidget(worldSkyPropView);
            widgetPropertyLayout->addWidget(worldModesPropView);
            widgetPropertyLayout->addWidget(worldGiPropView);
            widgetPropertyLayout->addWidget(worldAaPropView);
            widgetPropertyLayout->addWidget(worldShadowPropView);
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
                    materialPropView->setProject(project);
                    materialPropView->setServices(services);
                    materialPropView->expand();

                    physicsPropView->setParent(this);
                    meshPropView->setParent(this);
                    materialPropView->setParent(this);
                    physicsPropView->setSceneNode(sceneNode);
                    physicsPropView->setSceneView(sceneView);
                    meshPropView->setSceneNode(sceneNode);
                    materialPropView->setSceneNode(sceneNode);

                    if (!(services && services->playback && services->playback->isSimulationRunning())) {
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

void SceneNodePropertiesWidget::setSceneView(IEditorViewport *sceneView)
{
    this->sceneView = sceneView;
    if (worldSkyPropView) worldSkyPropView->wireViewportEvents(sceneView);
}

void SceneNodePropertiesWidget::setServices(StudioServices *services)
{
    this->services = services;
    if (transformWidget) transformWidget->setServices(services);
    if (skyPropView) skyPropView->eventBus = services ? services->eventBus : nullptr;
}

void SceneNodePropertiesWidget::setDatabase(Database *db)
{
    this->db = db;
}

void SceneNodePropertiesWidget::setProject(Project *project)
{
    // Phase 4: every panel that used to read the Globals::project static now
    // carries the pointer (AccordianBladeWidget::project, which its add*()
    // helpers forward to the controls they build).
    this->project = project;
    if (worldPropView)    worldPropView->setProject(project);
    if (skyPropView)      skyPropView->setProject(project);
    if (worldSkyPropView) worldSkyPropView->setProject(project);
    if (emitterPropView)  emitterPropView->setProject(project);
    if (shaderPropView)   shaderPropView->setProject(project);
    // materialPropView is created on demand in setSceneNode() and gets the
    // pointer there (the member is not null-initialised).
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
