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
#include "../irisgl/src/core/scenenode.h"
#include "timelinewidget.h"
#include <QtMath>

//https://kernelcoder.wordpress.com/2010/08/25/how-to-insert-ruler-scale-type-widget-into-a-qabstractscrollarea-type-widget/
TimelineWidget::TimelineWidget(QWidget* parent):
    QWidget(parent)
{

    bgColor = QColor::fromRgb(30,30,30);
    itemColor = QColor::fromRgb(255,255,255);

    //maxTimeInSeconds = 30;
    cursorPos = 10;

    linePen = QPen(itemColor);
    cursorPen = QPen(QColor::fromRgb(142,45,197));
    cursorPen.setWidth(3);

    setMouseTracking(true);

    dragging = false;

    //node=nullptr;
    drawHighlight = false;

    rangeStart = 0;
    rangeEnd = 100;

    leftButtonDown = false;
    middleButtonDown = false;
    rightButtonDown = false;
}

void TimelineWidget::showHighlight(float start,float end)
{
    highlightStart = start;
    highlightEnd = end;
    drawHighlight = true;
    this->repaint();
}

void TimelineWidget::hideHighlight()
{
    drawHighlight = false;
    this->repaint();
}

void TimelineWidget::setSceneNode(iris::SceneNodePtr node)
{
    this->node = node;
}

float TimelineWidget::getTimeAtCursor()
{
    return posToTime(cursorPos);
}

void TimelineWidget::setTime(float time)
{
    this->setCursorPos(timeToPos(time));
}

void TimelineWidget::setTimeRange(float start,float end)
{
    rangeStart = start;
    rangeEnd = end;
    this->repaint();
}

void TimelineWidget::setStartTime(float start)
{
    rangeStart = start;
    this->repaint();
}

void TimelineWidget::setEndTime(float end)
{
    rangeEnd = end;
    this->repaint();
}

float TimelineWidget::getStartTimeRange()
{
    return rangeStart;
}

float TimelineWidget::getEndTimeRange()
{
    return rangeEnd;
}

void TimelineWidget::paintEvent(QPaintEvent *painter)
{
    Q_UNUSED(painter);

    int widgetWidth = this->geometry().width();
    int widgetHeight = this->geometry().height();

    QPainter paint(this);

    //black bg
    paint.fillRect(0,0,widgetWidth,widgetHeight,bgColor);

    // draws the dark part of the bottom of the frame
    paint.setPen(QColor::fromRgb(100,100,100));
    paint.fillRect(0,0,widgetWidth,widgetHeight/2,QColor::fromRgb(50,50,50));
    paint.setPen(linePen);

    QPen smallPen = QPen(QColor::fromRgb(55,55,55));
    QPen bigPen = QPen(QColor::fromRgb(200,200,200));


    //find increment automatically
    float increment = 10.0f;//start on the smallest level
    auto range = rangeEnd-rangeStart;
    while(increment*10<range)
        increment*=10;

    float startTime = rangeStart - fmod(rangeStart,increment)-increment;
    float endTime = rangeEnd - fmod(rangeEnd,increment)+increment;

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

    //highlights the animation range of the selected node
    if(drawHighlight)
    {
        int start = timeToPos(highlightStart);
        int end = timeToPos(highlightEnd-highlightStart);

        paint.fillRect(start,0,end,widgetWidth,QColor(200,200,255,100));
    }


    //cursor
    paint.setPen(cursorPen);
    paint.drawLine(cursorPos,0,cursorPos,widgetHeight);
}

int TimelineWidget::timeToPos(float timeInSeconds)
{
    float timeSpacePos = (timeInSeconds-rangeStart)/(rangeEnd-rangeStart);
    return (int)(timeSpacePos*this->geometry().width());
}

float TimelineWidget::posToTime(int xpos)
{
    float range = rangeEnd-rangeStart;
    return rangeStart+range*((float)xpos/this->geometry().width());
}

void TimelineWidget::setCursorPos(float timeInSeconds)
{
    //assuming x is local
    cursorPos = timeInSeconds;

    this->repaint();
    emit cursorMoved(timeInSeconds);
}

void TimelineWidget::mousePressEvent(QMouseEvent* evt)
{
    Q_UNUSED(evt);
    mousePos = evt->pos();
    clickPos = mousePos;

    if(evt->button() == Qt::LeftButton)
        leftButtonDown = true;
    if(evt->button() == Qt::MiddleButton)
        middleButtonDown = true;
    if(evt->button() == Qt::RightButton)
        rightButtonDown = true;

    if(evt->button() == Qt::LeftButton)
    {
        setCursorPos(posToTime(evt->x()));
    }
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent* evt)
{
    Q_UNUSED(evt);
    dragging = false;
    mousePos = evt->pos();

    if(evt->button() == Qt::LeftButton)
        leftButtonDown = false;
    if(evt->button() == Qt::MiddleButton)
        middleButtonDown = false;
    if(evt->button() == Qt::RightButton)
        rightButtonDown = false;
}

void TimelineWidget::mouseMoveEvent(QMouseEvent* evt)
{
    if(dragging)
    {
        setCursorPos(evt->x());
    }

    if(middleButtonDown)
    {
        //qDebug()<<"middle mouse dragging"<<endl;
        auto timeDiff = posToTime(evt->x())-posToTime(cursorPos);
        rangeStart-=timeDiff;
        rangeEnd-=timeDiff;
    }

    cursorPos = evt->x();
    mousePos = evt->pos();
    this->repaint();
}

void TimelineWidget::wheelEvent(QWheelEvent* evt)
{
    auto delta = evt->delta();
    float sign = delta<0?-1:1;

    //0.2f here is the zoom speed
    float scale = 1.0f - sign * 0.2f;

    float timeSpacePivot = posToTime(evt->x());
    rangeStart = timeSpacePivot+(rangeStart-timeSpacePivot)*scale;
    rangeEnd = timeSpacePivot+(rangeEnd-timeSpacePivot)*scale;

    this->repaint();
}

void TimelineWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
}
