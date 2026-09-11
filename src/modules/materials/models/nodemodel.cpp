/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#include <QUuid>
#include <QString>
#include <QColor>

#include "nodemodel.h"
#include "socketmodel.h"
#include "../core/guidhelper.h"
#include "ui/style/stylesheet.h"

NodeModel::NodeModel()
{
	id = GuidHelper::createGuid();
	widget = nullptr;
	headerWidget = nullptr;
	connect(this, &NodeModel::titleColorChanged, [=]() {
		setNodeTitleColor();
	});

	enablePreview = false;

	x = 0;
	y = 0;
}

void NodeModel::updateStyle()
{
	widget->setStyleSheet(StyleSheet::MaterialsNodeMenu());
}

void NodeModel::addInputSocket(SocketModel *sock)
{
	inSockets.append(sock);
	sock->setNode(this);
}

void NodeModel::addOutputSocket(SocketModel *sock)
{
	outSockets.append(sock);
	sock->setNode(this);
}

void NodeModel::setWidget(QWidget * wid)
{
	widget = wid;
	updateStyle();
}

NodeGraph *NodeModel::getGraph() const
{
	return graph;
}

void NodeModel::setGraph(NodeGraph *value)
{
	graph = value;
}

QColor NodeModel::setNodeTitleColor()
{
	switch (nodeType) {
	case NodeCategory::Input:
		icon.addPixmap({":/icons/input.png"	});
		return titleColor = QColor(0, 121, 107);
		break;
	case NodeCategory::Math:
		icon.addPixmap({ ":/icons/math.png" });
		return titleColor = QColor(25,118,210);
		break;
	case NodeCategory::Constants:
		icon.addPixmap({ ":/icons/constant.png" });
		return titleColor = QColor(150, 24, 35);
		break;
	default:
		return titleColor = QColor(0, 0, 0, 0);
		break;
	}

}

QString NodeModel::getEnumString(NodeCategory type) {

	switch (type) {
	case NodeCategory::Input:
		return "Input";
	case NodeCategory::Math:
		return "Math";
	case NodeCategory::Constants:
		return "Constants";
	case NodeCategory::Object:
		return "Object";
	case NodeCategory::Texture:
		return "Texture";
	case NodeCategory::Vector:
		return "Vector";
	case NodeCategory::Utility:
		return "Utility";
	default:
		return "";
	}
}

void NodeModel::setNodeType(NodeCategory type)
{
	nodeType = type;
	emit titleColorChanged();
	switch (type) {
	case NodeCategory::Input:
		iconPath = ":/icons/input.png";
		icon = QIcon(iconPath);
		return;
	case NodeCategory::Math:
		iconPath = ":/icons/math.png";
		icon = QIcon(iconPath);
		return;
	case NodeCategory::Constants:
		iconPath = ":/icons/constant.png";
		icon = QIcon(iconPath);
		return;
	case NodeCategory::Object:
		iconPath = ":/icons/object.png";
		icon = QIcon(iconPath);
		return;
	case NodeCategory::Texture:
		iconPath = ":/icons/texture.png";
		icon = QIcon(iconPath);
		return;
	case NodeCategory::Vector:
		iconPath = ":/icons/vector.png";
		icon = QIcon(iconPath);
		return;
	default:
		return;
	}
}

SocketModel* NodeModel::getSocketById(const QString& sockId)
{
	for (auto sock : inSockets)
		if (sock->id == sockId)
			return sock;

	for (auto sock : outSockets)
		if (sock->id == sockId)
			return sock;

	return nullptr;
}