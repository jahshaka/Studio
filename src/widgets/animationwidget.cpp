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
#include <QToolButton>
#include <QTime>
#include "../irisgl/src/animation/keyframeanimation.h"
#include "../irisgl/src/animation/keyframeset.h"
#include "../irisgl/src/animation/animation.h"
#include "../irisgl/src/animation/propertyanim.h"
#include "../irisgl/src/animation/animableproperty.h"
#include "../irisgl/src/core/scenenode.h"
#include "../irisgl/src/core/scene.h"


AnimationWidget::AnimationWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::AnimationWidget)
{
    ui->setupUi(this);

    ui->keywidgetView->setLabelWidget(ui->keylabelView);
    ui->keylabelView->setAnimWidget(this);

    //timer
    timer = new QTimer(this);
    //timer = new QElapsedTimer(this);
    connect(timer,SIGNAL(timeout()),this,SLOT(updateAnim()));
    elapsedTimer = new QElapsedTimer();

    timeAtCursor = 0;
    timerSpeed = 1.0f/60;//60 fps
    //startedTime = ui->timeline->getTimeAtCursor();
    loopAnim = false;

    //buttons that affect timer
    // connect(ui->play,SIGNAL(pressed()),this,SLOT(startTimer()));
    // connect(ui->stop,SIGNAL(pressed()),this,SLOT(stopTimer()));

    //connect(ui->keywidgetView,SIGNAL(cursorTimeChanged(float)),this,SLOT(onObjectAnimationTimeChanged(float)));
    connect(ui->timeline,SIGNAL(cursorMoved(float)),this,SLOT(onSceneAnimationTimeChanged(float)));
    connect(ui->timeline,SIGNAL(cursorMoved(float)),ui->keywidgetView,SLOT(cursorTimeChanged(float)));
    connect(ui->timeline,SIGNAL(timeRangeChanged(float,float)),ui->keywidgetView,SLOT(setTimeRange(float,float)));
    connect(ui->keywidgetView,SIGNAL(timeRangeChanged(float,float)),ui->timeline,SLOT(setTimeRange(float,float)));

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

    if (!!node) {
        scene = node->scene;

        animableProperties = node->getAnimableProperties();

        // rebuild menu
        auto menu = new QMenu();
        int index = 0;
        for (auto prop : animableProperties) {
            auto action = new QAction();
            action->setText(prop.name);
            action->setData(index++);

            menu->addAction(action);
        }

        animation = node->animation;
        connect(menu, SIGNAL(triggered(QAction*)), this ,SLOT(addPropertyKey(QAction*)));
        ui->insertFrame->setMenu(menu);
    } else {
        ui->insertFrame->setMenu(new QMenu());
        animation.clear();
    }
}

void AnimationWidget::updateAnim()
{
    timeAtCursor += elapsedTimer->nsecsElapsed()/(1000.0f*1000.0f*1000.0f);
    elapsedTimer->restart();

    ui->keywidgetView->setTime(timeAtCursor);
    onObjectAnimationTimeChanged(timeAtCursor);
}

void AnimationWidget::startTimer()
{
    timeAtCursor = ui->keywidgetView->getTimeAtCursor();
    timer->start(timerSpeed);
    elapsedTimer->start();
}

void AnimationWidget::stopTimer()
{
    timer->stop();
}

void AnimationWidget::setAnimLength(float length)
{
}

void AnimationWidget::stopAnimation()
{
    stopTimer();
}

void AnimationWidget::fixLayout()
{
}

void AnimationWidget::repaintViews()
{
    //ui->timeline->repaint();
    ui->keywidgetView->repaint();
    ui->keylabelView->repaint();
}

iris::PropertyAnim *AnimationWidget::createPropertyAnim(const iris::AnimableProperty& prop)
{
    iris::PropertyAnim* anim;

    switch (prop.type) {
    case iris::AnimablePropertyType::Float:
        anim = new iris::FloatPropertyAnim();
    break;

    case iris::AnimablePropertyType::Vector3:
        anim = new iris::Vector3DPropertyAnim();
    break;

    case iris::AnimablePropertyType::Color:
        anim = new iris::ColorPropertyAnim();
    break;

    default:
        Q_ASSERT(false);
    }

    anim->setName(prop.name);
    return anim;
}

void AnimationWidget::setLooping(bool loop)
{
    if (!!node) {
        node->animation->setLooping(loop);
    }
}

void AnimationWidget::addPropertyKey(QAction *action)
{
    if (!animation)
        return;

    auto index = action->data().toInt();
    auto animProp = animableProperties[index];

    // Get or create property
    iris::PropertyAnim* anim;
    if (animation->hasPropertyAnim(animProp.name))
    {
        anim = animation->getPropertyAnim(animProp.name);
    } else {
        anim = createPropertyAnim(animProp);
        anim->setName(animProp.name);
        animation->addPropertyAnim(anim);
        ui->keylabelView->addProperty(animProp.name);
    }

    auto val = node->getAnimPropertyValue(animProp.name);

    switch (animProp.type) {
    case iris::AnimablePropertyType::Float:
    {
        auto value = val.toFloat();
        anim->getKeyFrames()[0].keyFrame->
                addKey(value,this->timeAtCursor);
    }
        break;
    case iris::AnimablePropertyType::Vector3:
    {
        auto value = val.value<QVector3D>();
        auto frames =  anim->getKeyFrames();
        frames[0].keyFrame->addKey(value.x(), this->timeAtCursor);
        frames[1].keyFrame->addKey(value.y(), this->timeAtCursor);
        frames[2].keyFrame->addKey(value.z(), this->timeAtCursor);
    }
        break;
    case iris::AnimablePropertyType::Color:
    {
        auto value = val.value<QColor>();
        auto frames =  anim->getKeyFrames();
        frames[0].keyFrame->addKey(value.redF(),    this->timeAtCursor);
        frames[1].keyFrame->addKey(value.greenF(),  this->timeAtCursor);
        frames[2].keyFrame->addKey(value.blueF(),   this->timeAtCursor);
        frames[3].keyFrame->addKey(value.alphaF(),  this->timeAtCursor);
    }
        break;
    }


    this->repaintViews();
}

void AnimationWidget::timeEditChanged(QTime time)
{
    int totalSecs = time.second()+time.minute()*60;
    this->setAnimLength(totalSecs);
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
    this->timeAtCursor = timeInSeconds;
    if(!!scene)
    {
        scene->updateSceneAnimation(timeInSeconds);
    }
}

