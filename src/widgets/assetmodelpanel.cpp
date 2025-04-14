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
#include <QMessageBox>

#include "irisgl/src/core/irisutils.h"

#include "constants.h"
#include "core/materialpreset.h"
#include "core/guidmanager.h"
#include "io/materialpresetreader.h"
#include "mainwindow.h"

AssetModelPanel::AssetModelPanel(QWidget *parent) : AssetPanel(parent)
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
    layout->setContentsMargins(0, 0, 0, 0);

    setLayout(layout);
    listView->setContextMenuPolicy(Qt::CustomContextMenu);
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(listView,   SIGNAL(customContextMenuRequested(const QPoint&)),
            this,       SLOT(showContextMenu(const QPoint&)));

    connect(listView, SIGNAL(doubleClicked(QModelIndex)), SLOT(addObjectToScene(QModelIndex)));

    setStyleSheet(
        "QListWidget { padding: 4px; border: 0; background: #202020; }"
    );

    addDefaultItems();
    addFavorites();
}

AssetModelPanel::~AssetModelPanel()
{
}

void AssetModelPanel::addDefaultItems()
{
    defaultModels.append(DefaultModel {
        "Plane", ":/content/primitives/plane.obj", "app/modelpresets/plane.png"
    });

    defaultModels.append(DefaultModel {
        "Cube", ":/content/primitives/cube.obj", "app/modelpresets/cube.png"
    });
    
    defaultModels.append(DefaultModel {
        "Cylinder", ":/content/primitives/cylinder.obj", "app/modelpresets/cylinder.png"
    });

    defaultModels.append(DefaultModel {
        "Sphere", ":/content/primitives/sphere.obj", "app/modelpresets/sphere.png"
    });

    defaultModels.append(DefaultModel {
        "Torus", ":/content/primitives/torus.obj", "app/modelpresets/torus.png"
    });

    defaultModels.append(DefaultModel {
        "Pyramid", ":/content/primitives/pyramid.obj", "app/modelpresets/pyramid.png"
    });

    defaultModels.append(DefaultModel {
        "Capsule", ":/content/primitives/capsule.obj", "app/modelpresets/capsule.png"
    });

    defaultModels.append(DefaultModel {
        "Cone", ":/content/primitives/cone.obj", "app/modelpresets/cone.png"
    });

    defaultModels.append(DefaultModel {
        "Gear", ":/content/primitives/gear.obj", "app/modelpresets/gear.png"
    });

    defaultModels.append(DefaultModel {
        "Steps", ":/content/primitives/steps.obj", "app/modelpresets/steps.png"
    });

    defaultModels.append(DefaultModel {
        "Teapot", ":/content/primitives/teapot.obj", "app/modelpresets/teapot.png"
    });

    defaultModels.append(DefaultModel {
        "Sponge", ":/content/primitives/sponge.obj", "app/modelpresets/sponge.png"
    });

    for (const auto &object : defaultModels) {
        auto item = new QListWidgetItem;
        item->setData(Qt::DisplayRole, object.objectName);
        item->setData(Qt::UserRole, object.objectName);

        item->setData(MODEL_TYPE_ROLE, static_cast<int>(ModelTypes::Object));
        item->setData(MODEL_MESH_ROLE, object.meshPath);
        item->setData(MODEL_GUID_ROLE, Constants::Reserved::DefaultPrimitives.key(object.objectName));

        item->setIcon(QIcon(IrisUtils::getAbsoluteAssetPath(object.objectIcon)));

        listView->addItem(item);
    }
}

void AssetModelPanel::addDefaultItem(const AssetRecord &asset)
{

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
    handle->addFavorite(itemInc->data(MODEL_GUID_ROLE).toString());
}

void AssetModelPanel::addFavorites()
{
    populateFavorites();

    for (const auto &asset : favoriteAssets) {
        if (asset.type == static_cast<int>(ModelTypes::Object)) {
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
}

bool AssetModelPanel::eventFilter(QObject *watched, QEvent *event)
{
    QPoint startPos;

    if (watched == listView->viewport()) {
        switch (event->type()) {
            case QEvent::ContextMenu: {
                auto evt = static_cast<QContextMenuEvent*>(event);
                AssetModelPanel::contextMenuEvent(evt);
                break;
            }

            case QEvent::MouseButtonPress: {
                auto evt = static_cast<QMouseEvent*>(event);
                if (evt->button() == Qt::LeftButton) {
                    startPos = evt->pos();
                    QModelIndex index = listView->indexAt(evt->pos());
                }

                AssetModelPanel::mousePressEvent(evt);
                break;
            }

            case QEvent::MouseButtonRelease: {
                auto evt = static_cast<QMouseEvent*>(event);
                AssetModelPanel::mouseReleaseEvent(evt);
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
                            drag->setPixmap(item->icon().pixmap(64, 64));
                            drag->exec();
                        }
                    }
                }

                AssetModelPanel::mouseMoveEvent(evt);
                break;
            }

            default: break;
        }
    }

    return QObject::eventFilter(watched, event);
}

void AssetModelPanel::addObjectToScene(QModelIndex itemIndex)
{
    if (mainWindow == Q_NULLPTR) return;
    auto item = listView->item(itemIndex.row());
    auto text = item->text();

    if (text == "Plane")    mainWindow->addPlane();
    if (text == "Cone")     mainWindow->addCone();
    if (text == "Cube")     mainWindow->addCube();
    if (text == "Cylinder") mainWindow->addCylinder();
    if (text == "Sphere")   mainWindow->addSphere();
    if (text == "Torus")    mainWindow->addTorus();
    if (text == "Capsule")  mainWindow->addCapsule();
    if (text == "Gear")     mainWindow->addGear();
    if (text == "Pyramid")  mainWindow->addPyramid();
    if (text == "Teapot")   mainWindow->addTeapot();
    if (text == "Sponge")   mainWindow->addSponge();
    if (text == "Steps")    mainWindow->addSteps();
}

void AssetModelPanel::removeFavorite(const QString &assetGuid)
{
    handle->removeFavorite(assetGuid);
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

        auto option = QMessageBox::question(this,
            "Deleting Favorite",
            "Are you sure you want to delete this favorite?",
            QMessageBox::Yes | QMessageBox::Cancel);

        if (option == QMessageBox::Yes) {
            auto item = listView->itemAt(pos);
            removeFavorite(item->data(MODEL_GUID_ROLE).toString());
            listView->removeItemWidget(item);
            delete item;
        }
    });

    contextMenu.addAction(&action);
    contextMenu.exec(mapToGlobal(pos));
}
