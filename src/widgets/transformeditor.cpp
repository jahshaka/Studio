/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "transformeditor.h"
#include "ui_transformeditor.h"

#include "../irisgl/src/scenegraph/scenenode.h"

TransformEditor::TransformEditor(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::TransformEditor)
{
    ui->setupUi(this);

    connect(ui->xpos,   SIGNAL(valueChanged(double)),   SLOT(xPosChanged(double)));
    connect(ui->ypos,   SIGNAL(valueChanged(double)),   SLOT(yPosChanged(double)));
    connect(ui->zpos,   SIGNAL(valueChanged(double)),   SLOT(zPosChanged(double)));

    connect(ui->xrot,   SIGNAL(valueChanged(double)),   SLOT(xRotChanged(double)));
    connect(ui->yrot,   SIGNAL(valueChanged(double)),   SLOT(yRotChanged(double)));
    connect(ui->zrot,   SIGNAL(valueChanged(double)),   SLOT(zRotChanged(double)));

    connect(ui->xscale, SIGNAL(valueChanged(double)),   SLOT(xScaleChanged(double)));
    connect(ui->yscale, SIGNAL(valueChanged(double)),   SLOT(yScaleChanged(double)));
    connect(ui->zscale, SIGNAL(valueChanged(double)),   SLOT(zScaleChanged(double)));

    connect(ui->resetBtn, SIGNAL(clicked(bool)),        SLOT(onResetBtnClicked()));
}

TransformEditor::~TransformEditor()
{
    delete ui;
}

void TransformEditor::onResetBtnClicked()
{
    // in the future, this should be the imported models defaults instead of assumed scene's
    if (!!sceneNode) {
        xPosChanged(0);
        yPosChanged(0);
        zPosChanged(0);

        xRotChanged(0);
        yRotChanged(0);
        zRotChanged(0);

        auto scale = defaultStateNode->getLocalScale();
        xScaleChanged(scale.x());
        yScaleChanged(scale.y());
        zScaleChanged(scale.z());

        ui->xpos->setValue(0);
        ui->ypos->setValue(0);
        ui->zpos->setValue(0);

        ui->xrot->setValue(0);
        ui->yrot->setValue(0);
        ui->zrot->setValue(0);

        ui->xscale->setValue(scale.x());
        ui->yscale->setValue(scale.y());
        ui->zscale->setValue(scale.z());
    }
}

void TransformEditor::setSceneNode(QSharedPointer<iris::SceneNode> sceneNode)
{
    this->sceneNode = defaultStateNode = sceneNode;

    if (!!sceneNode) {
		refreshUi();
    }
}

void TransformEditor::refreshUi()
{
	// ui might have a null node
	if (!!sceneNode) {
		auto pos = sceneNode->getLocalPos();
		ui->xpos->setValue(pos.x());
		ui->ypos->setValue(pos.y());
		ui->zpos->setValue(pos.z());

		auto rot = sceneNode->getLocalRot().toEulerAngles();
		ui->xrot->setValue(rot.x());
		ui->yrot->setValue(rot.y());
		ui->zrot->setValue(rot.z());

		auto scale = sceneNode->getLocalScale();
		ui->xscale->setValue(scale.x());
		ui->yscale->setValue(scale.y());
		ui->zscale->setValue(scale.z());
	}
}

void TransformEditor::xPosChanged(double value)
{
    if (!!sceneNode) {
        auto pos = sceneNode->getLocalPos();
        pos.setX(value);
        sceneNode->setLocalPos(pos);
    }
}

void TransformEditor::yPosChanged(double value)
{
    if (!!sceneNode) {
        auto pos = sceneNode->getLocalPos();
        pos.setY(value);
        sceneNode->setLocalPos(pos);
    }
}

void TransformEditor::zPosChanged(double value)
{
    if (!!sceneNode) {
        auto pos = sceneNode->getLocalPos();
        pos.setZ(value);
        sceneNode->setLocalPos(pos);
    }
}

/**
 * rotation change callbacks
 */
void TransformEditor::xRotChanged(double value)
{
    if (!!sceneNode) {
        auto rot = sceneNode->getLocalRot().toEulerAngles();
        rot.setX(value);
        sceneNode->setLocalRot(QQuaternion::fromEulerAngles(rot));
    }
}

void TransformEditor::yRotChanged(double value)
{
    if (!!sceneNode) {
        auto rot = sceneNode->getLocalRot().toEulerAngles();
        rot.setY(value);
        sceneNode->setLocalRot(QQuaternion::fromEulerAngles(rot));
    }
}

void TransformEditor::zRotChanged(double value)
{
    if (!!sceneNode) {
        auto rot = sceneNode->getLocalRot().toEulerAngles();
        rot.setZ(value);
        sceneNode->setLocalRot(QQuaternion::fromEulerAngles(rot));
    }
}

/**
 * scale change callbacks
 */
void TransformEditor::xScaleChanged(double value)
{
    if (!!sceneNode) {
        auto scale = sceneNode->getLocalScale();
        scale.setX(value);
        sceneNode->setLocalScale(scale);
    }
}

void TransformEditor::yScaleChanged(double value)
{
    if (!!sceneNode) {
        auto scale = sceneNode->getLocalScale();
        scale.setY(value);
        sceneNode->setLocalScale(scale);
    }
}

void TransformEditor::zScaleChanged(double value)
{
    if (!!sceneNode) {
        auto scale = sceneNode->getLocalScale();
        scale.setZ(value);
        sceneNode->setLocalScale(scale);
    }
}

