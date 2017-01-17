/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include <QWidget>
#include <QDebug>
#include <QPainter>
#include <QMouseEvent>
#include <vector>
//#include "../scenegraph/scenenodes.h"
#include "../irisgl/src/core/scenenode.h"
#include "../irisgl/src/animation/keyframeset.h"
#include "../irisgl/src/animation/keyframeanimation.h"
#include "../irisgl/src/animation/animation.h"
#include "keyframewidget.h"
#include <QMenu>
#include <math.h>

KeyFrameWidget::KeyFrameWidget(QWidget* parent):
    QWidget(parent)
{
    //this->setGeometry(0,0,800,500);

    bgColor = QColor::fromRgb(50,50,50);
    itemColor = QColor::fromRgb(255,255,255);

    maxTimeInSeconds = 30;
    cursorPos = 5;

    linePen = QPen(itemColor);
    cursorPen = QPen(QColor::fromRgb(142,45,197));
    cursorPen.setWidth(3);

    setMouseTracking(true);

    dragging = false;

    scaleRatio = 30;

    //obj = nullptr;
    rangeStart = 0;
    rangeEnd = 100;

    minRange = 10;
    maxRange = 1000;

    leftButtonDown = false;
    middleButtonDown = false;
    rightButtonDown = false;
}

void KeyFrameWidget::setSceneNode(iris::SceneNodePtr node)
{
    obj = node;
}

void KeyFrameWidget::setMaxTimeInSeconds(float time)
{
    maxTimeInSeconds = time;
    this->setGeometry(this->x(),this->y(),time*scaleRatio,this->height());
}

void KeyFrameWidget::adjustLength()
{
}

void KeyFrameWidget::paintEvent(QPaintEvent *painter)
{
    Q_UNUSED(painter);

    int widgetWidth = this->geometry().width();
    int widgetHeight = this->geometry().height();
    QPainter paint(this);

    //black bg
    paint.fillRect(0,0,widgetWidth,widgetHeight,bgColor);

    // dont draw any lines if no scenenode is selected
    if(!obj)
        return;

    //cosmetic
    drawBackgroundLines(paint);

    paint.setPen(linePen);

    //draw each key frame set
    int frameHeight = 20;
    int ypos = -20;


    if(obj!=nullptr && !!obj->animation)
    {
        auto frameSet = obj->animation->keyFrameSet;

        for(auto frame:frameSet->keyFrames)
        {
            drawFrame(paint,frame,ypos+=frameHeight);
        }
    }

    //cursor
    auto cursorPen = QPen(QColor::fromRgb(142,45,197));
    cursorPen.setWidth(3);
    paint.setPen(cursorPen);
    auto cursorScreenPos = timeToPos(cursorPos);
    paint.drawLine(cursorScreenPos,0,cursorScreenPos,widgetHeight);

}

void KeyFrameWidget::drawFrame(QPainter& paint,iris::FloatKeyFrame* keyFrame,int yTop)
{
    float penSize = 14;
    float penSizeSquared = 7*7;

    QPen pen(QColor::fromRgb(255,255,255));
    pen.setWidth(penSize);
    pen.setCapStyle(Qt::RoundCap);

    QPen highlightPen(QColor::fromRgb(100,100,100));
    highlightPen.setWidth(penSize);
    highlightPen.setCapStyle(Qt::RoundCap);


    for(auto key:keyFrame->keys)
    {
        int xpos = this->timeToPos(key->time);

        float distSqrd = distanceSquared(xpos,yTop+10,mousePos.x(),mousePos.y());

        if(distSqrd < penSizeSquared)
        {
            paint.setPen(highlightPen);
            paint.drawPoint(xpos,yTop+10);//frame height should be 10
        }
        else
        {
            paint.setPen(pen);
            paint.drawPoint(xpos,yTop+10);//frame height should be 10
        }

    }
}

void KeyFrameWidget::drawBackgroundLines(QPainter& paint)
{
    QPen smallPen = QPen(QColor::fromRgb(55,55,55));
    QPen bigPen = QPen(QColor::fromRgb(200,200,200));


    //find increment automatically
    float increment = 10.0f;//start on the smallest level
    auto range = rangeEnd-rangeStart;
    while(increment*10<range)
        increment*=10;
    float smallIncrement = increment/10.0f;

    float startTime = rangeStart - fmod(rangeStart,increment)-increment;
    float endTime = rangeEnd - fmod(rangeEnd,increment)+increment;

    int widgetHeight = this->geometry().height();

    //small increments
    paint.setPen(smallPen);
    for(float x=startTime;x<endTime;x+=smallIncrement)
    {
        int screenPos = timeToPos(x);
        paint.drawLine(screenPos,0,screenPos,widgetHeight);
    }

    //big increments
    paint.setPen(bigPen);
    for(float x=startTime;x<endTime;x+=increment)
    {
        int screenPos = timeToPos(x);
        paint.drawLine(screenPos,0,screenPos,widgetHeight);

        int timeInSeconds = (int)x;
        int secs = timeInSeconds%60;
        int mins = timeInSeconds/60;
        int hours = timeInSeconds/3600;

        paint.drawText(screenPos+3,widgetHeight-5,QString("%1:%2:%3")
                       .arg(hours,2,10,QLatin1Char('0'))
                       .arg(mins,2,10,QLatin1Char('0'))
                       .arg(secs,2,10,QLatin1Char('0')));
    }

}

void KeyFrameWidget::mousePressEvent(QMouseEvent* evt)
{
    Q_UNUSED(evt);
    dragging = true;
    mousePos = evt->pos();
    clickPos = mousePos;

    if(evt->button() == Qt::LeftButton)
        leftButtonDown = true;
    if(evt->button() == Qt::MiddleButton)
        middleButtonDown = true;
    if(evt->button() == Qt::RightButton)
        rightButtonDown = true;

    if(mousePos==clickPos && evt->button() == Qt::LeftButton)
    {
        this->selectedKey = this->getSelectedKey(mousePos.x(),mousePos.y());
    }
    else if(leftButtonDown)
    {
        cursorPos = posToTime(evt->x());
        emit cursorTimeChanged(cursorPos);
    }
}

void KeyFrameWidget::mouseReleaseEvent(QMouseEvent* evt)
{
    dragging = false;
    mousePos = evt->pos();

    if(mousePos==clickPos && evt->button() == Qt::RightButton)
    {
        //qDebug()<<"Context Menu"<<endl;

        //show context menu
        QMenu menu;

        menu.addAction("delete");
        //menu.exec();
    }

    if(evt->button() == Qt::LeftButton)
        leftButtonDown = false;
    if(evt->button() == Qt::MiddleButton)
        middleButtonDown = false;
    if(evt->button() == Qt::RightButton)
        rightButtonDown = false;


}

void KeyFrameWidget::mouseMoveEvent(QMouseEvent* evt)
{
    if(leftButtonDown && selectedKey!=nullptr)
    {
        //key dragging
        auto timeDiff = posToTime(evt->x())-posToTime(mousePos.x());
        selectedKey->time+=timeDiff;
    }
    else if(leftButtonDown)
    {
        cursorPos = posToTime(evt->x());
        emit cursorTimeChanged(cursorPos);
    }

    if(middleButtonDown)
    {
        //qDebug()<<"middle mouse dragging"<<endl;
        auto timeDiff = posToTime(evt->x())-posToTime(mousePos.x());
        rangeStart-=timeDiff;
        rangeEnd-=timeDiff;
    }


    mousePos = evt->pos();
    this->repaint();
}

void KeyFrameWidget::wheelEvent(QWheelEvent* evt)
{
    auto delta = evt->delta();
    float sign = delta<0?-1:1;

    //0.2f here is the zoom speed
    float scale = 1.0f-sign*0.2f;

    float timeSpacePivot = posToTime(evt->x());
    rangeStart = timeSpacePivot+(rangeStart-timeSpacePivot)*scale;
    rangeEnd = timeSpacePivot+(rangeEnd-timeSpacePivot)*scale;

    //min start

    this->repaint();
}


float KeyFrameWidget::getTimeAtCursor()
{
    return cursorPos;
}

void KeyFrameWidget::setTime(float time)
{
    cursorPos = time;
    this->repaint();
}

void KeyFrameWidget::setTimeRange(float start,float end)
{
    rangeStart = start;
    rangeEnd = end;
    this->repaint();
}

void KeyFrameWidget::setStartTime(float start)
{
    rangeStart = start;
    this->repaint();
}

void KeyFrameWidget::setEndTime(float end)
{
    rangeEnd = end;
    this->repaint();
}

float KeyFrameWidget::getStartTimeRange()
{
    return rangeStart;
}

float KeyFrameWidget::getEndTimeRange()
{
    return rangeEnd;
}

int KeyFrameWidget::timeToPos(float timeInSeconds)
{
    float timeSpacePos = (timeInSeconds-rangeStart)/(rangeEnd-rangeStart);
    return (int)(timeSpacePos*this->geometry().width());
}

float KeyFrameWidget::posToTime(int xpos)
{
    float range = rangeEnd-rangeStart;
    return rangeStart+range*((float)xpos/this->geometry().width());
}

float KeyFrameWidget::distanceSquared(float x1,float y1,float x2,float y2)
{
    float dx = x2-x1;
    float dy = y2-y1;
    return dx*dx + dy*dy;
}

iris::FloatKey* KeyFrameWidget::getSelectedKey(int x,int y)
{
    if(!obj)
        return nullptr;

    float penSizeSquared = 7*7;

    int frameHeight = 20;
    int ypos = -20;
    for(auto keyFrame:obj->animation->keyFrameSet->keyFrames)
    {
        ypos+=frameHeight;
        for(auto key:keyFrame->keys)
        {
            int xpos = this->timeToPos(key->time);

            float distSqrd = distanceSquared(xpos,ypos+10,x,y);

            if(distSqrd < penSizeSquared)
            {
                return key;
            }
        }
    }

    return nullptr;
}
