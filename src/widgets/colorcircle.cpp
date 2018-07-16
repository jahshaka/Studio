#include "colorcircle.h"

#include <QPainter>
#include <QVector2D>
#include <QtMath>
#include <QMouseEvent>
#include <QStyleOption>

ColorCircle::ColorCircle(QWidget *parent, QColor ic) : QWidget(parent)
{
	setGeometry(offset, offset, parent->width() - offset, parent->height() - offset);
	radius = width() / 2 - offset;
	centerPoint.setX(radius + offset);
	centerPoint.setY(radius + offset);
	colorValue = 255;
	drawCircleColorBackground();
	repaint();

	configureResetButton();
	setInitialColor(ic);
	this->setStyleSheet("background-image:url(:/images/bg.png); ");

}

void ColorCircle::drawSmallCircle(QColor color)
{
	qreal theta = qDegreesToRadians((qreal)color.hsvHue() + 90);
	auto sat = color.hsvSaturation() / 255.0f * radius;
	qreal x = sat * qCos(theta) + centerPoint.x();
	qreal y = sat * qSin(theta) + centerPoint.y();
	QPoint vector(x, y);
	pos = QPoint(x, y);
	this->colorValue = color.value();
	repaint();
}

void ColorCircle::setValueInColor(QColor color)
{
	this->colorValue = color.value();
	currentColor = color;
	drawCircleColorBackground();
	repaint();
}

void ColorCircle::setInitialColor(QColor ic)
{
	initialColor = ic;
	setValueInColor(ic);
	drawSmallCircle(ic);
	emit positionChanged(ic);
}

void ColorCircle::paintEvent(QPaintEvent *event)
{
	QStyleOption opt;
	opt.init(this);
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setRenderHint(QPainter::HighQualityAntialiasing, true);
	style()->drawPrimitive(QStyle::PE_Widget, &opt, &painter, this);
	painter.drawImage(0, 0, *image);
	QColor color1(240, 240, 240);
	QPen pen(color1, 2);
	painter.setPen(pen);
	painter.drawEllipse(4, 4, width() - 8, height() - 8);

	if (colorValue < 195)	color1 = color1.fromRgb(250, 250, 250);
	else	color1 = color1.fromRgb(50, 50, 50);
	pen.setColor(color1);
	painter.setPen(pen);
	painter.drawEllipse(QPoint(pos.x(), pos.y()), 2, 2);

	//draw initial color
	pen.setColor(initialColor);
	pen.setWidth(8);
	painter.setPen(pen);
	painter.drawEllipse(resetButton->geometry());

	//draw circle around initial color 
	pen.setColor(QColor(250, 250, 250));
	pen.setWidth(2);
	painter.setPen(pen);
	painter.drawEllipse(resetButton->geometry().x() - 4, resetButton->geometry().y() - 4, resetButton->geometry().width() + 8, resetButton->geometry().height() + 8);

	//draw current color
	pen.setColor(currentColor);
	pen.setWidth(8);
	painter.setPen(pen);
	painter.drawEllipse(7, height() - 15, 8, 8);

	//draw circle around current color
	pen.setColor(QColor(250, 250, 250));
	pen.setWidth(2);
	painter.setPen(pen);
	painter.drawEllipse(3, height() - 19, 16, 16);


	QWidget::paintEvent(event);

}

void ColorCircle::mouseMoveEvent(QMouseEvent *event)
{
	setCirclePosition(event);
}

void ColorCircle::mousePressEvent(QMouseEvent *event)
{
	setCirclePosition(event);
}


void ColorCircle::setCirclePosition(QMouseEvent * event)
{
	QPoint mousePoint = event->pos();
	QVector2D mousePos(mousePoint.x(), mousePoint.y());
	auto centerPos = QVector2D(centerPoint.x(), centerPoint.y());
	auto diff = mousePos - centerPos;
	if (diff.length() > radius) 	diff = diff.normalized() * radius;
	auto position = centerPos + diff;
	pos.setX(position.x());
	pos.setY(position.y());
	currentColor = getCurrentColorFromPosition();
	emit positionChanged(currentColor);
}

void ColorCircle::drawCircleColorBackground()
{

	image = new QImage(width(), height(), QImage::Format_ARGB32_Premultiplied);
	color.setRgb(50, 0, 0);
	color.setAlpha(0);

	//draw color circle image
	for (int i = 0; i < width(); i++) {
		for (int j = 0; j < width(); j++) {

			QPoint point(i, j);
			int d = qPow(point.rx() - centerPoint.rx(), 2) + qPow(point.ry() - centerPoint.ry(), 2);
			if (d <= qPow(radius, 2)) {

				saturation = (qSqrt(d) / radius)*255.0f;
				qreal theta = qAtan2(point.ry() - centerPoint.ry(), point.rx() - centerPoint.rx());

				theta = (180 + 90 + static_cast<int>(qRadiansToDegrees(theta))) % 360;
				color.setHsv(theta, saturation, colorValue, alpha);
				image->setPixelColor(i, j, color);
			}
			else {
				color.setRgb(26, 26, 26);
				image->setPixelColor(i, j, color);
			}
		}
	}
}

void ColorCircle::configureResetButton()
{
	resetButton = new QPushButton(this);
	resetButton->setGeometry(5, 5, 8, 8);
	resetButton->setStyleSheet("background: rgba(0,0,0,0); border: 1px solid transparent; ");
	resetButton->setToolTip("Reset color picker");
	connect(resetButton, &QPushButton::clicked, [=]() {
		drawSmallCircle(initialColor);
		emit positionChanged(initialColor);
	});
}


QColor ColorCircle::getCurrentColorFromPosition()
{
	int d = qPow(pos.rx() - centerPoint.rx(), 2) + qPow(pos.ry() - centerPoint.ry(), 2);
	saturation = (qSqrt(d) / radius)*255.0f;
	qreal theta = qAtan2(pos.ry() - centerPoint.ry(), pos.rx() - centerPoint.rx());
	theta = (180 + 90 + (int)qRadiansToDegrees(theta)) % 360;
	if (saturation >= 253)	saturation = 255;
	if (saturation <= 2)	saturation = 0;
	if (colorValue >= 253)	colorValue = 255;
	if (colorValue <= 2)	colorValue = 0;

	color.setHsv(theta, saturation, colorValue, 255);
	return color;
}

void ColorCircle::setOpacity(int a)
{
	this->alpha = a;
}
