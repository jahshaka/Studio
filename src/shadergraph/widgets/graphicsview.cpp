/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/
#include "graphicsview.h"
#include <QApplication>
#include <QDebug>
#include <QRect>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QPainter>
#include <QtMath>
#include <QShortcut>
#include "../graph/graphnodescene.h"

qreal GraphicsView::currentScale = 1.0;
GraphicsView::GraphicsView( QWidget *parent) : QGraphicsView(parent)
{
	setAcceptDrops(true);
	setRenderHint(QPainter::Antialiasing);
	setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	//setCacheMode(QGraphicsView::CacheBackground);
	setTransformationAnchor(QGraphicsView::AnchorUnderMouse);

	setCacheMode(QGraphicsView::CacheBackground);
	//setViewportUpdateMode(QGraphicsView::MinimalViewportUpdate);
	setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
	setDragMode(QGraphicsView::ScrollHandDrag);

	QGraphicsView::setAcceptDrops(true);

	addShortcuts();
}

void GraphicsView::increaseScale()
{
	double const step = 1.2;
    double const factor = pow(step, 1.0);

	QTransform t = transform();

	if (t.m11() > 2.0)
		return;

	scale(factor, factor);
}

void GraphicsView::decreaseScale()
{
	double const step = 1.2;
	double const factor = std::pow(step, -1.0);

	scale(factor, factor);
}

void GraphicsView::dragEnterEvent(QDragEnterEvent * event)
{
	event->setAccepted(true);
}

void GraphicsView::dropEvent(QDropEvent * event)
{
	event->acceptProposedAction();
	QGraphicsView::dropEvent(event);
}

void GraphicsView::dragMoveEvent(QDragMoveEvent * event)
{
	event->setAccepted(true);
}

void GraphicsView::drawBackground(QPainter * painter, const QRectF & rect)
{
	painter->setRenderHint(QPainter::Antialiasing);
	QGraphicsView::drawBackground(painter, rect);

	QRect   windowRect = this->rect();
	QPointF tl = mapToScene(windowRect.topLeft());
	QPointF br = mapToScene(windowRect.bottomRight());
	QRectF sceneRect(tl, br);
	painter->fillRect(sceneRect, QBrush(QColor(20, 20, 20)));

	auto drawGrid =
		[&](double gridStep)
	{

		double left = std::floor(tl.x() / gridStep - 0.5);
		double right = std::floor(br.x() / gridStep + 1.0);
		double bottom = std::floor(tl.y() / gridStep - 0.5);
		double top = std::floor(br.y() / gridStep + 1.0);

		QPen pen(QColor(35, 35, 35), 2);
		painter->setPen(pen);

		// vertical lines
		for (int xi = int(left); xi <= int(right); ++xi)
		{
			QLineF line(xi * gridStep, bottom * gridStep,
				xi * gridStep, top * gridStep);

			painter->drawLine(line);
		}

		// horizontal lines
		for (int yi = int(bottom); yi <= int(top); ++yi)
		{
			QLineF line(left * gridStep, yi * gridStep,
				right * gridStep, yi * gridStep);
			painter->drawLine(line);
		}
	};

	drawGrid(35);

}

void GraphicsView::wheelEvent(QWheelEvent * event)
{
	QPoint delta = event->angleDelta();

	if (delta.y() == 0)
	{
		event->ignore();
		return;
	}

    double const d = delta.y() / abs(delta.y());

	if (d > 0.0)
		increaseScale();
	else
		decreaseScale();

	QGraphicsView::wheelEvent(event);
}

void GraphicsView::mousePressEvent(QMouseEvent * event)
{
	QGraphicsView::mousePressEvent(event);

	if (event->button() == Qt::MiddleButton) {
		clickPos = mapToScene(event->pos());
		dragging = true;
	}
}

void GraphicsView::mouseReleaseEvent(QMouseEvent * event)
{
	QGraphicsView::mouseReleaseEvent(event);
	if (dragging) {
		dragging = false;
		QApplication::setOverrideCursor(Qt::ArrowCursor);
	}
}

void GraphicsView::mouseMoveEvent(QMouseEvent * event)
{
	QGraphicsView::mouseMoveEvent(event);

	if (dragging) {
		QApplication::setOverrideCursor(Qt::ClosedHandCursor);

		auto diff = clickPos - mapToScene(event->pos());
		setSceneRect(sceneRect().translated(diff.x(), diff.y()));
	
	}

}

void GraphicsView::addShortcuts()
{
	auto deleteShortcut = new QShortcut(this);
	deleteShortcut->setKey(Qt::Key_Delete);
	connect(deleteShortcut, &QShortcut::activated, [this]()
	{
		this->scene->deleteSelectedNodes();
		this->repaint();
	});

	// undo-redo
	auto undoShortcut = new QShortcut(this);
	undoShortcut->setKey(QKeySequence::Undo);
	connect(undoShortcut, &QShortcut::activated, [this]()
	{
		scene->undo();
		scene->update();
		//this->repaint();
	});

	auto redoShortcut = new QShortcut(this);
	redoShortcut->setKey(QKeySequence::Redo);
	connect(redoShortcut, &QShortcut::activated, [this]()
	{
		scene->redo();
		scene->update();
		//this->repaint();
	});
}

void GraphicsView::setScene(GraphNodeScene * scene)
{
	this->scene = scene;
	QGraphicsView::setScene((QGraphicsScene*)scene);
}
