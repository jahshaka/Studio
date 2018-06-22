/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "assetmodelpanel.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QFileInfoList>
#include <QPointer>

#include "irisgl/src/core/irisutils.h"

#include "core/materialpreset.h"
#include "io/materialpresetreader.h"
#include "mainwindow.h"

AssetModelPanel::AssetModelPanel(QWidget *parent) : QWidget(parent)
{
    mainWindow = nullptr;

    listView = new QListWidget;
    listView->setViewMode(QListWidget::IconMode);
    listView->setIconSize(QSize(64, 64));
    listView->setResizeMode(QListWidget::Adjust);
    listView->setMovement(QListView::Static);
    listView->setSelectionBehavior(QAbstractItemView::SelectItems);
    listView->setSelectionMode(QAbstractItemView::SingleSelection);
    listView->setSpacing(4);

    installEventFilter(this);
    listView->installEventFilter(this);
    listView->viewport()->installEventFilter(this);

    listView->setAttribute(Qt::WA_MacShowFocusRect, false);
    listView->setAttribute(Qt::WA_MacShowFocusRect, false);

    setMouseTracking(true);
    listView->setDragEnabled(true);
    listView->setDragDropMode(QAbstractItemView::DragDrop);

    listView->setTextElideMode(Qt::ElideRight);

    QVBoxLayout *layout = new QVBoxLayout;
    layout->addWidget(listView);
    layout->setMargin(0);

    listView->setItemDelegate(new FMListViewDelegate);

    setLayout(layout);
    listView->setContextMenuPolicy(Qt::CustomContextMenu);
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(listView,   SIGNAL(customContextMenuRequested(const QPoint&)),
            this,       SLOT(showContextMenu(const QPoint&)));

    setStyleSheet(
        "QListWidget { padding: 4px; border: 0; background: #202020; }"
    );
}

AssetModelPanel::~AssetModelPanel()
{
}

void AssetModelPanel::addDefaultItems()
{
}

void AssetModelPanel::addDefaultItem(const AssetRecord &asset)
{
    auto item = new QListWidgetItem;
    item->setData(Qt::DisplayRole, asset.name);
    item->setData(Qt::UserRole, asset.name);

    //item->setData(MODEL_TYPE_ROLE, static_cast<int>(ModelTypes::Object));
    //item->setData(MODEL_MESH_ROLE, "");
    //item->setData(MODEL_GUID_ROLE, GUIDManager::generate);
}

void AssetModelPanel::addNewItem(QListWidgetItem *itemInc)
{
    auto asset = handle->fetchAsset(itemInc->data(MODEL_GUID_ROLE).toString());

    auto item = new QListWidgetItem;
    item->setData(Qt::DisplayRole, asset.name);
    item->setData(Qt::UserRole, asset.name);

    item->setData(MODEL_TYPE_ROLE, itemInc->data(MODEL_TYPE_ROLE).toInt());
    item->setData(MODEL_MESH_ROLE, itemInc->data(MODEL_MESH_ROLE).toString());
    item->setData(MODEL_GUID_ROLE, itemInc->data(MODEL_GUID_ROLE).toString());

    QPixmap thumbnail;
    if (thumbnail.loadFromData(asset.thumbnail, "PNG")) {
        item->setIcon(QIcon(thumbnail));
    }
    else {
        item->setIcon(QIcon(":/icons/empty_object.png"));
    }

    listView->addItem(item);

    // add this asset to the assets table and set the VIEW so we know where it belongs
    // handle->addFavorite(itemInc->data(MODEL_GUID_ROLE).toString());
}

bool AssetModelPanel::eventFilter(QObject *watched, QEvent *event)
{
    return QObject::eventFilter(watched, event);
}

void AssetModelPanel::showContextMenu(const QPoint &pos)
{
    QMenu contextMenu;
    contextMenu.setStyleSheet(
        "QMenu { background-color: #1A1A1A; color: #EEE; padding: 0; margin: 0; }"
        "QMenu::item { background-color: #1A1A1A; padding: 6px 8px; margin: 0; }"
        "QMenu::item:selected { background-color: #3498db; color: #EEE; padding: 6px 8px; margin: 0; }"
        "QMenu::item : disabled { color: #555; }"
    );

    QAction action("Remove Item", this);
    connect(&action, &QAction::triggered, this, [this, pos]() {
        QModelIndex index = listView->indexAt(pos);
        if (!index.isValid()) return;
        auto item = listView->itemAt(pos);
        listView->removeItemWidget(item);
        delete item;
    });

    contextMenu.addAction(&action);
    contextMenu.exec(mapToGlobal(pos));
}
