#include "switch.h"

#include <QPainter>
#include <QPropertyAnimation>

Switch::Switch(QWidget *parent)
    : QCheckBox(parent), _height(28)
{
    _width = _height*2.5;
    _startPoint = 3;
    _endPoint = _width - _height ;
    setFixedSize(_width,_height);
    setOnColor(QColor(200,200,200));
    endColor = QColor(20,80,200);
    boundingRect = QRect(0,0,width(),height());
    setOn(false);
}

Switch::~Switch()
{

}

void Switch::setOnColor(QColor col)
{
    color = col;
}

QColor Switch::onColor()
{
    return color;
}

void Switch::setOn(bool val)
{
    isOn = val;
}

bool Switch::on()
{
    return isOn;
}

void Switch::setStartPoint(int point)
{
    _startPoint = point;
}

int Switch::startPoint()
{
    return _startPoint;
}

void Switch::setSize( int h)
{
    if(h == _height) return;
    _height = h;
    _width = _height*2.5;
    _startPoint = 3;
    _endPoint = _width - _height;
    setFixedSize(_width,_height);
    boundingRect = QRect(0,0,width(),height());
}

void Switch::setColor(QColor color)
{
    endColor = color;
}

void Switch::simulateClick()
{

    QPropertyAnimation *animation = new QPropertyAnimation(this, "startPoint");
        QPropertyAnimation *animation1 = new QPropertyAnimation(this, "onColor");
        if (!on()) { // if switch is off
            animation->setDuration(300);
            animation->setStartValue(startPoint());
            animation->setEndValue(_endPoint);
            animation->setEasingCurve(QEasingCurve::OutBounce);
            animation->start();
            animation1->setDuration(150);
            animation1->setStartValue(onColor());
            animation1->setEndValue(endColor.darker(120));
            animation1->setEasingCurve(QEasingCurve::OutCurve);
            animation1->start();
            connect(animation,&QPropertyAnimation::valueChanged,[=](){ update();});
            connect(animation1,&QPropertyAnimation::valueChanged,[=](){ update();});
            setCheckState(Qt::CheckState::Checked);
        }
        else {
            animation->setDuration(300);
            animation->setStartValue(_endPoint);
            animation->setEndValue(3);
            animation->setEasingCurve(QEasingCurve::OutCurve);
            animation->start();
            animation1->setDuration(250);
            animation1->setEndValue(QColor(200,200,200));
            animation1->setStartValue(endColor);
            animation1->setEasingCurve(QEasingCurve::OutCurve);
            animation1->start();
            connect(animation,&QPropertyAnimation::valueChanged,[=](){ update();});
            connect(animation1,&QPropertyAnimation::valueChanged,[=](){ update();});
            setCheckState(Qt::CheckState::Unchecked);

        }
        setOn(on() ? false:true);
        emit onChanged(on());

}

void Switch::mouseReleaseEvent(QMouseEvent *event){
    simulateClick();
}


void Switch::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        //draw groove
        painter.setPen(QPen(QColor(0,0,0,50), 2));
        painter.drawRoundedRect(boundingRect, _height/2, 100);
        //draw on color
        painter.setBrush(onColor());
        painter.drawRoundedRect(boundingRect, _height/2, 100);

        //draw handle
        if(on()){
            painter.setBrush(endColor);
            painter.drawEllipse(startPoint(),3,_height-6,_height-6);
        }else{
            painter.setBrush(onColor());
            painter.drawEllipse(startPoint(),3,_height-6,_height-6);
        }

}
