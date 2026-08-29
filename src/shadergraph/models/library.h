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
};

class NodeLibrary
{
public:
	QVector<NodeLibraryItem*> items;

	QVector<NodeLibraryItem*> getItems();

	QVector<NodeLibraryItem*> filter(QString name);

	void addNode(QString name, QString displayName, QIcon icon, NodeCategory type, std::function<NodeModel *()> factoryFunction);
	void addNode(QString name, QString displayName, QString iconPath, NodeCategory type, std::function<NodeModel *()> factoryFunction);
	bool hasNode(QString name);

	// returns null if node factory doesnt exist
	NodeModel* createNode(QString name);

	NodeLibrary();
	~NodeLibrary();
};

#endif// LIBRARY_H