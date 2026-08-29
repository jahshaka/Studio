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

enum class BlendMode {
	Opaque ,
	Blend,
	Additive,
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
};

class ModelContext
{
};

class NodeGraph
{
public:
	QMap<QString, NodeModel*> nodes;
	QMap<QString, ConnectionModel*> connections;
	NodeModel* masterNode = nullptr;
	QVector<Property*> properties;
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