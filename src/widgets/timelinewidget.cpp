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
    //this->setGeometry(0,0,800,20);

    bgColor = QColor::fromRgb(30,30,30);
    itemColor = QColor::fromRgb(255,255,255);

    //maxTimeInSeconds = 30;
    cursorPos = 10;

    linePen = QPen(itemColor);
    cursorPen = QPen(QColor::fromRgb(142,45,197));
    cursorPen.setWidth(3);

    setMouseTracking(true);

    dragging = false;

    //scaleRatio = 30;

    //node=nullptr;
    drawHighlight = false;
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

void TimelineWidget::oldpaintEvent(QPaintEvent *painter)
{
    Q_UNUSED(painter);
}

void TimelineWidget::paintEvent(QPaintEvent *painter)
{
    Q_UNUSED(painter);

    int widgetWidth = this->geometry().width();
    int widgetHeight = this->geometry().height();
    QPainter paint(this);

    //black bg
    paint.fillRect(0,0,widgetWidth,widgetHeight,bgColor);

    //cosmetic
    paint.setPen(QColor::fromRgb(100,100,100));
    //paint.drawLine(0,0,widgetWidth,0);
    paint.fillRect(0,0,widgetWidth,widgetHeight/2,QColor::fromRgb(50,50,50));

    paint.setPen(linePen);

    float timeRange = rangeEnd-rangeStart;
    float timeStepWidth = widgetWidth/10;
    int majorInterval = qFloor(timeStepWidth);

    for(int x=0;x<widgetWidth;x+=1)
    {
        if(x%majorInterval==0)
        {
            paint.drawLine(x,widgetHeight,x,widgetHeight-15);

            int timeInSeconds = rangeStart+((float)x/widgetWidth)*timeRange;

            int secs = timeInSeconds%60;
            int mins = timeInSeconds/60;
            int hours = timeInSeconds/3600;

            paint.drawText(x+3,widgetHeight-5,QString("%1:%2:%3")
                           .arg(hours,2,10,QLatin1Char('0'))
                           .arg(mins,2,10,QLatin1Char('0'))
                           .arg(secs,2,10,QLatin1Char('0')));
        }
    }

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
    float range = rangeEnd-rangeStart;
    return (int)((timeInSeconds/range)*this->geometry().width());
}

float TimelineWidget::posToTime(int xpos)
{
    float range = rangeEnd-rangeStart;
    return range*((float)xpos/this->geometry().width());
}

void TimelineWidget::setCursorPos(int x)
{
    //assuming x is local
    cursorPos = x;
    if(node!=nullptr)
    {
        float time = getTimeAtCursor();
        //node->applyAnimationAtTime(time);
    }

    this->repaint();
    emit cursorMoved(posToTime(x));
}

void TimelineWidget::mousePressEvent(QMouseEvent* evt)
{
    Q_UNUSED(evt);
    dragging = true;
    setCursorPos(evt->x());
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent* evt)
{
    Q_UNUSED(evt);
    dragging = false;
}

void TimelineWidget::mouseMoveEvent(QMouseEvent* evt)
{
    if(dragging)
    {
        setCursorPos(evt->x());
    }
}

void TimelineWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
}
