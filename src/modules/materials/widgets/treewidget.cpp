/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#include "treewidget.h"
#include "data/project.h"
#include <QHeaderView>
#include <QScrollBar>
#include "ui/style/stylesheet.h"

TreeWidget::TreeWidget() : QTreeWidget()
{
	setStyleSheet(StyleSheet::MaterialsTree());
	verticalScrollBar()->setStyleSheet(StyleSheet::MaterialsTreeScrollBar());

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
