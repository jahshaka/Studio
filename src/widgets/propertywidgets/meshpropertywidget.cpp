/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "meshpropertywidget.h"
#include "../filepickerwidget.h"
#include "../../irisgl/src/scenegraph/meshnode.h"
#include "../../globals.h"
#include "../sceneviewwidget.h"
#include "../comboboxwidget.h"

MeshPropertyWidget::MeshPropertyWidget()
{
    //meshPicker = this->addFilePicker("Mesh Path");
	faceCullMode = this->addComboBox("Face Cull Mode");
	faceCullMode->addItem("Front");
	faceCullMode->addItem("Back");
	faceCullMode->addItem("None");
	faceCullMode->addItem("DefinedInMaterial");
	connect(faceCullMode, SIGNAL(currentIndexChanged(const QString&)), this, SLOT(onCullModeChanged(const QString&)));

    //connect(meshPicker, SIGNAL(onPathChanged(QString)), SLOT(onMeshPathChanged(QString)));
}

MeshPropertyWidget::~MeshPropertyWidget()
{

}

void MeshPropertyWidget::onMeshPathChanged(const QString &path)
{
    //Globals::sceneViewWidget->makeCurrent();
    //meshNode->setMesh(path);
    //Globals::sceneViewWidget->doneCurrent();
}

void MeshPropertyWidget::onCullModeChanged(const QString& cullMode)
{
	if (cullMode == "Front")
		meshNode->setFaceCullingMode(iris::FaceCullingMode::Front);
	else if (cullMode == "Back")
		meshNode->setFaceCullingMode(iris::FaceCullingMode::Back);
	else if (cullMode == "None")
		meshNode->setFaceCullingMode(iris::FaceCullingMode::None);
	else
		meshNode->setFaceCullingMode(iris::FaceCullingMode::DefinedInMaterial);
}

void MeshPropertyWidget::setSceneNode(iris::SceneNodePtr sceneNode)
{
    if (!!sceneNode && sceneNode->sceneNodeType == iris::SceneNodeType::Mesh) {
        this->meshNode = sceneNode.staticCast<iris::MeshNode>();
        //meshPicker->setFilepath(meshNode->meshPath);

		switch (meshNode->getFaceCullingMode())
		{
		case iris::FaceCullingMode::Back:
			faceCullMode->setCurrentItem("Back");
			break;
		case iris::FaceCullingMode::Front:
			faceCullMode->setCurrentItem("Front");
			break;
		case iris::FaceCullingMode::None:
			faceCullMode->setCurrentItem("None");
			break;
		case iris::FaceCullingMode::DefinedInMaterial:
			faceCullMode->setCurrentItem("DefinedInMaterial");
			break;
		}
    } else {
        this->meshNode.clear();
    }
}
