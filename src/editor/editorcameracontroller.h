/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef EDITORCAMERACONTROLLER_H
#define EDITORCAMERACONTROLLER_H

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

using namespace Qt3DRender;
using namespace Qt3DLogic;

class EditorCameraController:public Qt3DCore::QEntity
{
    Q_OBJECT

    QCamera* camera;
    Qt3DInput::QMouseDevice* mouse;
    Qt3DInput::QKeyboardDevice* keyboard;
    Qt3DInput::QLogicalDevice* logic;
    QFrameAction* frameAction;



    Qt3DInput::QAction *leftMouseButtonAction;
    Qt3DInput::QAction *middleMouseButtonAction;
    Qt3DInput::QAction *rightMouseButtonAction;
    Qt3DInput::QAction *altButtonAction;
    Qt3DInput::QAction *shiftButtonAction;

    Qt3DInput::QAxis *mouseXAxis;
    Qt3DInput::QAxis *mouseYAxis;
    Qt3DInput::QAxis *txAxis;
    Qt3DInput::QAxis *tyAxis;
    Qt3DInput::QAxis *tzAxis;

    Qt3DInput::QAxis *wheelAxis;

    Qt3DInput::QActionInput *leftMouseButtonInput;
    Qt3DInput::QActionInput *middleMouseButtonInput;
    Qt3DInput::QActionInput *rightMouseButtonInput;
    Qt3DInput::QActionInput *altButtonInput;
    Qt3DInput::QActionInput *shiftButtonInput;

    Qt3DInput::QAnalogAxisInput *mouseXInput;
    Qt3DInput::QAnalogAxisInput *mouseYInput;

    Qt3DInput::QAnalogAxisInput *mouseWheelInput;

    Qt3DInput::QButtonAxisInput *keyboardXPosInput;
    Qt3DInput::QButtonAxisInput *keyboardXNegInput;
    Qt3DInput::QButtonAxisInput *keyboardYPosInput;
    Qt3DInput::QButtonAxisInput *keyboardYNegInput;

    float lookSpeed;
    float linearSpeed;

public:
    EditorCameraController(Qt3DCore::QEntity* parent,QCamera* cam);

    QCamera* getCamera();
    void setCamera(QCamera* cam);

    QVector3D getPos();

    void setLinearSpeed(float speed);
    float getLinearSpeed();

    void setLookSpeed(float speed);
    float getLookSpeed();

    QVector3D unproject(int viewPortWidth, int viewPortHeight,QPoint pos);

private slots:
    void onFrame(float dt);
};

#endif // EDITORCAMERACONTROLLER_H
