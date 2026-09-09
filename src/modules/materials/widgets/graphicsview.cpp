/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

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
#include <QKeyEvent>
#include <QCursor>
#include "../graph/graphnodescene.h"
#include "../graph/nodestyle.h"
#include "../dialogs/searchdialog.h"

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
	double factor = NodeStyle::Canvas::zoomStep;
	const double current = transform().m11();

	if (current * factor > NodeStyle::Canvas::zoomMax)
		factor = NodeStyle::Canvas::zoomMax / current;
	if (factor <= 1.0001)
		return;

	scale(factor, factor);
}

void GraphicsView::decreaseScale()
{
	double factor = 1.0 / NodeStyle::Canvas::zoomStep;
	const double current = transform().m11();

	// clamp zoom-out (was unbounded)
	if (current * factor < NodeStyle::Canvas::zoomMin)
		factor = NodeStyle::Canvas::zoomMin / current;
	if (factor >= 0.9999)
		return;

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

// Two-level line grid after NodeGraphQt's viewer background (MIT,
// Copyright (c) 2017 Johnny Chan): a fine grid that fades out as you
// zoom away, over a coarse grid at 8x the spacing.
void GraphicsView::drawBackground(QPainter * painter, const QRectF & rect)
{
	QGraphicsView::drawBackground(painter, rect);

	painter->fillRect(rect, NodeStyle::Canvas::background);

	const qreal zoom = transform().m11();

	auto drawGrid = [&](double gridStep, QColor color)
	{
		QPen pen(color, 1.0);
		pen.setCosmetic(true); // hairline regardless of zoom
		painter->setPen(pen);

		const double left = std::floor(rect.left() / gridStep) * gridStep;
		const double top = std::floor(rect.top() / gridStep) * gridStep;

		for (double x = left; x <= rect.right(); x += gridStep)
			painter->drawLine(QLineF(x, rect.top(), x, rect.bottom()));
		for (double y = top; y <= rect.bottom(); y += gridStep)
			painter->drawLine(QLineF(rect.left(), y, rect.right(), y));
	};

	// fine grid fades with zoom so it never turns into noise
	if (zoom > 0.35) {
		auto fine = NodeStyle::Canvas::grid;
		fine.setAlphaF(qMin(1.0, (zoom - 0.35) / 0.4));
		drawGrid(NodeStyle::Canvas::gridSize, fine);
	}

	drawGrid(NodeStyle::Canvas::gridSize * NodeStyle::Canvas::gridZoomFactor,
		NodeStyle::Canvas::gridCoarse);
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

	// consume the event: the base class would additionally scroll the
	// view, so every zoom used to drift the canvas
	event->accept();
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
	// NO Delete / Ctrl+D / Ctrl+C / Ctrl+V / undo / redo shortcuts here.
	//
	// Every one of those chords now has exactly ONE claimant — a ShortcutRegistry
	// entry on MainWindow — and the shell routes it by ACTIVE SPACE: on the
	// Materials page to EffectsPage::graph{Undo,Redo,DeleteSelected,
	// DuplicateSelected,CopySelected,Paste}, everywhere else to the editor's
	// selection set (EDITOR_MULTISELECT_SPEC §2.6).
	//
	// Why it has to be that way, measured: a bare QShortcut here is
	// Qt::WindowShortcut, so is the registry's, and Qt answers an AMBIGUOUS
	// shortcut event by ignoring it — the chord then does NOTHING on this page.
	// That is exactly what happened to Ctrl+Z (deep audit 2026-09, area 1:
	// "The following shortcuts are about to be activated ambiguously", then
	// QShortcutEvent("Ctrl+Z", ..., TRUE)), and re-adding any of these would
	// reproduce it. If the graph view ever needs its own binding, register it
	// in ShortcutRegistry with a distinct sequence.

	// F frames the selection (all nodes when nothing is selected)
	auto fitShortcut = new QShortcut(this);
	fitShortcut->setKey(Qt::Key_F);
	connect(fitShortcut, &QShortcut::activated, [this]()
	{
		fitSelection();
	});

	// H resets the zoom
	auto resetZoomShortcut = new QShortcut(this);
	resetZoomShortcut->setKey(Qt::Key_H);
	connect(resetZoomShortcut, &QShortcut::activated, [this]()
	{
		resetZoom();
	});
}

void GraphicsView::fitSelection()
{
	if (scene == nullptr)
		return;

	QRectF bounds;
	const auto selected = scene->selectedItems();
	if (!selected.isEmpty()) {
		for (auto item : selected)
			bounds = bounds.united(item->sceneBoundingRect());
	}
	else {
		bounds = scene->itemsBoundingRect();
	}
	if (!bounds.isValid())
		return;

	const auto margin = NodeStyle::Canvas::fitMargin;
	bounds.adjust(-margin, -margin, margin, margin);

	// panning translates sceneRect by hand, so re-anchor it before fitting
	setSceneRect(bounds);
	fitInView(bounds, Qt::KeepAspectRatio);

	// keep the result inside the zoom clamps
	const qreal zoom = transform().m11();
	if (zoom > NodeStyle::Canvas::zoomMax)
		scale(NodeStyle::Canvas::zoomMax / zoom, NodeStyle::Canvas::zoomMax / zoom);
	else if (zoom < NodeStyle::Canvas::zoomMin)
		scale(NodeStyle::Canvas::zoomMin / zoom, NodeStyle::Canvas::zoomMin / zoom);
}

void GraphicsView::resetZoom()
{
	const qreal zoom = transform().m11();
	if (zoom > 0.0)
		scale(1.0 / zoom, 1.0 / zoom);
}

void GraphicsView::setScene(GraphNodeScene * scene)
{
	this->scene = scene;
	QGraphicsView::setScene((QGraphicsScene*)scene);
}

// Tab opens the node-search palette at the cursor (NodeGraphQt's tab
// search); intercepted in event() because Tab otherwise walks focus.
bool GraphicsView::event(QEvent *event)
{
	if (event->type() == QEvent::KeyPress) {
		auto ke = static_cast<QKeyEvent*>(event);
		if (ke->key() == Qt::Key_Tab && underMouse() && scene != nullptr) {
			openNodeSearch();
			ke->accept();
			return true;
		}
	}
	return QGraphicsView::event(event);
}

bool GraphicsView::openNodeSearch()
{
	// scene was dereferenced unguarded here; the Tab path happened to test it
	// first, and the Space shortcut can arrive before a graph is created.
	if (scene == nullptr || scene->getNodeGraph() == nullptr)
		return false;
	// The dialog opens at the mouse (Tab's behaviour). Driven from the KEYBOARD
	// the pointer may be anywhere on screen — off this page entirely — so fall
	// back to the middle of the view.
	QPoint at = QCursor::pos();
	if (!underMouse())
		at = viewport()->mapToGlobal(viewport()->rect().center());
	auto dialog = new SearchDialog(scene->getNodeGraph(), scene, at);
	dialog->exec();
	dialog->deleteLater();
	return true;
}
