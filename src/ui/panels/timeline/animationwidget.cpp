/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/panels/timeline/animationwidget.h"
#include "ui_animationwidget.h"
#include <QMenu>
#include <QAction>
#include <QSet>
#include <QTimer>
#include <QElapsedTimer>
#include <QToolButton>
#include <QTime>
#include "irisgl/document/animation/keyframeanimation.h"
#include "irisgl/document/animation/keyframeset.h"
#include "irisgl/document/animation/animation.h"
#include "irisgl/document/animation/propertyanim.h"
#include "irisgl/document/animation/animableproperty.h"
#include "irisgl/core/logger.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/meshnode.h"

#include "irisgl/document/materials/material.h"
#include "irisgl/document/materials/custommaterial.h"

#include "ui/panels/timeline/propertyanimfactory.h"
#include "ui/panels/timeline/keyframewidget.h"
#include "ui/panels/timeline/keyframecurvewidget.h"
#include "ui/panels/timeline/animationwidgetdata.h"
#include "ui/panels/timeline/createanimationwidget.h"
#include "ui/dialogs/getnamedialog.h"


AnimationWidget::AnimationWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::AnimationWidget)
{
    ui->setupUi(this);

    connect(ui->addAnimBtn,SIGNAL(clicked(bool)), this, SLOT(addAnimation()));
    connect(ui->deleteAnimBtn,SIGNAL(clicked(bool)), this, SLOT(deleteAnimation()));
    connect(ui->animList,SIGNAL(currentTextChanged(QString)), this, SLOT(OnAnimationChanged(QString)));
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
        const int propIndex = index++;
        // Only the types the timeline can build a keyframe track for. The
        // document nodes reflect bool/int/string fields too (name, visible,
        // lightType, meshPath...). This filter is defence in depth: since
        // 2026-09-01 makePropertyAnim() returns nullptr rather than an
        // indeterminate pointer for the rest, and addPropertyKey() bails out on
        // it. Same predicate on both sides so they cannot drift apart.
        if (!isAnimatablePropertyType(prop->type))
            continue;

        auto action = new QAction();
        action->setText(prop->name);
        action->setData(propIndex);

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

//! Returns nullptr for property types the timeline has no track shape for.
//! See src/ui/panels/timeline/propertyanimfactory.h.
iris::PropertyAnim *AnimationWidget::createPropertyAnim(iris::Property* prop)
{
    if (!prop)
        return nullptr;

    return makePropertyAnim(prop->type, prop->name);
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
    if (index < 0 || index >= nodeProperties.count())
        return;

    auto animProp = nodeProperties[index];
    if (!animProp)
        return;

    // Get or create property
    iris::PropertyAnim* anim;
    if (animation->hasPropertyAnim(animProp->name))
    {
        anim = animation->getPropertyAnim(animProp->name);
    } else {
        anim = createPropertyAnim(animProp);
        if (!anim) {
            // The menu filter (buildPropertiesMenu) and the factory share one
            // predicate, so a null here can only mean the two got out of sync
            // — or an action reached this slot from somewhere else. Report it
            // once per property instead of dereferencing garbage.
            static QSet<QString> reported;
            if (!reported.contains(animProp->name)) {
                reported.insert(animProp->name);
                irisLog(QString("AnimationWidget: property '%1' has no animatable "
                                "track type (%2) — key ignored")
                            .arg(animProp->name)
                            .arg(static_cast<int>(animProp->type)));
            }
            return;
        }
        animation->addPropertyAnim(anim);
        ui->keylabelView->addProperty(animProp->name);
    }

    // Covers the other branch: hasPropertyAnim() said yes but getPropertyAnim()
    // handed back nothing.
    if (!anim)
        return;

    auto val = node->getPropertyValue(animProp->name);
    auto frames = anim->getKeyFrames();

    // An existing track for this name may have been built for a different
    // shape than the property now reports (a Float track for a Vec3 property,
    // say, after a document change). Check the track width before indexing it.
    const auto hasFrames = [&frames](int count) {
        if (frames.count() < count)
            return false;
        for (int i = 0; i < count; ++i)
            if (!frames[i].keyFrame)
                return false;
        return true;
    };

    switch (animProp->type) {
    case iris::PropertyType::Float:
    {
        if (!hasFrames(1))
            return;
        auto value = val.toFloat();
        frames[0].keyFrame->addKey(value, animWidgetData->cursorPosInSeconds);
    }
        break;
    case iris::PropertyType::Vec3:
    {
        if (!hasFrames(3))
            return;
        auto value = val.value<QVector3D>();
        frames[0].keyFrame->addKey(value.x(), animWidgetData->cursorPosInSeconds);
        frames[1].keyFrame->addKey(value.y(), animWidgetData->cursorPosInSeconds);
        frames[2].keyFrame->addKey(value.z(), animWidgetData->cursorPosInSeconds);
    }
        break;
    case iris::PropertyType::Color:
    {
        if (!hasFrames(4))
            return;
        auto value = val.value<QColor>();
        frames[0].keyFrame->addKey(value.redF(),    animWidgetData->cursorPosInSeconds);
        frames[1].keyFrame->addKey(value.greenF(),  animWidgetData->cursorPosInSeconds);
        frames[2].keyFrame->addKey(value.blueF(),   animWidgetData->cursorPosInSeconds);
        frames[3].keyFrame->addKey(value.alphaF(),  animWidgetData->cursorPosInSeconds);
    }
        break;
    default:
        // Unreachable while the menu filter and makePropertyAnim agree; a
        // no-op rather than an unhandled shape if they ever do not.
        return;
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

void AnimationWidget::OnAnimationChanged(QString name)
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

