/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef ANIMATIONWIDGET_H
#define ANIMATIONWIDGET_H

#include <QWidget>
#include <QTime>
#include <QSharedPointer>
//#include "ui_animationwidget.h"
#include "../irisgl/src/irisglfwd.h"

class QWidget;
class QElapsedTimer;
class TimelineWidget;

class QMenu;
class QTreeWidget;

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

    float startedTime;
    bool loopAnim;

    TimelineWidget* mainTimeline;

    QMenu* addMenu;
    QMenu* deleteMenu;

    QList<iris::AnimableProperty> animableProperties;
    iris::AnimationPtr animation;
public:
    explicit AnimationWidget(QWidget *parent = 0);
    ~AnimationWidget();

    void setScene(iris::ScenePtr scene);
    void setSceneNode(iris::SceneNodePtr node);

    void setMainTimelineWidget(TimelineWidget* tl)
    {
        mainTimeline = tl;
    }

    void setAnimLength(float length);

    void stopAnimation();
    void fixLayout();

    void repaintViews();

    //startRange and endRange are in seconds
    void setTimeViewRange(float startRange,float endRange);

    //sets cursor position at time
    void setCursorPositionAtTime(float timeInSeconds);

private:
    iris::PropertyAnim* createPropertyAnim(const iris::AnimableProperty& prop);

public slots:
    void setLooping(bool loop);

private slots:
    void addPropertyKey(QAction* action);

    void updateAnim();
    void startTimer();
    void stopTimer();

    void timeEditChanged(QTime);
    void setAnimstart(int timeAtCursor);

    void onObjectAnimationTimeChanged(float timeInSeconds);
    void onSceneAnimationTimeChanged(float timeInSeconds);
private:
    float timeAtCursor;
    float timerSpeed;
    Ui::AnimationWidget *ui;
};

#endif // ANIMATIONWIDGET_H
