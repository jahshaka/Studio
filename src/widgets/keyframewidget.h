/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef KEYFRAMEWIDGET_H
#define KEYFRAMEWIDGET_H

#include <QWidget>
#include <QDebug>
#include <QPainter>
#include <QMouseEvent>
#include <vector>
#include <QVector2D>
#include <QSharedPointer>
#include "../irisgl/src/irisglfwd.h"

namespace iris
{
    class SceneNode;
}

class KeyFrameWidget:public QWidget
{
    Q_OBJECT

    /**
     * viewLeft and viewRight are like the "camera" of the keyframes
     * they are used for translating keys from their local "key space"
     * to viewport space and vice versa.
     */
    float viewLeft;
    float viewRight;

private:
    QColor bgColor;
    QColor itemColor;

    QPen linePen;
    QPen cursorPen;

    float maxTimeInSeconds;

    //the range is in the time space
    float rangeStart;
    float rangeEnd;

    float minRange;
    float maxRange;

    //indicates time at cursor
    float cursorPos;

    bool dragging;
    int scaleRatio;

    iris::SceneNodePtr obj;
    QPoint mousePos;
    QPoint clickPos;

    iris::FloatKey* selectedKey;

    bool leftButtonDown;
    bool middleButtonDown;
    bool rightButtonDown;

public:
    KeyFrameWidget(QWidget* parent);

    void setSceneNode(iris::SceneNodePtr node);
    void setMaxTimeInSeconds(float time);
    void adjustLength();

    float getStartTimeRange();
    float getEndTimeRange();

    float getTimeAtCursor();
    //void setMaxTimeInSeconds(float time);
    void setTimeRange(float start,float end);
    void setStartTime(float start);
    void setEndTime(float end);
    void setTime(float time);
    //void setCursorPos(int x);

    void drawFrame(QPainter& paint,iris::FloatKeyFrame* keyFrame,int y);
    void drawBackgroundLines(QPainter& paint);
    int getXPosFromSeconds(float seconds);

    void mousePressEvent(QMouseEvent* evt);
    void mouseReleaseEvent(QMouseEvent* evt);
    void mouseMoveEvent(QMouseEvent* evt);
    void wheelEvent(QWheelEvent* evt);
    //void resizeEvent(QResizeEvent* event);
    void paintEvent(QPaintEvent *painter);

signals:
    void cursorTimeChanged(float timeInSeconds);

private:
    /**
     * Finds squared distance between 2 points
     */
    static float distanceSquared(float x1,float y1,float x2,float y2);

    float posToTime(int xpos);
    int timeToPos(float timeInSeconds);

    iris::FloatKey* getSelectedKey(int x,int y);
};

#endif // KEYFRAMEWIDGET_H
