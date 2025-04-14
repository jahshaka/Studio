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
#include "../irisgl/src/scenegraph/scenenode.h"
#include "../irisgl/src/scenegraph/scene.h"
#include "../irisgl/src/scenegraph/meshnode.h"

#include "../irisgl/src/graphics/material.h"
#include "../irisgl/src/materials/custommaterial.h"

#include "keyframewidget.h"
#include "keyframecurvewidget.h"
#include "animationwidgetdata.h"
#include "createanimationwidget.h"
#include "../dialogs/getnamedialog.h"


AnimationWidget::AnimationWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::AnimationWidget)
{
    ui->setupUi(this);

    connect(ui->addAnimBtn,SIGNAL(clicked(bool)), this, SLOT(addAnimation()));
    connect(ui->deleteAnimBtn,SIGNAL(clicked(bool)), this, SLOT(deleteAnimation()));
    connect(ui->animList,SIGNAL(currentTextChanged(QString)), this, SLOT(animationChanged(QString)));
    connect(ui->loopCheckBox,SIGNAL(clicked(bool)), this, SLOT(setLooping(bool)));

    animWidgetData = new AnimationWidgetData();

    keyFrameWidget = new KeyFrameWidget(this);
    keyFrameWidget->setLabelWidget(ui->keylabelView);
    keyFrameWidget->setAnimWidgetData(animWidgetData);
    //keyFrameWidget->hide();

    curveWidget = new KeyFrameCurveWidget();
    curveWidget->setLabelWidget(ui->keylabelView);
    curveWidget->setAnimWidgetData(animWidgetData);
    curveWidget->hide();

    createAnimWidget = new CreateAnimationWidget();
    connect(createAnimWidget->getCreateButton(),SIGNAL(clicked(bool)), this, SLOT(addAnimation()));
    createAnimWidget->hide();
    this->layout()->addWidget(createAnimWidget);

    ui->timeline->setAnimWidgetData(animWidgetData);

    auto gridLayout = new QGridLayout();
    gridLayout->setContentsMargins(0, 0, 0, 0);
    gridLayout->setSpacing(0);
    gridLayout->addWidget(keyFrameWidget);
    gridLayout->addWidget(curveWidget);
    ui->keyFrameHolder->setLayout(gridLayout);

    animWidgetData->addDisplayWidget(keyFrameWidget);
    animWidgetData->addDisplayWidget(curveWidget);
    animWidgetData->addDisplayWidget(ui->timeline);

    //ui->keywidgetView->setLabelWidget(ui->keylabelView);
    ui->keylabelView->setAnimWidget(this);

    //timer
    timer = new QTimer(this);
    connect(timer,SIGNAL(timeout()),this,SLOT(updateAnim()));
    elapsedTimer = new QElapsedTimer();

    ui->sceneNodeName->setText("");

    //timeAtCursor = 0;
    timerSpeed = 1.0f/60;//60 fps
    loopAnim = false;

    //buttons that affect timer
    connect(ui->playBtn,SIGNAL(pressed()),this,SLOT(startTimer()));
    connect(ui->stopBtn,SIGNAL(pressed()),this,SLOT(stopTimer()));

    //connect(ui->keywidgetView,SIGNAL(cursorTimeChanged(float)),this,SLOT(onObjectAnimationTimeChanged(float)));
    connect(ui->timeline,SIGNAL(cursorMoved(float)),this,SLOT(onSceneAnimationTimeChanged(float)));
    //connect(ui->timeline,SIGNAL(cursorMoved(float)),keyFrameWidget,SLOT(cursorTimeChanged(float)));

    //dopesheet and curve buttons
    connect(ui->dopeSheetBtn,SIGNAL(pressed()),this,SLOT(showKeyFrameWidget()));
    connect(ui->curvesBtn,SIGNAL(pressed()),this,SLOT(showCurveWidget()));

    mainTimeline = nullptr;
    playIcon = QIcon(":/icons/play-arrow.svg");
    pauseIcon = QIcon(":/icons/pause.svg");

    ui->dopeSheetBtn->setStyleSheet("background: #2980b9");

    // null scene node
    setSceneNode(iris::SceneNodePtr());
}

AnimationWidget::~AnimationWidget()
{
    delete ui;
}

KeyFrameCurveWidget* AnimationWidget::getCurveWidget()
{
	return curveWidget;
}

void AnimationWidget::setScene(iris::ScenePtr scene)
{
    this->scene = scene;
}

void AnimationWidget::setSceneNode(iris::SceneNodePtr node)
{
    // at times the timer could still be running when another object is clicked on
    timer->stop();

    keyFrameWidget->setSceneNode(node);
    //ui->timeline->setSceneNode(node);
    ui->keylabelView->setSceneNode(node);

    keyFrameWidget->repaint();
    curveWidget->repaint();
    ui->keylabelView->repaint();
    this->node = node;

    if (!!node) {
        nodeProperties = node->getProperties();
        scene = node->scene;
        ui->sceneNodeName->setText(node->name);

        if(node->getAnimations().count() == 0) {
            //showCreateAnimWidget();
            //updateCreationWidgetMessage(node);
            auto newAnim = iris::Animation::create("Animation");
            node->addAnimation(newAnim);
            node->setAnimation(newAnim);
        }

        buildPropertiesMenu();

        animation = node->getAnimation();
        refreshAnimationList();
        showKeyFrameWidget();
        hideCreateAnimWidget();
        ui->loopCheckBox->setChecked(animation->getLooping());

        // enable ui
        ui->deleteAnimBtn->setEnabled(true);
        ui->insertFrame->setEnabled(true);
        ui->addAnimBtn->setEnabled(true);
        ui->animList->setEnabled(true);
    }
    else {
        ui->insertFrame->setMenu(new QMenu());
        animation.clear();
        ui->sceneNodeName->setText("");

        // disable ui
        ui->deleteAnimBtn->setEnabled(false);
        ui->insertFrame->setEnabled(false);
        ui->addAnimBtn->setEnabled(false);
        ui->animList->setEnabled(false);
    }
}

void AnimationWidget::buildPropertiesMenu()
{
    // rebuild menu
    auto menu = new QMenu();
    int index = 0;
    for (auto prop : nodeProperties) {
        auto action = new QAction();
        action->setText(prop->name);
        action->setData(index++);

        menu->addAction(action);
    }

    // todo: add materials
//    if (node->sceneNodeType == iris::SceneNodeType::Mesh ) {
//        int index = 0;
//        auto mat = node.staticCast<iris::MeshNode>()->getMaterial().staticCast<iris::CustomMaterial>();
//        auto props = mat->getProperties();

//        auto matMenu = new QMenu("Material");

//        for(auto prop : props) {
//            auto action = new QAction();
//            action->setText(prop->displayName);
//            action->setData(index++);

//            matMenu->addAction(action);
//        }

//        menu->addMenu(matMenu);
//    }


    connect(menu, SIGNAL(triggered(QAction*)), this ,SLOT(addPropertyKey(QAction*)));
    ui->insertFrame->setMenu(menu);
}

void AnimationWidget::clearPropertiesMenu()
{
    ui->insertFrame->setMenu(nullptr);
}

void AnimationWidget::updateAnim()
{
    animWidgetData->cursorPosInSeconds += elapsedTimer->nsecsElapsed()/(1000.0f*1000.0f*1000.0f);
    elapsedTimer->restart();
    animWidgetData->refreshWidgets();

    onObjectAnimationTimeChanged(animWidgetData->cursorPosInSeconds);
}

// called when the play button is hit
void AnimationWidget::startTimer()
{
    if (!timer->isActive()) {
        //timeAtCursor = keyFrameWidget->getTimeAtCursor();
        //startedTime = timeAtCursor;
        startedTime = animWidgetData->cursorPosInSeconds;

        timer->start(timerSpeed);
        elapsedTimer->start();
        ui->playBtn->setIcon(pauseIcon);
    } else
    {
        // do a pause
        ui->playBtn->setIcon(playIcon);
        animWidgetData->refreshWidgets();
        timer->stop();
    }
}

void AnimationWidget::stopTimer()
{
    if (timer->isActive()) {
        animWidgetData->cursorPosInSeconds = startedTime;
        animWidgetData->refreshWidgets();
        timer->stop();
        ui->playBtn->setIcon(playIcon);
    }
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
    keyFrameWidget->repaint();
    //curveWidget->repaint();
    ui->keylabelView->repaint();
}

void AnimationWidget::refreshAnimationList()
{
    ui->animList->clear();
    auto animList = QStringList();
    for (auto anim : node->getAnimations()) {
        animList.append(anim->getName());
    }

    ui->animList->addItems(animList);

    if (animList.size()>0) {
        //set active anim to current anim
        ui->animList->setCurrentIndex(animList.size()-1);
    }
}

void AnimationWidget::clearAnimationList()
{
    ui->animList->clear();
}

void AnimationWidget::removeProperty(QString propertyName)
{
    if (!!node) {
        node->getAnimation()->removePropertyAnim(propertyName);
        ui->keylabelView->removeProperty(propertyName);

        this->repaintViews();
    }
}

void AnimationWidget::clearPropertyKeys(QString propertyName)
{

}

iris::PropertyAnim *AnimationWidget::createPropertyAnim(iris::Property* prop)
{
    iris::PropertyAnim* anim;

    switch (prop->type) {
    case iris::PropertyType::Float:
        anim = new iris::FloatPropertyAnim();
    break;

    case iris::PropertyType::Vec3:
        anim = new iris::Vector3DPropertyAnim();
    break;

    case iris::PropertyType::Color:
        anim = new iris::ColorPropertyAnim();
    break;

    default:
        Q_ASSERT(false);
    }

    anim->setName(prop->name);
    return anim;
}

void AnimationWidget::setLooping(bool loop)
{
    if (!!node) {
        node->getAnimation()->setLooping(loop);
    }
}

void AnimationWidget::addAnimation()
{
    if(!node)
        return;

    GetNameDialog dialog;
    auto defaultName = QString("Animation%1").arg(node->getAnimations().count()+1);
    dialog.setName(defaultName);
    dialog.setWindowTitle("New Animation Name");
    if (dialog.exec() == QDialog::Rejected)
        return;

    auto name = dialog.getName();
    animation = iris::Animation::create(name);

    node->addAnimation(animation);
    node->setAnimation(animation);

    // todo: create method for updating views
    //this->setSceneNode(node);

    this->repaintViews();
    ui->keylabelView->setActiveAnimation(animation);
    this->refreshAnimationList();
    this->buildPropertiesMenu();

    //hide Create Animation widget if it's showing
    this->hideCreateAnimWidget();
}

void AnimationWidget::deleteAnimation()
{
    node->deleteAnimation(node->getAnimation());

    //refresh ui
    this->setSceneNode(node);
}

void AnimationWidget::addPropertyKey(QAction *action)
{
    if (!animation)
        return;

    auto index = action->data().toInt();
    auto animProp = nodeProperties[index];

    // Get or create property
    iris::PropertyAnim* anim;
    if (animation->hasPropertyAnim(animProp->name))
    {
        anim = animation->getPropertyAnim(animProp->name);
    } else {
        anim = createPropertyAnim(animProp);
        anim->setName(animProp->name);
        animation->addPropertyAnim(anim);
        ui->keylabelView->addProperty(animProp->name);
    }

    auto val = node->getPropertyValue(animProp->name);

    switch (animProp->type) {
    case iris::PropertyType::Float:
    {
        auto value = val.toFloat();
        anim->getKeyFrames()[0].keyFrame->
                addKey(value, animWidgetData->cursorPosInSeconds);
    }
        break;
    case iris::PropertyType::Vec3:
    {
        auto value = val.value<QVector3D>();
        auto frames =  anim->getKeyFrames();
        frames[0].keyFrame->addKey(value.x(), animWidgetData->cursorPosInSeconds);
        frames[1].keyFrame->addKey(value.y(), animWidgetData->cursorPosInSeconds);
        frames[2].keyFrame->addKey(value.z(), animWidgetData->cursorPosInSeconds);
    }
        break;
    case iris::PropertyType::Color:
    {
        auto value = val.value<QColor>();
        auto frames =  anim->getKeyFrames();
        frames[0].keyFrame->addKey(value.redF(),    animWidgetData->cursorPosInSeconds);
        frames[1].keyFrame->addKey(value.greenF(),  animWidgetData->cursorPosInSeconds);
        frames[2].keyFrame->addKey(value.blueF(),   animWidgetData->cursorPosInSeconds);
        frames[3].keyFrame->addKey(value.alphaF(),  animWidgetData->cursorPosInSeconds);
    }
        break;
    default:
        break;
    }

    animation->calculateAnimationLength();

    // recalc summary keys for this property
    ui->keylabelView->recalcPropertySummaryKeys(animProp->name);

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
    animWidgetData->cursorPosInSeconds = timeInSeconds;
    if(!!scene)
    {
        scene->updateSceneAnimation(timeInSeconds);
    }
}

void AnimationWidget::showKeyFrameWidget()
{
    keyFrameWidget->show();
    curveWidget->hide();

    ui->dopeSheetBtn->setStyleSheet("background: #2980b9");
    ui->curvesBtn->setStyleSheet("background: #555");
}

void AnimationWidget::showCurveWidget()
{
    keyFrameWidget->hide();
    curveWidget->show();
    ui->keylabelView->highlightDefaultProperty();

    ui->curvesBtn->setStyleSheet("background: #2980b9");
    ui->dopeSheetBtn->setStyleSheet("background: #555;");
}

void AnimationWidget::hideCreateAnimWidget()
{
    createAnimWidget->hide();
    ui->splitter->show();// main splitter
}

void AnimationWidget::showCreateAnimWidget()
{
    createAnimWidget->show();
    ui->splitter->hide();// main splitter
}

void AnimationWidget::updateCreationWidgetMessage(iris::SceneNodePtr node)
{
    if (!node) {
        createAnimWidget->hideButton();
    } else {
        createAnimWidget->showButton();
        createAnimWidget->setButtonText("Create Animation for "+node->getName());
    }
}

void AnimationWidget::animationChanged(QString name)
{
    auto animList = node->getAnimations();
    for (auto anim : animList)
    {
        if (anim->getName() == name) {
            node->setAnimation(anim);
            ui->keylabelView->setActiveAnimation(anim);
            this->repaintViews();
        }
    }
}

