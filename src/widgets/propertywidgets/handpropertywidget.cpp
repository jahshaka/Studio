/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include <irisgl/SceneGraph.h>
#include "handpropertywidget.h"
#include "../filepickerwidget.h"
#include "../../irisgl/src/scenegraph/meshnode.h"
#include "../../globals.h"
#include "../sceneviewwidget.h"
#include "../comboboxwidget.h"
#include "../hfloatsliderwidget.h"

HandPropertyWidget::HandPropertyWidget()
{
	poseType = this->addComboBox("Pose Type");
	poseType->addItem("Grab");
	connect(poseType, SIGNAL(currentIndexChanged(const QString&)), this, SLOT(onPoseTypeChanged(const QString&)));

	poseFactor = this->addFloatValueSlider("Pose Factor", 0.0f, 1.0f);
    connect(poseFactor, SIGNAL(valueChanged(float)), this, SLOT(onPoseFactorChanged(float)));
}

HandPropertyWidget::~HandPropertyWidget()
{

}

void HandPropertyWidget::onPoseTypeChanged(const QString&)
{

}

void HandPropertyWidget::onPoseFactorChanged(float value)
{
	if (this->grabNode) {
		this->grabNode->poseFactor = value;
	}
}

void HandPropertyWidget::setSceneNode(iris::SceneNodePtr sceneNode)
{
	poseType->blockSignals(true);
	poseFactor->blockSignals(true);
    if (!!sceneNode && sceneNode->sceneNodeType == iris::SceneNodeType::Grab) {
        this->grabNode = sceneNode.staticCast<iris::GrabNode>();

		switch (grabNode->handPose->getPoseType())
		{
		default:
			poseType->setCurrentItem("Grab");
			break;
		}

		poseFactor->setValue(grabNode->poseFactor);
    } else {
        this->grabNode.clear();
    }
	poseType->blockSignals(false);
	poseFactor->blockSignals(false);
}
