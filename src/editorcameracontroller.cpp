/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Karsten Becker <jahshaka@gmail.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include <Qt3DCore/QEntity>
#include <Qt3DRender/QCamera>
#include <Qt3DInput/QAxis>
#include <Qt3DInput/QAnalogAxisInput>
#include <Qt3DInput/QButtonAxisInput>
#include <Qt3DInput/QAction>
#include <Qt3DInput/QActionInput>
#include <Qt3DInput/QLogicalDevice>
#include <Qt3DInput/QKeyboardDevice>
#include <Qt3DInput/QMouseDevice>
#include <Qt3DInput/QMouseEvent>
#include <Qt3DLogic/QFrameAction>
#include "editorcameracontroller.h"
#include <QVector3D>

using namespace Qt3DRender;
using namespace Qt3DLogic;

EditorCameraController::EditorCameraController(Qt3DCore::QEntity* parent,QCamera* cam):
    Qt3DCore::QEntity(parent)
{
    camera = cam;

    lookSpeed = 200;
    linearSpeed = 1;

    mouse = new Qt3DInput::QMouseDevice();
    keyboard = new Qt3DInput::QKeyboardDevice();
    logic = new Qt3DInput::QLogicalDevice();

    leftMouseButtonAction = new Qt3DInput::QAction();
    leftMouseButtonInput = new Qt3DInput::QActionInput();

    middleMouseButtonAction = new Qt3DInput::QAction();
    middleMouseButtonInput = new Qt3DInput::QActionInput();

    rightMouseButtonAction = new Qt3DInput::QAction();
    rightMouseButtonInput = new Qt3DInput::QActionInput();

    txAxis = new Qt3DInput::QAxis();
    tyAxis = new Qt3DInput::QAxis();

    wheelAxis = new Qt3DInput::QAxis();
    mouseWheelInput = new Qt3DInput::QAnalogAxisInput();

    keyboardXPosInput = new Qt3DInput::QButtonAxisInput();
    keyboardXNegInput = new Qt3DInput::QButtonAxisInput();
    keyboardYPosInput = new Qt3DInput::QButtonAxisInput();
    keyboardYNegInput = new Qt3DInput::QButtonAxisInput();


    frameAction = new QFrameAction();
    this->addComponent(frameAction);

    QObject::connect(frameAction,SIGNAL(triggered(float)),this,SLOT(onFrame(float)));

    //init
    leftMouseButtonInput->setButtons(QVector<int>() << Qt::LeftButton);
    leftMouseButtonInput->setSourceDevice(mouse);
    leftMouseButtonAction->addInput(leftMouseButtonInput);

    middleMouseButtonInput->setButtons(QVector<int>() << Qt::MiddleButton);
    middleMouseButtonInput->setSourceDevice(mouse);
    middleMouseButtonAction->addInput(middleMouseButtonInput);

    rightMouseButtonInput->setButtons(QVector<int>() << Qt::RightButton);
    rightMouseButtonInput->setSourceDevice(mouse);
    rightMouseButtonAction->addInput(rightMouseButtonInput);

    //mouse movement
    mouseXAxis = new Qt3DInput::QAxis();
    mouseYAxis = new Qt3DInput::QAxis();

    mouseXInput = new Qt3DInput::QAnalogAxisInput();
    mouseYInput = new Qt3DInput::QAnalogAxisInput();

    mouseXInput->setAxis(Qt3DInput::QMouseDevice::X);
    mouseXInput->setSourceDevice(mouse);
    mouseXAxis->addInput(mouseXInput);

    mouseYInput->setAxis(Qt3DInput::QMouseDevice::Y);
    mouseYInput->setSourceDevice(mouse);
    mouseYAxis->addInput(mouseYInput);

    //positive x-axis
    keyboardXPosInput->setButtons(QVector<int>() << Qt::Key_Right);
    keyboardXPosInput->setScale(1.0f);
    keyboardXPosInput->setSourceDevice(keyboard);
    txAxis->addInput(keyboardXPosInput);

    //negative x-axis
    keyboardXNegInput->setButtons(QVector<int>() << Qt::Key_Left);
    keyboardXNegInput->setScale(-1.0f);
    keyboardXNegInput->setSourceDevice(keyboard);
    txAxis->addInput(keyboardXNegInput);


    //postive y-axis
    keyboardYPosInput->setButtons(QVector<int>() << Qt::Key_Up);
    keyboardYPosInput->setScale(1.0f);
    keyboardYPosInput->setSourceDevice(keyboard);
    tyAxis->addInput(keyboardYPosInput);

    //negative y-axis
    keyboardYNegInput->setButtons(QVector<int>() << Qt::Key_Down);
    keyboardYNegInput->setScale(-1.0f);
    keyboardYNegInput->setSourceDevice(keyboard);
    tyAxis->addInput(keyboardYNegInput);


    logic->addAxis(mouseXAxis);
    logic->addAxis(mouseYAxis);

    logic->addAxis(txAxis);
    logic->addAxis(tyAxis);

    logic->addAction(leftMouseButtonAction);
    logic->addAction(middleMouseButtonAction);
    logic->addAction(rightMouseButtonAction);

    this->addComponent(logic);
}

QCamera* EditorCameraController::getCamera()
{
    return camera;
}

void EditorCameraController::setCamera(QCamera* cam)
{
    this->camera = cam;
}

QVector3D EditorCameraController::getPos()
{
    return this->camera->position();
}

void EditorCameraController::setLinearSpeed(float speed)
{
    linearSpeed = speed;
}

float EditorCameraController::getLinearSpeed()
{
    return linearSpeed;
}

void EditorCameraController::setLookSpeed(float speed)
{
    lookSpeed = speed;
}

float EditorCameraController::getLookSpeed()
{
    return lookSpeed;
}

//returns ray direction
//stolen from qtlabs 3d editor
QVector3D EditorCameraController::unproject(int viewPortWidth, int viewPortHeight,QPoint pos)
{
    auto projectionMatrix = camera->projectionMatrix();
    auto viewMatrix = camera->viewMatrix();

    float x = ((2.0f * pos.x()) / viewPortWidth) - 1.0f;
    float y = 1.0f - ((2.0f * pos.y()) / viewPortHeight);

    QVector4D ray = projectionMatrix.inverted() * QVector4D(x, y, -1.0f, 1.0f);
    ray.setZ(-1.0f);
    ray.setW(0.0f);
    ray = viewMatrix.inverted() * ray;
    return ray.toVector3D().normalized();
}

void EditorCameraController::onFrame(float dt)
{
    if(rightMouseButtonAction->isActive())
    {

        camera->tilt((mouseYAxis->value() * lookSpeed) * dt);
        camera->pan((mouseXAxis->value() * lookSpeed) * dt, QVector3D(0,1,0));
    }

    if(middleMouseButtonAction->isActive())
    {
        auto panSpeed = 1.0f;

        camera->translate(QVector3D(-mouseXAxis->value()*panSpeed,-mouseYAxis->value()*panSpeed,0));
        //qDebug()<<"pan";
    }

    QVector3D upVector(0,1,0);
    QVector3D viewVector = camera->viewCenter() - camera->position();
    auto x = QVector3D::crossProduct(viewVector, upVector).normalized();
    //auto z = viewVector.normalized();
    auto z = QVector3D::crossProduct(upVector,x).normalized();

    camera->translateWorld(txAxis->value()*x*linearSpeed);
    camera->translateWorld(tyAxis->value()*z*linearSpeed);
    //camera->translateWorld(QVector3D(txAxis->value()*linearSpeed*x,0,tyAxis->value()*linearSpeed*z));

}
