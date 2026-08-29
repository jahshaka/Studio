#pragma once
#include <QUndoStack>
#include <QPoint>
#include <QVBoxLayout>
#include "../graph/graphnode.h"
#include "../graph/graphnodescene.h"
#include "../widgets/materialsettingswidget.h"
#include "../propertywidgets/basepropertywidget.h"

class PropertyListWidget;
class UndoRedo : public QUndoCommand
{
public:
	UndoRedo();
	~UndoRedo();

	void undo() override;
	void redo() override;
	static int count;
};

class AddNodeCommand : public UndoRedo
{
public:
	AddNodeCommand(NodeModel * nodeModel, GraphNodeScene *);

	void undo() override;
	void redo() override;

private :
	QPointF initialPosition;
	GraphNodeScene* scene;
	NodeModel * nodeModel;
	GraphNode *node;
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
	GraphNode* node;
	SocketConnection* connection;
	QString leftNodeId;
	QString rightNodeId;
};

class MoveNodeCommand : public UndoRedo
{
public:
	MoveNodeCommand(GraphNode *node, GraphNodeScene *,  QPointF oldPos, QPointF newPos);

	void undo() override;
	void redo() override;
private:
	QPointF oldPos;
	QPointF newPos;
	GraphNode* node;
	GraphNodeScene* scene;
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

class DeleteNodeCommand : public UndoRedo
{
public:
	DeleteNodeCommand(QList<GraphNode*> &, GraphNodeScene *);

	void undo() override;
	void redo() override;
private:
	GraphNodeScene* scene;
	QList<GraphNode*> list;
	GraphNode* node;
	//QVector<ConnectionModel*> connections;
	QMap<QString, ConnectionModel*> connections;
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

class AddPropertyCommand : public UndoRedo
{
public:
	AddPropertyCommand(QVBoxLayout *, QVector<BasePropertyWidget*> &, BasePropertyWidget *, int index, PropertyListWidget *);

	void undo();
	void redo();
private:
	QVBoxLayout * lay;
	QVector<BasePropertyWidget*> *list;
	BasePropertyWidget *wid;
	PropertyListWidget *propertyList;
	int index;
};

class DeletePropertyCommand : public UndoRedo
{
public:
	DeletePropertyCommand(QVBoxLayout *, BasePropertyWidget *, int index, PropertyListWidget *);

	void undo();
	void redo();
private:
	QVBoxLayout * lay;
	BasePropertyWidget *wid;
	PropertyListWidget *propertyList;
	int index;
};

