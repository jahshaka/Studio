/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef TIMELINEWIDGET_H
#define TIMELINEWIDGET_H

#include <QWidget>
#include <QDebug>
#include <QPainter>
#include <QMouseEvent>
//#include "../scenegraph/scenenodes.h"
#include "../irisgl/src/core/scenenode.h"


//https://kernelcoder.wordpress.com/2010/08/25/how-to-insert-ruler-scale-type-widget-into-a-qabstractscrollarea-type-widget/
class TimelineWidget:public QWidget
{
    Q_OBJECT
private:
    int timeToPos(float timeInSeconds);
    float posToTime(int xpos);

    bool leftButtonDown;
    bool middleButtonDown;
    bool rightButtonDown;

    QPoint clickPos;
    QPoint mousePos;

public:
    QColor bgColor;
    QColor itemColor;

    QPen linePen;
    QPen cursorPen;

    //raneg of time being viewed in seconds
    float rangeStart;
    float rangeEnd;

    //indicates current time at cursor in seconds
    float cursorPos;

    bool dragging;
    //int scaleRatio;

    //timeline widget doesnt manage lifetime of this pointer
    iris::SceneNodePtr node;

    float highlightStart;
    float highlightEnd;
    bool drawHighlight;

    TimelineWidget(QWidget* parent);

    float getStartTimeRange();
    float getEndTimeRange();

    void showHighlight(float start,float end);
    void hideHighlight();

    void setSceneNode(iris::SceneNodePtr node);

    float getTimeAtCursor();

    //void setMaxTimeInSeconds(float time);
    void setTimeRange(float start,float end);
    void setStartTime(float start);
    void setEndTime(float end);
    void setTime(float time);
    void setCursorPos(float timeInSeconds);

    void paintEvent(QPaintEvent *painter);
    void mousePressEvent(QMouseEvent* evt);
    void mouseReleaseEvent(QMouseEvent* evt);
    void mouseMoveEvent(QMouseEvent* evt);
    void resizeEvent(QResizeEvent* event);
    void wheelEvent(QWheelEvent* evt);

signals:
    void cursorMoved(float timeInSeconds);

};


#endif // TIMELINEWIDGET_H
