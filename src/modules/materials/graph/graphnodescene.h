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
#include "../models/nodemodel.h"
#include "../models/connectionmodel.h"
#include "../models/library.h"
#include "../widgets/graphicsview.h"
#include "../core/undoredo.h"

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

	// socket the live-drag loose end is currently over (for highlight)
	Socket* dragHoverSocket = nullptr;

	QGraphicsItemGroup *conGroup;
	bool readOnly = false;
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
	void refreshNodeTitle(QString id);
	void setNodeGraph(NodeGraph* value);
	void addNodeModel(NodeModel* model, bool addToGraph = true);
	GraphNode* addNodeModel(NodeModel* model, float x, float y, bool addToGraph = true);

	QMenu* createContextMenu(float x, float y);
	QMenu* removeConnectionContextMenu(float x, float y);

	QJsonObject serialize();
	// (`currentlyEditing`, `loadedShadersGUID` and `setList` are DELETED —
	// MATERIALS_TABS_SPEC §7. The first was a throwaway QListWidgetItem the
	// drop handler minted and leaked so the page could read a guid off it;
	// the other two were written by nobody and read by nobody. WHICH
	// MATERIAL THIS CANVAS IS SHOWING is the document's business now.)

	void addNodeFromSearchDialog(QTreeWidgetItem* item, const QPoint& point);

	void deleteSelectedNodes();
	void deleteNode(GraphNode* node);

	// Delete BY ID, through the same undo commands deleteSelectedNodes pushes
	// (verb-coverage audit F2). The canvas deletes what is selected; the
	// graph.removeNode / graph.disconnect verbs address a node or a pipe by
	// id and must not have to fake a selection to do it — and must land on
	// the page's edit stack, or graph.undo would silently not cover them.
	//
	// Refusals are answers, not exceptions: false = no such node/connection
	// in this scene, or (for the master) a node this scene will not delete.
	// With no undo stack wired the delete still happens, unrecorded — the
	// same degradation every other command site here has.
	bool deleteNodeById(const QString& nodeId);
	bool deleteConnectionById(const QString& connectionId);

	void clearDragHighlight();

	// selection API (§3a): the panel and the graph.selectNode/selectedNode/
	// deselect verbs drive selection through these
	bool selectNodeById(const QString& id);
	QString selectedNodeId();
	NodeModel* selectedNodeModel();
	void deselectAll();

	// clipboard copy/paste/duplicate of the selected nodes and the
	// connections that run between them
	void copySelectedToClipboard();
	void pasteFromClipboard();
	void duplicateSelected();

	bool areSocketsComptible(Socket* sock1, Socket* sock2);

	void emitGraphInvalidated();

	/// THE CANVAS REFUSES EVERY EDIT (PRESET-UNIFY-1 fix round). A shipped
	/// preset opens here to be READ, and until this existed the scene took
	/// the edits anyway: nodes could be added, wired, dragged and retyped,
	/// `saveShader` quietly returned, and Customise then built the copy from
	/// the SHIPPED definition — so the work went into a window that showed it
	/// and into nothing else. An editor that accepts an edit it will not keep
	/// is worse than one that says no, so this says no: no add, no delete, no
	/// connect, no paste, no drop, no drag, and every node's own widgets are
	/// disabled. It is a property of the SCENE rather than a check at each
	/// gesture so that a gesture added later cannot forget it.
	void setReadOnly(bool readOnly);
	bool isReadOnly() const { return readOnly; }

protected:
	void dropEvent(QGraphicsSceneDragDropEvent *event) override;
    void drawBackground(QPainter *painter, const QRectF &rect) override;

public:
	// GraphNode calls this from itemChange so the page can persist node
	// positions (the signal itself is protected to outsiders)
	void notifyNodeMoved() { emit nodeMoved(); }

signals:
	// a node's position changed (drags fire it repeatedly - debounce)
	void nodeMoved();
	void newConnection(SocketConnection* connection);
	// not emitted when a node is deleted
	void connectionRemoved(SocketConnection* connection);
	void nodeRemoved(GraphNode* connection);
	void nodeValueChanged(NodeModel* nodeModel, int socketIndex);
	// exactly one node selected -> its model; empty or multi selection -> null
	void nodeSelected(NodeModel* model);
	/// A MATERIAL TILE WAS DROPPED ON THE CANVAS: open this guid. (It used
	/// to carry a QListWidgetItem the handler built for the purpose — a
	/// widget item with no list, leaked on every drop, whose only cargo was
	/// the guid and a label the page re-read from the real tile anyway.)
	void loadGraph(const QString &guid);
	// (loadGraphFromPreset / loadGraphFromPreset2 are DELETED — PRESET-UNIFY-1.
	// They carried a dropped GRAPH TEMPLATE's name, in two flavours because
	// the templates lived in two folders. A preset tile is an ordinary
	// Material tile now, so a preset dropped on the canvas takes the
	// loadGraph branch above and opens read-only like every other one.)

	// called whenever something is done that should cause the shader
	// to be invalidated such as:
	// adding a connection
	// removing a connection
	// deleting a node
	// changing a value
	void graphInvalidated();
};





