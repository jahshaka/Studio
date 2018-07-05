#include "loader.h"

#include <QGridLayout>
#include <QPainter>
#include <QPropertyAnimation>
#include <QApplication>

Loader::Loader(QWidget *parent) : QDialog(parent)
{

	setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::X11BypassWindowManagerHint);
	setAttribute(Qt::WA_TranslucentBackground);
    label = new QLabel();
	setStyleSheet("*{color : white; }");
	setFixedSize(160, 160);
	auto layout = new QGridLayout;
	label->setAlignment(Qt::AlignCenter);
	layout->addWidget(label);
	setLayout(layout);
	startAnimation();
	show();
	QApplication::processEvents();
}


void Loader::setText(QString string)
{
    label->setText(string);
}

void Loader::stop()
{
    hide();
    destroy(this);
}

qreal Loader::length()
{
    return _length;
}

void Loader::setLength(qreal val)
{
    _length = val;
}

qreal Loader::startAngle()
{
    return _startAngle;
}

void Loader::setStartAngle(qreal val)
{
    _startAngle = val*16;
}

void Loader::setConnections()
{
   
}

void Loader::startAnimation()
{
    auto anim = new QPropertyAnimation(this,"startAngle");
    anim->setDuration(1500);
    anim->setStartValue(0);
    anim->setEndValue(-360);
    anim->setLoopCount(-1);
    anim->setEasingCurve(QEasingCurve::BezierSpline);
    anim->start();

    connect(anim,&QPropertyAnimation::valueChanged,[=](){
        update();
    });
    forwardAnimation();
}

void Loader::reverseAnimation()
{
    auto anim1 = new QPropertyAnimation(this,"length");
    anim1->setDuration(1000);
    anim1->setStartValue(4);
    anim1->setEndValue(20);
    anim1->setLoopCount(1);
    anim1->setEasingCurve(QEasingCurve::OutCurve);
    anim1->start();
    connect(anim1,&QPropertyAnimation::valueChanged,[=](){
        update();
    });
    connect(anim1,&QPropertyAnimation::finished,[=](){
        forwardAnimation();
    });
}

void Loader::forwardAnimation()
{
    auto anim1 = new QPropertyAnimation(this,"length");
    anim1->setDuration(1000);
    anim1->setStartValue(20);
    anim1->setEndValue(4);
    anim1->setLoopCount(1);
    anim1->setEasingCurve(QEasingCurve::OutCurve);
    anim1->start();
    connect(anim1,&QPropertyAnimation::valueChanged,[=](){
        update();
    });
    connect(anim1,&QPropertyAnimation::finished,[=](){
        reverseAnimation();
    });
}

void Loader::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QPen pen(QColor(52, 152, 219),10);
    pen.setCapStyle(Qt::RoundCap);
    painter.setPen(pen);
    int spanAngle = 90*length();
    painter.drawArc(offset, offset, width()-offset*2, height()-offset*2, startAngle(), spanAngle);
    pen.setColor(QColor(0,0,0,15));
    painter.setPen(pen);
    painter.drawArc(offset, offset, width()-offset*2, height()-offset*2, 0, 360*16);
}
