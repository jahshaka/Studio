/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "keyframecurvewidget.h"
#include "ui_keyframecurvewidget.h"

#include <QWidget>
#include <QDebug>
#include <QPainter>
#include <QMouseEvent>
#include <vector>
#include <QPainterPath>
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
#include <QMenu>
#include <math.h>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVariant>


struct CurveHandle
{
    //CurveHandleType type;
    iris::FloatKey* key;
};

void KeyFrameCurveWidget::setAnimWidgetData(AnimationWidgetData *value)
{
    animWidgetData = value;
}

KeyFrameCurveWidget::KeyFrameCurveWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::KeyFrameCurveWidget)
{
    ui->setupUi(this);

    bgColor = QColor::fromRgb(50,50,50);
    propColor = QColor::fromRgb(70, 70, 70, 50);
    itemColor = QColor::fromRgb(255,255,255);

    linePen = QPen(itemColor);
    cursorPen = QPen(QColor::fromRgb(142,45,197));
    cursorPen.setWidth(3);

    curvePen = QPen(QColor::fromRgb(255,255,255));
    curvePen.setWidth(5);

    dragHandleType = DragHandleType::None;

    setMouseTracking(true);

    dragging = false;

    leftButtonDown = false;
    middleButtonDown = false;
    rightButtonDown = false;

    labelWidget = nullptr;
    animWidgetData = nullptr;

    selectedKey = nullptr;

    // point's diameter
    keyPointRadius = 7;
    handlePointRadius = keyPointRadius - 2;
    handleLength = 50;
}

KeyFrameCurveWidget::~KeyFrameCurveWidget()
{
    delete ui;
}


void KeyFrameCurveWidget::setLabelWidget(KeyFrameLabelTreeWidget *value)
{
    labelWidget = value;

    connect(labelWidget->getTree(), SIGNAL(itemSelectionChanged()), this, SLOT(selectedCurveChanged()));
}

void KeyFrameCurveWidget::paintEvent(QPaintEvent *painter)
{
    Q_UNUSED(painter);

    if (!animWidgetData)
        return;

    int widgetWidth = this->geometry().width();
    int widgetHeight = this->geometry().height();
    QPainter paint(this);
    paint.setRenderHint(QPainter::Antialiasing, true);

    //black bg
    auto bgColor = QColor::fromRgb(50,50,50);
    paint.fillRect(0,0,widgetWidth,widgetHeight,bgColor);

    //draw grid
    drawGrid(paint);

    // dont draw any lines if no scenenode is selected
    // if(!animWidgetData->sceneNode)
    //     return;

    drawKeyFrames(paint);
    drawKeys(paint);
}

void KeyFrameCurveWidget::mousePressEvent(QMouseEvent *evt)
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

    if(evt->button() == Qt::LeftButton) {
        // This means a key is selected and the handles are showing
        // Check for a handle hits first
        if (selectedKey != nullptr) {

            if (isLeftHandleHit(selectedKey, mousePos.x(), mousePos.y())) {
                dragHandleType = DragHandleType::LeftTangent;
            } else if(isRightHandleHit(selectedKey, mousePos.x(), mousePos.y())) {
                dragHandleType = DragHandleType::RightTangent;
            } else {
                this->selectedKey = this->getKeyAt(mousePos.x(),mousePos.y());
                if( selectedKey != nullptr )
                    dragHandleType = DragHandleType::Point;
                else
                    dragHandleType = DragHandleType::None;
            }

        } else {
            this->selectedKey = this->getKeyAt(mousePos.x(),mousePos.y());

            if( selectedKey != nullptr )
                dragHandleType = DragHandleType::Point;
            else
                dragHandleType = DragHandleType::None;
        }

        this->repaint();
    }
}

void KeyFrameCurveWidget::mouseReleaseEvent(QMouseEvent *evt)
{
    dragging = false;
    mousePos = evt->pos();

    if(mousePos==clickPos && evt->button() == Qt::RightButton)
    {
        auto key = this->getKeyAt(mousePos.x(),mousePos.y());

        if (key != nullptr) {
            QMenu menu;
            menu.addAction("Delete");
            menu.addSeparator();

            auto handleMenu = new QMenu("Handles");
            //handleMenu.setTitle("Handles");
            handleMenu->addAction("Auto");
            handleMenu->addAction("Broken");
            handleMenu->addAction("Flat");
            menu.addMenu(handleMenu);

            auto leftTangent = new QMenu("Left Tangent");
            leftTangent->addAction("Free");
            leftTangent->addAction("Linear");
            leftTangent->addAction("Constant");
            menu.addMenu(leftTangent);

            auto rightTangent = new QMenu("Right Tangent");
            rightTangent->addAction("Free");
            rightTangent->addAction("Linear");
            rightTangent->addAction("Constant");
            menu.addMenu(rightTangent);

            menu.exec(this->mapToGlobal(mousePos));
        }
    }

    if(evt->button() == Qt::LeftButton)
        leftButtonDown = false;
    if(evt->button() == Qt::MiddleButton)
        middleButtonDown = false;
    if(evt->button() == Qt::RightButton)
        rightButtonDown = false;
}

void KeyFrameCurveWidget::mouseMoveEvent(QMouseEvent *evt)
{
    int widgetHeight = this->geometry().height();

    if(leftButtonDown && selectedKey!=nullptr)
    {

        if(dragHandleType == DragHandleType::Point) {
            //key dragging
            auto timeDiff = posToTime(evt->x())-posToTime(mousePos.x());
            selectedKey->time += timeDiff;

            auto valDiff = animWidgetData->posToValue(evt->y(), widgetHeight) -
                           animWidgetData->posToValue(mousePos.y(), widgetHeight);
            selectedKey->value += valDiff;

            // resort keys
            for(auto frame : keyFrames)
                frame->sortKeys();

			emit keyChanged(selectedKey);

        } else if (dragHandleType == DragHandleType::LeftTangent) {
            // move handle and recalc tangent
            // this is all performed in the space of the canvas
            auto point = getLeftTangentHandlePoint(selectedKey);
            auto diff = evt->pos() - mousePos;
            point += diff;

            // now normalize and recalc tangent
            //auto relPos = point - getKeyFramePoint(selectedKey);
            auto relPos = mousePos - getKeyFramePoint(selectedKey);
            auto relVec = QVector2D(relPos.x(),relPos.y()).normalized();

            if(relVec.x() <= 0)
                selectedKey->leftSlope = relVec.y() / -relVec.x();
            else {
                if(relVec.y() < 0)
                    selectedKey->leftSlope = -100000000;
                else
                    selectedKey->leftSlope = 100000000;
            }

            if (selectedKey->handleMode == iris::HandleMode::Joined) {
                selectedKey->rightSlope = -selectedKey->leftSlope;
            }

			emit keyChanged(selectedKey);

        } else if(dragHandleType == DragHandleType::RightTangent) {
            // move handle and recalc tangent
            // this is all performed in the space of the canvas
            auto point = getRightTangentHandlePoint(selectedKey);
            auto diff = evt->pos() - mousePos;
            point += diff;

            // now normalize and recalc tangent
            auto relPos = mousePos - getKeyFramePoint(selectedKey);
            auto relVec = QVector2D(relPos.x(),relPos.y()).normalized();

            if(relVec.x() >= 0)
                selectedKey->rightSlope = relVec.y() / relVec.x();
            else if(relVec.y() < 0)
                selectedKey->rightSlope = -100000000;
            else
                selectedKey->rightSlope = 100000000;

            if (selectedKey->handleMode == iris::HandleMode::Joined) {
                selectedKey->leftSlope = -selectedKey->rightSlope;
            }

			emit keyChanged(selectedKey);
        }

        this->repaint();
    }
    else if(leftButtonDown)
    {
        animWidgetData->cursorPosInSeconds = posToTime(evt->x());
    }
    if(middleButtonDown)
    {
        // move time
        auto timeDiff = posToTime(evt->x()) - posToTime(mousePos.x());
        animWidgetData->rangeStart-=timeDiff;
        animWidgetData->rangeEnd-=timeDiff;

        // move value
        auto widgetHeight = this->geometry().height();
        auto valDiff = animWidgetData->posToValue(evt->y(), widgetHeight) -
                       animWidgetData->posToValue(mousePos.y(), widgetHeight);

        animWidgetData->minValue -= valDiff;
        animWidgetData->maxValue -= valDiff;

        animWidgetData->refreshWidgets();
    }

    mousePos = evt->pos();
}

void KeyFrameCurveWidget::wheelEvent(QWheelEvent *evt)
{

}

void KeyFrameCurveWidget::drawGrid(QPainter &paint)
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

    // values on the side
    increment = 1.0f;//start on the smallest level
    range = animWidgetData->maxValue - animWidgetData->minValue;
    while(increment * 10 < range)
        increment *= 10;
    smallIncrement = increment / 10.0f;

    float startValue = animWidgetData->minValue - fmod(animWidgetData->minValue, increment) - increment;
    float endValue = animWidgetData->maxValue - fmod(animWidgetData->maxValue, increment) + increment;

    //small increments
    paint.setPen(smallPen);
    for(float x=startValue;x<endValue;x+=smallIncrement)
    {
        int screenPos = animWidgetData->valueToPos(x, widgetHeight);
        paint.drawLine(0,screenPos,15,screenPos);
    }

    //big increments
    paint.setPen(bigPen);
    for(float x=startValue;x<endValue;x+=increment)
    {
        int screenPos = animWidgetData->valueToPos(x, widgetHeight);
        paint.drawLine(0,screenPos,15,screenPos);
    }
}

void KeyFrameCurveWidget::drawKeyFrames(QPainter &paint)
{
    paint.setPen(curvePen);

    auto widgetWidth = this->geometry().width();
    auto widgetHeight = this->geometry().height();

    for (auto keyFrame : keyFrames) {
        for( unsigned i = 0; i<keyFrame->keys.size() - 1; i++) {
            iris::FloatKey* a = keyFrame->keys[i];
            iris::FloatKey* b = keyFrame->keys[i+1];

            /*
            paint.drawLine(animWidgetData->timeToPos(a->time, widgetWidth),
                           animWidgetData->valueToPos(a->value, widgetHeight),
                           animWidgetData->timeToPos(b->time, widgetWidth),
                           animWidgetData->valueToPos(b->value, widgetHeight));
            */

            QPoint ap = QPoint(animWidgetData->timeToPos(a->time, widgetWidth),
                              animWidgetData->valueToPos(a->value, widgetHeight));

            QPoint bp = QPoint(animWidgetData->timeToPos(b->time, widgetWidth),
                              animWidgetData->valueToPos(b->value, widgetHeight));

            auto dist = bp.x() - ap.x();
            float third = dist * 0.33333f;

            QPainterPath path;
            path.moveTo(ap);
            path.cubicTo(ap + QPoint(third, third * a->rightSlope),bp - QPoint(third, third * -b->leftSlope),bp);

            paint.drawPath(path);
        }
    }
}

void KeyFrameCurveWidget::drawKeys(QPainter &paint)
{
    auto defaultBrush = QBrush(QColor::fromRgb(255, 255, 255), Qt::SolidPattern);
    auto innerBrush = QBrush(QColor::fromRgb(155, 155, 155), Qt::SolidPattern);
    auto highlightBrush = QBrush(QColor::fromRgb(155, 155, 155), Qt::SolidPattern);

    auto widgetWidth = this->geometry().width();
    auto widgetHeight = this->geometry().height();


    paint.setPen(Qt::NoPen);
    for (auto keyFrame : keyFrames) {
        for( unsigned i = 0; i<keyFrame->keys.size(); i++) {
            iris::FloatKey* a = keyFrame->keys[i];

            QPoint ap = QPoint(animWidgetData->timeToPos(a->time, widgetWidth),
                              animWidgetData->valueToPos(a->value, widgetHeight));

            if (a == selectedKey) {
                // draw handles

                paint.setPen(Qt::white);

                // left handle
                auto handlePoint = getLeftTangentHandlePoint(a);

                paint.drawLine(ap, handlePoint);

                paint.setBrush(defaultBrush);
                paint.drawEllipse(handlePoint, (int)keyPointRadius - 1, (int)keyPointRadius -1);

                paint.setBrush(innerBrush);
                paint.drawEllipse(handlePoint, ((int)keyPointRadius - 2),
                                               ((int)keyPointRadius - 2));

                // right handle
                handlePoint = getRightTangentHandlePoint(a);

                paint.drawLine(ap, handlePoint);

                paint.setBrush(defaultBrush);
                paint.drawEllipse(handlePoint, (int)keyPointRadius - 1, (int)keyPointRadius -1);

                paint.setBrush(innerBrush);
                paint.drawEllipse(handlePoint, ((int)keyPointRadius - 2),
                                               ((int)keyPointRadius - 2));

                paint.setPen(Qt::NoPen);


            }

            paint.setBrush(defaultBrush);
            paint.drawEllipse(ap, (int)keyPointRadius, (int)keyPointRadius);

            paint.setBrush(innerBrush);
            paint.drawEllipse(ap, ((int)keyPointRadius - 2),
                                  ((int)keyPointRadius - 2));


        }
    }
}

void KeyFrameCurveWidget::drawKeyHandles(QPainter &paint)
{

}

iris::FloatKey* KeyFrameCurveWidget::getKeyAt(int x, int y)
{
    int widgetWidth = this->geometry().width();
    int widgetHeight = this->geometry().height();

    auto mousePos = QVector2D(x, y);

    for (auto keyFrame : keyFrames) {
        for( unsigned i = 0; i<keyFrame->keys.size(); i++) {
            iris::FloatKey* a = keyFrame->keys[i];

            auto ap = QVector2D(animWidgetData->timeToPos(a->time, widgetWidth),
                              animWidgetData->valueToPos(a->value, widgetHeight));


            //qDebug() << ap.distanceToPoint(mousePos);
            if(ap.distanceToPoint(mousePos) <= keyPointRadius)
                return a;
        }
    }

    return nullptr;
}

QPoint KeyFrameCurveWidget::getLeftTangentHandlePoint(iris::FloatKey *key)
{
    auto offset = QVector2D(-1, key->leftSlope).normalized() * handleLength;
    return QPoint(offset.x(), offset.y()) + getKeyFramePoint(key);
}

QPoint KeyFrameCurveWidget::getRightTangentHandlePoint(iris::FloatKey *key)
{
    auto offset = QVector2D(1, key->rightSlope).normalized() * handleLength;
    return QPoint(offset.x(), offset.y()) + getKeyFramePoint(key);
}

QPoint KeyFrameCurveWidget::getKeyFramePoint(iris::FloatKey *key)
{
    return QPoint(animWidgetData->timeToPos(key->time, this->geometry().width()),
                  animWidgetData->valueToPos(key->value, this->geometry().height()));
}

bool KeyFrameCurveWidget::isLeftHandleHit(iris::FloatKey* key, int x, int y)
{
    auto pos = getLeftTangentHandlePoint(key);
    if (QVector2D(pos).distanceToPoint(QVector2D(x,y)) <= handlePointRadius)
        return true;

    return false;
}

bool KeyFrameCurveWidget::isRightHandleHit(iris::FloatKey* key, int x, int y)
{
    auto pos = getRightTangentHandlePoint(key);
    if (QVector2D(pos).distanceToPoint(QVector2D(x,y)) <= handlePointRadius)
        return true;

    return false;
}

void KeyFrameCurveWidget::selectedCurveChanged()
{
    keyFrames.clear();

    // get selected treeitem from labelwidget and draw its curves
    auto treeItem = labelWidget->getSelectedTreeItem();
    if (!treeItem)
        return;

    KeyFrameData data = treeItem->data(0,Qt::UserRole).value<KeyFrameData>();
    if (data.isProperty() && data.keyFrame == nullptr) {
        for (int i =0;i< treeItem->childCount(); i++) {
            auto childItem = treeItem->child(i);
            auto childData = childItem->data(0,Qt::UserRole).value<KeyFrameData>();

            keyFrames.append(childData.keyFrame);
        }
    } else {
        keyFrames.append(data.keyFrame);
    }

    this->repaint();
}

int KeyFrameCurveWidget::timeToPos(float timeInSeconds)
{
    return animWidgetData->timeToPos(timeInSeconds, this->geometry().width());
}

float KeyFrameCurveWidget::posToTime(int xpos)
{
    return animWidgetData->posToTime(xpos, this->geometry().width());
}

void KeyFrameCurveWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent( event );

    // highlight a keyframe if none is highlighted so curves can show

}
