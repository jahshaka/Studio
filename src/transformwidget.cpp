/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Karsten Becker <jahshaka@gmail.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "transformwidget.h"
#include "scenenode.h"
#include "ui_transformsliders.h"


#define TRANSFORM_VALUE_SCALE 100

#define TRANSLATION_RANGE 3000
#define ROTATION_RANGE 3000
#define SCALE_RANGE 3000

TransformWidget::TransformWidget(QWidget* parent):
    QWidget(parent)
{
    ui = new Ui::TransformSliders();
    ui->setupUi(this);

    connect(ui->xPos,SIGNAL(valueChanged(int)),this,SLOT(sliderXPos(int)));
    connect(ui->yPos,SIGNAL(valueChanged(int)),this,SLOT(sliderYPos(int)));
    connect(ui->zPos,SIGNAL(valueChanged(int)),this,SLOT(sliderZPos(int)));
    ui->xPos->setValueRange(-TRANSLATION_RANGE,TRANSLATION_RANGE);
    ui->xPos->setLabelText("x");
    ui->yPos->setValueRange(-3000,3000);
    ui->yPos->setLabelText("y");
    ui->zPos->setValueRange(-3000,3000);
    ui->zPos->setLabelText("z");

    connect(ui->xScale,SIGNAL(valueChanged(int)),this,SLOT(sliderXScale(int)));
    connect(ui->yScale,SIGNAL(valueChanged(int)),this,SLOT(sliderYScale(int)));
    connect(ui->zScale,SIGNAL(valueChanged(int)),this,SLOT(sliderZScale(int)));
    ui->xScale->setValueRange(-800,800);
    ui->xScale->setLabelText("x");
    ui->yScale->setValueRange(-800,800);
    ui->yScale->setLabelText("y");
    ui->zScale->setValueRange(-800,800);
    ui->zScale->setLabelText("z");

    connect(ui->xRot,SIGNAL(valueChanged(int)),this,SLOT(sliderXRot(int)));
    connect(ui->yRot,SIGNAL(valueChanged(int)),this,SLOT(sliderYRot(int)));
    connect(ui->zRot,SIGNAL(valueChanged(int)),this,SLOT(sliderZRot(int)));
    ui->xRot->setValueRange(-50000,50000);
    ui->xRot->setLabelText("x");
    ui->yRot->setValueRange(-50000,50000);
    ui->yRot->setLabelText("y");
    ui->zRot->setValueRange(-50000,50000);
    ui->zRot->setLabelText("z");

    //clear buttons
    connect(ui->clearPos,SIGNAL(clicked(bool)),this,SLOT(resetPos()));
    connect(ui->clearRot,SIGNAL(clicked(bool)),this,SLOT(resetRot()));
    connect(ui->clearScale,SIGNAL(clicked(bool)),this,SLOT(resetScale()));

    activeSceneNode = nullptr;
}

void TransformWidget::setActiveSceneNode(SceneNode* node)
{
    activeSceneNode = nullptr;

    //pos
    ui->xPos->setValue(node->pos.x()*100);
    ui->yPos->setValue(node->pos.y()*100);
    ui->zPos->setValue(node->pos.z()*100);

    //scale
    ui->xScale->setValue(node->scale.x()*100);
    ui->yScale->setValue(node->scale.y()*100);
    ui->zScale->setValue(node->scale.z()*100);

    //rot
    //auto rot = node->rot.toEulerAngles();
    auto rot = node->rot;
    ui->xRot->setValue(rot.x()*100);
    ui->yRot->setValue(rot.y()*100);
    ui->zRot->setValue(rot.z()*100);

    //put this here to node wont be updated when the values are set
    activeSceneNode = node;
}

void TransformWidget::sliderXPos(int val)
{
    if(activeSceneNode!=nullptr)
    {
        activeSceneNode->pos.setX(val/100.0f);//scale it down
        activeSceneNode->_updateTransform();
    }
}

void TransformWidget::sliderYPos(int val)
{
    if(activeSceneNode!=nullptr)
    {
        activeSceneNode->pos.setY(val/100.0f);//scale it down
        activeSceneNode->_updateTransform();
    }
}

void TransformWidget::sliderZPos(int val)
{
    if(activeSceneNode!=nullptr)
    {
        activeSceneNode->pos.setZ(val/100.0f);//scale it down
        activeSceneNode->_updateTransform();
    }
}

void TransformWidget::sliderXScale(int val)
{
    if(activeSceneNode!=nullptr)
    {
        activeSceneNode->scale.setX(val/100.0f);//scale it down
        activeSceneNode->_updateTransform();
    }
}

void TransformWidget::sliderYScale(int val)
{
    if(activeSceneNode!=nullptr)
    {
        activeSceneNode->scale.setY(val/100.0f);//scale it down
        activeSceneNode->_updateTransform();
    }
}

void TransformWidget::sliderZScale(int val)
{
    if(activeSceneNode!=nullptr)
    {
        activeSceneNode->scale.setZ(val/100.0f);//scale it down
        activeSceneNode->_updateTransform();
    }
}

void TransformWidget::sliderXRot(int val)
{
    if(activeSceneNode!=nullptr)
    {
        auto euler = activeSceneNode->rot;//.toEulerAngles();
        euler.setY(val/100.0f);//x is pitch, which is
        activeSceneNode->rot=euler;
        //activeSceneNode->rot=QQuaternion::fromEulerAngles(euler);
        activeSceneNode->_updateTransform();
    }
}

void TransformWidget::sliderYRot(int val)
{
    if(activeSceneNode!=nullptr)
    {
        auto euler = activeSceneNode->rot;//.toEulerAngles();
        euler.setX(val/100.0f);//y is yaw, which is X
        activeSceneNode->rot=euler;
        activeSceneNode->_updateTransform();
    }
}

void TransformWidget::sliderZRot(int val)
{
    if(activeSceneNode!=nullptr)
    {
        auto euler = activeSceneNode->rot;//.toEulerAngles();
        euler.setZ(val/100.0f);//z is roll
        activeSceneNode->rot=euler;
        activeSceneNode->_updateTransform();
    }
}

void TransformWidget::resetScale()
{
    ui->xScale->setValue(100);
    ui->yScale->setValue(100);
    ui->zScale->setValue(100);
}

void TransformWidget::resetPos()
{
    ui->xPos->setValue(0);
    ui->yPos->setValue(0);
    ui->zPos->setValue(0);
}

void TransformWidget::resetRot()
{
    ui->xRot->setValue(0);
    ui->yRot->setValue(0);
    ui->zRot->setValue(0);
}
