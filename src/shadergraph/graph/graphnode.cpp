/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

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

#include <irisgl/IrisGL.h>
#include <irisgl/Graphics.h>
#include "nodegraph.h"
#include "graphnodescene.h"
#include "../core/texturemanager.h"
#include "../core/materialhelper.h"
#include "../models/socketmodel.h"
#include "../assets.h"
//#include "irisgl/src/graphics/graphicshelper.h"
#include "../widgets/scenewidget.h"
#include "../../engine/enginehost.h"

CustomRenderWidget::CustomRenderWidget() :
	iris::RenderWidget(nullptr)
{

}

QString CustomRenderWidget::assetPath(QString relPath)
{
	return QDir::cleanPath(QDir::currentPath() + QDir::separator() + relPath);
}

void CustomRenderWidget::start()
{
	//targetFPS = 0;

	scale = 1.0;

	iris::VertexLayout layout;
	layout.addAttrib(iris::VertexAttribUsage::Position, GL_FLOAT, 3, sizeof(float) * 3);
	//vertexBuffer->setData()

	cam = iris::CameraNode::create();
	cam->setLocalPos(QVector3D(2, 0, 3));
	cam->lookAt(QVector3D(0, 0, 0));
	cam->nearClip = 1.0;
	cam->farClip = 5.0f;
	/*
	shader = iris::Shader::load(
	":assets/shaders/color.vert",
	":assets/shaders/color.frag");
	*/

	//mesh = iris::Mesh::loadMesh(MaterialHelper::assetPath("lowpoly_sphere.obj"));
	mesh = shadergraph::Assets::sphereMesh;
	//mat = iris::DefaultMaterial::create();

	font = iris::Font::create(device);

	vertString = iris::GraphicsHelper::loadAndProcessShader(MaterialHelper::assetPath("preview.vert"));
	fragString = iris::GraphicsHelper::loadAndProcessShader(MaterialHelper::assetPath("preview.frag"));
	updateShader("void preview(inout PreviewMaterial preview){}");

	renderTime = 0;

	lights.clear();

	// setup lights
	auto main = iris::LightNode::create();
	main->setLightType(iris::LightType::Point);
	main->setLocalPos(QVector3D(-3, 0, 3));
	main->setVisible(true);
	main->color = QColor(255, 255, 255);
	main->intensity = 0.8f;
	lights.append(main);
}

void CustomRenderWidget::update(float dt)
{
	fps = 1.0 / dt;
	renderTime += dt;
}

void CustomRenderWidget::render()
{
	auto vpWidth = (int)(width() * devicePixelRatioF());
	auto vpHeight = (int)(height() * devicePixelRatioF());

	cam->aspectRatio = vpWidth / vpHeight;
	cam->update(0.016f);

	//device->clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, QColor(qMin((int)(renderTime*0.1f * 255), 255), 0, 0));
	device->clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, QColor(128, 128, 128, 255));

	device->setBlendState(iris::BlendState::createOpaque());
	device->setDepthState(iris::DepthState());

	auto& graphics = device;
	device->setViewport(QRect(0, 0, vpWidth, vpHeight));
	device->setShader(shader);
	device->setShaderUniform("u_viewMatrix", cam->viewMatrix);
	device->setShaderUniform("u_projMatrix", cam->projMatrix);
	QMatrix4x4 world;
	world.setToIdentity();
	world.rotate(rot);
	world.scale(scale);

	device->setShaderUniform("u_worldMatrix", world);
	device->setShaderUniform("u_normalMatrix", world.normalMatrix());
	device->setShaderUniform("color", QVector4D(0.0, 0.0, 0.0, 1.0));
	device->setShaderUniform("u_textureScale", 1.0f);

	graphics->setShaderUniform("u_eyePos", cam->getLocalPos());
	graphics->setShaderUniform("u_sceneAmbient", QVector3D(0, 0, 0));
	graphics->setShaderUniform("u_time", SceneWidget::getRenderTime());

	// pass textures
	auto texMan = TextureManager::getSingleton();
	texMan->loadUnloadedTextures();
	int i = 0;
	for (auto tex : texMan->textures) {
		graphics->setTexture(i, tex->texture);
		graphics->setShaderUniform(tex->uniformName, i);
		i++;
	}

	passNodeGraphUniforms();

    graphics->setShaderUniform("u_lightCount", static_cast<int>(lights.size()));

	mesh->draw(device);
}

void CustomRenderWidget::updateShader(QString shaderCode)
{
	shader = iris::Shader::create(
		vertString,
		fragString + shaderCode);

	//qDebug() << "-------- PREVIEW SHADER --------";
	//qDebug().noquote() << fragString + shaderCode;
	//qDebug() << "-------- PREVIEW SHADER --------";
}

void CustomRenderWidget::resetRenderTime()
{
	renderTime = 0;
}

void CustomRenderWidget::passNodeGraphUniforms()
{
	if (graph == nullptr)
		return;

	for (auto prop : graph->properties) {
		switch (prop->type) {
		case PropertyType::Bool:
			device->setShaderUniform(prop->getUniformName(), prop->getValue().toBool());
			break;
		case PropertyType::Int:
			device->setShaderUniform(prop->getUniformName(), prop->getValue().toInt());
			break;
		case PropertyType::Float:
			device->setShaderUniform(prop->getUniformName(), prop->getValue().toFloat());
			break;
		case PropertyType::Vec2:
			device->setShaderUniform(prop->getUniformName(), prop->getValue().value<QVector2D>());
			break;
		case PropertyType::Vec3:
			device->setShaderUniform(prop->getUniformName(), prop->getValue().value<QVector3D>());
			break;
		case PropertyType::Vec4:
			device->setShaderUniform(prop->getUniformName(), prop->getValue().value<QVector4D>());
			break;
		}
	}
}

void CustomRenderWidget::setNodeGraph(NodeGraph *graph)
{
	this->graph = graph;
}

long GraphNode::pressedZValue = 0;
GraphNode::GraphNode(QGraphicsItem* parent) :
	QGraphicsPathItem(parent)
{
	nodeType = 0;
	proxyWidget = nullptr;


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
	previewWidget = nullptr;
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
	// left-aligned next to the 18px header icon (NodeGraphQt layout)
	text->setPos(10 + NodeStyle::Node::iconSize, titleHeight / 2 - textHeight / 2);
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

void GraphNode::setPreviewShader(QString shader)
{
	if (this->previewWidget != nullptr) {
		this->previewWidget->updateShader(shader);
		this->update();
	}
}

void GraphNode::enablePreviewWidget()
{
	// Engine viewport mode runs on xcb, where realizing any QOpenGLWidget flips
	// the whole top-level window to GL compositing and painting stops (Qt cannot
	// create a GL context there). Skip the per-node GL preview; the graph itself
	// is raster. setPreviewShader/setNodeGraph already tolerate a null preview.
	if (EngineHost::viewportBackend() == ViewportBackend::Engine)
		return;
	proxyPreviewWidget = new QGraphicsProxyWidget(this);
	previewWidget = new CustomRenderWidget();
	proxyPreviewWidget->setWidget(previewWidget);
	proxyPreviewWidget->setGeometry(QRectF(0, 260, 160, 160));

	if (this->nodeGraph)	previewWidget->setNodeGraph(nodeGraph);

	QObject::connect(&updateTimer, &QTimer::timeout, [this]()
	{
		proxyPreviewWidget->update();
	});
	updateTimer.start(1000 / 30);

	layout();
}

void GraphNode::setNodeGraph(NodeGraph* graph)
{
	this->nodeGraph = graph;
	if (this->previewWidget) {
		this->previewWidget->setNodeGraph(graph);
	}
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



