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
class QTreeWidgetItem;

class KeyFrameWidget;
class KeyFrameCurveWidget;
class AnimationWidgetData;

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
    KeyFrameWidget* keyFrameWidget;
    KeyFrameCurveWidget* curveWidget;

    QMenu* addMenu;
    QMenu* deleteMenu;

    QList<iris::Property*> nodeProperties;
    QList<iris::Property*> matProperties;
    iris::AnimationPtr animation;

    AnimationWidgetData* animWidgetData;
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
    void refreshAnimationList();

    //startRange and endRange are in seconds
    void setTimeViewRange(float startRange,float endRange);

    //sets cursor position at time
    void setCursorPositionAtTime(float timeInSeconds);

    void removeProperty(QString propertyName);
    void clearPropertyKeys(QString propertyName);

private:
    iris::PropertyAnim* createPropertyAnim(iris::Property* prop);

public slots:
    void setLooping(bool loop);
    void addAnimation();

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

    void animationChanged(QString name);

private:
    //float timeAtCursor;
    float timerSpeed;
    Ui::AnimationWidget *ui;
};

#endif // ANIMATIONWIDGET_H
