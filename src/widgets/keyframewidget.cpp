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
#include "../irisgl/src/scenegraph/scenenode.h"
#include "../irisgl/src/animation/keyframeset.h"
#include "../irisgl/src/animation/keyframeanimation.h"
#include "../irisgl/src/animation/animation.h"
#include "keyframewidget.h"
#include "keyframelabelwidget.h"
#include "keyframelabeltreewidget.h"
#include "keyframelabel.h"
#include "animationwidgetdata.h"
#include "../uimanager.h"
#include "animationwidget.h"
#include <QMenu>
#include <math.h>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVariant>

void KeyFrameWidget::setAnimWidgetData(AnimationWidgetData *value)
{
    animWidgetData = value;
}

KeyFrameWidget::KeyFrameWidget(QWidget* parent):
    QWidget(parent)
{
    bgColor = QColor::fromRgb(50,50,50);
    propColor = QColor::fromRgb(70, 70, 70, 50);
    itemColor = QColor::fromRgb(255,255,255);

    maxTimeInSeconds = 30;

    linePen = QPen(itemColor);
    cursorPen = QPen(QColor::fromRgb(142,45,197));
    cursorPen.setWidth(3);

    setMouseTracking(true);

    dragging = false;

    scaleRatio = 30;

    leftButtonDown = false;
    middleButtonDown = false;
    rightButtonDown = false;

    labelWidget = nullptr;
    animWidgetData = nullptr;
}

void KeyFrameWidget::setSceneNode(iris::SceneNodePtr node)
{
    obj = node;
}

void KeyFrameWidget::adjustLength()
{
}

void KeyFrameWidget::paintEvent(QPaintEvent *painter)
{
    Q_UNUSED(painter);

    if (!animWidgetData)
        return;

    int widgetWidth = this->geometry().width();
    int widgetHeight = this->geometry().height();
    QPainter paint(this);
    paint.setRenderHint(QPainter::Antialiasing, true);

    //black bg
    paint.fillRect(0,0,widgetWidth,widgetHeight,bgColor);

    // dont draw any lines if no scenenode is selected
    if(!obj)
        return;

    //cosmetic
    drawBackgroundLines(paint);

    paint.setPen(linePen);

    //draw each key frame set
    int ypos = 0;

    if (labelWidget != nullptr) {
        /*
        auto frameSet = obj->animation->keyFrameSet;

        for (auto frame:frameSet->keyFrames) {
            drawFrame(paint,frame,ypos+=frameHeight);
        }
        */
        /*
        auto labels = labelWidget->getLabels();
        for (auto label : labels) {
            drawFrame(paint, label->getKeyFrame(),ypos,label->height());

            ypos+=label->height();
        }
        */

        auto top = 0;
        auto tree = labelWidget->getTree();
        for( int i = 0; i < tree->invisibleRootItem()->childCount(); ++i ) {
            auto item = tree->invisibleRootItem()->child(i);

            drawFrame(paint, tree, item, top );
        }
    }

    //cursor
    auto cursorPen = QPen(QColor::fromRgb(142,45,197));
    cursorPen.setWidth(3);
    paint.setPen(cursorPen);
    auto cursorScreenPos = timeToPos(animWidgetData->cursorPosInSeconds);
    paint.drawLine(cursorScreenPos,0,cursorScreenPos,widgetHeight);

}

void KeyFrameWidget::drawFrame(QPainter& paint, QTreeWidget* tree, QTreeWidgetItem* item, int& yTop)
{
    auto data = item->data(0,Qt::UserRole).value<KeyFrameData>();
    auto height = tree->visualItemRect(item).height();

    float penSize = 7;
    float penSizeSquared = 7*7;

    QPen pen(QColor::fromRgb(255,255,255));
    pen.setWidth(penSize);
    pen.setCapStyle(Qt::RoundCap);

    QPen innerPen(QColor::fromRgb(155,155,155));
    innerPen.setWidth(penSize-4);
    innerPen.setCapStyle(Qt::RoundCap);

    QPen highlightPen(QColor::fromRgb(100,100,100));
    highlightPen.setWidth(penSize);
    highlightPen.setCapStyle(Qt::RoundCap);
    auto halfHeight = + height / 2.0f;

    auto defaultBrush = QBrush(QColor::fromRgb(255, 255, 255), Qt::SolidPattern);
    auto innerBrush = QBrush(QColor::fromRgb(155, 155, 155), Qt::SolidPattern);
    auto highlightBrush = QBrush(QColor::fromRgb(155, 155, 155), Qt::SolidPattern);
    paint.setPen(Qt::white);


    if (data.keyFrame != nullptr) {
        for(auto key:data.keyFrame->keys)
        {
            int xpos = this->timeToPos(key->time);

            float distSqrd = distanceSquared(xpos, yTop + halfHeight, mousePos.x(), mousePos.y());
            auto point = QPoint(xpos, yTop + height / 2.0f);

            if(distSqrd < penSizeSquared)
            {
                paint.setBrush(highlightBrush);
                paint.drawEllipse(point, penSize, penSize);

                paint.setBrush(innerBrush);
                paint.drawEllipse(point, penSize-2, penSize-2);
            }
            else
            {
                paint.setBrush(defaultBrush);
                paint.drawEllipse(point, penSize, penSize);

                paint.setBrush(innerBrush);
                paint.drawEllipse(point, penSize-2, penSize-2);
            }
        }
    } else if(data.isProperty()){ // draw summary keys
        //paint.fillRect(0, yTop, this->width(), height, propColor);
        for(auto& key:data.summaryKeys)
        {
            int xpos = this->timeToPos(key.getTime());

            float distSqrd = distanceSquared(xpos, yTop + halfHeight, mousePos.x(), mousePos.y());
            auto point = QPoint(xpos, yTop + height / 2.0f);

            if(distSqrd < penSizeSquared)
            {
                paint.setBrush(highlightBrush);
                paint.drawEllipse(point, penSize, penSize);

                paint.setBrush(innerBrush);
                paint.drawEllipse(point, penSize-2, penSize-2);
            }
            else
            {
                paint.setBrush(defaultBrush);
                paint.drawEllipse(point, penSize, penSize);

                paint.setBrush(innerBrush);
                paint.drawEllipse(point, penSize-2, penSize-2);
            }
        }
    }

    QPen smallPen = QPen(QColor::fromRgb(55,55,55));
    paint.setPen(smallPen);
    paint.drawLine(0, yTop + height, this->width(), yTop + height);

    yTop += height;

    if (item->isExpanded()) {
        for( int i = 0; i < item->childCount(); ++i ) {
            auto childItem = item->child(i);

            drawFrame(paint, tree, childItem, yTop );
        }
    }
}

void KeyFrameWidget::drawBackgroundLines(QPainter& paint)
{
    QPen smallPen = QPen(QColor::fromRgb(55,55,55));
    QPen bigPen = QPen(QColor::fromRgb(150,150,150));


    //find increment automatically
    float increment = 10.0f;//start on the smallest level
    auto range = animWidgetData->rangeEnd - animWidgetData->rangeStart;
    while(increment * 10 < range)
        increment *= 10;
    float smallIncrement = increment / 10.0f;

    float startTime = animWidgetData->rangeStart - fmod(animWidgetData->rangeStart, increment) - increment;
    float endTime = animWidgetData->rangeEnd - fmod(animWidgetData->rangeEnd, increment) + increment;

    int widgetWidth = this->geometry().width();
    int widgetHeight = this->geometry().height();

    //small increments
    paint.setPen(smallPen);
    for(float x=startTime;x<endTime;x+=smallIncrement)
    {
        int screenPos = animWidgetData->timeToPos(x, widgetWidth);
        paint.drawLine(screenPos,0,screenPos,widgetHeight);
    }

    //big increments
    paint.setPen(bigPen);
    for(float x=startTime;x<endTime;x+=increment)
    {
        int screenPos = animWidgetData->timeToPos(x, widgetWidth);
        paint.drawLine(screenPos,0,screenPos,widgetHeight);
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
    /*
    else if(leftButtonDown)
    {
        cursorPos = posToTime(evt->x());
        emit cursorTimeChanged(cursorPos);
    }
    */
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
    if(leftButtonDown && !selectedKey.isNull())
    {
        //key dragging
        auto timeDiff = posToTime(evt->x())-posToTime(mousePos.x());
        selectedKey.move(timeDiff);
        if(selectedKey.keyType == DopeKeyType::FloatKey)
        {
            // recalculate summary keys
            // todo: recalc only summary keys for this key's property
            labelWidget->recalcPropertySummaryKeys(selectedKey.propertyName);
        }
        this->repaint();
    }
    else if(leftButtonDown)
    {
        animWidgetData->cursorPosInSeconds = posToTime(evt->x());
        //emit cursorTimeChanged(animWidgetData->cursorPosInSeconds);
    }
    if(middleButtonDown)
    {
        auto timeDiff = posToTime(evt->x()) - posToTime(mousePos.x());
        animWidgetData->rangeStart-=timeDiff;
        animWidgetData->rangeEnd-=timeDiff;

        //emit timeRangeChanged(rangeStart, rangeEnd);

        animWidgetData->refreshWidgets();
    }


    mousePos = evt->pos();
    //this->repaint();
}

void KeyFrameWidget::wheelEvent(QWheelEvent* evt)
{
    auto delta = evt->delta();
    float sign = delta<0?-1:1;

    //0.2f here is the zoom speed
    float scale = 1.0f-sign*0.2f;

    float timeSpacePivot = posToTime(evt->x());
    animWidgetData->rangeStart = timeSpacePivot+(animWidgetData->rangeStart-timeSpacePivot) * scale;
    animWidgetData->rangeEnd = timeSpacePivot+(animWidgetData->rangeEnd-timeSpacePivot) * scale;

    animWidgetData->refreshWidgets();
}

int KeyFrameWidget::timeToPos(float timeInSeconds)
{
    return animWidgetData->timeToPos(timeInSeconds, this->geometry().width());
}

float KeyFrameWidget::posToTime(int xpos)
{
    return animWidgetData->posToTime(xpos, this->geometry().width());
}

float KeyFrameWidget::distanceSquared(float x1,float y1,float x2,float y2)
{
    float dx = x2-x1;
    float dy = y2-y1;
    return dx*dx + dy*dy;
}

DopeKey KeyFrameWidget::getSelectedKey(int x,int y)
{
    /*
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
    */

    int widgetWidth = this->geometry().width();
    int widgetHeight = this->geometry().height();

    auto mousePos = QVector2D(x, y);
    //qDebug() << mousePos;

    auto top = 0;
    auto tree = labelWidget->getTree();
    for( int i = 0; i < tree->invisibleRootItem()->childCount(); ++i ) {
        auto item = tree->invisibleRootItem()->child(i);

        auto dopeKey = getSelectedKey(tree, item, top );
        return dopeKey;
    }

    return DopeKey::Null();
}

DopeKey KeyFrameWidget::getSelectedKey(QTreeWidget* tree,QTreeWidgetItem *item, int &yTop)
{
    auto keyPointRadius = 7;//*7;

    auto data = item->data(0,Qt::UserRole).value<KeyFrameData>();
    auto height = tree->visualItemRect(item).height();

    if (data.keyFrame != nullptr) {
        for(auto key:data.keyFrame->keys)
        {
            int xpos = this->timeToPos(key->time);

            auto point = QVector2D(xpos, yTop + height / 2.0f);
            if(point.distanceToPoint(QVector2D(mousePos)) <= keyPointRadius)
                return DopeKey(key, data.propertyName, data.subPropertyName);
        }
    } else if(data.isProperty()){ // draw summary keys
        //paint.fillRect(0, yTop, this->width(), height, propColor);
        for(auto keyTime:data.summaryKeys.keys())
        {
            int xpos = this->timeToPos(keyTime);

            auto point = QVector2D(xpos, yTop + height / 2.0f);
            if(point.distanceToPoint(QVector2D(mousePos)) <= keyPointRadius)
                return DopeKey(data.summaryKeys[keyTime], data.propertyName, data.subPropertyName);

        }
    }

    yTop += height;

    if (item->isExpanded()) {
        for( int i = 0; i < item->childCount(); ++i ) {
            auto childItem = item->child(i);

            auto key = getSelectedKey(tree, childItem, yTop );
            if (!key.isNull())
                return key;
        }
    }

    return DopeKey::Null();
}

void KeyFrameWidget::setLabelWidget(KeyFrameLabelTreeWidget *value)
{
    labelWidget = value;
}

void DopeKey::move(float timeIncr)
{
    if (keyType == DopeKeyType::FloatKey)
        floatKey->time += timeIncr;
    else if(keyType == DopeKeyType::SummaryKey) {
        for(auto key : summaryKey.keys)
            key->time += timeIncr;
    }
}
