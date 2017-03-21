#include "keyframecurvewidget.h"
#include "ui_keyframecurvewidget.h"

#include <QPainter>
#include "animationwidgetdata.h"

void KeyFrameCurveWidget::setAnimWidgetData(AnimationWidgetData *value)
{
    animWidgetData = value;
}

KeyFrameCurveWidget::KeyFrameCurveWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::KeyFrameCurveWidget)
{
    ui->setupUi(this);
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

}

void KeyFrameCurveWidget::mouseReleaseEvent(QMouseEvent *evt)
{

}

void KeyFrameCurveWidget::mouseMoveEvent(QMouseEvent *evt)
{

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


}
