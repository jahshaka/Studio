/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#include "socket.h"
#include "nodestyle.h"
#include "socketconnection.h"
#include "graphnodescene.h"
#include <QGuiApplication>
#include <QScreen>
#include <QGraphicsItem>
#include <QFontMetrics>
#include <QApplication>
#include <QDebug>

Socket::Socket(QGraphicsItem* parent, SocketType socketType, QString title) :
	QGraphicsPathItem(parent), 
	socketType(socketType)
{
	this->setFlag(QGraphicsItem::ItemSendsScenePositionChanges);
	this->setAcceptHoverEvents(true);
	text = new QGraphicsTextItem(this);
	text->setPlainText(title);
	text->setDefaultTextColor(QColor(200, 200, 200));

	QFont font = text->font();
    font.setWeight(QFont::Medium);
    auto ratio = QGuiApplication::primaryScreen()->devicePixelRatio();
	font.setPixelSize(14);
	text->setFont(font);

	QFontMetrics fm(parent->scene()->font());
	QRect textRect = fm.boundingRect(title);
	owner = (GraphNode*)parent;
	radius = qCeil(textRect.height() / 2.0f) * ratio;
	dimentions = textRect.height();
	QPainterPath path;

	outSocketXOffset = -textRect.width() -20 + getSocketPosition().x();
	inSocketXOffset = 10 + getSocketPosition().x();

	// socket positions are at the outer right or outer left of the graph node
	if (socketType == SocketType::Out)
	{
		// socket on the right    out socket
		int val = ratio > 1 ? 3 : 4;
		path.addRoundedRect(-radius * val , -radius / 2, dimentions, dimentions, radius, radius);
		socketPos = QPoint(-radius * val, -radius / 2);
	}
	else
	{
		//socket on the left      in socket
		path.addRoundedRect(radius*2, -radius / 2, dimentions, dimentions, radius, radius);
		socketPos = QPoint(radius * 2, -radius / 2);
	}

	if (socketType == SocketType::Out) {

		outSocketXOffset = -textRect.width() - 30 + getSocketPosition().x();
		text->setPos(outSocketXOffset, -radius);

	}
	else {
		inSocketXOffset = 10 + getSocketPosition().x();
		text->setPos(inSocketXOffset, -radius);
	}

	setPath(path);

	
}

void Socket::addConnection(SocketConnection* con)
{
	setConnected(true);
	connections.append(con);
	text->setDefaultTextColor(QColor(255, 255, 255));
	updateSocket();
}

void Socket::removeConnection(SocketConnection * con)
{
	connections.removeOne(con);
	if (connections.length() <= 0) {
		setSocketColor(getSocketColor());
		setConnected(false);
	}
	text->setDefaultTextColor(QColor(200, 200, 200));
	updateSocket();
}

float Socket::calcHeight()
{
	return radius * 2;
}

float Socket::getRadius()
{
	return radius;
}

QPointF Socket::getPos()
{
	return socketPos;
}

float Socket::getSocketOffset()
{
	return socketPos.x();
}

int Socket::type() const
{
	return (int)GraphicsItemType::Socket;
}

QPoint Socket::getSocketPosition()
{
	if (socketType == SocketType::Out)
	{
		QRect rect(socketPos.x(), socketPos.y(), dimentions, dimentions);
		auto center = rect.center();
		return this->scenePos().toPoint() + center;
	}
	else {
		QRect rect(socketPos.x(), socketPos.y(), dimentions, dimentions);
		auto center = rect.center();
		return this->scenePos().toPoint() + center;

	}
}

QVariant Socket::itemChange(GraphicsItemChange change, const QVariant &value)
{
	if (change == ItemScenePositionHasChanged)
	{
		for (auto con : connections)
		{
			con->updatePosFromSockets();
			con->updatePath();
		}
	}
	return value;
}

QColor Socket::getSocketColor()
{
	return socketColor;
}

void Socket::setSocketColor(QColor color)
{
    socketColor = color;
}

void Socket::setConnected(bool value)
{
	connected = value;
}


void Socket::updateSocket()
{
	this->update();
}

void Socket::setDragHighlight(SocketDragHighlight state)
{
	if (dragHighlight != state) {
		dragHighlight = state;
		update();
	}
}

void Socket::hoverEnterEvent(QGraphicsSceneHoverEvent *event)
{
	hovered = true;
	update();
	QGraphicsPathItem::hoverEnterEvent(event);
}

void Socket::hoverLeaveEvent(QGraphicsSceneHoverEvent *event)
{
	hovered = false;
	update();
	QGraphicsPathItem::hoverLeaveEvent(event);
}

// Three-state port painting after NodeGraphQt's qgraphics/port.py
// (https://github.com/jchanvfx/NodeGraphQt, MIT, Copyright (c) 2017
// Johnny Chan): idle ring, hover flare, connected inner dot. The ring
// carries the port's TYPE colour so socket types stay readable.
void Socket::paint(QPainter * painter, const QStyleOptionGraphicsItem * option, QWidget * widget)
{
	Q_UNUSED(option);
	Q_UNUSED(widget);

	painter->setRenderHint(QPainter::Antialiasing);

	const QRectF circle(socketPos.x(), socketPos.y(), dimentions, dimentions);

	QColor fill;
	QColor ring;
	if (dragHighlight == SocketDragHighlight::Valid) {
		fill = NodeStyle::Port::hoverFill;
		ring = NodeStyle::Port::validTargetBorder;
	}
	else if (dragHighlight == SocketDragHighlight::Invalid) {
		fill = NodeStyle::Port::hoverFill;
		ring = NodeStyle::Port::invalidTargetBorder;
	}
	else if (hovered) {
		fill = NodeStyle::Port::hoverFill;
		ring = NodeStyle::Port::hoverBorder;
	}
	else if (connected) {
		fill = NodeStyle::Port::connectedFill;
		ring = socketColor;
	}
	else {
		fill = socketColor.darker(400);
		ring = socketColor;
	}

	painter->setPen(QPen(ring, NodeStyle::Port::borderWidth));
	painter->setBrush(fill);
	painter->drawEllipse(circle);

	// connected marker: inner dot in the type colour
	if (connected) {
		const qreal pad = dimentions * 0.32;
		painter->setPen(Qt::NoPen);
		painter->setBrush(hovered ? NodeStyle::Port::hoverBorder : socketColor);
		painter->drawEllipse(circle.adjusted(pad, pad, -pad, -pad));
	}
}
