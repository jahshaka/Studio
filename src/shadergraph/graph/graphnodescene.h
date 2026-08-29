#pragma once

#include <QDropEvent>
#include <QGraphicsSceneDragDropEvent>
#include <QGraphicsScene>
#include <QGraphicsRectItem>
#include <QGraphicsPathItem>
#include <QGraphicsTextItem>
#include <QGraphicsItem>
#include <QGraphicsObject>
#include <QFontMetrics>
#include <QtMath>
#include <QGraphicsSceneMouseEvent>
#include <QJsonObject>
#include <QJsonArray>
#include <QListWidgetItem>
#include <QMenu>
#include <QPointF>
#include <QPropertyAnimation>
#include <QTreeWidgetItem>
#include <QUndoStack>
#include "socketconnection.h"
#include "graphnode.h"
#include "nodegraph.h"
#include "socket.h"
#include "../core/sockethelper.h"
#include "../models/nodemodel.h"
#include "../models/connectionmodel.h"
#include "../models/library.h"
#include "../widgets/graphicsview.h"
#include "../core/undoredo.h"
//#include "nodes/test.h"

#include <QDebug>


class NodeGraph;
class NodeModel;
class Property;
class GraphNodeScene : public QGraphicsScene
{
	Q_OBJECT

		//QVector<GraphNode> nodes;

		// only used when dragging
		SocketConnection* con;

	QGraphicsItemGroup *conGroup;
public:
	GraphNodeScene(QWidget* parent);
	// model for scene
	NodeGraph* nodeGraph;
	GraphNode* selectedNode;
	QUndoStack *stack;
	template<class nodeType>
	GraphNode* createNode()
	{
		//        static_assert(
		//                std::is_base_of<MyBase, T>::value,
		//                "T must be a descendant of MyBase"
		//            );
		auto node = new nodeType(nullptr);
		this->addItem(node);

		return node;
	}

	SocketConnection* addConnection(QString leftNodeId, int leftSockIndex, QString rightNodeId, int rightSockIndex);
	SocketConnection* removeConnection(SocketConnection* connection, bool removeFromNodeGraph = true, bool emitSignal = true);
	void removeConnection(const QString& conId, bool removeFromNodeGraph = true, bool emitSignal = true);
	SocketConnection* addConnection(Socket* leftCon, Socket* rightCon);
	void setUndoRedoStack(QUndoStack *);

	void undo();
	void redo();

    bool eventFilter(QObject *o, QEvent *e) override;
	Socket* getSocketAt(float x, float y);
	SocketConnection* getConnectionAt(float x, float y);
	SocketConnection* getConnection(const QString& conId);

	bool canSocketConnect(Socket* outSock, Socket* inSock);

	// checks to see if potential connection will cause a loop
	// sock1 is the one already in the connection
	// sock2 is the connection to be added
	bool willConnectionBeALoop(Socket* sock1, Socket* sock2);

	// assigns inSock and outsock given two sockets
	// doesnt not handle the case where sock1 and sock2 are both In or Out
	// Out sockets are on the left and In sockets are on the right
	void determineOutAndInSockets(Socket* sock1, Socket* sock2, Socket** outSock, Socket** inSock);

	GraphNode* getNodeById(QString id);
    QVector<GraphNode*> getNodes();
	GraphNode* getNodeByPos(QPointF point);
	//QVector<SocketConnection*> socketConnections;
	NodeGraph *getNodeGraph() const;
	GraphNode* getNodeByPropertyId(QString id);
	void refreshNodeTitle(QString id);
	void setNodeGraph(NodeGraph* value);
	void addNodeModel(NodeModel* model, bool addToGraph = true);
	GraphNode* addNodeModel(NodeModel* model, float x, float y, bool addToGraph = true);
	void addPropertyNode(Property* prop, float x, float y, bool addToGraph = true);

	QMenu* createContextMenu(float x, float y);
	QMenu* removeConnectionContextMenu(float x, float y);

	QJsonObject serialize();
	QListWidgetItem *currentlyEditing = Q_NULLPTR;
	QList<QString> loadedShadersGUID;

	void setList(QList<QString> list) { loadedShadersGUID = list; }
	void updatePropertyNodeTitle(QString title, QString propId);

	void addNodeFromSearchDialog(QTreeWidgetItem* item, const QPoint& point);

	void deleteSelectedNodes();
	void deleteNode(GraphNode* node);

	bool areSocketsComptible(Socket* sock1, Socket* sock2);

	void emitGraphInvalidated();

protected:
	void dropEvent(QGraphicsSceneDragDropEvent *event) override;
    void drawBackground(QPainter *painter, const QRectF &rect) override;

signals:
	void newConnection(SocketConnection* connection);
	// not emitted when a node is deleted
	void connectionRemoved(SocketConnection* connection);
	void nodeRemoved(GraphNode* connection);
	void nodeValueChanged(NodeModel* nodeModel, int socketIndex);
	void loadGraph(QListWidgetItem *item);
	void loadGraphFromPreset(QString name);
	void loadGraphFromPreset2(QString name);

	// called whenever something is done that should cause the shader
	// to be invalidated such as:
	// adding a connection
	// removing a connection
	// deleting a node
	// changing a value
	void graphInvalidated();
};





