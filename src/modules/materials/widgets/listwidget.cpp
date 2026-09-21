/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#include "listwidget.h"
#include "../graph/nodestyle.h"
#include <QDrag>
#include <QMimeData>
#include <QMouseEvent>
#include <QPixmap>
#include <QPainter>
#include <QScrollBar>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLayout>
#include <QMenu>
#include "data/project.h"
#include "ui/style/stylesheet.h"

QVariantAnimation* ListWidget::anim = Q_NULLPTR;

ListWidget::ListWidget() : QListWidget()
{
	QSize currentSize(70, 70);
	setAlternatingRowColors(false);
	setSpacing(0);
	setContentsMargins(10, 3, 10, 10);
	setViewMode(QListWidget::IconMode);
	setIconSize(currentSize);
	setMouseTracking(true);
	setDragDropMode(QAbstractItemView::DragDrop);
	setResizeMode(QListWidget::Adjust);
	setDefaultDropAction(Qt::CopyAction);
	setSelectionMode(QAbstractItemView::SingleSelection);
	setSelectionRectVisible(false);
	setDragEnabled(true);
	viewport()->setAcceptDrops(true);
	setAcceptDrops(true);
	setDropIndicatorShown(true);
	setWordWrap(true);
	setGridSize(QSize(90, 90));
	setSortingEnabled(true);
	sortItems();
    setEditTriggers(QAbstractItemView::EditKeyPressed);
    setContextMenuPolicy(Qt::CustomContextMenu);

	numberOfItemPerRow = 3;
	QFont font = this->font();
    font.setWeight(QFont::Medium);
	font.setPixelSize(12);
	setFont(font);
	setContentsMargins(0, 0, 0, 0);
	verticalScrollBar()->setStyleSheet(StyleSheet::MaterialsListScrollBar());

	setStyleSheet(StyleSheet::MaterialsListTiles());

    connect(this, SIGNAL(customContextMenuRequested(const QPoint&)),this, SLOT(customContextMenu(const QPoint&)));
	
}


ListWidget::~ListWidget()
{
}

void ListWidget::updateThumbnailImage(QByteArray arr, QListWidgetItem *item)
{
	auto size = 35;
	auto img = QImage::fromData(arr, "PNG");
	auto pixmap = QPixmap::fromImage(img);
	pixmap = pixmap.scaled({ 90,90 }, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

	// shader overlay file lost - recreate
	/*QPixmap pixmap_overlay(":/icons/shader_overlay.png");
	pixmap_overlay = pixmap_overlay.scaled({ size,size }, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
	QPainter painter(&pixmap);
	if(true) painter.drawPixmap(QRect(pixmap.width() - size, 0, size, size), pixmap_overlay);*/

	item->setIcon(QIcon(pixmap));
	//item->icon().addPixmap(QPixmap(":/icons/shader_overlay.png"));

}

void ListWidget::highlightNodeForInterval(int seconds, QListWidgetItem * item)
{
	// A guid no drawer holds has no tile to flash (fix round F1's family):
	// every line below reads the item, and the callers get theirs from
	// `selectCorrectItemFromDrop`, which answers null by design.
	if (!item) return;
	// THE FLASH OUTLIVED THE TILE (MATERIALS-TABS-1 fix round, found by
	// scripting.e2e.material_tabs — a SEGV at 0x21 inside QListWidgetItem::
	// setIcon). This animation holds a RAW QListWidgetItem* for two seconds
	// and paints into it on every tick; a drawer refill inside those two
	// seconds — `updateAssetDock`'s `clear()`, which is what any save,
	// duplicate, import or add-to-project runs into — deletes every item
	// under it. It was also never stopped and never deleted: one leaked
	// animation per highlight, all of them still running.
	//
	// One at a time, owned, and stopped by `ListWidget::clear()` before the
	// items it points into go.
	stopHighlightedNode();
	anim = new QVariantAnimation;
	anim->setStartValue(QColor(50, 148, 213, 255));
	anim->setEndValue(QColor(50, 148, 213, 0));
	anim->setDuration(seconds*1000);
	QObject::connect(anim, &QAbstractAnimation::finished, anim, []() { anim = Q_NULLPTR; });
	anim->start(QAbstractAnimation::DeleteWhenStopped);
	QPixmap pixmap = item->icon().pixmap(90, 90);

	connect(anim, &QVariantAnimation::valueChanged, [=](const QVariant &value) {
		QPixmap bg(pixmap);
		bg.fill(value.value<QColor>());
		QPainter painter(&bg);
        painter.setRenderHint(QPainter::Antialiasing);
		painter.drawPixmap(QRect(0, 0, 90, 90), pixmap);
		item->setIcon(bg);
	});
}

void ListWidget::stopHighlightedNode()
{
	if (!anim) return;
	// `DeleteWhenStopped` frees it; the `finished` handler nulls the pointer,
	// and this clears it too for the case where the animation had already
	// ended.
	QVariantAnimation *dying = anim;
	anim = Q_NULLPTR;
	if (dying->state() == QAbstractAnimation::Running) dying->stop();
	else delete dying;
}

void ListWidget::clear()
{
	// THE TILES GO, SO THE FLASH GOES. Every refill of a drawer runs through
	// here, and the highlight animation paints into an item by raw pointer.
	stopHighlightedNode();
	QListWidget::clear();
}

void ListWidget::displayAllContents()
{
	//setGridSize(QSize(95, 95));

	//int num_of_items_per_row = width() / (gridSize().width()+6);
	//int num_of_models = model()->rowCount();
	//int number_of_rows = num_of_models / num_of_items_per_row;
	//if (num_of_models % num_of_items_per_row != 0) number_of_rows++;
	//int calculated_height = number_of_rows * (gridSize().height() + num_of_items_per_row*2);

	//setFixedHeight(calculated_height);
}




QMimeData * ListWidget::mimeData(const QList<QListWidgetItem *> &items) const
{

	
	QMimeData *data = new QMimeData();
	//set text for shader
	data->setText(items[0]->data(Qt::DisplayRole).toString());
	//set html for node
	data->setHtml(items[0]->data(Qt::UserRole).toString());
	data->setData("MODEL_TYPE_ROLE", items[0]->data(MODEL_TYPE_ROLE).toByteArray());
	data->setData("MODEL_GUID_ROLE", items[0]->data(MODEL_GUID_ROLE).toByteArray());

	if (!items[0]->data(MODEL_EXT_ROLE).isNull()) data->setData("index", items[0]->data(MODEL_EXT_ROLE).toByteArray());
	return data;
}

void ListWidget::resizeEvent(QResizeEvent * event)
{
	QListWidget::resizeEvent(event);
	if(isResizable) displayAllContents();
}

void ListWidget::addToListWidget(QListWidgetItem * item)
{
	addItem(item);
}

void ListWidget::dropEvent(QDropEvent * event)
{
    QListWidget::dropEvent(event);
}

void ListWidget::customContextMenu(QPoint pos)
{
    QModelIndex index = indexAt(pos);
    auto guid = index.data(MODEL_GUID_ROLE).toString();

    QMenu menu;
    menu.setStyleSheet(StyleSheet::MaterialsContextMenu());


    if(shaderContextMenuAllowed){
        if(index.isValid()){
            // THE WORDS ARE "MATERIAL", because that is what these tiles are
            // (owner Q3: "only materials"; there is no shader/effect asset any
            // more). Each acts on THIS ONE ROW.
            auto actionRename = new QAction("Rename");
            auto actionDuplicate = new QAction("Duplicate");
            auto actionExport = new QAction("Export material…");
            auto actionEdit = new QAction("Edit");
            auto actionDelete = new QAction("Delete");
            auto actionProject = new QAction("Add to project");

            connect(actionRename,&QAction::triggered,[guid ,this](){
                emit renameShader(guid);
            });
            connect(actionExport,&QAction::triggered,[guid ,this](){
                emit exportShader(guid);
            });
            connect(actionDuplicate,&QAction::triggered,[guid ,this](){
                emit duplicateShader(guid);
            });
            connect(actionEdit,&QAction::triggered,[guid, index ,this](){
                emit editShader(guid);
            });
			connect(actionDelete, &QAction::triggered, [guid, this]() {
				emit deleteShader(guid);
			});
			connect(actionProject, &QAction::triggered, [guid, this]() {
				emit addToProject(this->currentItem());
			});

            menu.addActions({actionEdit,actionRename,actionDuplicate,actionExport,actionDelete});
			if (sceneOpenProbe && sceneOpenProbe() && addToProjectMenuAllowed) menu.addAction(actionProject);
            menu.exec(this->mapToGlobal(pos));
        }else{
            auto actionCreate = new QAction("New material");
            auto actionImport = new QAction("Import material…");

            connect(actionCreate,&QAction::triggered,[guid ,this](){
                emit createShader(guid);
            });
            connect(actionImport,&QAction::triggered,[guid ,this](){
                emit importShader(guid);
            });

            menu.addActions({actionCreate, actionImport});
            menu.exec(this->mapToGlobal(pos));
        }
    }
    // (THE PRESETS DRAWER HAS NO MENU — PRESET-EDIT-1's Deletes. Its one item
    // was Customise, which existed because a preset was locked wherever it
    // was; a preset a project holds is editable in place now and the first
    // edit makes the project its own copy, so the gesture has nothing left to
    // do. Double-clicking a preset opens it, which is the whole interaction.)
}


