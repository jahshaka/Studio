/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#include "nodegraph.h"
#include "../models/connectionmodel.h"
#include "../nodes/test.h"
#include "../nodes/pbrmasternode.h"
#include "../models/library.h"
#include "../core/guidhelper.h"

#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>

#include <QDebug>

void NodeGraph::addProperty(Property* prop)
{
	if(!properties.contains(prop))	this->properties.append(prop);
}

void NodeGraph::removeProperty(Property * prop)
{
	this->properties.removeOne(prop);
}


Property *NodeGraph::getPropertyByName(const QString &name)
{
	for (auto prop : properties)
		if (prop->name == name)
			return prop;
	return nullptr;
}

Property *NodeGraph::getPropertyById(const QString &id)
{
	for (auto prop : properties)
		if (prop->id == id)
			return prop;
	return nullptr;
}

QVector<NodeModel*> NodeGraph::getNodesByTypeName(QString name)
{
	QVector<NodeModel *> list;
	for (auto node : nodes.values())
		if (node->typeName == name) {
			
			list.append(node);
			
		}
			

	return list;
}

void NodeGraph::setNodeLibrary(NodeLibrary* lib)
{
	this->library = lib;
}


void NodeGraph::addNode(NodeModel *model)
{
	nodes.insert(model->id, model);
}

NodeModel* NodeGraph::getNode(const QString& nodeId)
{
	return nodes[nodeId];
}

QVector<ConnectionModel*> NodeGraph::getNodeConnections(const QString& nodeId)
{
	auto node = getNode(nodeId);
	QVector<ConnectionModel*> conns;

	for (auto con : this->connections.values()) {
		if (con->leftSocket->node == node || con->rightSocket->node == node) {
			conns.append(con);
		}
	}

	return conns;
}

void NodeGraph::removeNode(const QString& nodeId)
{
	auto conns = getNodeConnections(nodeId);

	for (auto con : conns) {
		removeConnection(con->id);
	}

	nodes.remove(nodeId);
}

void NodeGraph::setMasterNode(NodeModel *masterNode)
{
	this->masterNode = masterNode;
}

NodeModel *NodeGraph::getMasterNode()
{
	return masterNode;
}

ConnectionModel* NodeGraph::addConnection(NodeModel *leftNode, int leftSockIndex, NodeModel *rightNode, int rightSockIndex)
{
	// todo: check if the indices are correct
	return addConnection(leftNode->id, leftSockIndex, rightNode->id, rightSockIndex);
}

ConnectionModel* NodeGraph::addConnection(QString leftNodeId, int leftSockIndex, QString rightNodeId, int rightSockIndex)
{
	// todo: check if the ids and socket indices are correct
	auto leftNode = nodes[leftNodeId];
	auto leftSock = leftNode->outSockets[leftSockIndex];

	auto rightNode = nodes[rightNodeId];
	auto rightSock = rightNode->inSockets[rightSockIndex];

	// todo: check if socket with pair already exists

	auto con = new ConnectionModel();
	con->leftSocket = leftSock;
	con->rightSocket = rightSock;

	leftSock->connection = con;
	rightSock->connection = con;
	connections.insert(con->id, con);

	return con;
}

void NodeGraph::removeConnection(QString connectionId)
{
	//if (!connections.contains(connectionId))
	//	return;

	auto con = connections[connectionId];

	// assuming it's a complete connection
	con->leftSocket->connection = nullptr;
	con->rightSocket->connection = nullptr;
	connections.remove(connectionId);
}

QJsonObject NodeGraph::serialize()
{
	QJsonObject graph;

	QJsonArray nodesJson;

	// save nodes
	for (auto node : this->nodes.values()) {
		QJsonObject nodeObj;
		nodeObj["id"] = node->id;
		nodeObj["value"] = node->serializeWidgetValue();
		nodeObj["type"] = node->typeName;
		nodeObj["x"] = node->getX();
		nodeObj["y"] = node->getY();
		nodesJson.append(nodeObj);
	}
	graph.insert("nodes", nodesJson);

	// save connections
	QJsonArray consJson;
	for (auto con : this->connections.values()) {
		QJsonObject conObj;
		conObj["id"] = con->id;
		conObj["leftNodeId"] = con->leftSocket->node->id;
		conObj["leftNodeSocketIndex"] = con->leftSocket->node->outSockets.indexOf(con->leftSocket);//todo: ugly, cleanup.
		conObj["rightNodeId"] = con->rightSocket->node->id;
		conObj["rightNodeSocketIndex"] = con->rightSocket->node->inSockets.indexOf(con->rightSocket);//todo: ugly, cleanup.

		consJson.append(conObj);
	}
	graph.insert("connections", consJson);
	graph.insert("masternode", this->masterNode->id);

	//todo: save settings (acceptLighting, blendstate, depthstate, etc..)

	graph["settings"] = serializeMaterialSettings();

	//todo: save parameters
	QJsonArray propJson;
	for (auto prop : this->properties) {
		QJsonObject propObj = prop->serialize();
		propJson.append(propObj);
	}
	graph["properties"] = propJson;
	graph["materialGuid"] = materialGuid;
	return graph;
}

NodeGraph* NodeGraph::deserialize(QJsonObject graphObj, NodeLibrary* library)
{
	auto graph = new NodeGraph();
	graph->setNodeLibrary(library);
	//registerModels(graph);

	// read settings

	// read properties
	auto propList = graphObj["properties"].toArray();
	for (auto propObj : propList) {
		auto prop = Property::parse(propObj.toObject());
		graph->addProperty(prop);
	}

	// read nodes
	auto nodeList = graphObj["nodes"].toArray();
	for (auto nodeVar : nodeList) {
		auto nodeObj = nodeVar.toObject();
		auto type = nodeObj["type"].toString();

		// migration: TruncNode wrote "truncate" for years while its library
		// key was "trunc" — those saves used to crash on load (audit D1)
		if (type == "truncate")
			type = "trunc";

		// master nodes are constructed directly, not through the library:
		// "PbrMaterial" is the PBR master (default for new graphs),
		// "Material" the legacy Blinn-Phong one
		NodeModel* nodeModel = nullptr;
		if (type == "PbrMaterial") {
			nodeModel = new PbrMasterNode();
		}
		else if (type == "Material") {
			nodeModel = new SurfaceMasterNode();
		}
		else {
			//nodeModel = graph->modelFactories[type]();
			nodeModel = graph->library->createNode(type);
		}
		// a type the library doesn't know (e.g. graphs saved while
		// TruncNode wrote "truncate" instead of its key "trunc") used
		// to null-deref here; skip the node and keep loading the file
		if (nodeModel == nullptr)
			continue;
		nodeModel->id = nodeObj["id"].toString();
		nodeModel->setX(nodeObj["x"].toDouble());
  		nodeModel->setY(nodeObj["y"].toDouble());


		// special case for properties
		if (type == "property") {
			auto propId = nodeObj["value"].toString();
			auto prop = graph->getPropertyById(propId);
			((PropertyNode*)nodeModel)->setProperty(prop);
		}
		else {
			nodeModel->deserializeWidgetValue(nodeObj["value"]);
		}

		graph->addNode(nodeModel);
		if (type == "Material" || type == "PbrMaterial") {
			graph->setMasterNode(nodeModel);
		}
	}

	// read connections
	auto conList = graphObj["connections"].toArray();
	for (auto conVar : conList) {
		auto conObj = conVar.toObject();
		auto id = conObj["id"].toString();
		auto leftNodeId = conObj["leftNodeId"].toString();
		auto leftSockIndex = conObj["leftNodeSocketIndex"].toInt();
		auto rightNodeId = conObj["rightNodeId"].toString();
		auto rightSockIndex = conObj["rightNodeSocketIndex"].toInt();

		// endpoints may be missing when an unknown node type was skipped
		if (!graph->nodes.contains(leftNodeId) || !graph->nodes.contains(rightNodeId))
			continue;

		graph->addConnection(leftNodeId, leftSockIndex, rightNodeId, rightSockIndex);
	}
	
		// deserialize material settings
	graph->settings = graph->deserializeMaterialSettings(graphObj["settings"].toObject());
	graph->materialGuid = graphObj["materialGuid"].toString();
	
	return graph;
}


QJsonObject NodeGraph::serializeMaterialSettings()
{
	QJsonObject obj;

	QString blendType;
	switch (settings.blendMode) {
	case BlendMode::Opaque:
		blendType = "Opaque";
		break;
	case BlendMode::Blend:
		blendType = "Blend";
		break;
	case BlendMode::Additive:
		blendType = "Blend";
	}

	QString cullMode;
	switch (settings.cullMode) {
	case CullMode::Front:
		cullMode = "Front";
		break;
	case CullMode::Back:
		cullMode = "Back";
		break;
	case CullMode::None:
		cullMode = "None";
	}

	QString renderLayer;
	switch (settings.renderLayer) {
	case RenderLayer::Opaque:
		renderLayer = "Opaque";
		break;
	case RenderLayer::AlphaTested:
		renderLayer = "AlphaTested";
		break;
	case RenderLayer::Transparent:
		renderLayer = "Transparent";
		break;
	case RenderLayer::Overlay:
		renderLayer = "Overlay";
		break;
	}

	obj["name"] = settings.name;
	obj["zWrite"] = settings.zwrite;
	obj["depthTest"] = settings.depthTest;
	obj["fog"] = settings.fog;
	obj["castShadow"] = settings.castShadow;
	obj["receiveShadow"] = settings.receiveShadow;
	obj["acceptLighting"] = settings.acceptLighting;
	obj["blendMode"] = blendType;
	obj["cullMode"] = cullMode;
	obj["renderLayer"] = renderLayer;
	obj["bakeResolution"] = settings.bakeResolution;
	return obj;
}

MaterialSettings NodeGraph::deserializeMaterialSettings(QJsonObject obj)
{

	auto getBlendmode = [](QJsonObject obj) {
		if (obj["blendMode"].toString().toLower() == "opaque") return BlendMode::Opaque;
		if (obj["blendMode"].toString().toLower() == "blend") return BlendMode::Blend;
		if (obj["blendMode"].toString().toLower() == "additive") return BlendMode::Additive;
		return BlendMode::Opaque;
	};
	auto getCullMode = [](QJsonObject obj) {
		if (obj["cullMode"].toString().toLower() == "front") return CullMode::Front;
		if (obj["cullMode"].toString().toLower() == "back") return CullMode::Back;
		if (obj["cullMode"].toString().toLower() == "none") return CullMode::None;
		return CullMode::Front;
	};
	auto getRenderLayer = [](QJsonObject obj) {
		if (obj["renderLayer"].toString().toLower() == "opaque") return RenderLayer::Opaque;
		if (obj["renderLayer"].toString().toLower() == "alphatested") return RenderLayer::AlphaTested;
		if (obj["renderLayer"].toString().toLower() == "transparent") return RenderLayer::Transparent;
		if (obj["renderLayer"].toString().toLower() == "overlay") return RenderLayer::Overlay;
		return RenderLayer::Opaque;
	};

	MaterialSettings settings;
	settings.name = obj["name"].toString();
	settings.zwrite = obj["zWrite"].toBool();
	settings.depthTest = obj["depthTest"].toBool();
	settings.fog = obj["fog"].toBool();
	settings.castShadow = obj["castShadow"].toBool();
	settings.receiveShadow = obj["receiveShadow"].toBool();
	settings.acceptLighting = obj["acceptLighting"].toBool();
	settings.blendMode = getBlendmode(obj);
	settings.cullMode = getCullMode(obj);
	settings.renderLayer = getRenderLayer(obj);
	// absent in graphs saved before the Materials Evaluator program
	settings.bakeResolution = qBound(128, obj["bakeResolution"].toInt(1024), 4096);

	return settings;
}

void NodeGraph::setMaterialSettings(MaterialSettings setting)
{
	this->settings = setting;
}

