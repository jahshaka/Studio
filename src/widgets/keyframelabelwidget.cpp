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
#include "keyframelabelwidget.h"


KeyFrameLabelWidget::KeyFrameLabelWidget(QWidget* parent):
    QWidget(parent)
{
    bgColor = QColor::fromRgb(50,50,50);
    itemColor = QColor::fromRgb(255,255,255);

    cursorPen = QPen(QColor::fromRgb(255,255,255));
    cursorPen.setWidth(3);

    //obj = nullptr;
}

void KeyFrameLabelWidget::setSceneNode(iris::SceneNodePtr node)
{
    obj = node;
}

void KeyFrameLabelWidget::paintEvent(QPaintEvent *painter)
{
    Q_UNUSED(painter);

    int widgetWidth = this->geometry().width();
    int widgetHeight = this->geometry().height();
    QPainter paint(this);

    //black bg
    paint.fillRect(0,0,widgetWidth,widgetHeight,bgColor);

    //draw each key frame set
    int frameHeight = 24;
    int ypos = 0;

    if(!!obj && !!obj->animation)
    {
        auto keyFrames = obj->animation->keyFrameSet->keyFrames;
        for(auto iter = keyFrames.begin();
            iter!=keyFrames.end();
            iter++)
        {
            drawFrameLabel(iter.key(),&paint,ypos+=frameHeight);
        }

    }

}

void KeyFrameLabelWidget::drawFrameLabel(QString name,QPainter* paint,int yBottom)
{
    QPen pen(QColor::fromRgb(255,255,255));
    pen.setWidth(14);
    pen.setCapStyle(Qt::RoundCap);

    paint->setPen(pen);

    paint->drawText(0,yBottom-10,name);

}

