/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef ANIMATIONWIDGET_H
#define ANIMATIONWIDGET_H

#include <QWidget>
#include <QTime>
#include <QSharedPointer>
#include <QIcon>
//#include "ui_animationwidget.h"
#include "irisgl/irisglfwd.h"

class QWidget;
class QElapsedTimer;
class TimelineWidget;

class QMenu;
class QTreeWidget;
class QTreeWidgetItem;

class KeyFrameWidget;
class KeyFrameCurveWidget;
class AnimationWidgetData;
class CreateAnimationWidget;
class QUndoCommand;
class QPushButton;
struct StudioServices;

namespace Ui
{
    class AnimationWidget;
}

class AnimationWidget : public QWidget
{
    Q_OBJECT

    iris::ScenePtr scene;
    iris::SceneNodePtr node;
    QTimer* timer;
    //http://stackoverflow.com/questions/17571717/accessing-the-elapsed-seconds-of-a-qtimer
    QElapsedTimer* elapsedTimer;

    QIcon playIcon;
    QIcon pauseIcon;

    float startedTime;
    bool loopAnim;

    TimelineWidget* mainTimeline;
    KeyFrameWidget* keyFrameWidget;
    KeyFrameCurveWidget* curveWidget;
    CreateAnimationWidget* createAnimWidget;

    QMenu* addMenu;
    QMenu* deleteMenu;

    QList<iris::Property*> nodeProperties;
    QList<iris::Property*> matProperties;
    iris::AnimationPtr animation;

    AnimationWidgetData* animWidgetData;
public:
    explicit AnimationWidget(QWidget *parent = 0);
    ~AnimationWidget();

	KeyFrameCurveWidget* getCurveWidget();

    void setScene(iris::ScenePtr scene);
    void setSceneNode(iris::SceneNodePtr node);
    void buildPropertiesMenu();
    void clearPropertiesMenu();

    void setMainTimelineWidget(TimelineWidget* tl)
    {
        mainTimeline = tl;
    }

    void setAnimLength(float length);

    void stopAnimation();
    void fixLayout();

    void repaintViews();
    void refreshAnimationList();
    void clearAnimationList();

    //startRange and endRange are in seconds
    void setTimeViewRange(float startRange,float endRange);

    //sets cursor position at time
    void setCursorPositionAtTime(float timeInSeconds);

    void removeProperty(QString propertyName);
    void clearPropertyKeys(QString propertyName);

    /// The service layer, for UNDO (verb-coverage audit F16). Nullable — the
    /// panel works without it exactly as it did before, only without the undo
    /// record. Wired by the shell once the services exist; the commands the
    /// panel pushes are the same ones the anim.* verbs push, because the edit
    /// underneath is the same animedits:: call.
    void setServices(StudioServices *s) { services = s; }

signals:
    void animationChanged(iris::SceneNodePtr ptr, iris::AnimationPtr anim);

public slots:
    void setLooping(bool loop);
    void addAnimation();
    void deleteAnimation();

private slots:
    void addPropertyKey(QAction* action);

    void updateAnim();
    void startTimer();
    void stopTimer();

    void timeEditChanged(QTime);

    void onObjectAnimationTimeChanged(float timeInSeconds);
    void onSceneAnimationTimeChanged(float timeInSeconds);

    void showKeyFrameWidget();
    void showCurveWidget();

    void hideCreateAnimWidget();
    void showCreateAnimWidget();
    void updateCreationWidgetMessage(iris::SceneNodePtr node);

    void OnAnimationChanged(QString name);

    /// The node's active animation, creating an empty "Animation" the first
    /// time a key is written. Selecting a node deliberately does NOT create one
    /// (see the .cpp): an empty clip minted on every click serializes.
    iris::AnimationPtr ensureAnimation();

private:
    /// Which of Dope sheet | Curves is showing (theme sweep): checked under
    /// Qlementine, Classic's swapped sheets otherwise. `idle` may be null.
    void markTimelineMode(QPushButton *active, QPushButton *idle);

private:
    /// Records a keyframe edit on the app's undo stack (F16). The edit has
    /// already been applied; with no services the command is deleted, not
    /// leaked, and the panel behaves as it always did.
    void pushEdit(QUndoCommand *command);

    //float timeAtCursor;
    float timerSpeed;
    Ui::AnimationWidget *ui;
    StudioServices *services = nullptr;
};

#endif // ANIMATIONWIDGET_H
