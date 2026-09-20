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

#include <algorithm>
#include <cmath>

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
	// The master node's socket layout, so a future renumbering can migrate
	// instead of silently re-pointing every connection (pbrmasternode.h).
	// Absent = layout 1, which is what every graph saved before P2 is.
	graph.insert("socketLayout", kSocketLayoutVersion);

	graph["settings"] = serializeMaterialSettings();

	// §3b: "properties" is no longer written. Old files carrying it stay
	// readable forever (deserialize migrates them into real nodes); the
	// values now live on the nodes themselves.
	graph["materialGuid"] = materialGuid;
	return graph;
}

namespace {

// ---------------------------------------------------------- legacy master
//
// THE BLINN-PHONG MASTER IS GONE (LEGACY-MASTER-CRUD, 2026-09-19). There is
// one master node, "PBR Material"; a saved graph whose master type is
// "Material" is converted on load and re-saves as PBR. Nothing is owed to old
// data beyond opening it correctly once.
//
// The deleted SurfaceMasterNode's inputs, in order:
//   0 Diffuse  1 Specular  2 Shininess  3 Normal  4 Ambient
//   5 Emission 6 Alpha     7 Alpha Cutoff  8 Vertex Offset  9 Vertex Extrusion
// PbrMasterNode's (socket layout 2):
//   0 Base Color  1 Metallic  2 Roughness  3 Normal  4 Emissive
//   5 Alpha       6 Alpha Cutoff  7 Vertex Offset  8 Vertex Extrusion
//
// -1 = no PBR equivalent, the connection is dropped and NAMED:
//   * SPECULAR. A Blinn specular colour/intensity is not a PBR input. Where a
//     legacy graph used one to fake a METAL (a saturated specular over a dark
//     diffuse) the answer is Metallic 1 with that colour as Base Color — a
//     judgement about the picture that only a human looking at it can make, so
//     the conversion never guesses it; it says what it dropped instead.
//     (None of the shipped presets was that case: all nine of their specular
//     maps measure mean saturation 0.000 over bright, coloured diffuses.)
//   * AMBIENT. PBR has no ambient term at all — the sky lights the scene.
constexpr int kLegacyMasterSocketCount = 10;
constexpr int kLegacyMasterSocketTarget[kLegacyMasterSocketCount] =
    { 0, -1, 2, 3, -1, 4, 5, 6, 7, 8 };

const char* legacyMasterSocketName(int index)
{
	static const char* kNames[kLegacyMasterSocketCount] = {
		"Diffuse", "Specular", "Shininess", "Normal", "Ambient",
		"Emission", "Alpha", "Alpha Cutoff", "Vertex Offset", "Vertex Extrusion"
	};
	if (index < 0 || index >= kLegacyMasterSocketCount) return "an unknown input";
	return kNames[index];
}

// Shininess -> Roughness. TWO CONVENTIONS shared that one socket, and which
// one a graph meant is readable from the number itself — the legacy baker
// already split on it (`s > 1 ? s/100 : s`), it just converted the exponent
// branch by dividing by 100, which sends the classic n=100 to roughness 0.
double roughnessFromLegacyShininess(double shininess)
{
	if (shininess > 1.0) {
		// A BLINN/PHONG SPECULAR EXPONENT n. The GGX lobe of the same width is
		// alpha = sqrt(2/(n+2)) (the standard exponent-to-alpha fit), and
		// PbrMaterial's roughness is PERCEPTUAL — HlmsPbs squares it
		// (mPerceptualRoughness) — so roughness = sqrt(alpha). n = 100 lands
		// 0.374, a believable polished surface, where /100 landed 0.0.
		const double alpha = std::sqrt(2.0 / (shininess + 2.0));
		return std::max(std::sqrt(alpha), NodeGraph::kConvertedGlossRoughnessFloor);
	}
	// A 0..1 GLOSS — the convention every shipped preset used. Roughness is
	// its complement, floored (see kConvertedGlossRoughnessFloor).
	return std::max(1.0 - std::max(0.0, shininess),
	                NodeGraph::kConvertedGlossRoughnessFloor);
}

} // namespace

NodeGraph* NodeGraph::deserialize(QJsonObject graphObj, NodeLibrary* library)
{
	auto graph = new NodeGraph();
	graph->setNodeLibrary(library);
	//registerModels(graph);

	// set when this file's master type is the deleted "Material" one
	bool legacyMasterConverted = false;
	QStringList droppedLegacySockets;

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

		// THE UV MERGE (MATERIAL_UV_NODES_SPEC D-3). "texCoords" (bare UV) and
		// "uvTransform" (uv*tiling+offset) are one "uv" node now. Both are
		// registered as hidden library aliases, so this is a RENAME on load,
		// not a rebuild: the alias node keeps in 0/1/2 and out 0 at the same
		// indices, and connections are stored BY INDEX, so nothing re-points.
		// `texCoords` had no inputs at all, so its saves reference out 0 only.
		const bool wasUvAlias = (type == "texCoords" || type == "uvTransform");

		// The master node is constructed directly, not through the library.
		// There is exactly ONE master: "PbrMaterial". A file whose master type
		// is "Material" is a graph authored on the deleted Blinn-Phong
		// "Surface Material" node; it is CONVERTED, socket by socket, in the
		// connection loop below (kLegacyMasterSocketTarget).
		NodeModel* nodeModel = nullptr;
		if (type == "PbrMaterial") {
			nodeModel = new PbrMasterNode();
		}
		else if (type == "Material") {
			nodeModel = new PbrMasterNode();
			legacyMasterConverted = true;
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
					auto v = iris::fromQt(prop->getValue().value<QVector2D>());
					QJsonObject o; o["x"] = v.x(); o["y"] = v.y();
					if (nodeModel) nodeModel->deserializeWidgetValue(o);
					break;
				}
				case PropertyType::Vec3: {
					nodeModel = graph->library->createNode("vector3");
					auto v = iris::fromQt(prop->getValue().value<QVector3D>());
					QJsonObject o; o["x"] = v.x(); o["y"] = v.y(); o["z"] = v.z();
					if (nodeModel) nodeModel->deserializeWidgetValue(o);
					break;
				}
				case PropertyType::Vec4: {
					nodeModel = graph->library->createNode("vector4");
					auto v = iris::fromQt(prop->getValue().value<QVector4D>());
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
		// A merged alias whose stored title is just the OLD DEFAULT takes the
		// new node's name — the user never renamed it, and leaving "UV
		// Transform" on a card that is now the UV node would be the merge
		// showing through. A title the user actually chose is kept, as always.
		if (wasUvAlias && (storedTitle == QLatin1String("UV Transform")
		                   || storedTitle == QLatin1String("Texture Coordinate")))
			storedTitle.clear();
		// The CONVERTED master must not keep reading "Surface Material" on its
		// card: the node under the title is the PBR one, and a graph that says
		// otherwise is a lie the user has to discover by clicking it. (A title
		// the user actually typed is kept, as always.)
		if (type == QLatin1String("Material") && storedTitle == QLatin1String("Surface Material"))
			storedTitle.clear();
		if (!storedTitle.isEmpty())
			nodeModel->title = storedTitle;

		graph->addNode(nodeModel);
		if (type == "Material" || type == "PbrMaterial") {
			graph->setMasterNode(nodeModel);
		}
	}

	// SOCKET-LAYOUT MIGRATION (HLMS_ADOPTION P2). Layout 1's PBR master had
	// "Occlusion" at input index 4; layout 2 does not, so every input index
	// above it moved down one. Connections are stored BY INDEX, so a layout-1
	// file read as layout 2 would land Emissive on Normal, Alpha on Emissive
	// and so on — silently. The version key makes that impossible instead of
	// unlikely. A connection INTO the removed socket is dropped and NAMED: it
	// fed a bake nothing ever rendered, and the user is owed the sentence.
	const int savedLayout = graphObj.contains("socketLayout")
	                            ? graphObj["socketLayout"].toInt(1) : 1;
	const QString masterId = graphObj["masternode"].toString();
	const bool pbrMaster = graph->masterNode && graph->masterNode->typeName == "PbrMaterial";
	// A CONVERTED legacy graph is never layout-1 PBR: its indices are the
	// Blinn master's and are remapped below, so the Occlusion shift must not
	// also run over them.
	const bool migrateSockets = savedLayout < 2 && pbrMaster && !legacyMasterConverted;
	constexpr int kRemovedOcclusionSocket = 4;

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

		if (migrateSockets && rightNodeId == masterId) {
			if (rightSockIndex == kRemovedOcclusionSocket) {
				// ONCE, however many connections carried it.
				const QString note =
				    QStringLiteral("The master node's Occlusion input was removed: the renderer "
				                   "has no ambient-occlusion input, so the map it baked was never "
				                   "read by anything. Its connection was dropped on load. Bake AO "
				                   "into the base colour at import if you need it.");
				if (!graph->migrationNotes.contains(note)) graph->migrationNotes << note;
				continue;
			}
			if (rightSockIndex > kRemovedOcclusionSocket) --rightSockIndex;
		}

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

		// THE LEGACY MASTER'S SOCKETS -> the PBR master's (see the table above).
		// This runs AFTER the texture-output collapse so a migrated texture
		// feeding Shininess is already pointing at its single output when the
		// inverter is spliced in front of it.
		if (legacyMasterConverted && rightNodeId == masterId) {
			const int target = (rightSockIndex >= 0 && rightSockIndex < kLegacyMasterSocketCount)
			                       ? kLegacyMasterSocketTarget[rightSockIndex] : -1;
			if (target < 0) {
				const QString dropped = QString::fromLatin1(legacyMasterSocketName(rightSockIndex));
				if (!droppedLegacySockets.contains(dropped)) droppedLegacySockets << dropped;
				continue;
			}
			if (rightSockIndex == 2) { // Shininess -> Roughness: gloss inverts
				auto* source = graph->nodes.value(leftNodeId);
				// A CONSTANT SHARED WITH ANOTHER INPUT must not be rewritten under
				// it (the lead's read): the gloss number that also scales, say, an
				// emission would change that too. The roughness gets a constant of
				// its own; the shared one keeps its value and its other wires.
				if (source && source->typeName == QLatin1String("float")) {
					int uses = 0;
					for (auto otherVar : conList)
						if (otherVar.toObject()["leftNodeId"].toString() == leftNodeId) ++uses;
					NodeModel* own = (uses > 1 && graph->library)
					                     ? graph->library->createNode("float") : nullptr;
					if (own) {
						own->deserializeWidgetValue(source->serializeWidgetValue());
						own->setX(source->getX());
						own->setY(source->getY() + 80.0);
						graph->addNode(own);
						source = own;
						leftNodeId = own->id;
						leftSockIndex = 0;
					}
				}
				if (source && source->typeName == QLatin1String("float")) {
					// A CONSTANT converts IN PLACE. The graph then holds a
					// roughness number the user can read and edit, which is
					// the point of converting instead of approximating.
					const double shininess = source->serializeWidgetValue().toDouble();
					source->deserializeWidgetValue(QJsonValue(roughnessFromLegacyShininess(shininess)));
					if (source->title == QLatin1String("Float Property")
					    || source->title == QLatin1String("Shininess"))
						source->title = QStringLiteral("Roughness");
				}
				else if (source) {
					// A CHAIN (a map, an expression) carries the inversion in a
					// real node the user can see and delete. A legacy chain fed
					// to Shininess used to be dropped as unsupported — that
					// socket had no map target at all — so this is the first
					// time one reaches the renderer.
					NodeModel* inverter = graph->library ? graph->library->createNode("oneminus") : nullptr;
					if (inverter == nullptr) {
						// no library to build one: say so rather than land a
						// gloss map on the roughness input, which is inverted
						if (!droppedLegacySockets.contains(QStringLiteral("Shininess")))
							droppedLegacySockets << QStringLiteral("Shininess");
						continue;
					}
					// A TEXTURE NODE'S OUT 0 IS A REFERENCE, NOT A SAMPLE
					// (bakeprogram.cpp D-2 option B1): it can only carry into a
					// map binding, and feeding it to maths folds the whole
					// chain to a constant, silently. Out 1 (RGBA) is the
					// sample, which is what an inversion needs.
					if (source->typeName == QLatin1String("texture") && leftSockIndex == 0
					    && source->outSockets.size() > 1)
						leftSockIndex = 1;
					inverter->setX((source->getX() + graph->masterNode->getX()) / 2.0);
					inverter->setY((source->getY() + graph->masterNode->getY()) / 2.0);
					graph->addNode(inverter);
					graph->addConnection(leftNodeId, leftSockIndex, inverter->id, 0);
					leftNodeId = inverter->id;
					leftSockIndex = 0;
				}
			}
			rightSockIndex = target;
		}

		graph->addConnection(leftNodeId, leftSockIndex, rightNodeId, rightSockIndex);
	}

	// ONE line for the user, whatever the graph carried (bakeInfo reports the
	// list; the log carries it for a headless run).
	if (legacyMasterConverted) {
		QString note = QStringLiteral(
		    "This material was authored on the old \"Surface Material\" (Blinn-Phong) master and "
		    "has been converted to \"PBR Material\": Diffuse became Base Color, Shininess became "
		    "Roughness, Emission became Emissive. Metallic is 0 until you set it.");
		if (!droppedLegacySockets.isEmpty())
			note += QStringLiteral(" No PBR equivalent exists for %1, so %2 disconnected; the "
			                       "nodes that fed %3 are still in the graph.")
			            .arg(droppedLegacySockets.join(QStringLiteral(" and ")),
			                 droppedLegacySockets.size() == 1 ? QStringLiteral("it was")
			                                                  : QStringLiteral("they were"),
			                 droppedLegacySockets.size() == 1 ? QStringLiteral("it")
			                                                  : QStringLiteral("them"));
		graph->migrationNotes << note;
		qWarning().noquote() << "NodeGraph:" << note;
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
		break;
	case BlendMode::Glass:
		blendType = "Glass";
		break;
	case BlendMode::Refractive:
		blendType = "Refractive";
	}

	// The eight inert keys (zWrite/depthTest/fog/castShadow/receiveShadow/
	// acceptLighting/cullMode/renderLayer) are no longer WRITTEN. Old files
	// keep carrying them and are still read fine — deserialize just ignores
	// names it no longer has fields for.
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
		if (mode == "blend" || mode == "translucent") return BlendMode::Translucent;
		if (mode == "additive") return BlendMode::Additive;
		if (mode == "modulate") return BlendMode::Modulate;
		if (mode == "glass") return BlendMode::Glass;
		if (mode == "refractive") return BlendMode::Refractive;
		return BlendMode::Opaque;
	};
	MaterialSettings settings;
	settings.name = obj["name"].toString();
	settings.blendMode = getBlendmode(obj);
	// absent in graphs saved before the Materials Evaluator program
	settings.bakeResolution = qBound(128, obj["bakeResolution"].toInt(1024), 4096);

	return settings;
}

void NodeGraph::setMaterialSettings(MaterialSettings setting)
{
	this->settings = setting;
}

NodeGraph::~NodeGraph()
{
	// EVERY NODE, EVERY CONNECTION, EVERY LEGACY PROPERTY (MATERIALS_TABS_SPEC
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
	qDeleteAll(properties);
	properties.clear();
}
