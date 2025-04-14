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
#include <QPainterPath>
#include <QMouseEvent>
#include "../irisgl/src/scenegraph/scenenode.h"
#include "timelinewidget.h"
#include <QtMath>
#include <cmath>

#include "animationwidgetdata.h"

//https://kernelcoder.wordpress.com/2010/08/25/how-to-insert-ruler-scale-type-widget-into-a-qabstractscrollarea-type-widget/
TimelineWidget::TimelineWidget(QWidget* parent):
    QWidget(parent)
{
    QSizePolicy sizePolicy;
    sizePolicy.setHorizontalPolicy(QSizePolicy::Fixed);
    sizePolicy.setVerticalPolicy(QSizePolicy::Preferred);
    this->setSizePolicy(sizePolicy);
    this->setGeometry(0, 0, 100, 21);

    bgColor = QColor::fromRgb(30,30,30);
    itemColor = QColor::fromRgb(255,255,255);

    linePen = QPen(itemColor);
    cursorPen = QPen(QColor::fromRgb(255, 255, 255));
    cursorPen.setWidth(1);

    setMouseTracking(true);

    dragging = false;

    drawHighlight = false;

    leftButtonDown = false;
    middleButtonDown = false;
    rightButtonDown = false;

    animWidgetData = nullptr;
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

void TimelineWidget::paintEvent(QPaintEvent *painter)
{
    Q_UNUSED(painter);

    if (!animWidgetData)
        return;

    int widgetWidth = this->geometry().width();
    int widgetHeight = this->geometry().height();

    QPainter paint(this);
    paint.setRenderHint(QPainter::Antialiasing);
    paint.setRenderHint(QPainter::Antialiasing);

    //black bg
    //paint.fillRect(0,0,widgetWidth,widgetHeight,bgColor);

    // draws the dark part of the bottom of the frame
    paint.setPen(QColor::fromRgb(100,100,100));
    paint.fillRect(0,0,widgetWidth,widgetHeight,QColor::fromRgb(80, 80, 80));
    paint.setPen(linePen);

    QPen smallPen = QPen(QColor::fromRgb(55,55,55));
    QPen bigPen = QPen(QColor::fromRgb(200,200,200));


    //find increment automatically
    float increment = 10.0f;//start on the smallest level
    auto range = animWidgetData->rangeEnd - animWidgetData->rangeStart;
    while(increment*10<range)
        increment*=10;

    float startTime = animWidgetData->rangeStart - fmod(animWidgetData->rangeStart, increment) - increment;
    float endTime = animWidgetData->rangeEnd - fmod(animWidgetData->rangeEnd, increment) + increment;

    //big increments
    paint.setPen(bigPen);
    for(float x=startTime;x<endTime;x+=increment)
    {
        int screenPos = timeToPos(x);
        paint.drawLine(screenPos,
                       widgetHeight - widgetHeight/3.0f,
                       screenPos,
                       widgetHeight);

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
    auto cursorX = timeToPos(animWidgetData->cursorPosInSeconds);
    paint.setPen(cursorPen);
    paint.drawLine(cursorX, 0, cursorX, widgetHeight);

    // draw cursor's "handle"
    int handleWidth = 10;
    int halfHandleWidth = handleWidth/2;
    int handleHeight = 15;

    QPainterPath path;
    path.moveTo(cursorX - halfHandleWidth, 0);
    path.lineTo(cursorX + halfHandleWidth, 0);
    path.lineTo(cursorX + halfHandleWidth, handleHeight);
    path.lineTo(cursorX, handleHeight+5);
    path.lineTo(cursorX - halfHandleWidth, handleHeight);
    //path.lineTo();
    paint.fillPath(path, QBrush(QColor(255, 255, 255)));
}

int TimelineWidget::timeToPos(float timeInSeconds)
{
    return animWidgetData->timeToPos(timeInSeconds, this->width());
}

float TimelineWidget::posToTime(int xpos)
{
    return animWidgetData->posToTime(xpos, this->width());
}

void TimelineWidget::setAnimWidgetData(AnimationWidgetData *value)
{
    animWidgetData = value;
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
        dragging = true;
        animWidgetData->cursorPosInSeconds = posToTime(evt->x());
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
    if(leftButtonDown)
    {
        animWidgetData->cursorPosInSeconds = posToTime(evt->x());
        cursorMoved(animWidgetData->cursorPosInSeconds);
        animWidgetData->refreshWidgets();
    }

    if(middleButtonDown)
    {
        //qDebug()<<"middle mouse dragging"<<endl;
        auto timeDiff = posToTime(evt->x()) - posToTime(mousePos.x());
        animWidgetData->rangeStart-=timeDiff;
        animWidgetData->rangeEnd-=timeDiff;

        animWidgetData->refreshWidgets();
    }

    mousePos = evt->pos();
}

void TimelineWidget::wheelEvent(QWheelEvent* evt)
{
    auto delta = evt->angleDelta().y();
    float sign = delta<0?-1:1;

    //0.2f here is the zoom speed
    float scale = 1.0f-sign*0.2f;

    float timeSpacePivot = posToTime(evt->position().x());
    animWidgetData->rangeStart = timeSpacePivot+(animWidgetData->rangeStart-timeSpacePivot) * scale;
    animWidgetData->rangeEnd = timeSpacePivot+(animWidgetData->rangeEnd-timeSpacePivot) * scale;

    animWidgetData->refreshWidgets();
}

void TimelineWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
}
