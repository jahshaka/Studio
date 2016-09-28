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
#include "../scenegraph/scenenodes.h"
#include "keyframewidget.h"
#include <QMenu>

KeyFrameWidget::KeyFrameWidget(QWidget* parent):
    QWidget(parent)
{
    this->setGeometry(0,0,800,500);

    bgColor = QColor::fromRgb(50,50,50);
    itemColor = QColor::fromRgb(255,255,255);

    maxTimeInSeconds = 30;
    cursorPos = 10;

    linePen = QPen(itemColor);
    cursorPen = QPen(QColor::fromRgb(142,45,197));
    cursorPen.setWidth(3);

    setMouseTracking(true);

    dragging = false;

    scaleRatio = 30;

    obj = nullptr;
}

void KeyFrameWidget::setSceneNode(SceneNode* node)
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

    //cosmetic
    drawBackgroundLines(paint);

    paint.setPen(linePen);

    //draw each key frame set
    int frameHeight = 20;
    int ypos = -20;

    if(obj!=nullptr)
    {
        if(obj->transformAnim!=nullptr)
        {
            if(obj->transformAnim->pos->hasKeys())
                drawFrame(obj->transformAnim->pos,&paint,ypos+=frameHeight);

            if(obj->transformAnim->rot->hasKeys())
                drawFrame(obj->transformAnim->rot,&paint,ypos+=frameHeight);

            if(obj->transformAnim->scale->hasKeys())
                drawFrame(obj->transformAnim->scale,&paint,ypos+=frameHeight);
        }

        if(obj->sceneNodeType == SceneNodeType::Light)
        {
            auto light = static_cast<LightNode*>(obj);
            if(light->lightAnim->color->hasKeys())
                drawFrame(light->lightAnim->color,&paint,ypos+=frameHeight);

            if(light->lightAnim->intensity->hasKeys())
                drawFrame(light->lightAnim->intensity,&paint,ypos+=frameHeight);
        }

    }

}

void KeyFrameWidget::drawBackgroundLines(QPainter& paint)
{
    QPen pen = QPen(QColor::fromRgb(55,55,55));
    paint.setPen(pen);

    int widgetWidth = this->geometry().width();
    int widgetHeight = this->geometry().height();
    for(int x=0;x<widgetWidth;x+=scaleRatio*3)
    {
        paint.drawLine(x,0,x,widgetHeight);
    }
}

int KeyFrameWidget::getXPosFromSeconds(float seconds)
{
    return (int)(seconds*scaleRatio);
}

void KeyFrameWidget::mousePressEvent(QMouseEvent* evt)
{
    Q_UNUSED(evt);
    dragging = true;
    mousePos = evt->pos();
    clickPos = mousePos;
}

void KeyFrameWidget::mouseReleaseEvent(QMouseEvent* evt)
{
    Q_UNUSED(evt);
    dragging = false;
    mousePos = evt->pos();

    qDebug()<<"Mouse Release"<<endl;

    if(mousePos==clickPos && evt->button() == Qt::RightButton)
    {
        qDebug()<<"Context Menu"<<endl;

        //show context menu
        QMenu menu;

        menu.addAction("delete");
        //menu.exec();
    }
}

void KeyFrameWidget::mouseMoveEvent(QMouseEvent* evt)
{
    mousePos = evt->pos();

    if(dragging)
    {
        //assuming this is local
        int x = evt->x();
        cursorPos = x;


    }

    this->repaint();
}


float KeyFrameWidget::getTimeAtCursor()
{
    return posToTime(cursorPos);
}

void KeyFrameWidget::setTime(float time)
{
    Q_UNUSED(time);
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
    float range = rangeEnd-rangeStart;
    return (int)((timeInSeconds/range)*this->geometry().width());
}

float KeyFrameWidget::posToTime(int xpos)
{
    float range = rangeEnd-rangeStart;
    return range*((float)xpos/this->geometry().width());
}

float KeyFrameWidget::distanceSquared(float x1,float y1,float x2,float y2)
{
    float dx = x2-x1;
    float dy = y2-y1;
    return dx*dx + dy*dy;
}
