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
#include <vector>
//#include "../scenegraph/scenenodes.h"
#include "../irisgl/src/scenegraph/scenenode.h"
#include "../irisgl/src/animation/keyframeset.h"
#include "../irisgl/src/animation/keyframeanimation.h"
#include "../irisgl/src/animation/animation.h"
#include "../irisgl/src/animation/propertyanim.h"
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

void KeyFrameWidget::deleteContextKey()
{
    auto anim = obj->getAnimation();

    auto propAnim = anim->getPropertyAnim(contextKey.propertyName);
    if (propAnim != nullptr) {
        if (contextKey.keyType == DopeKeyType::FloatKey) {
            auto frame = propAnim->getKeyFrame(contextKey.subPropertyName);
            frame->removeKey(contextKey.floatKey);

            labelWidget->recalcPropertySummaryKeys(contextKey.propertyName);
        } else if (contextKey.keyType == DopeKeyType::SummaryKey) {
            // loop through all keys and try to remove them from all each property
            // n^2, but its the best we can do right now
            for( auto key : contextKey.summaryKey.keys) {
                for( auto frame : propAnim->getKeyFrames()) {
                    frame.keyFrame->removeKey(key);
                }
            }

            labelWidget->recalcPropertySummaryKeys(contextKey.propertyName);
        }
    }

    contextKey = DopeKey::Null();

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

    pointPen = QPen(QColor::fromRgb(0, 0, 0));
    pointPen.setWidth(1);

    setMouseTracking(true);

    dragging = false;

    scaleRatio = 30;

    leftButtonDown = false;
    middleButtonDown = false;
    rightButtonDown = false;

    labelWidget = nullptr;
    animWidgetData = nullptr;

    defaultBrush = QBrush(QColor::fromRgb(255, 255, 255), Qt::SolidPattern);
    innerBrush = QBrush(QColor::fromRgb(155, 155, 155), Qt::SolidPattern);
    highlightBrush = QBrush(QColor::fromRgb(155, 155, 155), Qt::SolidPattern);

    keyPointSize = 8;
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
    //if(!obj)
    //    return;

    //cosmetic
    drawBackgroundLines(paint);

    paint.setPen(linePen);

    //draw each key frame set
    int ypos = 0;

    if (labelWidget != nullptr) {
        auto top = 0;
        auto tree = labelWidget->getTree();
        for( int i = 0; i < tree->invisibleRootItem()->childCount(); ++i ) {
            auto item = tree->invisibleRootItem()->child(i);

            drawFrame(paint, tree, item, top );
        }
    }

    //cursor
    auto cursorPen = QPen(QColor::fromRgb(255, 255, 255));
    cursorPen.setWidth(1);
    paint.setPen(cursorPen);
    auto cursorScreenPos = timeToPos(animWidgetData->cursorPosInSeconds);
    paint.drawLine(cursorScreenPos,0,cursorScreenPos,widgetHeight);

}

void KeyFrameWidget::drawPoint(QPainter& paint, QPoint point, bool isHighlight)
{
    int halfHandleWidth = keyPointSize;
    QPainterPath path;
    path.moveTo(point.x() - halfHandleWidth, point.y());
    path.lineTo(point.x() , point.y() - halfHandleWidth);
    path.lineTo(point.x() + halfHandleWidth, point.y());
    path.lineTo(point.x() , point.y() + halfHandleWidth);

    if (isHighlight) {
        paint.fillPath(path, highlightBrush);
        paint.strokePath(path, pointPen);
    } else {
        paint.fillPath(path, defaultBrush);
        paint.strokePath(path, pointPen);
    }
}

void KeyFrameWidget::drawFrame(QPainter& paint, QTreeWidget* tree, QTreeWidgetItem* item, int& yTop)
{
    auto data = item->data(0,Qt::UserRole).value<KeyFrameData>();
    auto height = tree->visualItemRect(item).height();

    float penSizeSquared = keyPointSize * keyPointSize;
    auto halfHeight = + height / 2.0f;


    if (data.keyFrame != nullptr) {
        paint.fillRect(0, yTop, width(), height,QBrush(QColor(0,0,0,15)));

        for(auto key:data.keyFrame->keys)
        {
            int xpos = this->timeToPos(key->time);

            float distSqrd = distanceSquared(xpos, yTop + halfHeight, mousePos.x(), mousePos.y());
            auto point = QPoint(xpos, yTop + height / 2.0f);

            if(distSqrd < penSizeSquared)
            {
                drawPoint(paint, point, true);
            }
            else
            {
                drawPoint(paint, point);
            }
        }
    } else if(data.isProperty()){ // draw summary keys
        paint.fillRect(0, yTop, width(), height,QBrush(QColor(0,0,0,40)));
        for(auto& key:data.summaryKeys)
        {
            int xpos = this->timeToPos(key.getTime());

            float distSqrd = distanceSquared(xpos, yTop + halfHeight, mousePos.x(), mousePos.y());
            auto point = QPoint(xpos, yTop + height / 2.0f);

            if(distSqrd < penSizeSquared)
            {
                drawPoint(paint, point, true);
            }
            else
            {
                drawPoint(paint, point);
            }
        }
    }

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

    if(evt->button() == Qt::LeftButton)
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
        contextKey = this->getSelectedKey(mousePos.x(),mousePos.y());

        if (!contextKey.isNull()) {
            //show context menu
            QMenu menu;

            auto deleteAction = menu.addAction("Delete");
            connect(deleteAction, SIGNAL(triggered(bool)), this, SLOT(deleteContextKey()));

            menu.exec(this->mapToGlobal(mousePos));
        }
    }

    if(evt->button() == Qt::LeftButton) {
        leftButtonDown = false;
        selectedKey = DopeKey::Null();

        // recalculate animation length
        if (!!obj && obj->hasActiveAnimation()) {
            auto anim = obj->getAnimation();
            anim->calculateAnimationLength();
        }
    }
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
    auto delta = evt->angleDelta().y();
    float sign = delta<0?-1:1;

    //0.2f here is the zoom speed
    float scale = 1.0f-sign*0.2f;

    float timeSpacePivot = posToTime(evt->position().x());
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
    //auto keyPointRadius = 7;//*7;

    auto data = item->data(0,Qt::UserRole).value<KeyFrameData>();
    auto height = tree->visualItemRect(item).height();

    if (data.keyFrame != nullptr) {
        for(auto key:data.keyFrame->keys)
        {
            int xpos = this->timeToPos(key->time);

            auto point = QVector2D(xpos, yTop + height / 2.0f);
            if(point.distanceToPoint(QVector2D(mousePos)) <= keyPointSize)
                return DopeKey(key, data.propertyName, data.subPropertyName);
        }
    } else if(data.isProperty()){ // draw summary keys
        //paint.fillRect(0, yTop, this->width(), height, propColor);
        for(auto keyTime:data.summaryKeys.keys())
        {
            int xpos = this->timeToPos(keyTime);

            auto point = QVector2D(xpos, yTop + height / 2.0f);
            if(point.distanceToPoint(QVector2D(mousePos)) <= keyPointSize)
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
