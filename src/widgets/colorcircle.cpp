#include "colorcircle.h"

#include <QPainter>
#include <qvector2d.h>
#include <qmath.h>
#include <QDebug>
#include <QMouseEvent>
#include <QStyleOption>

ColorCircle::ColorCircle(QWidget *parent, QColor ic) : QWidget(parent)
{
    setGeometry(4,4,parent->width()-4,parent->height()-4);
    radius = width()/2-4;
    centerPoint.setX(radius+4);
    centerPoint.setY(radius+4);
    v=255;
    drawCircleColorBackground();
    repaint();

    QPalette palette;
    palette.setBrush(this->backgroundRole(), QBrush(QImage("bg.png")));
  //  qDebug() << QImage("bg.png");
    this->setPalette(palette);
    this->setStyleSheet("background-image:url(bg.png); ");
    configureResetButton();
   setInitialColor(ic);


}

void ColorCircle::drawSmallCircle(QColor color)
{
   qreal theta =qDegreesToRadians((qreal)color.hsvHue()+90);
   auto sat = color.hsvSaturation()/255.0f * radius;
   qreal x = sat* qCos(theta) + centerPoint.x();
   qreal y = sat* qSin(theta) + centerPoint.y();
    QPoint vector(x,y);
    pos = QPoint(x,y);
    this->v = color.value();
    repaint();
}

void ColorCircle::setValueInColor(QColor color)
{
    this->v = color.value();
    currentColor = color;
    drawCircleColorBackground();
    repaint();
}

void ColorCircle::setInitialColor(QColor ic)
{
    initialColor = ic;
}

void ColorCircle::paintEvent(QPaintEvent *event)
{
    QStyleOption opt;
    opt.init(this);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::HighQualityAntialiasing, true);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &painter, this);
    painter.drawImage(0,0,*image);
    QColor color1(240,240,240);
    QPen pen(color1,2);
    painter.setPen(pen);
    painter.drawEllipse(4,4,width()-8, height()-8);

    if(v < 195)
        color1 = color1.fromRgb(250,250,250);
    else
        color1 = color1.fromRgb(50,50,50);
   // color1.setRgb(0,0,0,250);
    pen.setColor(color1);
    painter.setPen(pen);
    painter.drawEllipse(QPoint(pos.x(), pos.y()), 2,2);

    pen.setColor(initialColor);
    pen.setWidth(8);
    painter.setPen(pen);
    painter.drawEllipse(resetButton->geometry());

    pen.setColor(currentColor);
    pen.setWidth(8);
    painter.setPen(pen);
    painter.drawEllipse(7,height()-12,8,8);


    QWidget::paintEvent(event);

}

void ColorCircle::mouseMoveEvent(QMouseEvent *event)
{
    QPoint mousePoint = event->pos();
        QVector2D mousePos(mousePoint.x(), mousePoint.y());
        auto centerPos = QVector2D(centerPoint.x(), centerPoint.y());
        auto diff = mousePos - centerPos;
        if (diff.length() > radius) {
            diff = diff.normalized() * radius;
        }
        auto position = centerPos + diff;
        pos.setX(position.x());
        pos.setY(position.y());
      //  repaint();
        currentColor = getCurrentColorFromPosition();
        emit positionChanged( currentColor );
}

void ColorCircle::mousePressEvent(QMouseEvent *event)
{
    QPoint mousePoint = event->pos();
        QVector2D mousePos(mousePoint.x(), mousePoint.y());
        auto centerPos = QVector2D(centerPoint.x(), centerPoint.y());
        auto diff = mousePos - centerPos;
        if (diff.length() > radius) {
            diff = diff.normalized() * radius;
        }
        auto position = centerPos + diff;
        pos.setX(position.x());
        pos.setY(position.y());
       // repaint();
        currentColor = getCurrentColorFromPosition();
        emit positionChanged( currentColor );

}




void ColorCircle::drawCircleColorBackground()
{

    image = new QImage(width(),height(), QImage::Format_ARGB32_Premultiplied);
    color.setRgb(50,0,0);
    color.setAlpha(0);
    for(int i=0; i < width(); i++){
        for(int j=0; j < width(); j++){

            QPoint point(i,j);
               int d = qPow(point.rx()-centerPoint.rx(), 2) + qPow(point.ry() - centerPoint.ry(), 2);
               if(d <= qPow(radius,2)) {

                   s = (qSqrt(d)/radius)*255.0f;
                   qreal theta = qAtan2(point.ry()-centerPoint.ry(), point.rx()-centerPoint.rx());

                   theta = (180 +90 + (int)qRadiansToDegrees(theta))%360;
                   color.setHsv(theta,s,v,alpha);
                   image->setPixelColor(i,j,color);
               }else{
                   color.setRgb(50,50,50);
                   image->setPixelColor(i,j,color);
               }
        }
    }
}

void ColorCircle::configureResetButton()
{
    resetButton = new QPushButton(this);
    resetButton->setGeometry(6,6,7,7);
    resetButton->setStyleSheet("background: rgba(0,0,0,0); border: 1px solid transparent; ");
    connect(resetButton, &QPushButton::clicked, [=](){
        drawSmallCircle(initialColor);
        emit positionChanged(initialColor);
    });
}


QColor ColorCircle::getCurrentColorFromPosition()
{
    int d = qPow(pos.rx()-centerPoint.rx(), 2) + qPow(pos.ry() - centerPoint.ry(), 2);


        s = (qSqrt(d)/radius)*255.0f;
        qreal theta = qAtan2(pos.ry()-centerPoint.ry(), pos.rx()-centerPoint.rx());
        theta = (180 +90 + (int)qRadiansToDegrees(theta))%360;
        if(s >= 253)
            s=255;
        if(s <= 2)
            s=0;
        if(v >= 253)
            v=255;
        if(v<=2)
            v=0;

            color.setHsv(theta,s,v,255);
        return color;


}

void ColorCircle::setOpacity(int a)
{
    this->alpha = a;
}
