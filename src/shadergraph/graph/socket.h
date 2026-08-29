#pragma once
#include <QGraphicsPathItem>
#include "graphnode.h"

enum class SocketType
{
	In,
	Out
};

// live-drag feedback set by the scene while a connection is dragged
enum class SocketDragHighlight
{
	None,
	Valid,
	Invalid
};

class GraphNode;
class GraphNodeScene;
class SocketConnection;
class Socket : public QGraphicsPathItem
{

public:

	// note: in sockets can only have one connection
	QVector<SocketConnection*> connections;
	SocketType socketType;
	float radius;
	float dimentions;
	qreal opactyValue = 0.0;
	QGraphicsTextItem* text;
	GraphNode* node;
	GraphNode* owner;

	int socketIndex = -1;

	Socket(QGraphicsItem* parent, SocketType socketType, QString title);
	void addConnection(SocketConnection* con);
	void removeConnection(SocketConnection* con);
	float calcHeight();
	float getRadius();
	QPointF getPos();
	float getSocketOffset();
	virtual int type() const override;
	QColor getSocketColor();
	void setSocketColor(QColor color);
	void updateSocket();
	void setDragHighlight(SocketDragHighlight state);
	bool isConnected() const { return connected; }
	QPoint getSocketPosition();
	QVariant itemChange(GraphicsItemChange change, const QVariant &value);
private:
	QPointF socketPos;
	QColor socketColor;
	bool connected = false;
	bool hovered = false;
	SocketDragHighlight dragHighlight = SocketDragHighlight::None;
	int outSocketXOffset;
	int outSocketYOffset;
	int inSocketXOffset;
	int inSocketYOffset;

	void setConnected(bool value);

protected:
	void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget = Q_NULLPTR);
	void hoverEnterEvent(QGraphicsSceneHoverEvent *event) override;
	void hoverLeaveEvent(QGraphicsSceneHoverEvent *event) override;
};
