/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/panels/presets/assetmaterialpanel.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QFileInfoList>
#include <QPointer>
#include <QMessageBox>

#include "irisgl/core/irisutils.h"

#include "data/constants.h"
#include "data/guidmanager.h"
#include "io/materialpresetreader.h"
#include "bridge/enginehost.h"
#include "io/assetmanager.h"
#include "shell/mainwindow.h"
#include "modules/materials/core/materialhelper.h"

#include "io/scenewriter.h"
#include "ui/panels/singledragowner.h"
#include "ui/style/stylesheet.h"
#include "ui/controls/assetdrag.h"
#include "io/materialpresets.h"
#include "services/services.h"
#include "services/materialpresetassets.h"
#include "services/materialpresetseeder.h"
#include "services/thumbnailrebuild.h"
#include "services/projectservice.h"
#include "services/sceneeditservice.h"
#include "services/jahlog.h"
#include "services/selectionservice.h"

AssetMaterialPanel::AssetMaterialPanel(QWidget *parent) : AssetPanel(parent)
{
    installEventFilter(this);
    listView->installEventFilter(this);
    listView->viewport()->installEventFilter(this);

    setMouseTracking(true);
    listView->setDragDropMode(QAbstractItemView::DragDrop);
    // ONE DRAG OWNER (ui/panels/singledragowner.h) — see assetmodelpanel.cpp.
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

    // double-click applies (owner request): a single click on the preset
    // image must not swap the selected object's material any more
    connect(listView,   SIGNAL(itemDoubleClicked(QListWidgetItem*)),
            this,       SLOT(applyMaterialPreset(QListWidgetItem*)));

    setStyleSheet(StyleSheet::PresetsListPanel());

    // The starter tiles only. The FAVOURITES need the database, which arrives
    // with setDatabaseHandle after this returns (CLOSE-2).
    addDefaultItems();
}

AssetMaterialPanel::~AssetMaterialPanel()
{
}

void AssetMaterialPanel::addDefaultItems()
{
    // THE ONE PRESET LIST (io/materialpresets.h, MATERIAL-PREVIEW-1 item
    // c): this was the third copy of the same directory walk, reader and PBR
    // filter, beside the asset panel's registration loop and materials.presets.
    for (const MaterialPreset &preset : MaterialPresets::all()) {
        auto item = new QListWidgetItem;
        item->setData(Qt::DisplayRole, preset.name);
        item->setData(Qt::UserRole, preset.name);

        item->setData(MODEL_TYPE_ROLE, static_cast<int>(ModelTypes::Material));
        item->setData(MODEL_GUID_ROLE, Constants::Reserved::DefaultMaterials.key(preset.name));

        item->setIcon(QIcon(preset.icon));

        listView->addItem(item);
    }
}

void AssetMaterialPanel::addNewItem(QListWidgetItem *itemInc)
{
    auto asset = handle->fetchAsset(itemInc->data(MODEL_GUID_ROLE).toString());

    auto item = new QListWidgetItem;
    item->setData(Qt::DisplayRole, QFileInfo(asset.name).baseName());
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

    listView->addItem(item);
    
    // add this asset to the assets table and set the VIEW so we know where it belongs
    handle->addFavorite(itemInc->data(MODEL_GUID_ROLE).toString());
}

void AssetMaterialPanel::addFavorites()
{
    populateFavorites();

    for (const auto &asset : favoriteAssets) {
        if (asset.type == static_cast<int>(ModelTypes::Material)) {
            auto item = new QListWidgetItem;

            item->setData(Qt::DisplayRole, QFileInfo(asset.name).baseName());
            item->setData(Qt::UserRole, asset.name);

            item->setData(MODEL_TYPE_ROLE, asset.type);
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

bool AssetMaterialPanel::eventFilter(QObject *watched, QEvent *event)
{
    // The drag anchor lives on the panel (AssetPanel::dragStartPos /
    // dragCandidate), NOT here: a local reset itself to (0,0) on every event,
    // so a plain selecting click started a drag. See assetpanel.h.
    if (watched == listView->viewport()) {
        switch (event->type()) {
            case QEvent::ContextMenu: {
                auto evt = static_cast<QContextMenuEvent*>(event);
                AssetMaterialPanel::contextMenuEvent(evt);
                break;
            }

            case QEvent::MouseButtonPress: {
                auto evt = static_cast<QMouseEvent*>(event);
                if (evt->button() == Qt::LeftButton) {
                    dragStartPos = evt->pos();
                    dragCandidate = listView->indexAt(evt->pos()).isValid();
                }

                AssetMaterialPanel::mousePressEvent(evt);
                break;
            }

            case QEvent::MouseButtonRelease: {
                auto evt = static_cast<QMouseEvent*>(event);
                dragCandidate = false;
                AssetMaterialPanel::mouseReleaseEvent(evt);
                break;
            }

            case QEvent::MouseMove: {
                auto evt = static_cast<QMouseEvent*>(event);
                if (dragCandidate && (evt->buttons() & Qt::LeftButton)) {
                    int distance = (evt->pos() - dragStartPos).manhattanLength();
                    if (distance >= QApplication::startDragDistance()) {
                        // One drag per press (see assetmodelpanel.cpp).
                        dragCandidate = false;
                        auto item = listView->currentItem();

                        if (item) {
                            auto drag = QPointer<QDrag>(new QDrag(this));
                            // ONE payload builder (ui/controls/assetdrag.h). The
                            // mesh slot used to carry the string "not used" here.
                            drag->setMimeData(AssetDrag::mimeFor(
                                item->data(MODEL_TYPE_ROLE).toInt(),
                                item->data(Qt::UserRole).toString(),
                                QString(),
                                item->data(MODEL_GUID_ROLE).toString()));

                            // only hide for object models
                            drag->setPixmap(item->icon().pixmap(64, 64));
                            drag->exec();
                            // The release exec() ate (singledragowner.h).
                            singledrag::clearViewPressState(listView);
                            // ONE drop per gesture: consume the move, or the
                            // view's own startDrag runs a second QDrag from it
                            // (see assetmodelpanel.cpp for the traced defect).
                            return true;
                        }
                    }
                }

                AssetMaterialPanel::mouseMoveEvent(evt);
                break;
            }

            default: break;
        }
    }

    return QObject::eventFilter(watched, event);
}

void AssetMaterialPanel::showContextMenu(const QPoint &pos)
{
    QMenu contextMenu;
    contextMenu.setStyleSheet(StyleSheet::PresetsContextMenu());

    // CUSTOMISE (R18, the owner's words: "we can't edit presets — we have to
    // create a new material from a starter template"). A preset is read-only
    // in fact — the definition writer refuses one by name — so the gesture
    // that makes one editable is a COPY, in the user's own drawer, named
    // "<Preset>-1". It calls the same one implementation the verb
    // `materials.createFromPreset` calls; the suffix rule lives there, once.
    const QString presetGuid =
        MaterialPresetAssets::guidFor(listView->indexAt(pos).data(MODEL_GUID_ROLE).toString());
    QAction customise(tr("Customise"), this);
    if (!presetGuid.isEmpty()) {
        connect(&customise, &QAction::triggered, this, [this, presetGuid]() {
            MaterialPresetSeeder::instance().finishNow();   // one importer at a time
            QString error;
            Project *project = services && services->project ? services->project->current()
                                                             : nullptr;
            const QString copy = MaterialPresetAssets::customise(presetGuid, QString(),
                                                                 handle, project, &error);
            if (copy.isEmpty()) {
                irisLog("Customise: " + error);
                return;
            }
            // THE TILE IS A RENDER OF THE COPY (owner review R9(a)): the
            // customised copy inherits the shipped preset's ICON otherwise,
            // and an icon is not a sphere of this material.
            thumbrebuild::rebuildOne(handle, project, copy, EngineHost::instance().engine());
            // The copy is a library material the project now holds: every
            // drawer that lists one has to hear about it (the four-drawer
            // rule — one list, two windows).
            if (services && services->sceneEdit)
                services->sceneEdit->requestAssetViewRefresh();
        });
        contextMenu.addAction(&customise);
    }

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

void AssetMaterialPanel::removeFavorite(const QString &assetGuid)
{
    handle->removeFavorite(assetGuid);
}

void AssetMaterialPanel::applyMaterialPreset(QListWidgetItem *item)
{
    // BY GUID, NOT BY INDEX (the audit's F6). The tile's index into the preset
    // list was stashed in role 0x32 — and a FAVOURITE tile never had one:
    // addNewItem wrote the index of the LAST starter preset and addFavorites
    // wrote nothing at all, so `toInt()` returned 0 and double-clicking any
    // favourite applied the first starter preset instead. The guid is on the
    // tile already; it is what the drag carries and what the ONE apply takes.
    if (!item || !services || !services->sceneEdit || !services->selection) return;
    services->sceneEdit->applyMaterial(item->data(MODEL_GUID_ROLE).toString(),
                                       services->selection->selected());
}
