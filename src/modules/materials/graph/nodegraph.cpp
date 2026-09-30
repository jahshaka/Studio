/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#include "irisgl/core/math/qtinterop.h"
#include "irisgl/core/math/vec.h"
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

	// AN INPUT TAKES ONE WIRE, AND THE NEW ONE REPLACES THE OLD
	// (PRESET-UNIFY-1 fix round 2). The old wire's socket pointers were
	// simply overwritten and its ConnectionModel LEFT IN `connections` — so
	// it still serialized, and a graph re-wired through the verb came back
	// with a connection into a socket that no longer believed in it. The
	// CANVAS replaces (its in-socket press detaches the old wire before the
	// new one lands), so the model does the same thing rather than a second
	// thing.
	if (rightSock->connection) removeConnection(rightSock->connection->id);

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
	// The guard is BACK (F2, 2026-09-06). Commented out, `connections[id]` on
	// an unknown id is QMap::operator[]'s INSERT-a-default, so the miss both
	// grew a null entry in the map and then dereferenced it — a crash reachable
	// from any caller that does not already know the id is live, which is every
	// scripted caller.
	if (!connections.contains(connectionId))
		return;

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
	// The master node's socket layout: the reader REFUSES any other (a
	// renumbering bumps it; connections are stored by index).
	graph.insert("socketLayout", kSocketLayoutVersion);

	graph["settings"] = serializeMaterialSettings();

	graph["materialGuid"] = materialGuid;
	return graph;
}

NodeGraph* NodeGraph::deserialize(QJsonObject graphObj, NodeLibrary* library,
                                  QString* refusalReason)
{
	if (refusalReason) refusalReason->clear();

	// THE MASTER IS THE PBR ONE, OR THE FILE IS REFUSED (LEGACY-MASTER-CRUD,
	// 2026-09-20). There is exactly one master node class in this build. A
	// graph saved on the deleted Blinn-Phong "Surface Material" master used to
	// be CONVERTED here, socket by socket, with a note telling the user what
	// the conversion had dropped; the owner's call is that no legacy graph
	// exists to convert — every shipped preset is an authored PBR graph and no
	// shipped sample carries a material row at all — so the conversion is
	// deleted rather than carried. The CRUD law: nothing is owed to old data.
	//
	// A MASTER THAT IS NOT NAMED AT ALL is refused by the same rule (fix
	// round). A file with no `masternode` key, an empty one, or one naming a
	// node that is not in the array used to load as a graph with
	// `masterNode == nullptr`: it bakes nothing, draws as an empty canvas, and
	// `serialize()` dereferences that null the moment anything saves it. There
	// is no such thing as a graph without a master, so there is no such thing
	// as loading one.
	//
	// Refusing costs ONE pre-pass over the node array, before a single node is
	// built, so a refused file allocates no graph and leaves nothing behind.
	// (The `new LibraryV1()` every CALLER hands in is still leaked on a
	// refusal, exactly as it is leaked on every successful load — NodeGraph
	// has no destructor. Recorded as debt, not changed here.)
	{
		const QString masterId = graphObj["masternode"].toString();
		const QJsonArray nodesForMaster = graphObj["nodes"].toArray();
		QString masterType;
		bool found = false;
		if (!masterId.isEmpty()) {
			for (const auto& nodeVar : nodesForMaster) {
				const QJsonObject nodeObj = nodeVar.toObject();
				if (nodeObj["id"].toString() != masterId) continue;
				masterType = nodeObj["type"].toString();
				found = true;
				break;
			}
		}
		if (!found || masterType != QLatin1String("PbrMaterial")) {
			if (refusalReason) {
				if (!found)
					*refusalReason = QStringLiteral(
					    "This material has no master node and cannot be opened. Recreate it "
					    "on the \"PBR Material\" node.");
				else if (masterType == QLatin1String("Material"))
					*refusalReason = QStringLiteral(
					    "This material was saved with the removed \"Surface Material\" node "
					    "and cannot be opened. Recreate it on the \"PBR Material\" node.");
				else
					*refusalReason = QStringLiteral(
					    "This material's master node is of an unknown type (\"%1\") and "
					    "cannot be opened. Recreate it on the \"PBR Material\" node.")
					        .arg(masterType);
			}
			qWarning().noquote()
			    << "NodeGraph: refused a graph whose master is"
			    << (found ? masterType : QStringLiteral("absent"));
			return nullptr;
		}
	}

	// THE SOCKET LAYOUT IS THIS BUILD'S, OR THE FILE IS REFUSED (FORWARD-ONLY-1).
	// Connections are stored BY INDEX, so a graph written against another
	// master layout would land every connection on the wrong socket. There is
	// no conversion: every graph this build writes stamps kSocketLayoutVersion.
	{
		const int savedLayout = graphObj.value(QStringLiteral("socketLayout")).toInt(0);
		if (savedLayout != kSocketLayoutVersion) {
			if (refusalReason)
				*refusalReason = QStringLiteral(
				    "This material was saved by an older version of Jahshaka (socket layout "
				    "%1, this build reads %2) and cannot be opened. Recreate it.")
				        .arg(savedLayout).arg(kSocketLayoutVersion);
			qWarning().noquote() << "NodeGraph: refused a graph with socket layout" << savedLayout;
			return nullptr;
		}
	}

	auto graph = new NodeGraph();
	graph->setNodeLibrary(library);

	// read nodes
	auto nodeList = graphObj["nodes"].toArray();
	for (auto nodeVar : nodeList) {
		auto nodeObj = nodeVar.toObject();
		auto type = nodeObj["type"].toString();

		// The master node is constructed directly, not through the library.
		// There is exactly ONE master: "PbrMaterial" — a file written on any
		// other was refused above.
		NodeModel* nodeModel = nullptr;
		if (type == "PbrMaterial") {
			nodeModel = new PbrMasterNode();
		}
		else {
			nodeModel = graph->library->createNode(type);
		}
		// a type the library doesn't know (a newer build's node) is
		// skipped; the rest of the file keeps loading
		if (nodeModel == nullptr)
			continue;
		nodeModel->id = nodeObj["id"].toString();
		nodeModel->setX(nodeObj["x"].toDouble());
  		nodeModel->setY(nodeObj["y"].toDouble());

		nodeModel->deserializeWidgetValue(nodeObj["value"]);
		const QString storedTitle = nodeObj["title"].toString();
		if (!storedTitle.isEmpty())
			nodeModel->title = storedTitle;

		graph->addNode(nodeModel);
		if (type == "PbrMaterial") {
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
	case BlendMode::Masked:
		blendType = "Masked";
		break;
	case BlendMode::Translucent:
		blendType = "Translucent";
		break;
	case BlendMode::Additive:
		blendType = "Additive";
		break;
	case BlendMode::Modulate:
		blendType = "Modulate";
		break;
	case BlendMode::Glass:
		blendType = "Glass";
		break;
	case BlendMode::Refractive:
		blendType = "Refractive";
	}

	obj["name"] = settings.name;
	obj["blendMode"] = blendType;
	obj["bakeResolution"] = settings.bakeResolution;
	return obj;
}

MaterialSettings NodeGraph::deserializeMaterialSettings(QJsonObject obj)
{

	auto getBlendmode = [](QJsonObject obj) {
		const QString mode = obj["blendMode"].toString().toLower();
		if (mode == "opaque") return BlendMode::Opaque;
		if (mode == "masked") return BlendMode::Masked;
		if (mode == "translucent") return BlendMode::Translucent;
		if (mode == "additive") return BlendMode::Additive;
		if (mode == "modulate") return BlendMode::Modulate;
		if (mode == "glass") return BlendMode::Glass;
		if (mode == "refractive") return BlendMode::Refractive;
		return BlendMode::Opaque;
	};
	MaterialSettings settings;
	settings.name = obj["name"].toString();
	settings.blendMode = getBlendmode(obj);
	settings.bakeResolution = qBound(128, obj["bakeResolution"].toInt(1024), 4096);

	return settings;
}

void NodeGraph::setMaterialSettings(MaterialSettings setting)
{
	this->settings = setting;
}

NodeGraph::~NodeGraph()
{
	// EVERY NODE AND EVERY CONNECTION (MATERIALS_TABS_SPEC
	// §2.8). Connections first — they hold a socket on each side — then the
	// nodes, which own their sockets and (unless a scene's proxy took it) their
	// widget.
	//
	// NOT the library (see the header), and NOT a node the undo stack is
	// holding for its redo: a deleted node leaves `nodes`, and the command that
	// deleted it owns it until the stack is cleared.
	qDeleteAll(connections);
	connections.clear();
	qDeleteAll(nodes);
	nodes.clear();
	masterNode = nullptr;
}
