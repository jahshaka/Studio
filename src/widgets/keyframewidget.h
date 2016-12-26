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
#include "../irisgl/src/irisgl.h"

namespace iris
{
    class SceneNode;
}

class KeyFrameWidget:public QWidget
{
    /**
     * viewLeft and viewRight are like the "camera" of the keyframes
     * they are used for translating keys from their local "key space"
     * to viewport space and vice versa.
     */
    float viewLeft;
    float viewRight;

private:
    float screenSpaceToKeySpace(float x);
    float keySpaceToScreenSpace(float x);

private:
    QColor bgColor;
    QColor itemColor;

    QPen linePen;
    QPen cursorPen;

    float maxTimeInSeconds;

    float rangeStart;
    float rangeEnd;

    //indicates current time
    int cursorPos;

    bool dragging;
    int scaleRatio;

    iris::SceneNodePtr obj;
    QPoint mousePos;
    QPoint clickPos;

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

    /*
    template<typename T>
    void drawFrame(iris::KeyFrame<T>* frame,QPainter* paint,int yTop)
    {
        float penSize = 14;
        float penSizeSquared = 7*7;

        QPen pen(QColor::fromRgb(255,255,255));
        pen.setWidth(penSize);
        pen.setCapStyle(Qt::RoundCap);

        QPen highlightPen(QColor::fromRgb(100,100,100));
        highlightPen.setWidth(penSize);
        highlightPen.setCapStyle(Qt::RoundCap);


        for(size_t i=0;i<frame->keys.size();i++)
        {
            iris::Key<T>* key = frame->keys[i];
            int xpos = this->timeToPos(key->time);

            float distSqrd = distanceSquared(xpos,yTop+10,mousePos.x(),mousePos.y());

            if(distSqrd < penSizeSquared)
            {
                paint->setPen(highlightPen);
                paint->drawPoint(xpos,yTop+10);//frame height should be 10
            }
            else
            {
                paint->setPen(pen);
                paint->drawPoint(xpos,yTop+10);//frame height should be 10
            }
        }
    }
    */


    void drawBackgroundLines(QPainter& paint);
    int getXPosFromSeconds(float seconds);

    void mousePressEvent(QMouseEvent* evt);
    void mouseReleaseEvent(QMouseEvent* evt);
    void mouseMoveEvent(QMouseEvent* evt);
    //void resizeEvent(QResizeEvent* event);
    void paintEvent(QPaintEvent *painter);

private:
    /**
     * Finds squared distance between 2 points
     */
    static float distanceSquared(float x1,float y1,float x2,float y2);
};

#endif // KEYFRAMEWIDGET_H
