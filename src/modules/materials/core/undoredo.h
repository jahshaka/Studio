#pragma once
#include <QUndoStack>
#include <memory>
#include <vector>
#include <QPoint>
#include <QVBoxLayout>
#include "../graph/graphnode.h"
#include "../models/nodemodel.h"
#include "../graph/graphnodescene.h"
#include "../widgets/materialsettingswidget.h"

class UndoRedo : public QUndoCommand
{
public:
	UndoRedo();
	~UndoRedo();

	void undo() override;
	void redo() override;
	static int count;
};

/// ONE OWNER PER NODE ACROSS THE UNDO STACK (LEAK-1 L4): while the add is
/// undone the COMMAND owns the node — its model and its canvas item, both
/// detached (GraphNodeScene::detachNode) — and the redo hands the same two
/// back; while it is done the graph and the canvas own them. An undone add
/// whose command dies takes the node with it. (The undo used to remove the
/// node and keep neither half: every undo leaked a GraphNode and, with the
/// stack cleared, the model.)
class AddNodeCommand : public UndoRedo
{
public:
	AddNodeCommand(NodeModel * nodeModel, GraphNodeScene *);

	void undo() override;
	void redo() override;

private :
	QPointF initialPosition;
	GraphNodeScene* scene;
	NodeModel * nodeModel;   // identity (its id); owned per the note above
	std::unique_ptr<NodeModel> heldModel;   // declared before heldItem: the item dies first
	std::unique_ptr<GraphNode> heldItem;
};

class AddConnectionCommand : public UndoRedo
{
public:
	AddConnectionCommand(const QString &,const QString &, GraphNodeScene *, int leftSocket, int rightSocket);

	void undo() override;
	void redo() override;
	QString connectionID;
private:
	int left, right;
	GraphNodeScene* scene;
	GraphNode* node = nullptr;
	SocketConnection* connection = nullptr;
	QString leftNodeId;
	QString rightNodeId;
};

class DeleteConnectionCommand : public UndoRedo
{
public:
	DeleteConnectionCommand(SocketConnection *con, GraphNodeScene *);

	void undo() override;
	void redo() override;
private:
	GraphNodeScene* scene;
	QString leftNodeId;
	QString rightNodeId;
	QString connectionID;
	int left, right;
};

class MoveMultipleCommand : public UndoRedo
{
public:
	MoveMultipleCommand(QList<GraphNode*> &, GraphNodeScene*);

	void undo() override;
	void redo() override;
private:
	QList<GraphNode*> nodes;
	QMap<QString, QPair<QPointF, QPointF>> map;
	GraphNodeScene* scene;

};

/// The deleted nodes are the COMMAND's while the delete is done (LEAK-1 L4):
/// each one's model and canvas item, detached; the undo hands them back. The
/// connections the delete took are kept BY VALUE (the graph frees a removed
/// connection), and rebuilt on the undo.
class DeleteNodeCommand : public UndoRedo
{
public:
	DeleteNodeCommand(QList<GraphNode*> &, GraphNodeScene *);

	void undo() override;
	void redo() override;
private:
	struct Held { std::unique_ptr<NodeModel> model; std::unique_ptr<GraphNode> item; };   // item dies first
	struct Wire { QString leftNodeId; int leftIndex = -1; QString rightNodeId; int rightIndex = -1; };
	GraphNodeScene* scene;
	QList<GraphNode*> list;
	std::vector<Held> held;
	QMap<QString, Wire> wires;   // by the connection's id: a wire between two deleted nodes is kept once
};

class MaterialSettingsChangeCommand : public UndoRedo
{
public :
	MaterialSettingsChangeCommand(NodeGraph *, MaterialSettings &, MaterialSettingsWidget *);

	void undo();
	void redo();
private:
	NodeGraph * graph;
	MaterialSettings settings;
	MaterialSettings oldSettings;
	MaterialSettingsWidget *mat;
};


