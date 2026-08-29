/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/
#include <QUuid>
#include <QString>
#include <QColor>

#include "nodemodel.h"
#include "socketmodel.h"
#include "../core/guidhelper.h"

NodeModel::NodeModel()
{
	id = GuidHelper::createGuid();
	widget = nullptr;
	connect(this, &NodeModel::titleColorChanged, [=]() {
		setNodeTitleColor();
	});

	enablePreview = false;

	x = 0;
	y = 0;
}

void NodeModel::updateStyle()
{
	widget->setStyleSheet("QMenu{	background: rgba(26,26,26,.9); color: rgba(250,250, 250,.9);}"
		"QMenu::item{padding: 2px 5px 2px 20px;	}"
		"QMenu::item:hover{	background: rgba(40,128, 185,.9);}"
		"QMenu::item:selected{	background: rgba(40,128, 185,.9);}"
		"QCheckBox {   spacing: 2px 5px;}"
		"QCheckBox::indicator {   width: 28px;   height: 28px; }"
		"QCheckBox::indicator::unchecked {	image: url(:/icons/check-unchecked.png);}"
		"QCheckBox::indicator::checked {		image: url(:/icons/check-checked.png);}"
		"QLineEdit {	border: 0;	background: #292929;	padding: 6px;	margin: 0;}"
		"QToolButton {	background: #1E1E1E;	border: 0;	padding: 6px;}"
		"QToolButton:pressed {	background: #111;}"
		"QToolButton:hover {	background: #404040;}"
		"QDoubleSpinBox, QSpinBox {	border-radius: 1px;	padding: 6px;	background: #292929;}"
		"QDoubleSpinBox::up-arrow, QSpinBox::up-arrow {	width:0;}"
		"QDoubleSpinBox::up-button, QSpinBox::up-button, QDoubleSpinBox::down-button, QSpinBox::down-button {	width:0;}"
		"QListView::item:selected {    background: #404040;}"
		"QComboBox:editable {}"
		"QComboBox QAbstractItemView::item {    show-decoration-selected: 1;}"
		"QComboBox QAbstractItemView::item {    padding: 6px;}"
		"QComboBox  {    background: rgba(0,0,0,0);   border: 2px solid rgba(0,0,0,.4);    outline: none; padding: 6px 10px; color: rgba(250,250,250,1);}"
		//	"QComboBox:!editable, QComboBox::drop-down:editable {     background: #1A1A1A;}"
		//	"QComboBox:!editable:on, QComboBox::drop-down:editable:on {    background: #1A1A1A;}"
		"QComboBox QAbstractItemView { background: rgba(0,0,0,.2);  color: rgba(250,250,250,1);  selection-background-color: #404040; border: 2px solid rgba(0,0,0,.4); outline: none; padding: 4px 10px; }"
		"QComboBox QAbstractItemView::item {    border: none; padding: 4px 10px;}"
		"QComboBox QAbstractItemView::item:selected {    background: #404040;    padding-left: 5px;}"
		"QComboBox::drop-down {    subcontrol-origin: padding;    subcontrol-position: top right;    width: 18px;    border-left-width: 1px;}"
		"QComboBox::down-arrow {    image: url(:/icons/down_arrow_check.png);	width: 18px;	height: 14px;} "
		"QComboBox::down-arrow:!enabled {    image: url(:/icons/down_arrow_check_disabled.png);    width: 18px;    height: 14px;}"
		"QLabel{}"
		"QPushButton{padding : 3px; }"
	);
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

QString NodeModel::getValueFromInputSocket(int index)
{
	auto sock = inSockets[index];
	if (sock->hasConnection()) {
		//return sock->getConnectedSocket()->getVarName();

		// converts the var before sending it back
		return sock->getConnectedSocket()->convertVarTo(sock);
	}

	return sock->getValue();
}

QString NodeModel::getOutputSocketVarName(int index)
{
	auto sock = outSockets[index];
	return sock->getVarName();
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
	case NodeCategory::Properties:
		icon.addPixmap({ ":/icons/properties.png" });
		return titleColor = QColor(230, 74, 25);
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
	case NodeCategory::Properties:
		return "Properties";
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
	case NodeCategory::Properties:
		iconPath = ":/icons/property.png";
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