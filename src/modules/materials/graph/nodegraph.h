#ifndef NODEGRAPH2_H
#define NODEGRAPH2_H

#include <QObject>
#include <QVector>
#include <QString>
#include <QMap>
#include <QUuid>
#include <QJsonValue>
#include <QJsonObject>
#include <functional>
#include "../models/properties.h"

class NodeModel;
class ConnectionModel;
class NodeLibrary;

// Unreal-order blend modes for the master material (serialized as STRINGS in
// serializeMaterialSettings, so the numeric order can match the settings-view
// combo). Opaque keeps the baker's auto rules (a connected cutoff -> Masked, a
// baked alpha chain -> Translucent); the others force the material's alphaMode.
enum class BlendMode {
	Opaque,
	Masked,       // alpha cutout (material alphaMode 1)
	Translucent,  // plain alpha blend — the mode formerly named "Blend" (alphaMode 2)
	Additive,     // Final = Src + Dest (alphaMode 4)
	Modulate,     // Final = Src × Dest (alphaMode 5)
};

enum class CullMode {
	Front,
	Back,
	None,
};

enum class RenderLayer {
	Opaque,
	AlphaTested,
	Transparent,
	Overlay,
};

struct MaterialSettings {
	QString name = "";
	bool zwrite = true;
	bool depthTest = true;
	bool fog = true;
	bool castShadow = true;
	bool receiveShadow = true;
	bool acceptLighting = true;
	BlendMode blendMode = BlendMode::Opaque;
	CullMode cullMode = CullMode::Back;
	RenderLayer renderLayer = RenderLayer::Opaque;
	// Final bake resolution for this material's UV-varying chains
	// (MATERIALS_EVALUATOR_SPEC section 2); previews always bake 256.
	int bakeResolution = 1024;
};

class NodeGraph
{
public:
	QMap<QString, NodeModel*> nodes;
	QMap<QString, ConnectionModel*> connections;
	NodeModel* masterNode = nullptr;
	// Legacy graph-global uniform parameters. Since the §3b migration these
	// are READ (old files stay loadable forever; the values fold into real
	// nodes at load time) but never written back — serialize() stops
	// emitting "properties".
	QVector<Property*> properties;
	// §3b migration record: id of every node that replaced a PropertyNode
	// instance at load time -> the property id it carried. Lets the preset
	// loader (and tools) re-target texture assignments that used to key off
	// the property list.
	QMap<QString, QString> migratedPropertyNodes;
	MaterialSettings settings;
	QString materialGuid = "";

	void addProperty(Property* prop);
	void removeProperty(Property* prop);
	Property* getPropertyByName(const QString& name);
	Property* getPropertyById(const QString& id);
	QVector<NodeModel *> getNodesByTypeName(QString name);

	//QMap<QString, std::function<NodeModel*()>> modelFactories;
	//void registerModel(QString name, std::function<NodeModel*()> factoryFunction);
	NodeLibrary* library;
	void setNodeLibrary(NodeLibrary* lib);

	void addNode(NodeModel* model);
	NodeModel* getNode(const QString& nodeId);
	QVector<ConnectionModel*> getNodeConnections(const QString& nodeId);
	void removeNode(const QString& nodeId);

	// master node must already be added as a node
	void setMasterNode(NodeModel* masterNode);
	NodeModel* getMasterNode();

	ConnectionModel* addConnection(NodeModel* leftNode, int leftSockIndex, NodeModel* rightNode, int rightSockIndex);
	ConnectionModel* addConnection(QString leftNodeId, int leftSockIndex, QString rightNodeId, int rightSockIndex);

	void removeConnection(QString connectionId);

	// gets the output node and socket for a given input node and socket
	ConnectionModel* getConnectionFromOutputNode(NodeModel* node, int socketIndex);
	QJsonObject serialize();
	static NodeGraph* deserialize(QJsonObject obj, NodeLibrary* lib);
	QJsonObject serializeMaterialSettings();
	static MaterialSettings deserializeMaterialSettings(QJsonObject obj);

	void setMaterialSettings(MaterialSettings setting);
};

#endif// NODEGRAPH2_H