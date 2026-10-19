/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#include "undoredo.h"
#include <QPoint>
#include <QDebug>


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
	heldModel = scene->detachNode(node);
	heldItem.reset(node);
	scene->update(scene->itemsBoundingRect());
	scene->views()[0]->update();
	UndoRedo::undo();
}

void AddNodeCommand::redo()
{
	if (heldItem) scene->reattachNode(heldItem.release(), std::move(heldModel));   // the node the undo took
	else scene->addNodeModel(nodeModel, nodeModel->getX(), nodeModel->getY());   // the first redo: the push
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

	scene->removeConnection(connectionID, true);
	UndoRedo::undo();

}

void AddConnectionCommand::redo()
{
	// A SCENE THAT REFUSES THE WIRE MUST NOT BE ASKED FOR ITS ID
	// (PRESET-UNIFY-1 fix round 2). This mutated the MODEL first and then
	// dereferenced whatever `GraphNodeScene::addConnection` answered — and a
	// read-only scene answers nullptr. Dragging a wire on a shipped preset
	// was a SEGV. The press is refused at source now; this is the second
	// belt, because a command that assumes its scene will do as it is told is
	// one refusal away from a crash whatever the reason for the refusal.
	auto conModel = scene->nodeGraph->addConnection(leftNodeId, this->left, rightNodeId, this->right);
	auto con = scene->addConnection(leftNodeId, this->left, rightNodeId, this->right);
	if (!con || !conModel) {
		if (conModel) scene->nodeGraph->removeConnection(conModel->id);
		UndoRedo::redo();
		return;
	}
	connectionID = con->connectionId = conModel->id; // very important!
	

	con->updatePosFromSockets();
	con->updatePath();
	//remind nick to add connections to models!
	UndoRedo::redo();

}

DeleteConnectionCommand::DeleteConnectionCommand(SocketConnection *con, GraphNodeScene *scene)
{
	this->scene = scene;

	auto outSock = con->getOutSocket();
	auto inSock = con->getInSocket();
	leftNodeId = outSock->node->nodeId;
	left = outSock->socketIndex;
	rightNodeId = inSock->node->nodeId;
	right = inSock->socketIndex;
	connectionID = con->connectionId;

	setText(QObject::tr("Delete %1").arg("connection"));
}

void DeleteConnectionCommand::redo()
{
	scene->removeConnection(connectionID, true);
	UndoRedo::redo();
}

void DeleteConnectionCommand::undo()
{
	auto conModel = scene->nodeGraph->addConnection(leftNodeId, left, rightNodeId, right);
	auto con = scene->addConnection(leftNodeId, left, rightNodeId, right);
	connectionID = con->connectionId = conModel->id;

	con->updatePosFromSockets();
	con->updatePath();
	UndoRedo::undo();
}

DeleteNodeCommand::DeleteNodeCommand(QList<GraphNode*> & list, GraphNodeScene *scene)
{
	this->list = list;
	this->scene = scene;
}

void DeleteNodeCommand::undo()
{
	for (Held &h : held)
		scene->reattachNode(h.item.release(), std::move(h.model));
	held.clear();

	for (const Wire &w : wires) {
		auto conModel = scene->nodeGraph->addConnection(w.leftNodeId, w.leftIndex, w.rightNodeId, w.rightIndex);
		auto newCon = scene->addConnection(w.leftNodeId, w.leftIndex, w.rightNodeId, w.rightIndex);
		if (conModel && newCon) newCon->connectionId = conModel->id;
	}
	wires.clear();

	scene->emitGraphInvalidated();
	scene->update();
	UndoRedo::undo();
}

void DeleteNodeCommand::redo()
{
	// the wires first, by value, while both ends are still in the graph
	for (auto node : list) {
		for (auto conModel : scene->nodeGraph->getNodeConnections(node->nodeId)) {
			auto leftNode = conModel->leftSocket->node;
			auto rightNode = conModel->rightSocket->node;
			wires.insert(conModel->id, Wire{ leftNode->id, int(leftNode->outSockets.indexOf(conModel->leftSocket)),
			                                 rightNode->id, int(rightNode->inSockets.indexOf(conModel->rightSocket)) });
		}
		Held h;
		h.model = scene->detachNode(node);
		h.item.reset(node);
		held.push_back(std::move(h));
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


