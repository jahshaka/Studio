/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#include "library.h"
#include "nodemodel.h"

QVector<NodeLibraryItem*> NodeLibrary::getItems()
{
	return items;
}

QVector<NodeLibraryItem*> NodeLibrary::filter(QString name)
{
	QVector<NodeLibraryItem*> filtered;

	for (auto item : items) {
		if (item->displayName.toLower().contains(name))
			filtered.append(item);
	}

	return filtered;
}

void NodeLibrary::addNode(QString name, QString displayName, QIcon icon, NodeCategory type, std::function<NodeModel *()> factoryFunction)
{
	items.append(new NodeLibraryItem{ name, displayName, icon , type, factoryFunction });
}

void NodeLibrary::addNode(QString name, QString displayName, QString iconPath, NodeCategory type, std::function<NodeModel *()> factoryFunction)
{
	items.append(new NodeLibraryItem{ name, displayName, QIcon(iconPath) ,type, factoryFunction });
}

bool NodeLibrary::hasNode(QString name)
{
	for (auto item : items)
		if (item->name == name)
			return true;
	return false;
}

NodeModel* NodeLibrary::createNode(QString name)
{
	for (auto item : items)
		if (item->name == name) {
			auto node = item->factoryFunction();
			node->setNodeType(item->nodeCategory);
			// the drawer's display name IS the node's title, verbatim —
			// constructors may not drift from it, and nothing renames a
			// node after creation (property nodes take the property's
			// user-given name in setProperty, which is their drawer name)
			node->title = item->displayName;
			return node;
		}
	return nullptr;
}

NodeLibrary::NodeLibrary()
{

}

NodeLibrary::~NodeLibrary()
{

}