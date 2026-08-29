/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/
#include "socketconnection.h"
#include "socket.h"
#include <QPainter>
#include <QDebug>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsSceneEvent>


SocketConnection::SocketConnection()
{
	socket1 = nullptr;
	socket2 = nullptr;

	pos1 = QPointF(0, 0);
	pos2 = QPointF(0, 0);

	auto pen = QPen(QColor(200, 200, 200));
	pen.setBrush(QColor(50, 150, 250));
	pen.setCapStyle(Qt::RoundCap);
	pen.setWidth(lineThickness);
	setPen(pen);

	status = SocketConnectionStatus::Finished;
}

Socket* SocketConnection::getInSocket()
{
	if (socket1 != nullptr && socket1->socketType == SocketType::In)
		return socket1;

	if (socket2 != nullptr && socket2->socketType == SocketType::In)
		return socket2;

	return nullptr;
}

Socket* SocketConnection::getOutSocket()
{
	if (socket1 != nullptr && socket1->socketType == SocketType::Out)
		return socket1;

	if (socket2 != nullptr && socket2->socketType == SocketType::Out)
		return socket2;

	return nullptr;
}

void SocketConnection::updatePosFromSockets()
{
	pos1 = socket1->getSocketPosition();
	pos2 = socket2->getSocketPosition();
}

void SocketConnection::updatePath()
{

	p = new QPainterPath;
	p->moveTo(pos1);

	qreal dx = pos2.x() - pos1.x();
	qreal dy = pos2.y() - pos1.y();

	QPointF ctr1(pos1.x() + dx * 0.25, pos1.y() + dy * 0.1);
	QPointF ctr2(pos1.x() + dx * 0.75, pos1.y() + dy * 0.9);

	p->cubicTo(ctr1, ctr2, pos2);
	p->setFillRule(Qt::OddEvenFill);

	
	setPath(*p);
}

int SocketConnection::type() const
{
	return (int)GraphicsItemType::Connection;
}

void SocketConnection::paint(QPainter * painter, const QStyleOptionGraphicsItem * option, QWidget * widget)
{
	painter->setRenderHint(QPainter::Antialiasing);
	if (status == SocketConnectionStatus::Started || status == SocketConnectionStatus::Inprogress) {
		QPen pen(QColor(90, 90, 90), lineThickness);
		pen.setStyle(Qt::DashLine);
		pen.setDashOffset(6);
		painter->setPen(pen);
		painter->drawPath(*p);
	}
	if(status == SocketConnectionStatus::Finished) {

        // create gradient for line
		QLinearGradient grad;
		grad.setStart(pos1-pos2);
		grad.setFinalStop( pos2 - pos1);
		grad.setColorAt(0.0, socket1->connectedColor);
		grad.setColorAt(1.0, socket2->connectedColor);
        QPen pen(grad, lineThickness);
		painter->setPen(pen);
		painter->drawPath(*p);
	}
	if (status == SocketConnectionStatus::Editing) {
		QPen pen(QColor(240, 90, 90), lineThickness);
		pen.setStyle(Qt::DashLine);
		pen.setDashOffset(6);
		painter->setPen(pen);
		painter->drawPath(*p);
	}

    Q_UNUSED(option);
    Q_UNUSED(widget);

}

