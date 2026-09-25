/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/panels/timeline/animationwidget.h"
#include "services/editgate.h"
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

#include "commands/animationcommands.h"
#include "services/animationedits.h"
#include "services/services.h"
#include "services/undoservice.h"
#include "ui/panels/timeline/keyframewidget.h"
#include "ui/panels/timeline/keyframecurvewidget.h"
#include "ui/panels/timeline/animationwidgetdata.h"
#include "ui/panels/timeline/createanimationwidget.h"
#include "ui/dialogs/getnamedialog.h"
#include "ui/style/stylesheet.h"
#include <QButtonGroup>


AnimationWidget::AnimationWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::AnimationWidget)
{
    ui->setupUi(this);
    // animationwidget.ui used to embed these (classic-only now; theme sweep)
    setStyleSheet(StyleSheet::AnimationWidgetRoot());
    ui->insertFrame->setStyleSheet(StyleSheet::AnimationWidgetInsertFrame());

    connect(ui->addAnimBtn,SIGNAL(clicked(bool)), this, SLOT(addAnimation()));
    connect(ui->deleteAnimBtn,SIGNAL(clicked(bool)), this, SLOT(deleteAnimation()));
    connect(ui->animList,SIGNAL(currentTextChanged(QString)), this, SLOT(OnAnimationChanged(QString)));
    connect(ui->loopCheckBox,SIGNAL(clicked(bool)), this, SLOT(setLooping(bool)));

    animWidgetData = new AnimationWidgetData();

    keyFrameWidget = new KeyFrameWidget(this);
    keyFrameWidget->setLabelWidget(ui->keylabelView);
    keyFrameWidget->setAnimWidgetData(animWidgetData);

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

    ui->keylabelView->setAnimWidget(this);

    //timer
    timer = new QTimer(this);
    connect(timer,SIGNAL(timeout()),this,SLOT(updateAnim()));
    elapsedTimer = new QElapsedTimer();

    ui->sceneNodeName->setText("");

    timerSpeed = 1.0f/60;//60 fps
    loopAnim = false;

    //buttons that affect timer
    connect(ui->playBtn,SIGNAL(pressed()),this,SLOT(startTimer()));
    connect(ui->stopBtn,SIGNAL(pressed()),this,SLOT(stopTimer()));

    connect(ui->timeline,SIGNAL(cursorMoved(float)),this,SLOT(onSceneAnimationTimeChanged(float)));

    //dopesheet and curve buttons
    connect(ui->dopeSheetBtn,SIGNAL(pressed()),this,SLOT(showKeyFrameWidget()));
    connect(ui->curvesBtn,SIGNAL(pressed()),this,SLOT(showCurveWidget()));

    mainTimeline = nullptr;
    playIcon = QIcon(":/icons/play-arrow.svg");
    pauseIcon = QIcon(":/icons/pause.svg");

    // Dope sheet | Curves is a two-way mode switch. Qlementine paints the
    // mode that is showing as a CHECKED button (the style's accent); Classic
    // keeps its two swapped sheets (markTimelineMode).
    // (An exclusive group, so a click on the mode already showing cannot
    // un-check it on release.)
    if (!StyleSheet::classicThemeActive()) {
        ui->dopeSheetBtn->setCheckable(true);
        ui->curvesBtn->setCheckable(true);
        auto *modes = new QButtonGroup(this);
        modes->setExclusive(true);
        modes->addButton(ui->dopeSheetBtn);
        modes->addButton(ui->curvesBtn);
    }
    markTimelineMode(ui->dopeSheetBtn, nullptr);

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
    ui->keylabelView->setSceneNode(node);

    // UPDATE, NOT REPAINT (SELECT-COST-1, 2026-09-18). `repaint()` paints the
    // widget SYNCHRONOUSLY, right here, three times per selection — 1.7 of the
    // 8 ms a pick cost at 1k nodes, and every one of those paints is thrown
    // away by the paint the event-loop turn does anyway. `update()` posts one
    // paint per widget, coalesced, and paints nothing at all while the
    // timeline is not on screen (a bottom tab behind Assets or Console, which
    // is how the editor opens).
    keyFrameWidget->update();
    curveWidget->update();
    ui->keylabelView->update();
    this->node = node;

    if (!!node) {
        nodeProperties = node->getProperties();
        scene = node->getScene();
        ui->sceneNodeName->setText(node->name);

        // NO auto-create here (anim-lane finding, 2026-09-04). Selecting a node
        // used to mint an empty "Animation" on it, and SceneWriter serializes
        // every animation a node carries — so merely CLICKING through a scene
        // grew the blob an empty clip per node, each of which then showed up in
        // the animation list forever. The clip is created on the first keyframe
        // instead (ensureAnimation, called from addPropertyKey); the "Add"
        // button still creates one explicitly whenever the user asks.

        buildPropertiesMenu();

        // AFTER refreshAnimationList: selecting a row is what makes an
        // animation active (OnAnimationChanged), and a node can arrive here
        // with NONE active — deleting the active one of several leaves the
        // node without one until something picks the next.
        refreshAnimationList();
        animation = node->getAnimation();
        showKeyFrameWidget();
        hideCreateAnimWidget();
        if (!!animation)
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
        // it. Same predicate on both sides so they cannot drift apart — and
        // since the anim.* verbs landed, the same predicate the SCRIPTS see
        // through anim.properties (animedits::, src/services/animationedits.h).
        if (!animedits::isAnimatablePropertyType(prop->type))
            continue;

        auto action = new QAction();
        action->setText(prop->name);
        action->setData(propIndex);

        menu->addAction(action);
    }


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

void AnimationWidget::pushEdit(QUndoCommand *command)
{
    if (!command) return;
    // THE EDIT GATE (owner, ledger §423). Every timeline edit APPLIES first
    // and records after — the key is inserted, the track removed, the clip
    // deleted, and the command carries the snapshot to go back to. So a
    // refusal here is that command's own undo(): the restore it was built to
    // perform, run instead of stored. (Each of these commands' undo() is a
    // snapshot restore that does not assume its redo() ever ran.)
    if (editgate::refuse()) {
        command->undo();
        delete command;
        return;
    }
    if (services && services->undo) {
        services->undo->push(command);
        return;
    }
    delete command;
}

void AnimationWidget::removeProperty(QString propertyName)
{
    if (!!node) {
        // getAnimation() is null on a node whose only animation was just
        // deleted (and on one that never had one) — removeTrack answers false
        // for both. The label row goes either way: if there is a row for a
        // track that is not there, that row is exactly what should not stay.
        const auto anim = node->getAnimation();
        const animedits::TrackSnapshot before = animedits::snapshotTrack(anim, propertyName);
        if (animedits::removeTrack(anim, propertyName))
            pushEdit(new RemovePropertyCommand(anim, propertyName, before));
        ui->keylabelView->removeProperty(propertyName);

        this->repaintViews();
    }
}

void AnimationWidget::clearPropertyKeys(QString propertyName)
{

}

void AnimationWidget::setLooping(bool loop)
{
    if (!!node) {
        if (auto anim = node->getAnimation())
            anim->setLooping(loop);
    }
}

void AnimationWidget::addAnimation()
{
    if(!node)
        return;
    // The edit gate (round 2, item 6): New Animation adds a clip to the node
    // with no command behind it, so the spine never sees it. Asked before the
    // name dialog, not after — refusing a name the user has just typed is
    // worse than not asking for it.
    if (editgate::refuse()) return;

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
    if (!node)
        return;
    // Through the shared service (animedits::removeAnimation), which also
    // clears the node's ACTIVE animation: SceneNode::deleteAnimation only
    // drops it from the list, and the node went on holding — and keying into
    // — a clip that no longer appeared in the list.
    const auto doomed = node->getAnimation();
    if (animedits::removeAnimation(node, doomed))
        pushEdit(new RemoveAnimationCommand(node, doomed, true));

    //refresh ui
    this->setSceneNode(node);
}

// The lazy half of the auto-create removal: the first key on a node that has no
// animation makes one. Named "Animation" exactly as the old auto-create did, so
// nothing downstream sees a different clip name.
iris::AnimationPtr AnimationWidget::ensureAnimation()
{
    if (!!animation) return animation;
    if (!node) return iris::AnimationPtr();
    // The edit gate (round 2, item 6). This runs BEFORE the key-insert push
    // that pushEdit gates, and it is itself a document write — the first key
    // on a node with no clip creates one. Its only caller is a hand action
    // (addPropertyKey); a script keys through anim.keyframe, inside a verb.
    if (editgate::refuse()) return iris::AnimationPtr();

    animation = iris::Animation::create("Animation");
    node->addAnimation(animation);
    node->setAnimation(animation);

    refreshAnimationList();
    ui->keylabelView->setActiveAnimation(animation);
    ui->loopCheckBox->setChecked(animation->getLooping());
    hideCreateAnimWidget();
    return animation;
}

void AnimationWidget::addPropertyKey(QAction *action)
{
    if (!ensureAnimation())
        return;

    auto index = action->data().toInt();
    if (index < 0 || index >= nodeProperties.count())
        return;

    auto animProp = nodeProperties[index];
    if (!animProp)
        return;

    // ONE keyframe writer (services/animationedits.h): the get-or-create, the
    // track-shape check, the overwrite-at-the-same-time rule and the length
    // recompute are the same code the anim.* verbs run, so the panel and a
    // script cannot key a property differently. The panel keeps only what is
    // its own: the menu index, the label view and the reporting.
    animedits::PropertyInfo info;
    info.name = animProp->name;
    info.displayName = animProp->displayName;
    info.type = animProp->type;
    info.index = index;
    // The cached Property carries the value the node had when it was SELECTED;
    // the key must carry what it holds now.
    info.value = node->getPropertyValue(animProp->name);

    bool createdTrack = false;
    QString error;
    // F16: the panel's insert-key button is undoable now, through the SAME
    // command the anim.keyframe verb pushes.
    const animedits::TrackSnapshot before = animedits::snapshotTrack(animation, animProp->name);
    if (!animedits::setKeyframe(animation, info, animWidgetData->cursorPosInSeconds,
                                QVariant(), &createdTrack, &error)) {
        // The menu filter and the writer share one predicate, so a refusal here
        // can only mean the two got out of sync — or an action reached this slot
        // from somewhere else. Report it once per property.
        static QSet<QString> reported;
        if (!reported.contains(animProp->name)) {
            reported.insert(animProp->name);
            irisLog(QString("AnimationWidget: %1 — key ignored").arg(error));
        }
        return;
    }

    pushEdit(new SetKeyframeCommand(animation, animProp->name, before,
                                    animedits::snapshotTrack(animation, animProp->name)));

    if (createdTrack)
        ui->keylabelView->addProperty(animProp->name);

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

    markTimelineMode(ui->dopeSheetBtn, ui->curvesBtn);
}

void AnimationWidget::showCurveWidget()
{
    keyFrameWidget->hide();
    curveWidget->show();
    ui->keylabelView->highlightDefaultProperty();

    markTimelineMode(ui->curvesBtn, ui->dopeSheetBtn);
}

void AnimationWidget::markTimelineMode(QPushButton *active, QPushButton *idle)
{
    if (StyleSheet::classicThemeActive()) {
        active->setStyleSheet(StyleSheet::TimelineModeActive());
        if (idle) idle->setStyleSheet(StyleSheet::TimelineModeIdle());
        return;
    }
    active->setChecked(true);
    if (idle) idle->setChecked(false);
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
    // The edit gate (round 2, item 6): picking another clip in the combo
    // writes the node's ACTIVE animation — a document field, no command.
    if (editgate::refuse()) return;
    auto animList = node->getAnimations();
    for (auto anim : animList)
    {
        if (anim->getName() == name) {
            node->setAnimation(anim);
            // The panel's own handle has to follow too: addPropertyKey keys
            // THIS pointer, so leaving it behind meant switching animations in
            // the combo and then keying into the previous one.
            animation = anim;
            ui->keylabelView->setActiveAnimation(anim);
            this->repaintViews();
        }
    }
}

