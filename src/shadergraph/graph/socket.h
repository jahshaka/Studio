#pragma once
#include <QGraphicsPathItem>
#include "graphnode.h"

enum class SocketType
{
	In,
	Out
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
    QColor connectedColor = QColor(250, 250, 50);
	QPoint getSocketPosition();
	QVariant itemChange(GraphicsItemChange change, const QVariant &value);
private:
	QPointF socketPos;
	QColor socketColor;
	QColor disconnectedColor = QColor(60, 60, 64).darker(175);
	QColor regularColor = QColor(97, 97, 97, 150);
	bool connected = false;
	bool rounded = true;
	int outSocketXOffset;
	int outSocketYOffset;
	int inSocketXOffset;
	int inSocketYOffset;



	void setConnected(bool value);
	bool setShouldAddInvisibleCover = false;

protected:
	void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget = Q_NULLPTR);


};
