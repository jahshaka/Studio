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

#include "../irisgl/src/core/scenenode.h"

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

        xScaleChanged(defaultStateNode->scale.x());
        yScaleChanged(defaultStateNode->scale.x());
        zScaleChanged(defaultStateNode->scale.x());

        ui->xpos->setValue(0);
        ui->ypos->setValue(0);
        ui->zpos->setValue(0);

        ui->xrot->setValue(0);
        ui->yrot->setValue(0);
        ui->zrot->setValue(0);

        ui->xscale->setValue(defaultStateNode->scale.x());
        ui->yscale->setValue(defaultStateNode->scale.y());
        ui->zscale->setValue(defaultStateNode->scale.z());
    }
}

void TransformEditor::setSceneNode(QSharedPointer<iris::SceneNode> sceneNode)
{
    this->sceneNode = defaultStateNode = sceneNode;

    if (!!sceneNode) {
        ui->xpos->setValue(sceneNode->pos.x());
        ui->ypos->setValue(sceneNode->pos.y());
        ui->zpos->setValue(sceneNode->pos.z());

        auto rot = sceneNode->rot.toEulerAngles();
        ui->xrot->setValue(rot.x());
        ui->yrot->setValue(rot.y());
        ui->zrot->setValue(rot.z());

        ui->xscale->setValue(sceneNode->scale.x());
        ui->yscale->setValue(sceneNode->scale.y());
        ui->zscale->setValue(sceneNode->scale.z());
    }
}

void TransformEditor::xPosChanged(double value)
{
    if (!!sceneNode) {
        sceneNode->pos.setX(value);
    }
}

void TransformEditor::yPosChanged(double value)
{
    if (!!sceneNode) {
        sceneNode->pos.setY(value);
    }
}

void TransformEditor::zPosChanged(double value)
{
    if (!!sceneNode) {
        sceneNode->pos.setZ(value);
    }
}

/**
 * rotation change callbacks
 */
void TransformEditor::xRotChanged(double value)
{
    if (!!sceneNode) {
        auto rot = sceneNode->rot.toEulerAngles();
        rot.setX(value);
        sceneNode->rot = QQuaternion::fromEulerAngles(rot);
    }
}

void TransformEditor::yRotChanged(double value)
{
    if (!!sceneNode) {
        auto rot = sceneNode->rot.toEulerAngles();
        rot.setY(value);
        sceneNode->rot = QQuaternion::fromEulerAngles(rot);
    }
}

void TransformEditor::zRotChanged(double value)
{
    if (!!sceneNode) {
        auto rot = sceneNode->rot.toEulerAngles();
        rot.setZ(value);
        sceneNode->rot = QQuaternion::fromEulerAngles(rot);
    }
}

/**
 * scale change callbacks
 */
void TransformEditor::xScaleChanged(double value)
{
    if (!!sceneNode) {
        sceneNode->scale.setX(value);
    }
}

void TransformEditor::yScaleChanged(double value)
{
    if (!!sceneNode) {
        sceneNode->scale.setY(value);
    }
}

void TransformEditor::zScaleChanged(double value)
{
    if (!!sceneNode) {
        sceneNode->scale.setZ(value);
    }
}

