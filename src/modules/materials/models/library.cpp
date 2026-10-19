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
	// Hidden entries are load aliases (NodeLibraryItem::hidden) — constructible
	// by name, never listed. Every palette goes through here or filter().
	QVector<NodeLibraryItem*> visible;
	for (auto item : mView)
		if (!item->hidden) visible.append(item);
	return visible;
}

QVector<NodeLibraryItem*> NodeLibrary::filter(QString name)
{
	QVector<NodeLibraryItem*> filtered;

	for (auto item : mView) {
		if (item->hidden) continue;
		if (item->displayName.toLower().contains(name))
			filtered.append(item);
	}

	return filtered;
}

void NodeLibrary::addNode(QString name, QString displayName, QIcon icon, NodeCategory type, std::function<NodeModel *()> factoryFunction)
{
	add(std::make_unique<NodeLibraryItem>(NodeLibraryItem{ name, displayName, icon, type, factoryFunction }));
}

void NodeLibrary::addNode(QString name, QString displayName, QString iconPath, NodeCategory type, std::function<NodeModel *()> factoryFunction)
{
	add(std::make_unique<NodeLibraryItem>(NodeLibraryItem{ name, displayName, QIcon(iconPath), type, factoryFunction }));
}

bool NodeLibrary::addAlias(QString name, QString existingName)
{
	for (auto item : mView) {
		if (item->name != existingName) continue;
		add(std::make_unique<NodeLibraryItem>(NodeLibraryItem{ name, item->displayName, item->icon,
		                                                       item->nodeCategory, item->factoryFunction, true }));
		return true;
	}
	return false;
}

bool NodeLibrary::hasNode(QString name)
{
	for (auto item : mView)
		if (item->name == name)
			return true;
	return false;
}

NodeModel* NodeLibrary::createNode(QString name)
{
	for (auto item : mView)
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

NodeLibrary::~NodeLibrary() = default;

void NodeLibrary::add(std::unique_ptr<NodeLibraryItem> item)
{
	mView.append(item.get());
	mOwned.push_back(std::move(item));
}