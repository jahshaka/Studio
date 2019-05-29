/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "projectmaterialpanel.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QFileInfoList>
#include <QPointer>
#include <QMessageBox>
#include <QVBoxLayout>

#include "irisgl/src/core/irisutils.h"
#include "irisgl/src/materials/custommaterial.h"

#include "constants.h"
#include "core/guidmanager.h"
#include "io/materialpresetreader.h"
#include "io/assetmanager.h"
#include "mainwindow.h"
#include "../src/shadergraph/core/materialhelper.h"
#include "misc/stylesheet.h"

#include "io/scenewriter.h"

ProjectMaterialPanel::ProjectMaterialPanel(Database* handle, QWidget * parent) : QWidget(parent)
{
	this->database = handle;
	listWidget = new ListWidget;

	auto layout = new QVBoxLayout;
	setLayout(layout);
	layout->addWidget(listWidget);

	listWidget->setContextMenuPolicy(Qt::CustomContextMenu);
	listWidget->setResizeMode(QListWidget::Adjust);
	listWidget->setMovement(QListView::Static);
	listWidget->setSelectionBehavior(QAbstractItemView::SelectItems);
	listWidget->setSelectionMode(QAbstractItemView::ExtendedSelection);
	listWidget->setSelectionRectVisible(false);
	listWidget->setDragEnabled(true);
	listWidget->setDragDropMode(QAbstractItemView::DragDrop);
	listWidget->setItemDelegate(new ListViewDelegate());
	listWidget->setTextElideMode(Qt::ElideRight);
	listWidget->setGridSize({ 120,120 });

	listWidget->viewport()->installEventFilter(this);
	listWidget->setStyleSheet(StyleSheet::QListWidget());
}

ProjectMaterialPanel::~ProjectMaterialPanel()
{
}

void ProjectMaterialPanel::addItem(const AssetRecord& assetData)
{
	QListWidgetItem* item = new QListWidgetItem;
	item->setData(Qt::DisplayRole, QFileInfo(assetData.name).baseName());
	item->setData(Qt::UserRole, assetData.name);
	item->setData(MODEL_TYPE_ROLE, assetData.type);
	item->setData(MODEL_ITEM_TYPE, MODEL_ASSET);
	item->setData(MODEL_GUID_ROLE, assetData.guid);
	item->setData(MODEL_PARENT_ROLE, assetData.parent);

	QPixmap thumbnail;
	if (thumbnail.loadFromData(assetData.thumbnail, "PNG")) {
		item->setIcon(QIcon(thumbnail));
	}
	else {
		item->setIcon(QIcon(":/icons/empty_object.png"));
	}
	item->setTextAlignment(Qt::AlignCenter);
	item->setFlags(item->flags() | Qt::ItemIsEditable);

	listWidget->addItem(item);

}

void ProjectMaterialPanel::refresh()
{
	listWidget->clear();
	for (const auto& asset : database->fetchChildAssets(Globals::project->getProjectGuid(),-1,false)) {
		if(asset.type == static_cast<int>(ModelTypes::Material))	addItem(asset);
	}
}

bool ProjectMaterialPanel::eventFilter(QObject* watched, QEvent* event)
{
	if (watched == listWidget->viewport()) {
		switch (event->type()) {
		case QEvent::MouseButtonPress: {
			auto evt = static_cast<QMouseEvent*>(event);
			if (evt->button() == Qt::LeftButton) {
				startPos = evt->pos();
				QModelIndex index = listWidget->indexAt(evt->pos());
				if (index.isValid()) draggingItem = true;
			}

			ProjectMaterialPanel::mousePressEvent(evt);
			break;
		}

		case QEvent::MouseButtonRelease: {
			auto evt = static_cast<QMouseEvent*>(event);
			draggingItem = false;
			emit assetItemSelected(nullptr);
			ProjectMaterialPanel::mouseReleaseEvent(evt);
			break;
		}

		case QEvent::MouseMove: {
			auto evt = static_cast<QMouseEvent*>(event);
			if (evt->buttons() & Qt::LeftButton) {
				if (draggingItem) {
					int distance = (evt->pos() - startPos).manhattanLength();
					if (distance >= QApplication::startDragDistance()) {
						auto item = listWidget->currentItem();

						if (item) {
							auto drag = QPointer<QDrag>(new QDrag(this));
							auto mimeData = QPointer<QMimeData>(new QMimeData);

							QByteArray mdata;
							QDataStream stream(&mdata, QIODevice::WriteOnly);
							QMap<int, QVariant> roleDataMap;

							roleDataMap[0] = QVariant(item->data(MODEL_TYPE_ROLE).toInt());
							roleDataMap[1] = QVariant(item->data(Qt::UserRole).toString());
							roleDataMap[2] = QVariant(item->data(MODEL_MESH_ROLE).toString());
							roleDataMap[3] = QVariant(item->data(MODEL_GUID_ROLE).toString());

							stream << roleDataMap;

							mimeData->setData(QString("application/x-qabstractitemmodeldatalist"), mdata);
							drag->setMimeData(mimeData);

							drag->setPixmap(item->icon().pixmap(64, 64));
							drag->exec();
						}
					}
				}
			}

			ProjectMaterialPanel::mouseMoveEvent(evt);
			break;
		}

		default: break;
		}
	}

	return QObject::eventFilter(watched, event);
}
