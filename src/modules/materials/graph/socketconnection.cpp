/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// The curved pipe layout (Y-flat control points pushed toward each other
// in X by a tangent clamped to the node width) is transliterated from
// NodeGraphQt's qgraphics/pipe.py `_draw_path_horizontal`:
//
//   MIT License, Copyright (c) 2017 Johnny Chan
//   https://github.com/jchanvfx/NodeGraphQt
//
//   Permission is hereby granted, free of charge, to any person obtaining
//   a copy of this software and associated documentation files (the
//   "Software"), to deal in the Software without restriction, including
//   without limitation the rights to use, copy, modify, merge, publish,
//   distribute, sublicense, and/or sell copies of the Software, subject
//   to the above copyright notice and this permission notice being
//   included in all copies or substantial portions of the Software.

#include "socketconnection.h"
#include "socket.h"
#include "nodestyle.h"
#include <QPainter>
#include <QPainterPathStroker>
#include <QStyleOptionGraphicsItem>
#include <QDebug>
#include <QGraphicsSceneEvent>


SocketConnection::SocketConnection()
{
	socket1 = nullptr;
	socket2 = nullptr;

	pos1 = QPointF(0, 0);
	pos2 = QPointF(0, 0);

	setFlag(QGraphicsItem::ItemIsSelectable);
	setAcceptHoverEvents(true);
	setZValue(-1); // pipes run under the node cards

	setPen(QPen(NodeStyle::Pipe::color, NodeStyle::Pipe::width));

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
	QPainterPath path;
	path.moveTo(pos1);

	const qreal dx = pos2.x() - pos1.x();
	const qreal tangent = qMin(qAbs(dx), NodeStyle::Pipe::maxTangent);

	// control points keep each endpoint's Y and push toward the other
	// end in X; an out port pushes +x, an in port pushes -x. The loose
	// end of a live drag takes the opposite sign of the grabbed socket.
	const qreal dir1 = (socket1 != nullptr && socket1->socketType == SocketType::In) ? -1.0 : 1.0;
	const qreal dir2 = (socket2 != nullptr)
		? ((socket2->socketType == SocketType::In) ? -1.0 : 1.0)
		: -dir1;

	const QPointF ctr1(pos1.x() + tangent * dir1, pos1.y());
	const QPointF ctr2(pos2.x() + tangent * dir2, pos2.y());

	path.cubicTo(ctr1, ctr2, pos2);
	setPath(path);
}

int SocketConnection::type() const
{
	return (int)GraphicsItemType::Connection;
}

QPainterPath SocketConnection::shape() const
{
	// fat pick shape so thin pipes stay clickable
	QPainterPathStroker stroker;
	stroker.setWidth(NodeStyle::Pipe::pickWidth);
	return stroker.createStroke(path());
}

void SocketConnection::hoverEnterEvent(QGraphicsSceneHoverEvent *event)
{
	hovered = true;
	update();
	QGraphicsPathItem::hoverEnterEvent(event);
}

void SocketConnection::hoverLeaveEvent(QGraphicsSceneHoverEvent *event)
{
	hovered = false;
	update();
	QGraphicsPathItem::hoverLeaveEvent(event);
}

void SocketConnection::paint(QPainter * painter, const QStyleOptionGraphicsItem * option, QWidget * widget)
{
	painter->setRenderHint(QPainter::Antialiasing);
	painter->setBrush(Qt::NoBrush);

	if (status == SocketConnectionStatus::Started ||
		status == SocketConnectionStatus::Inprogress ||
		status == SocketConnectionStatus::Editing) {
		// live connection preview: thin dashed line, tinted by whether
		// the loose end is over a compatible socket
		auto color = (liveTarget == SocketDragHighlight::Invalid)
			? NodeStyle::Pipe::liveInvalid
			: NodeStyle::Pipe::liveDrag;
		QPen pen(color, NodeStyle::Pipe::liveWidth);
		pen.setStyle(Qt::DashLine);
		painter->setPen(pen);
		painter->drawPath(path());
	}
	else if (status == SocketConnectionStatus::Finished) {
		QPen pen;
		if (option->state.testFlag(QStyle::State_Selected)) {
			pen = QPen(NodeStyle::Pipe::selected, NodeStyle::Pipe::selectedWidth);
		}
		else if (hovered) {
			pen = QPen(NodeStyle::Pipe::hover, NodeStyle::Pipe::hoverWidth);
		}
		else {
			// pipe carries the source (out) socket's type colour
			auto out = getOutSocket();
			auto color = out != nullptr ? out->getSocketColor() : NodeStyle::Pipe::color;
			pen = QPen(color, NodeStyle::Pipe::width);
		}
		pen.setCapStyle(Qt::RoundCap);
		painter->setPen(pen);
		painter->drawPath(path());
	}

	Q_UNUSED(widget);
}
