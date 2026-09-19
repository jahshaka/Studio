/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/panels/presets/assetmodelpanel.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QFileInfoList>
#include <QPointer>
#include <QMessageBox>

#include "irisgl/core/irisutils.h"

#include "data/constants.h"
#include "data/primitives.h"
#include "data/materialpreset.h"
#include "data/guidmanager.h"
#include "io/materialpresetreader.h"
#include "shell/mainwindow.h"
#include "ui/panels/singledragowner.h"
#include "services/sceneeditservice.h"
#include "services/services.h"
#include "ui/style/stylesheet.h"
#include "ui/controls/assetdrag.h"

AssetModelPanel::AssetModelPanel(QWidget *parent) : AssetPanel(parent)
{
    installEventFilter(this);
    listView->installEventFilter(this);
    listView->viewport()->installEventFilter(this);

    setMouseTracking(true);
    listView->setDragDropMode(QAbstractItemView::DragDrop);
    // ONE DRAG OWNER (ui/panels/singledragowner.h). This panel's eventFilter
    // starts the QDrag; the view's own machinery must stay disarmed or the
    // gesture is owned twice — the double-add AND the sticky item that
    // survives the drag. LAST, because setDragDropMode arms dragEnabled.
    singledrag::disarmViewDrag(listView);
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

    setStyleSheet(StyleSheet::PresetsListPanel());

    // The starter tiles only. The FAVOURITES need the database, which arrives
    // with setDatabaseHandle after this returns (CLOSE-2).
    addDefaultItems();
}

AssetModelPanel::~AssetModelPanel()
{
}

void AssetModelPanel::addDefaultItems()
{
    // THE TILES ARE THE TABLE (owner review R6, src/data/primitives.h). This
    // was a THIRD copy of the primitive list — its own order, its own icon
    // paths, Pyramid appended out of order, Ground absent, and the guid looked
    // up by reverse name search through Constants::Reserved::DefaultPrimitives.
    // A row with no guid (Ground) is not a tile: it is 100 m of floor, an Add
    // menu entry and a verb name, not something to drag out of a drawer.
    for (const primitives::Def &def : primitives::all()) {
        if (!def.guid || !def.icon) continue;

        auto item = new QListWidgetItem;
        const QString name = QString::fromLatin1(def.name);
        item->setData(Qt::DisplayRole, name);
        item->setData(Qt::UserRole, name);

        item->setData(MODEL_TYPE_ROLE, static_cast<int>(ModelTypes::Object));
        item->setData(MODEL_MESH_ROLE, QString::fromLatin1(def.mesh));
        item->setData(MODEL_GUID_ROLE, QString::fromLatin1(def.guid));

        item->setIcon(QIcon(IrisUtils::getAbsoluteAssetPath(QString::fromLatin1(def.icon))));

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
    // The drag anchor lives on the panel (AssetPanel::dragStartPos /
    // dragCandidate), NOT here: a local reset itself to (0,0) on every event,
    // so a plain selecting click started a drag. See assetpanel.h.
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
                    dragStartPos = evt->pos();
                    dragCandidate = listView->indexAt(evt->pos()).isValid();
                }

                AssetModelPanel::mousePressEvent(evt);
                break;
            }

            case QEvent::MouseButtonRelease: {
                auto evt = static_cast<QMouseEvent*>(event);
                dragCandidate = false;
                AssetModelPanel::mouseReleaseEvent(evt);
                break;
            }

            case QEvent::MouseMove: {
                auto evt = static_cast<QMouseEvent*>(event);
                if (dragCandidate && (evt->buttons() & Qt::LeftButton)) {
                    int distance = (evt->pos() - dragStartPos).manhattanLength();
                    if (distance >= QApplication::startDragDistance()) {
                        // One drag per press: the exec() below runs a nested
                        // loop and the release that ends it can be consumed
                        // there, so clear the candidate BEFORE starting.
                        dragCandidate = false;
                        auto item = listView->currentItem();

                        if (item) {
                            auto drag = QPointer<QDrag>(new QDrag(this));
                            // ONE payload builder (ui/controls/assetdrag.h).
                            drag->setMimeData(AssetDrag::mimeFor(
                                item->data(MODEL_TYPE_ROLE).toInt(),
                                item->data(Qt::UserRole).toString(),
                                item->data(MODEL_MESH_ROLE).toString(),
                                item->data(MODEL_GUID_ROLE).toString()));

                            // only hide for object models
                            drag->setPixmap(item->icon().pixmap(64, 64));
                            drag->exec();
                            // THE RELEASE THE NESTED LOOP ATE. exec() runs its
                            // own event loop and the button release that ends
                            // the drag is consumed in there, so the view's
                            // press bookkeeping would otherwise outlive the
                            // gesture. See ui/panels/singledragowner.h.
                            singledrag::clearViewPressState(listView);
                            // CONSUME THE MOVE (2026-09-07, the preset-drop
                            // DOUBLE-ADD). Falling through here returned the
                            // event to QListWidget, whose OWN drag machinery
                            // (setDragEnabled above, QAbstractItemView's
                            // DraggingState) then called startDrag() from this
                            // same move — a SECOND QDrag, running with the
                            // button already released, which dropped itself
                            // immediately at the cursor. One gesture produced
                            // two drops (traced: source=AssetModelPanel, then
                            // source=QListWidget) and therefore two nodes and
                            // two undo entries. The drag this filter started IS
                            // the gesture's drag; nothing else may see the move.
                            return true;
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

    // The audit's emblematic reroute (§7.5): the panel calls the service verb
    // that is also the scripting registry's scene.addPrimitive.
    mainWindow->studioServices()->sceneEdit->addPrimitive(text);
}

void AssetModelPanel::removeFavorite(const QString &assetGuid)
{
    handle->removeFavorite(assetGuid);
}

void AssetModelPanel::showContextMenu(const QPoint &pos)
{
    QMenu contextMenu;
    contextMenu.setStyleSheet(StyleSheet::PresetsContextMenu());

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
