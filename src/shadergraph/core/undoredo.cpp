/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/
#include "undoredo.h"
#include <QPoint>
#include <QDebug>

#include "../widgets/propertylistwidget.h"


int UndoRedo::count = 0;
UndoRedo::UndoRedo()
{

}


UndoRedo::~UndoRedo()
{
}

void UndoRedo::undo()
{
	count--;

}

void UndoRedo::redo()
{
	count++;
}

AddNodeCommand::AddNodeCommand(NodeModel * nodeModel, GraphNodeScene* scene)
{
	initialPosition = {nodeModel->getX(), nodeModel->getY()};
	this->scene = scene;
	this->nodeModel = nodeModel;
	setText(QObject::tr("Add %1").arg(nodeModel->title + "command"));
}

void AddNodeCommand::undo()
{
	auto node = scene->getNodeById(nodeModel->id);
	node->setVisible(false);
	scene->deleteNode(node);
	scene->update(scene->itemsBoundingRect());
	scene->views()[0]->update();
	UndoRedo::undo();
}

void AddNodeCommand::redo()
{
	scene->addNodeModel(nodeModel, nodeModel->getX(), nodeModel->getY());
	UndoRedo::redo();

}

AddConnectionCommand::AddConnectionCommand(const QString &leftNodeId, const QString &rightNodeId, GraphNodeScene *scene, int leftSocket, int rightSocket)
{
	this->scene = scene;
	this->left = leftSocket;
	this->right = rightSocket;
	this->leftNodeId = leftNodeId;
	this->rightNodeId = rightNodeId;
	setText(QObject::tr("Add %1").arg("connection command"));

}

void AddConnectionCommand::undo()
{

	scene->removeConnection(connectionID, true, true);
	scene->removeItem(scene->getConnection(connectionID));
	//remind nick to remove connections from models!
	UndoRedo::undo();

}

void AddConnectionCommand::redo()
{
	auto conModel = scene->nodeGraph->addConnection(leftNodeId, this->left, rightNodeId, this->right);
	
	
	auto con = scene->addConnection(leftNodeId, this->left, rightNodeId, this->right);
	connectionID = con->connectionId = conModel->id; // very important!
	

	con->updatePosFromSockets();
	con->updatePath();
	//remind nick to add connections to models!
	UndoRedo::redo();

}

DeleteNodeCommand::DeleteNodeCommand(QList<GraphNode*> & list, GraphNodeScene *scene)
{
	this->list = list;
	this->scene = scene;
}

void DeleteNodeCommand::undo()
{
	for (auto node : list) {
		scene->nodeGraph->addNode(node->model);
		scene->addItem(node);
	}

	QMap<QString, ConnectionModel*> newConnections;
	for (auto con : connections) {
		auto leftNode = con->leftSocket->node;
		auto rightNode = con->rightSocket->node;

		auto conModel = scene->nodeGraph->addConnection(leftNode->id,
			leftNode->outSockets.indexOf(con->leftSocket),
			rightNode->id,
			rightNode->inSockets.indexOf(con->rightSocket));

		auto newCon = scene->addConnection(leftNode->id,
			leftNode->outSockets.indexOf(con->leftSocket),
			rightNode->id,
			rightNode->inSockets.indexOf(con->rightSocket));
		newCon->connectionId = conModel->id;

		
		newConnections[conModel->id] = conModel;
		delete con;
	}
	connections = newConnections;

	scene->emitGraphInvalidated();
	scene->update();
	UndoRedo::undo();
}

void DeleteNodeCommand::redo()
{
	// extract connections
	for (auto node : list) {
		auto conModels = scene->nodeGraph->getNodeConnections(node->nodeId);
		for (auto conModel : conModels) {
			auto con = scene->getConnection(conModel->id);
			connections.insert(conModel->id, conModel);
		}

		scene->deleteNode(node);
	}
	scene->emitGraphInvalidated();
	scene->update();
	UndoRedo::redo();
}

MoveMultipleCommand::MoveMultipleCommand(QList<GraphNode*>& list, GraphNodeScene *scene)
{
	this->scene = scene;
	this->nodes = list;
	for (auto node : nodes) {
		map.insert(node->nodeId, { node->initialPoint, node->movedPoint });
	}
}

void MoveMultipleCommand::undo()
{
	for (auto node : nodes) {
		node->setPos(map.value(node->nodeId).first);
		node->model->setX(map.value(node->nodeId).first.x());
		node->model->setY(map.value(node->nodeId).first.y());
	}
	UndoRedo::undo();

}

void MoveMultipleCommand::redo()
{
	QList<GraphNode*> nodeList;
	for (auto node : nodes) {
		auto actualNode = scene->getNodeById(node->nodeId);
		if(actualNode) nodeList.append(actualNode);
	}


	for (auto node : nodeList) {
			node->setPos(map.value(node->nodeId).second);
			node->model->setX(map[node->nodeId].second.x());
			node->model->setY(map[node->nodeId].second.y());
	}	
	UndoRedo::redo();
}

MaterialSettingsChangeCommand::MaterialSettingsChangeCommand(NodeGraph *graph, MaterialSettings &settings, MaterialSettingsWidget *mat)
{
	this->graph = graph;
	this->settings = settings;
	this->oldSettings = graph->settings;
	this->mat = mat;
}

void MaterialSettingsChangeCommand::undo()
{
	this->graph->settings = oldSettings;
	mat->updateMaterialSettingsWidget(oldSettings);
}

void MaterialSettingsChangeCommand::redo()
{
	this->graph->settings = settings;
	mat->updateMaterialSettingsWidget(settings);
}

AddPropertyCommand::AddPropertyCommand(QVBoxLayout *layout, QVector<BasePropertyWidget*>&list, BasePropertyWidget *widget, int index, PropertyListWidget *pl)
{
	this->lay = layout;
	this->list = &list;
	this->wid = widget;
	this->index = index;
	this->propertyList = pl;
}

void AddPropertyCommand::undo()
{
	lay->removeWidget(wid);
	list->removeOne(wid);
	index--;
	wid->setVisible(false);
	propertyList->graph->removeProperty(wid->modelProperty);
	propertyList->setCount(index);

}

void AddPropertyCommand::redo()
{
	lay->insertWidget(lay->count() - 1, wid); // minus one to account for stretch
	list->append(wid);
	wid->index = index;
	index++;
	wid->setVisible(true);
	propertyList->graph->addProperty(wid->modelProperty);
	propertyList->setCount(index);
}

DeletePropertyCommand::DeletePropertyCommand(QVBoxLayout *layout, BasePropertyWidget *wid, int index, PropertyListWidget *pl)
{
	this->lay = layout;
	this->index = index;
	this->wid = wid;
	this->propertyList = pl;
}

void DeletePropertyCommand::undo()
{
	lay->insertWidget(lay->count() - 1, wid); // minus one to account for stretch
	propertyList->referenceList.append(wid);
	wid->index = index;
	index++;
	wid->setVisible(true);
	propertyList->graph->addProperty(wid->modelProperty);
	propertyList->setCount(index);
}

void DeletePropertyCommand::redo()
{
	lay->removeWidget(wid);
	propertyList->referenceList.removeOne(wid);
	index--;
	wid->setVisible(false);
	propertyList->graph->removeProperty(wid->modelProperty);
	propertyList->setCount(index);
}

