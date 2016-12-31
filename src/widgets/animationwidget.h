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
#include "ui_animationwidget.h"
//#include "../core/scenenode.h"

class QWidget;
class QElapsedTimer;
class TimelineWidget;

namespace iris
{
    class SceneNode;
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
public:
    explicit AnimationWidget(QWidget *parent = 0);
    ~AnimationWidget();

    void setScene(iris::ScenePtr scene);
    void setSceneNode(iris::SceneNodePtr node);

    void setMainTimelineWidget(TimelineWidget* tl)
    {
        mainTimeline = tl;
    }

    void showHighlight()
    {
        /*
        if(node==nullptr)
            return;

        if(mainTimeline==nullptr)
            return;
        mainTimeline->showHighlight(node->animStartTime,node->animStartTime+node->animLength);
        */
    }

    void hideHighlight()
    {
        /*
        if(node==nullptr)
            return;
        mainTimeline->hideHighlight();
        */
    }

    void setAnimStartTime(int second)
    {
        /*
        if(node!=nullptr)
            node->setAnimStartTime(second);
            */
    }

    void setAnimLength(float length)
    {
        //todo
        int scaleRatio = 30;
        ui->values->setGeometry(0,0,length*scaleRatio,ui->values->height());
        this->showHighlight();
    }

    void stopAnimation()
    {
        stopTimer();
    }

    void fixLayout()
    {
        ui->values->adjustSize();
        //ui->values->update();

        ui->timeControls->adjustSize();
        //ui->timeControls->update();
    }
    void repaintViews();

    //startRange and endRange are in seconds
    void setTimeViewRange(float startRange,float endRange);

    //sets cursor position at time
    void setCursorPositionAtTime(float timeInSeconds);
private:

    void removeNodeSpecificActions();

    //add actions
    QAction* addLightColorKeyAction;
    QAction* addLightIntensityKeyAction;
    QAction* addSceneBackgroundColorKeyAction;
    QAction* addSceneActiveCameraKeyAction;

    //deletion actions
    QAction* deleteLightColorKeysAction;
    QAction* deleteLightIntensityKeysAction;

public slots:
    void setLooping(bool loop)
    {
        loopAnim = loop;
        //node->loopAnim = loop;
    }

private slots:
    void addPosKey();
    void addRotKey();
    void addScaleKey();
    void addPosRotKey();
    void addPosRotScaleKey();

    void addLightColorKey();
    void addLightIntensityKey();
    void deleteLightColorKeys();
    void deleteLightIntensityKeys();

    void addSceneBackgroundColorKey();
    void addSceneActiveCameraKey();

    void deletePosKeys();
    void deleteScaleKeys();
    void deleteRotKeys();
    void deleteAllKeys();

    void updateAnim();
    void startTimer();
    void stopTimer();

    void timeEditChanged(QTime);
    void setAnimstart(int time);

    void onObjectAnimationTimeChanged(float timeInSeconds);
    void onSceneAnimationTimeChanged(float timeInSeconds);
private:
    float time;
    float timerSpeed;
    Ui::AnimationWidget *ui;
};

#endif // ANIMATIONWIDGET_H
