/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/
#include "treewidget.h"
#include "src/core/project.h"
#include <QHeaderView>
#include <QScrollBar>

TreeWidget::TreeWidget() : QTreeWidget()
{
	setStyleSheet(
		"QTreeWidget { outline: none; selection-background-color: #404040; color: #EEE; }"
		"QTreeWidget::branch { background-color:rgba(0,0,0,0); }"
		"QTreeWidget::branch:hover { background-color: #303030; }"
		"QTreeView::branch:open {background-color:rgba(0,0,0,0); image: url(:/icons/expand_arrow_open.png); }"
		"QTreeView::branch:closed:has-children { background-color:rgba(0,0,0,0);image: url(:/icons/expand_arrow_closed.png); }"
		"QTreeWidget::branch:selected { background-color: #404040; }"
		"QTreeWidget::item:selected { selection-background-color: #404040;"
		"								background-color:rgba(0,0,0,0); outline: none; padding: 5px 0; }"
		/* Important, this is set for when the widget loses focus to fill the left gap */
		"QTreeWidget::item:selected:!active { background: #404040; padding: 5px 0; color: #EEE; }"
		"QTreeWidget::item:selected:active { background: #404040; padding: 5px 0; }"
		"QTreeWidget::item {background-color:rgba(0,0,0,0); padding: 5px 0; height : 10px; }"
		"QTreeWidget::item:hover { background: #303030; padding: 5px 0; }"
	);
	verticalScrollBar()->setStyleSheet(
		"QScrollBar:vertical {border : 0px solid black;	background: rgba(132, 132, 132, 0);width: 10px; }"
		"QScrollBar::handle{ background: rgba(62, 62, 62, 1);	border-radius: 4px;  left: 8px; }"
		"QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {	background: rgba(200, 200, 200, 0);}"
		"QScrollBar::up-arrow:vertical, QScrollBar::down-arrow:vertical {	background: rgba(0, 0, 0, 0);border: 0px solid white;}"
		"QScrollBar::sub-line, QScrollBar::add-line {	background: rgba(10, 0, 0, .0);}"

	);

	setColumnCount(1);
	setHeaderLabel("Nodes");
	setRootIsDecorated(true);
	setDragEnabled(true);
	setHeaderHidden(true);

	setAlternatingRowColors(false);
	setContentsMargins(10, 3, 10, 10);
	setMouseTracking(true);
	setDragDropMode(QAbstractItemView::DragDrop);
	setDefaultDropAction(Qt::CopyAction);
	setSelectionMode(QAbstractItemView::SingleSelection);
	setDragEnabled(true);
	viewport()->setAcceptDrops(true);
	setAcceptDrops(true);
	setDropIndicatorShown(true);
	setWordWrap(true);
	setSortingEnabled(true);
	setEditTriggers(QAbstractItemView::EditKeyPressed);
	setContextMenuPolicy(Qt::CustomContextMenu);
}

TreeWidget::~TreeWidget()
{
}

QMimeData * TreeWidget::mimeData(const QList<QTreeWidgetItem*> items) const
{
	QMimeData *data = new QMimeData();
	//set text for shader
	data->setText(items[0]->data(0, Qt::DisplayRole).toString());
	//set html for node
	data->setHtml(items[0]->data(0, Qt::UserRole).toString());
	data->setData("MODEL_TYPE_ROLE", items[0]->data(0, MODEL_TYPE_ROLE).toByteArray());
	data->setData("MODEL_GUID_ROLE", items[0]->data(0, MODEL_GUID_ROLE).toByteArray());

	if (!items[0]->data(0, MODEL_EXT_ROLE).isNull()) data->setData("index", items[0]->data(0, MODEL_EXT_ROLE).toByteArray());
	return data;
}
