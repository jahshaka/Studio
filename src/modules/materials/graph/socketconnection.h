#pragma once

#include <QGraphicsPathItem>

#include "socket.h"

enum class SocketConnectionStatus
{
	Started,
	Inprogress,
	Finished,
	Cancelled,
	Editing,
};

class Socket;
class GraphNode;
class SocketConnection : public QGraphicsPathItem
{
public:
	QString connectionId;

	Socket* socket1;
	Socket* socket2;

	QPointF pos1;
	QPointF pos2;

	SocketConnectionStatus status;

	// live-drag feedback: what the loose end is currently over
	SocketDragHighlight liveTarget = SocketDragHighlight::None;

	SocketConnection();

	Socket* getInSocket();
	Socket* getOutSocket();

	void updatePosFromSockets();
	void updatePath();
	virtual int type() const override;
	QPainterPath shape() const override;
	void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget = Q_NULLPTR) override;

protected:
	void hoverEnterEvent(QGraphicsSceneHoverEvent *event) override;
	void hoverLeaveEvent(QGraphicsSceneHoverEvent *event) override;

private:
	bool hovered = false;
};
