/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "assetmaterialpanel.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QFileInfoList>
#include <QPointer>

#include "irisgl/src/core/irisutils.h"

#include "core/materialpreset.h"
#include "core/guidmanager.h"
#include "io/materialpresetreader.h"
#include "mainwindow.h"

AssetMaterialPanel::AssetMaterialPanel(QWidget *parent) : AssetPanel(parent)
{
    installEventFilter(this);
    listView->installEventFilter(this);
    listView->viewport()->installEventFilter(this);

    setMouseTracking(true);
    listView->setDragEnabled(true);
    listView->setDragDropMode(QAbstractItemView::DragDrop);
    listView->setTextElideMode(Qt::ElideRight);
    listView->setItemDelegate(new FMListViewDelegate);

    QVBoxLayout *layout = new QVBoxLayout;
    layout->addWidget(listView);
    layout->setMargin(0);

    setLayout(layout);
    listView->setContextMenuPolicy(Qt::CustomContextMenu);
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(listView,   SIGNAL(customContextMenuRequested(const QPoint&)),
            this,       SLOT(showContextMenu(const QPoint&)));

    connect(listView,   SIGNAL(itemClicked(QListWidgetItem*)),
            this,       SLOT(applyMaterialPreset(QListWidgetItem*)));

    setStyleSheet(
        "QListWidget { padding: 4px; border: 0; background: #202020; }"
    );

    addDefaultItems();
}

AssetMaterialPanel::~AssetMaterialPanel()
{
}

void AssetMaterialPanel::addDefaultItems()
{
    auto dir = QDir(IrisUtils::getAbsoluteAssetPath("app/content/materials"));
    auto files = dir.entryInfoList(QStringList(), QDir::Files);

    auto reader = new MaterialPresetReader();

    for (const auto &file : files) {
        auto preset = reader->readMaterialPreset(file.absoluteFilePath());
        defaultMaterials.append(preset);
    }

    for (int i = 0; i < defaultMaterials.count(); ++i) {
        auto item = new QListWidgetItem;
        item->setData(Qt::DisplayRole, defaultMaterials[i].name);
        item->setData(Qt::UserRole, defaultMaterials[i].name);

        item->setData(MODEL_TYPE_ROLE, static_cast<int>(ModelTypes::Material));
        item->setData(MODEL_GUID_ROLE, GUIDManager::generateGUID());

        item->setIcon(QIcon(defaultMaterials[i].icon));
        item->setData(0x32, i); // used to get item index

        listView->addItem(item);
    }
}

void AssetMaterialPanel::addNewItem(QListWidgetItem *itemInc)
{
    auto asset = handle->fetchAsset(itemInc->data(MODEL_GUID_ROLE).toString());

    auto item = new QListWidgetItem;
    item->setData(Qt::DisplayRole, asset.name);
    item->setData(Qt::UserRole, asset.name);

    item->setData(MODEL_TYPE_ROLE, itemInc->data(MODEL_TYPE_ROLE).toInt());
    item->setData(MODEL_GUID_ROLE, itemInc->data(MODEL_GUID_ROLE).toString());

    QPixmap thumbnail;
    if (thumbnail.loadFromData(asset.thumbnail, "PNG")) {
        item->setIcon(QIcon(thumbnail));
    }
    else {
        item->setIcon(QIcon(":/icons/empty_object.png"));
    }

    item->setData(0x32, defaultMaterials.count() - 1);

    listView->addItem(item);
    
    
    // add this asset to the assets table and set the VIEW so we know where it belongs
    // handle->addFavorite(itemInc->data(MODEL_GUID_ROLE).toString());
}

bool AssetMaterialPanel::eventFilter(QObject *watched, QEvent *event)
{
    return QObject::eventFilter(watched, event);
}

void AssetMaterialPanel::showContextMenu(const QPoint &pos)
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

void AssetMaterialPanel::applyMaterialPreset(QListWidgetItem *item)
{
    if (!mainWindow) return;
    auto preset = defaultMaterials[item->data(0x32).toInt()];
    mainWindow->applyMaterialPreset(preset);
}
