#pragma once

#include <QQuaternion>
#include <QGraphicsPathItem>
#include <QGraphicsProxyWidget>
#include <QIcon>
#include <QWindow>
#include <irisgl/IrisGL.h>
#include <QTimer>

class NodeGraph;

class CustomRenderWidget : public iris::RenderWidget
{
	iris::MeshPtr mesh;
	iris::ShaderPtr shader;
	iris::MaterialPtr mat;

	iris::FontPtr font;
	float fps;

	iris::VertexBufferPtr vertexBuffer;
	iris::Texture2DPtr tex;

	iris::CameraNodePtr cam;

	QString vertString;
	QString fragString;

	float renderTime;
	QList<iris::LightNodePtr> lights;

	bool dragging;
	QPoint lastMousePos;
	QQuaternion rot;
	float scale;

	NodeGraph* graph = nullptr;
public:
	CustomRenderWidget();

	void start();

	void update(float dt);

	void render();

	void updateShader(QString shaderCode);

	void resetRenderTime();

	void passNodeGraphUniforms();
	void setNodeGraph(NodeGraph* graph);

	QString assetPath(QString relPath);
};

enum class GraphicsItemType : int
{
	Node = QGraphicsItem::UserType + 1,
	Socket = QGraphicsItem::UserType + 2,
	Connection = QGraphicsItem::UserType + 3
};

class Socket;
class SocketModel;
class SocketConnection;
class NodeModel;
class GraphNode : public QGraphicsPathItem
{
	QVector<Socket*> sockets;
	int nodeWidth;
	QGraphicsTextItem* text;
	QGraphicsProxyWidget* proxyWidget;
	QGraphicsProxyWidget* proxyPreviewWidget;
	

	int inSocketCount = 0;
	int outSocketCount = 0;
public:
	int nodeType;
	int level = 0;
	int titleHeight = 30;
	int titleRadius = 4;
	int increment = 12;
	bool isMasterNode = false;
	bool isHighlighted = false;
	bool currentSelectedState = false;
	bool check = false;
	QString nodeId;
	QColor titleColor;
	QIcon icon;
	CustomRenderWidget* previewWidget;
	QTimer updateTimer;
	NodeGraph* nodeGraph;
	NodeModel* model;

	GraphNode(QGraphicsItem* parent);
	~GraphNode();

	void setModel(NodeModel* model);
	void setIcon(QIcon icon);
	void setTitleColor(QColor color);
	void setTitle(QString title);
	void addInSocket(SocketModel *socket);
	void addOutSocket(SocketModel *socket);
	void addSocket(Socket* sock);
	void setWidget(QWidget* widget);
	void calcPath();
	int calcHeight();
	void resetPositionForColorWidget();
	bool doNotCheckProxyWidgetHeight = false;

	int getInSocketCount() { return inSocketCount; }
	int getOutSocketCount() { return outSocketCount; }
	Socket* getInSocket(int index);
	Socket* getOutSocket(int index);

	void layout();
	void setPreviewShader(QString shader);
	void enablePreviewWidget();

	void setNodeGraph(NodeGraph* graph);

	virtual void paint(QPainter *painter,
		const QStyleOptionGraphicsItem *option,
		QWidget *widget = 0) override;

	virtual int type() const override;

    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;

	QPointF initialPoint;
	QPointF movedPoint;

	static long pressedZValue;

private:
	QColor connectedColor = QColor(50, 150, 250);
	QColor disconnectedColor = QColor(90, 90, 90, 0);
	void highlightNode(bool val, int lvl);
	QFont font;
protected:
	void mousePressEvent(QGraphicsSceneMouseEvent *) override;
};

