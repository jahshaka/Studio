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
#include <QSet>

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
		// keeps user-facing names (e.g. a migrated property's "Diffuse")
		// across the save; absent/empty on load = the constructor's default
		nodeObj["title"] = node->title;
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

	// §3b: "properties" is no longer written. Old files carrying it stay
	// readable forever (deserialize migrates them into real nodes); the
	// values now live on the nodes themselves.
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
		if (prop != nullptr) // unknown/absent type parses to null
			graph->addProperty(prop);
	}

	// read nodes
	// ids of texture nodes that replaced texture PropertyNodes this load —
	// their connections need the output-index collapse below
	QSet<QString> migratedTextureNodes;
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
		// §3b migration: a PropertyNode instance becomes the real node for
		// its property's value — Float/Int/Bool -> float, Vec2/3/4 ->
		// vector2/3/4, Color -> color, Texture -> texture (carrying the
		// asset guid). Position and id are preserved so connections
		// re-attach 1:1; multiple instances of one property become
		// independent copies (owner-locked call, 2026-08-31).
		bool migratedTexture = false;
		if (type == "property") {
			auto propId = nodeObj["value"].toString();
			auto prop = graph->getPropertyById(propId);
			nodeModel = nullptr;
			if (prop != nullptr) {
				switch (prop->type) {
				case PropertyType::Float:
				case PropertyType::Int:
					nodeModel = graph->library->createNode("float");
					if (nodeModel) nodeModel->deserializeWidgetValue(QJsonValue(prop->getValue().toDouble()));
					break;
				case PropertyType::Bool:
					nodeModel = graph->library->createNode("float");
					if (nodeModel) nodeModel->deserializeWidgetValue(QJsonValue(prop->getValue().toBool() ? 1.0 : 0.0));
					break;
				case PropertyType::Vec2: {
					nodeModel = graph->library->createNode("vector2");
					auto v = prop->getValue().value<QVector2D>();
					QJsonObject o; o["x"] = v.x(); o["y"] = v.y();
					if (nodeModel) nodeModel->deserializeWidgetValue(o);
					break;
				}
				case PropertyType::Vec3: {
					nodeModel = graph->library->createNode("vector3");
					auto v = prop->getValue().value<QVector3D>();
					QJsonObject o; o["x"] = v.x(); o["y"] = v.y(); o["z"] = v.z();
					if (nodeModel) nodeModel->deserializeWidgetValue(o);
					break;
				}
				case PropertyType::Vec4: {
					nodeModel = graph->library->createNode("vector4");
					auto v = prop->getValue().value<QVector4D>();
					QJsonObject o; o["x"] = v.x(); o["y"] = v.y(); o["z"] = v.z(); o["w"] = v.w();
					if (nodeModel) nodeModel->deserializeWidgetValue(o);
					break;
				}
				case PropertyType::Color: {
					nodeModel = graph->library->createNode("color");
					auto c = prop->getValue().value<QColor>();
					QJsonObject o; o["r"] = c.redF(); o["g"] = c.greenF(); o["b"] = c.blueF(); o["a"] = c.alphaF();
					if (nodeModel) nodeModel->deserializeWidgetValue(o);
					break;
				}
				case PropertyType::Texture:
					nodeModel = graph->library->createNode("texture");
					if (nodeModel) nodeModel->deserializeWidgetValue(QJsonValue(prop->getValue().toString()));
					migratedTexture = true;
					break;
				default:
					break;
				}
			}
			// a property node whose property is missing or untyped:
			// skip it (matches the old skip-unknown-node rule)
			if (nodeModel == nullptr) {
				qWarning() << "NodeGraph: dropped property node" << nodeObj["id"].toString()
				           << "- no usable property" << propId;
				continue;
			}
			nodeModel->title = prop->displayName;
			nodeModel->id = nodeObj["id"].toString();
			nodeModel->setX(nodeObj["x"].toDouble());
			nodeModel->setY(nodeObj["y"].toDouble());
			graph->addNode(nodeModel);
			graph->migratedPropertyNodes.insert(nodeModel->id, propId);
			if (migratedTexture)
				migratedTextureNodes.insert(nodeModel->id);
			continue;
		}

		// a type the library doesn't know (e.g. graphs saved while
		// TruncNode wrote "truncate" instead of its key "trunc") used
		// to null-deref here; skip the node and keep loading the file
		if (nodeModel == nullptr)
			continue;
		nodeModel->id = nodeObj["id"].toString();
		nodeModel->setX(nodeObj["x"].toDouble());
  		nodeModel->setY(nodeObj["y"].toDouble());

		nodeModel->deserializeWidgetValue(nodeObj["value"]);
		auto storedTitle = nodeObj["title"].toString();
		if (!storedTitle.isEmpty())
			nodeModel->title = storedTitle;

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

		// §3b: a texture PropertyNode had outputs texture/rgba/normal — the
		// replacing texture node has the single texture output, and the
		// evaluator lands it on the same map slot, so all three collapse to
		// output 0. Its uv INPUT has no equivalent; connections into it drop.
		if (migratedTextureNodes.contains(leftNodeId))
			leftSockIndex = 0;
		if (migratedTextureNodes.contains(rightNodeId)) {
			qWarning() << "NodeGraph: dropped uv connection into migrated texture node" << rightNodeId;
			continue;
		}

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
	case BlendMode::Masked:
		blendType = "Masked";
		break;
	case BlendMode::Translucent:
		// legacy string kept so old builds still read new files as alpha blend
		blendType = "Blend";
		break;
	case BlendMode::Additive:
		// was serialized as "Blend" — Additive never survived a save/load
		blendType = "Additive";
		break;
	case BlendMode::Modulate:
		blendType = "Modulate";
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
		const QString mode = obj["blendMode"].toString().toLower();
		if (mode == "opaque") return BlendMode::Opaque;
		if (mode == "masked") return BlendMode::Masked;
		if (mode == "blend" || mode == "translucent") return BlendMode::Translucent;
		if (mode == "additive") return BlendMode::Additive;
		if (mode == "modulate") return BlendMode::Modulate;
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

