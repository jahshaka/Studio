/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "animationwidget.h"
#include "ui_animationwidget.h"
#include <QMenu>
#include <QAction>
#include <QTimer>
#include <QElapsedTimer>
#include <QTime>
#include "../irisgl/src/animation/keyframeanimation.h"
#include "../irisgl/src/animation/keyframeset.h"
#include "../irisgl/src/animation/animation.h"
#include "../irisgl/src/core/scenenode.h"
#include "../irisgl/src/core/scene.h"


AnimationWidget::AnimationWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::AnimationWidget)
{
    ui->setupUi(this);

    //auto menu = new QMenu();
    addMenu = new QMenu();

    //ADD KEYFRAME BUTTON
    QAction* action = new QAction("Location", this);
    addMenu->addAction(action);
    connect(action,SIGNAL(triggered()),this,SLOT(addPosKey()));

    action = new QAction("Rotation", this);
    addMenu->addAction(action);
    connect(action,SIGNAL(triggered()),this,SLOT(addRotKey()));

    action = new QAction("Scale", this);
    addMenu->addAction(action);
    connect(action,SIGNAL(triggered()),this,SLOT(addScaleKey()));

    action = new QAction("Location + Rotation", this);
    addMenu->addAction(action);
    connect(action,SIGNAL(triggered()),this,SLOT(addPosRotKey()));

    action = new QAction("Location + Rotation + Scale", this);
    addMenu->addAction(action);
    connect(action,SIGNAL(triggered()),this,SLOT(addPosRotScaleKey()));

    //todo: add other keyframe attribs based on scene node
    addLightColorKeyAction = new QAction("Light Color", this);
    //menu->addAction(addLightColorKeyAction);
    connect(addLightColorKeyAction,SIGNAL(triggered(bool)),this,SLOT(addLightColorKey()));

    addLightIntensityKeyAction = new QAction("Light Intensity", this);
    //menu->addAction(addLightIntensityKeyAction);
    connect(addLightIntensityKeyAction,SIGNAL(triggered(bool)),this,SLOT(addLightIntensityKey()));

    addSceneBackgroundColorKeyAction = new QAction("World Color", this);
    //menu->addAction(addSceneBackgroundColorKeyAction);
    connect(addSceneBackgroundColorKeyAction,SIGNAL(triggered(bool)),this,SLOT(addSceneBackgroundColorKey()));

    addSceneActiveCameraKeyAction = new QAction("Active Camera", this);
    //menu->addAction(addSceneBackgroundColorKeyAction);
    connect(addSceneBackgroundColorKeyAction,SIGNAL(triggered(bool)),this,SLOT(addSceneActiveCameraKey()));

    ui->insertFrame->setMenu(addMenu);

    //REMOVE KEYFRAME BUTTON
    //menu = new QMenu();
    deleteMenu = new QMenu();
    action = new QAction("Location", this);
    deleteMenu->addAction(action);
    connect(action,SIGNAL(triggered()),this,SLOT(deletePosKeys()));

    action = new QAction("Rotation", this);
    deleteMenu->addAction(action);
    connect(action,SIGNAL(triggered()),this,SLOT(deleteScaleKeys()));

    action = new QAction("Scale", this);
    deleteMenu->addAction(action);
    connect(action,SIGNAL(triggered()),this,SLOT(deleteRotKeys()));

    action = new QAction("All", this);
    deleteMenu->addAction(action);
    connect(action,SIGNAL(triggered()),this,SLOT(deleteAllKeys()));


    ui->deleteFrames->setMenu(deleteMenu);

    //timer
    timer = new QTimer(this);
    //timer = new QElapsedTimer(this);
    connect(timer,SIGNAL(timeout()),this,SLOT(updateAnim()));
    elapsedTimer = new QElapsedTimer();

    time = 0;
    timerSpeed = 1.0f/60;//60 fps
    //startedTime = ui->timeline->getTimeAtCursor();
    loopAnim = false;

    //buttons that affect timer
    connect(ui->play,SIGNAL(pressed()),this,SLOT(startTimer()));
    connect(ui->stop,SIGNAL(pressed()),this,SLOT(stopTimer()));

    connect(ui->keywidgetView,SIGNAL(cursorTimeChanged(float)),this,SLOT(onObjectAnimationTimeChanged(float)));
    //connect(ui->timeline,SIGNAL(cursorMoved(float)),this,SLOT(onSceneAnimationTimeChanged(float)));

    mainTimeline = nullptr;
}

AnimationWidget::~AnimationWidget()
{
    delete ui;
}

void AnimationWidget::setScene(iris::ScenePtr scene)
{
    this->scene = scene;
}

void AnimationWidget::setSceneNode(iris::SceneNodePtr node)
{
    ui->keywidgetView->setSceneNode(node);
    //ui->timeline->setSceneNode(node);
    ui->keylabelView->setSceneNode(node);

    ui->keywidgetView->repaint();
    ui->keylabelView->repaint();
    this->node = node;

    /*
    ui->animStartTime->setValue(node->getAnimStartTime());
    //ui->timeline->setMaxTimeInSeconds(node->animLength);
    ui->animLength->setValue(node->animLength);
    setAnimLength(node->animLength);
    ui->loopAnim->setChecked(node->loopAnim);
    ui->animStartTime->setValue(node->animStartTime);
    this->showHighlight();

    switch(node->sceneNodeType)
    {
    case SceneNodeType::Light:
        addMenu->addAction(addLightColorKeyAction);
        addMenu->addAction(addLightIntensityKeyAction);
        break;

    default:
        break;
    }

    */
}

void AnimationWidget::removeNodeSpecificActions()
{
    addMenu->removeAction(addLightColorKeyAction);
    addMenu->removeAction(addLightIntensityKeyAction);

    deleteMenu->removeAction(deleteLightColorKeysAction);
    deleteMenu->removeAction(deleteLightIntensityKeysAction);
}

void AnimationWidget::updateAnim()
{
    time += elapsedTimer->nsecsElapsed()/(1000.0f*1000.0f*1000.0f);
    elapsedTimer->restart();

    ui->keywidgetView->setTime(time);
    onObjectAnimationTimeChanged(time);

    /*
    ui->timeline->setTime(time);

    if(time>ui->timeline->getEndTimeRange())
    {
        stopTimer();
    }
    */
}

void AnimationWidget::startTimer()
{
    //time = ui->timeline->getTimeAtCursor();
    time = ui->keywidgetView->getTimeAtCursor();
    timer->start(timerSpeed);
    elapsedTimer->start();
    //timer->start();

    //startedTime = ui->timeline->getTimeAtCursor();
}

void AnimationWidget::stopTimer()
{
    timer->stop();
    //ui->timeline->setTime(startedTime);
}

void AnimationWidget::setAnimLength(float length)
{
    //todo
    int scaleRatio = 30;
    ui->values->setGeometry(0,0,length*scaleRatio,ui->values->height());
    this->showHighlight();
}

void AnimationWidget::stopAnimation()
{
    stopTimer();
}

void AnimationWidget::fixLayout()
{
    ui->values->adjustSize();
    //ui->values->update();

    ui->timeControls->adjustSize();
    //ui->timeControls->update();
}

void AnimationWidget::repaintViews()
{
    //ui->timeline->repaint();
    ui->keywidgetView->repaint();
    ui->keylabelView->repaint();
}

/*
//startRange and endRange are in seconds
void AnimationWidget::setAnimationViewRange(float startRange,float endRange)
{
    //ui->timeline->set
}

//sets cursor position at time
void AnimationWidget::setCursorPositionAtTime(float timeInSeconds)
{

}
*/
void AnimationWidget::addPosKey()
{
    if(!node)
        return;

    //float seconds = ui->timeline->getTimeAtCursor();
    float seconds = ui->keywidgetView->getTimeAtCursor();
    QVector3D pos = node->pos;
    //node->transformAnim->pos->addKey(pos,seconds);

    auto frameSet = node->animation->keyFrameSet;
    frameSet->getOrCreateFrame("Translation X")->addKey(pos.x(),seconds);
    frameSet->getOrCreateFrame("Translation Y")->addKey(pos.y(),seconds);
    frameSet->getOrCreateFrame("Translation Z")->addKey(pos.z(),seconds);

    //node->updateAnimPathFromKeyFrames();

    repaintViews();
}

void AnimationWidget::addRotKey()
{
    if(!node)
        return;

    //float seconds = ui->timeline->getTimeAtCursor();
    float seconds = ui->keywidgetView->getTimeAtCursor();
    QVector3D rot = node->rot.toEulerAngles();

    auto frameSet = node->animation->keyFrameSet;
    frameSet->getOrCreateFrame("Rotation X")->addKey(rot.x(),seconds);
    frameSet->getOrCreateFrame("Rotation Y")->addKey(rot.y(),seconds);
    frameSet->getOrCreateFrame("Rotation Z")->addKey(rot.z(),seconds);
    repaintViews();
}

void AnimationWidget::addScaleKey()
{
    if(!node)
        return;

    //float seconds = ui->timeline->getTimeAtCursor();
    float seconds = ui->keywidgetView->getTimeAtCursor();
    QVector3D scale = node->scale;

    auto frameSet = node->animation->keyFrameSet;
    frameSet->getOrCreateFrame("Scale X")->addKey(scale.x(),seconds);
    frameSet->getOrCreateFrame("Scale Y")->addKey(scale.y(),seconds);
    frameSet->getOrCreateFrame("Scale Z")->addKey(scale.z(),seconds);

    repaintViews();
}

void AnimationWidget::addPosRotKey()
{
    /*
    if(node==nullptr)
        return;

    float seconds = ui->timeline->getTimeAtCursor();

    QVector3D pos = node->pos;
    node->transformAnim->pos->addKey(pos,seconds);

    //QQuaternion rot = node->rot;
    node->transformAnim->rot->addKey(node->rot,seconds);

    repaintViews();
    */
}

void AnimationWidget::addPosRotScaleKey()
{
    /*
    if(node==nullptr)
        return;

    float seconds = ui->timeline->getTimeAtCursor();

    QVector3D pos = node->pos;
    node->transformAnim->pos->addKey(pos,seconds);

    //QQuaternion rot = node->rot;
    node->transformAnim->rot->addKey(node->rot,seconds);

    QVector3D scale = node->scale;
    node->transformAnim->scale->addKey(scale,seconds);

    repaintViews();
    */
}

void AnimationWidget::addLightColorKey()
{
    /*
    if(node->sceneNodeType == SceneNodeType::Light)
    {
        auto light = static_cast<LightNode*>(node);
        float seconds = ui->timeline->getTimeAtCursor();

        light->lightAnim->color->addKey(light->getColor(),seconds);
    }
    */
}

void AnimationWidget::addLightIntensityKey()
{
    /*
    if(node->sceneNodeType == SceneNodeType::Light)
    {
        auto light = static_cast<LightNode*>(node);
        float seconds = ui->timeline->getTimeAtCursor();

        light->lightAnim->intensity->addKey(light->getIntensity(),seconds);
    }
    */
}

void AnimationWidget::addSceneBackgroundColorKey()
{
    //todo
    /*
`   if(node->sceneNodeType == SceneNodeType::World)
    {
        auto world = static_cast<WorldNode*>(node);
        float seconds = ui->timeline->getTimeAtCursor();
        world->getColor();

        light->lightAnim->intensityKey->addKey(light->getIntensity(),seconds);
    }
    */
}

void AnimationWidget::addSceneActiveCameraKey()
{
    //todo
}

void AnimationWidget::deletePosKeys()
{
    /*
    node->transformAnim->pos->clear();
    repaintViews();
    */
}

void AnimationWidget::deleteScaleKeys()
{
    /*
    node->transformAnim->scale->clear();
    repaintViews();
    */
}

void AnimationWidget::deleteRotKeys()
{
    /*
    node->transformAnim->rot->clear();
    repaintViews();
    */
}

void AnimationWidget::deleteAllKeys()
{
    /*
    node->transformAnim->pos->clear();
    node->transformAnim->scale->clear();
    node->transformAnim->rot->clear();

    switch(node->sceneNodeType)
    {
        case SceneNodeType::Light:
        {
            auto light = static_cast<LightNode*>(node);
            light->lightAnim->color->clear();
            light->lightAnim->intensity->clear();
        }
        break;
    default:
        break;

    }

    repaintViews();
    */
}

void AnimationWidget::deleteLightColorKeys()
{

}

void AnimationWidget::deleteLightIntensityKeys()
{

}

void AnimationWidget::timeEditChanged(QTime time)
{
    int totalSecs = time.second()+time.minute()*60;
    this->setAnimLength(totalSecs);
    this->showHighlight();
}

void AnimationWidget::setAnimstart(int time)
{
    /*
    if(node==nullptr)
        return;
    node->animStartTime = time;
    this->showHighlight();
    */
}

void AnimationWidget::onObjectAnimationTimeChanged(float timeInSeconds)
{
    if(!!node)
    {
        node->updateAnimation(timeInSeconds);
    }
}

void AnimationWidget::onSceneAnimationTimeChanged(float timeInSeconds)
{
    if(!!scene)
    {
        scene->updateSceneAnimation(timeInSeconds);
    }
}
