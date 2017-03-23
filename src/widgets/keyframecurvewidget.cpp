#include "keyframecurvewidget.h"
#include "ui_keyframecurvewidget.h"

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
#include "keyframelabelwidget.h"
#include "keyframelabeltreewidget.h"
#include "keyframelabel.h"
#include "animationwidgetdata.h"
#include <QMenu>
#include <math.h>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVariant>

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

    setMouseTracking(true);

    dragging = false;

    leftButtonDown = false;
    middleButtonDown = false;
    rightButtonDown = false;

    labelWidget = nullptr;
    animWidgetData = nullptr;
}

KeyFrameCurveWidget::~KeyFrameCurveWidget()
{
    delete ui;
}


void KeyFrameCurveWidget::setLabelWidget(KeyFrameLabelTreeWidget *value)
{
    labelWidget = value;
}

void KeyFrameCurveWidget::paintEvent(QPaintEvent *painter)
{
    Q_UNUSED(painter);

    if (!animWidgetData)
        return;

    int widgetWidth = this->geometry().width();
    int widgetHeight = this->geometry().height();
    QPainter paint(this);

    //black bg
    auto bgColor = QColor::fromRgb(50,50,50);
    paint.fillRect(0,0,widgetWidth,widgetHeight,bgColor);

    //draw grid
    drawGrid(paint);

    // dont draw any lines if no scenenode is selected
    if(!animWidgetData->sceneNode)
        return;
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

    if(mousePos==clickPos && evt->button() == Qt::LeftButton)
    {
        //this->selectedKey = this->getSelectedKey(mousePos.x(),mousePos.y());
    }
}

void KeyFrameCurveWidget::mouseReleaseEvent(QMouseEvent *evt)
{
    dragging = false;
    mousePos = evt->pos();

    if(mousePos==clickPos && evt->button() == Qt::RightButton)
    {
        QMenu menu;
        menu.addAction("delete");
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
    if(leftButtonDown && selectedKey!=nullptr)
    {
        //key dragging
        auto timeDiff = posToTime(evt->x())-posToTime(mousePos.x());
        selectedKey->time+=timeDiff;
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

int KeyFrameCurveWidget::timeToPos(float timeInSeconds)
{
    return animWidgetData->timeToPos(timeInSeconds, this->geometry().width());
}

float KeyFrameCurveWidget::posToTime(int xpos)
{
    return animWidgetData->posToTime(xpos, this->geometry().width());
}
