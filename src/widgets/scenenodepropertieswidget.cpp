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

SceneNodePropertiesWidget::SceneNodePropertiesWidget(QWidget *parent) : QWidget(parent)
{
	transformPropView = nullptr;
	transformWidget = nullptr;

	materialPropView = nullptr;
	emitterPropView = nullptr;

	lightPropView = nullptr;
	worldPropView = nullptr;
	fogPropView = nullptr;
	meshPropView = nullptr;
	demoPane = nullptr;
    shaderPropView = nullptr;
}

/**
 * sets active scene node and determines which property ui should be shown
 * @param sceneNode
 */
void SceneNodePropertiesWidget::setSceneNode(QSharedPointer<iris::SceneNode> sceneNode)
{
    if (!!sceneNode) {
        if (sceneNode->isRootNode()) {
            /* remember this is used to test new widgets. Do NOT push to master enabled! */
            // demoPane = new DemoPane();
            // demoPane->setPanelTitle("Demo Pane");
            // demoPane->expand();
			clearLayout(this->layout());

            fogPropView = new FogPropertyWidget();
            fogPropView->setPanelTitle(tr("Fog"));
            fogPropView->setScene(sceneNode->scene);
            fogPropView->expand();

            worldPropView = new WorldPropertyWidget();
            worldPropView->setPanelTitle(tr("World"));
            worldPropView->setScene(sceneNode->scene);
            worldPropView->expand();

            auto layout = new QVBoxLayout();
            // layout->addWidget(demoPane);
            layout->addWidget(fogPropView);
            layout->addWidget(worldPropView);

            layout->addStretch();
            layout->setMargin(0);

            
            this->setLayout(layout);
        } else {
			clearLayout(this->layout());

            this->sceneNode = sceneNode;

            transformPropView = new AccordianBladeWidget();
            transformPropView->setPanelTitle(tr("Transformation"));
            transformWidget = transformPropView->addTransformControls();
            transformWidget->setSceneNode(sceneNode);

            meshPropView = new MeshPropertyWidget();
            meshPropView->setPanelTitle(tr("Mesh Properties"));
            meshPropView->setSceneNode(sceneNode);

            // nodePropView = new NodePropertyWidget();
            // nodePropView->setPanelTitle("Node Properties");
            // nodePropView->setSceneNode(sceneNode);

            lightPropView = new LightPropertyWidget();
            lightPropView->setPanelTitle(tr("Light"));
            lightPropView->setSceneNode(sceneNode);

            // try to move back mat prop here

            emitterPropView = new EmitterPropertyWidget();
            emitterPropView->setPanelTitle(tr("Emitter"));
            emitterPropView->setSceneNode(sceneNode);

            auto layout = new QVBoxLayout();
            layout->addWidget(transformPropView);
            transformPropView->expand();

            switch (sceneNode->getSceneNodeType()) {
                case iris::SceneNodeType::Light: {
                    layout->addWidget(lightPropView);
                    lightPropView->expand();
                    break;
                }

                case iris::SceneNodeType::Mesh: {
                    materialPropView = new MaterialPropertyWidget();
                    materialPropView->setPanelTitle(tr("Material"));
                    materialPropView->setSceneNode(sceneNode);
                    materialPropView->setDatabase(db);

                    layout->addWidget(meshPropView);
                    // layout->addWidget(nodePropView);
                    layout->addWidget(materialPropView);
                    meshPropView->expand();
                    materialPropView->expand();
                    // nodePropView->expand();
                    break;
                }

                case iris::SceneNodeType::ParticleSystem: {
                    layout->addWidget(emitterPropView);
                    emitterPropView->expand();
                    break;
                }

                default: break;
            }

            layout->addStretch();
            layout->setMargin(0);

            
            this->setLayout(layout);
        }
    }
    else {
        clearLayout(this->layout());
        this->setLayout(new QVBoxLayout());
    }
}

void SceneNodePropertiesWidget::setAssetItem(QListWidgetItem *item)
{
    if (item) {
		clearLayout(this->layout());

        shaderPropView = new ShaderPropertyWidget();
        shaderPropView->setPanelTitle("Shader Definitions");
        shaderPropView->setShaderGuid(item->data(MODEL_GUID_ROLE).toString());
        shaderPropView->setDatabase(db);
        shaderPropView->expand();

        auto layout = new QVBoxLayout();
        layout->addWidget(shaderPropView);

        layout->addStretch();
        layout->setMargin(0);

        this->setLayout(layout);
    }
    else {
        clearLayout(this->layout());
        this->setLayout(new QVBoxLayout());
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

#define NULLIFY(obj) if (obj) { obj = nullptr; }
	NULLIFY(transformPropView);
	NULLIFY(transformWidget);

	NULLIFY(materialPropView);
	NULLIFY(emitterPropView);

	NULLIFY(lightPropView);
	NULLIFY(worldPropView);
	NULLIFY(fogPropView);
	NULLIFY(meshPropView);
	NULLIFY(demoPane);
    NULLIFY(shaderPropView);
#undef NULLIFY

    while (auto item = layout->takeAt(0)) {
        if (auto widget = item->widget()) delete widget;
        if (auto childLayout = item->layout()) this->clearLayout(childLayout);
        delete item;
    }

    delete layout;
}
