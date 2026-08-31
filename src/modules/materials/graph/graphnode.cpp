/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#include "graphnode.h"
#include "nodestyle.h"
#include <QApplication>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QGraphicsEffect>
#include "socket.h"
#include "socketconnection.h"
#include <QPixmap>
#include <QGraphicsPixmapItem>
#include <QTimer>
#include <qmath.h>

#include "nodegraph.h"
#include "graphnodescene.h"
#include "../core/texturemanager.h"
#include "../core/materialhelper.h"
#include "../models/socketmodel.h"

long GraphNode::pressedZValue = 0;
GraphNode::GraphNode(QGraphicsItem* parent) :
	QGraphicsPathItem(parent)
{
	nodeType = 0;
	proxyWidget = nullptr;
	proxyHeaderWidget = nullptr;


	this->setFlag(QGraphicsItem::ItemIsMovable);
	this->setFlag(QGraphicsItem::ItemIsSelectable);
	this->setFlag(QGraphicsItem::ItemSendsGeometryChanges);
	this->setFlag(QGraphicsItem::ItemSendsScenePositionChanges);
	this->setCacheMode(QGraphicsItem::DeviceCoordinateCache);

	nodeWidth = 170;

	setPen(QPen(NodeStyle::Node::border, NodeStyle::Node::borderWidth));
	setBrush(NodeStyle::Node::fill);

	text = new QGraphicsTextItem(this);
	text->setPlainText("Title");

	text->setPos(5, 16);
	text->setDefaultTextColor(NodeStyle::Node::titleText);

    QFont font = text->font();
    font.setWeight(QFont::Medium);
    text->setFont(font);

	// preview widget
	proxyPreviewWidget = nullptr;
	model = nullptr;

	pressedZValue++;
	setZValue(pressedZValue);
}

GraphNode::~GraphNode()
{
}

void GraphNode::setModel(NodeModel * model)
{
	this->model = model;
}

void GraphNode::setIcon(QIcon icon)
{
	this->icon = icon;
}

void GraphNode::setTitleColor(QColor color)
{
	titleColor = color;

}

void GraphNode::setTitle(QString title)
{
	text->setPlainText(title);
	auto textHeight = text->boundingRect().height();
	// left-aligned next to the 18px header icon (NodeGraphQt layout);
	// an inline header editor, when present, sits between icon and text
	int x = 10 + NodeStyle::Node::iconSize;
	if (headerWidgetWidth > 0)
		x += headerWidgetWidth + 6;
	text->setPos(x, titleHeight / 2 - textHeight / 2);
}

void GraphNode::setHeaderWidget(QWidget *widget)
{
	// same re-embed guard as setWidget
	if (auto oldProxy = widget->graphicsProxyWidget())
		oldProxy->setWidget(nullptr);

	proxyHeaderWidget = new QGraphicsProxyWidget(this);
	proxyHeaderWidget->setWidget(widget);

	const int stripH = NodeStyle::Node::titleStripHeight;
	const auto size = proxyHeaderWidget->size();
	headerWidgetWidth = (int)size.width();
	proxyHeaderWidget->setPos(10 + NodeStyle::Node::iconSize,
		stripH + (titleHeight - stripH - size.height()) / 2);

	// re-run the title layout so the text clears the editor
	setTitle(text->toPlainText());
}

void GraphNode::addInSocket(SocketModel *socket)
{
	auto sock = new Socket(this, SocketType::In, socket->name);
	auto y = calcHeight();
	sock->setPos(-sock->getRadius(), y);
	sock->node = this;
	sock->socketIndex = inSocketCount++;
	sock->setSocketColor(socket->socketColor);
	addSocket(sock);
}

void GraphNode::addOutSocket(SocketModel *socket)
{
	auto sock = new Socket(this, SocketType::Out, socket->name);
	auto y = calcHeight();
	sock->setPos(nodeWidth + sock->getRadius(), y);
	sock->node = this;
	sock->socketIndex = outSocketCount++;
	sock->setSocketColor(socket->socketColor);
	addSocket(sock);
}

void GraphNode::addSocket(Socket* sock)
{
	sockets.append(sock);
	calcPath();
}

void GraphNode::setWidget(QWidget *widget)
{
	// gotta do this here before adding the widget
	auto y = calcHeight();

	// a deleted-then-undone node gets a fresh GraphNode for the same
	// model; unembed from the orphaned proxy or setWidget refuses
	if (auto oldProxy = widget->graphicsProxyWidget())
		oldProxy->setWidget(nullptr);

	proxyWidget = new QGraphicsProxyWidget(this);
	proxyWidget->setWidget(widget);
	proxyWidget->setPreferredWidth(5);
	proxyWidget->setPos((nodeWidth - proxyWidget->size().width()) / 2,	y);

	calcPath();

	layout();
}

//recalculates path
void GraphNode::calcPath()
{
	QPainterPath path_content;
	path_content.setFillRule(Qt::WindingFill);
	path_content.addRoundedRect(QRect(0, 0, nodeWidth, calcHeight()), titleRadius, titleRadius);
	setPath(path_content);
}

int GraphNode::calcHeight()
{
	int height = 0;
	height += titleHeight + 20;// title + padding

	for (auto socket : sockets)
	{
		height += socket->calcHeight();
		height += increment; // padding
	}

	if (proxyWidget != nullptr && !doNotCheckProxyWidgetHeight)
		height += proxyWidget->size().height();

	return height;
}

void GraphNode::resetPositionForColorWidget()
{
	if (proxyWidget) {
		proxyWidget->setPos(12, titleHeight+10);
		doNotCheckProxyWidgetHeight = true;
	}
}

Socket *GraphNode::getInSocket(int index)
{
	int i = 0;
	for (auto sock : sockets) {
		if (sock->socketType == SocketType::In) {
			if (index == i)
				return sock;
			i++;
		}
	}
	return nullptr;
}

Socket *GraphNode::getOutSocket(int index)
{
	int i = 0;
	for (auto sock : sockets) {
		if (sock->socketType == SocketType::Out) {
			if (index == i)
				return sock;
			i++;
		}
	}

	return nullptr;
}

void GraphNode::layout()
{
	int height = 0;
	height += titleHeight + 20;// title + padding

	for (auto socket : sockets)
	{
		height += socket->calcHeight();
		height += increment; // padding
	}

	if (proxyWidget != nullptr && !doNotCheckProxyWidgetHeight) {
		proxyWidget->setPos((nodeWidth - proxyWidget->size().width()) / 2,
			height);
		height += proxyWidget->size().height();
	}

	if (proxyPreviewWidget != nullptr  ) {
		height += 5;
		proxyPreviewWidget->setPos((nodeWidth - proxyPreviewWidget->size().width()) / 2,
			height);
		height += proxyPreviewWidget->size().height();
	}

	height += 5;
		
	// calculate path
	QPainterPath path_content;
	path_content.setFillRule(Qt::WindingFill);
	path_content.addRoundedRect(QRect(0, 0, nodeWidth, height), titleRadius, titleRadius);
	setPath(path_content);
}

void GraphNode::enablePreviewWidget()
{
	// The per-node GL preview died with the legacy viewport (step 14); the
	// graph item itself is raster and the Display dock shows the engine preview.
}

void GraphNode::setNodeGraph(NodeGraph* graph)
{
	this->nodeGraph = graph;
}

void GraphNode::paint(QPainter *painter,
	const QStyleOptionGraphicsItem *option,
	QWidget *widget)
{
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::TextAntialiasing);

    using namespace NodeStyle;

    const bool selected = option->state.testFlag(QStyle::State_Selected);

    // card fill (near-black, rounded)
    painter->fillPath(path(), QBrush(Node::fill));

    // everything inside the card is clipped to its rounded outline
    painter->save();
    painter->setClipPath(path());

    // title area: dark overlay + slim category colour strip
    const int stripH = isMasterNode ? Node::masterStripHeight : Node::titleStripHeight;
    painter->fillRect(QRectF(0, 0, nodeWidth, titleHeight),
        isMasterNode ? Node::masterTitleOverlay : Node::titleOverlay);
    if (titleColor.alpha() > 0)
        painter->fillRect(QRectF(0, 0, nodeWidth, stripH), titleColor);

    // header icon (loaded since forever, finally painted)
    if (!icon.isNull()) {
        const QRect iconRect(6, stripH + (titleHeight - stripH - Node::iconSize) / 2,
            Node::iconSize, Node::iconSize);
        icon.paint(painter, iconRect);
    }

    // selection wash over the fill
    if (selected)
        painter->fillPath(path(), QBrush(Node::selectedWash));

    painter->restore();

    // border: thin muted category tint at rest, yellow when selected
    painter->setBrush(Qt::NoBrush);
    if (selected)
        painter->setPen(QPen(Node::selectedBorder, Node::selectedBorderWidth));
    else
        painter->setPen(QPen(Node::mutedBorder(titleColor), Node::borderWidth));
    painter->drawPath(path());

    // sync the upstream-chain highlight with the selection state
    if (selected != currentSelectedState) {
        currentSelectedState = selected;
        highlightNode(currentSelectedState, 0);
    }

    if (isHighlighted && level == 0) {
        // selected root of the chain: blue outline
        painter->setPen(QPen(Node::chainRootBorder, Node::chainBorderWidth));
        painter->drawPath(path());
    }
    else if (isHighlighted && level > 0) {
        // upstream nodes feeding it: yellow outline
        painter->setPen(QPen(Node::chainLinkBorder, Node::chainBorderWidth));
        painter->drawPath(path());
    }
}

int GraphNode::type() const
{
	return (int)GraphicsItemType::Node;
}

QVariant GraphNode::itemChange(QGraphicsItem::GraphicsItemChange change, const QVariant & value)
{
	if (change == QGraphicsItem::ItemPositionChange && scene()) {
		// update positon for node
		if (model) {
			auto pos = value.value<QPointF>();
			model->setX(pos.x());
			model->setY(pos.y());
		}
	}

	return QGraphicsItem::itemChange(change, value);
}

void GraphNode::highlightNode(bool val, int lvl)
{
	isHighlighted = val;
	level = lvl;
	for (Socket* sock : sockets) {
		if (sock->socketType == SocketType::In) {
			for (SocketConnection* con : sock->connections) {
				if (con->socket1->socketType == SocketType::Out) {
					con->socket1->owner->isHighlighted = val;
					con->socket1->owner->highlightNode(val, level + 1);
					con->socket1->owner->currentSelectedState = false;
				}
				if (con->socket2->socketType == SocketType::Out) {
					con->socket2->owner->isHighlighted = val;
					con->socket2->owner->highlightNode(val, level + 1);
					con->socket2->owner->currentSelectedState = false;

				}
			}
		}
	}
	if (!val) check = val;
	update();
}

void GraphNode::mousePressEvent(QGraphicsSceneMouseEvent * event)
{
	pressedZValue++;
	setZValue(pressedZValue);
	QGraphicsPathItem::mousePressEvent(event);
}



