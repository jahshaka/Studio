#ifndef LIBRARY_H
#define LIBRARY_H

#include <QString>
#include <QVector>
#include <QIcon>
#include <functional>

class NodeModel;
enum class NodeCategory;
struct NodeLibraryItem
{
	QString name;
	QString displayName;
	QIcon icon;
	NodeCategory nodeCategory;
	std::function<NodeModel *()> factoryFunction;
	/// A LOAD ALIAS, not a palette entry (MATERIAL_UV_NODES_SPEC D-3).
	/// `texCoords` and `uvTransform` merged into the single `uv` node; both
	/// old typeNames must still CONSTRUCT — saved graphs, shipped presets and
	/// a decade of scripts name them — but neither may appear in the drawer,
	/// the tab-search or `graph.nodeTypes()`, because there is one node now.
	bool hidden = false;
};

class NodeLibrary
{
public:
	QVector<NodeLibraryItem*> items;

	QVector<NodeLibraryItem*> getItems();

	QVector<NodeLibraryItem*> filter(QString name);

	void addNode(QString name, QString displayName, QIcon icon, NodeCategory type, std::function<NodeModel *()> factoryFunction);
	void addNode(QString name, QString displayName, QString iconPath, NodeCategory type, std::function<NodeModel *()> factoryFunction);
	/// Registers `name` as a hidden constructor for the node already registered
	/// as `existingName`: same factory, same category, same display name (so a
	/// loaded alias node is titled like the node it became), invisible to every
	/// palette. Returns false if `existingName` is not registered.
	bool addAlias(QString name, QString existingName);
	bool hasNode(QString name);

	// returns null if node factory doesnt exist
	NodeModel* createNode(QString name);

	NodeLibrary();
	~NodeLibrary();
};

#endif// LIBRARY_H