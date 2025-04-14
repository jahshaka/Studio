/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "assetfavorites.h"

#include <QDir>
#include <QFileInfo>
#include <QFileInfoList>
#include <QPointer>
#include <QApplication>

#include "../io/materialpresetreader.h"
#include "../core/materialpreset.h"
#include "../irisgl/src/core/irisutils.h"

#include "../mainwindow.h"

AssetFavorites::AssetFavorites(QWidget *parent) : QWidget(parent)
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
    layout->setContentsMargins(0, 0, 0, 0);

    listView->setItemDelegate(new FListViewDelegate);

    setLayout(layout);
    listView->setContextMenuPolicy(Qt::CustomContextMenu);
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(listView,   SIGNAL(customContextMenuRequested(const QPoint&)),
            this,       SLOT(showContextMenu(const QPoint&)));

    setStyleSheet(
        "QListWidget { padding: 4px; border: 0; background: #202020; }"
    );
}

AssetFavorites::~AssetFavorites()
{
    // delete ui;
    // for (auto preset : presets) delete preset;
}

void AssetFavorites::addFavorite(const QListWidgetItem *itemInc)
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

    handle->addFavorite(itemInc->data(MODEL_GUID_ROLE).toString());
}

void AssetFavorites::removeFavorite(const QString &assetGuid)
{
    handle->removeFavorite(assetGuid);
}

void AssetFavorites::populateFavorites()
{
    for (const auto &asset : favoriteAssets) {
        auto item = new QListWidgetItem;
        item->setData(Qt::DisplayRole, asset.name);
        item->setData(Qt::UserRole, asset.name);

        item->setData(MODEL_TYPE_ROLE, asset.type);
        if (asset.type == static_cast<int>(ModelTypes::Object)) {
            QString meshGuid = handle->fetchObjectMesh(asset.guid, static_cast<int>(ModelTypes::Object), static_cast<int>(ModelTypes::Mesh));
            item->setData(MODEL_MESH_ROLE, meshGuid);
        }
        item->setData(MODEL_GUID_ROLE, asset.guid);

        QPixmap thumbnail;
        if (thumbnail.loadFromData(asset.thumbnail, "PNG")) {
            item->setIcon(QIcon(thumbnail));
        }
        else {
            item->setIcon(QIcon(":/icons/empty_object.png"));
        }

        listView->addItem(item);
    }
}

bool AssetFavorites::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == listView->viewport()) {
        switch (event->type()) {
            case QEvent::ContextMenu: {
                auto evt = static_cast<QContextMenuEvent*>(event);
                AssetFavorites::contextMenuEvent(evt);
                break;
            }

            case QEvent::MouseButtonPress: {
                auto evt = static_cast<QMouseEvent*>(event);
                if (evt->button() == Qt::LeftButton) {
                    startPos = evt->pos();
                    QModelIndex index = listView->indexAt(evt->pos());
                }

                AssetFavorites::mousePressEvent(evt);
                break;
            }

            case QEvent::MouseButtonRelease: {
                auto evt = static_cast<QMouseEvent*>(event);
                AssetFavorites::mouseReleaseEvent(evt);
                break;
            }

            case QEvent::MouseMove: {
                auto evt = static_cast<QMouseEvent*>(event);
                if (evt->buttons() & Qt::LeftButton) {
                    int distance = (evt->pos() - startPos).manhattanLength();
                    if (distance >= QApplication::startDragDistance()) {
                        auto item = listView->currentItem();

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

                            // only hide for object models
                            //drag->setPixmap(QPixmap());
                            drag->exec();
                        }
                    }
                }

                AssetFavorites::mouseMoveEvent(evt);
                break;
            }

            default: break;
        }
    }

    return QObject::eventFilter(watched, event);
}

void AssetFavorites::showContextMenu(const QPoint &pos)
{
    QMenu contextMenu;
    contextMenu.setStyleSheet(
        "QMenu { background-color: #1A1A1A; color: #EEE; padding: 0; margin: 0; }"
        "QMenu::item { background-color: #1A1A1A; padding: 6px 8px; margin: 0; }"
        "QMenu::item:selected { background-color: #3498db; color: #EEE; padding: 6px 8px; margin: 0; }"
        "QMenu::item : disabled { color: #555; }"
    );

    QAction action("Remove Favorite", this);
    connect(&action, &QAction::triggered, this, [this, pos]() {
        QModelIndex index = listView->indexAt(pos);
        if (!index.isValid()) return;
        auto item = listView->itemAt(pos);
        removeFavorite(item->data(Qt::UserRole).toString());
        listView->removeItemWidget(item);
        delete item;
    });

    contextMenu.addAction(&action);
    contextMenu.exec(mapToGlobal(pos));
}
