/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "dynamicgrid.h"
#include <QGraphicsDropShadowEffect>
#include <QGridLayout>
#include <QLabel>
#include <QResizeEvent>

#include <QDebug>

#include "itemgridwidget.hpp"
#include "../constants.h"
#include "../core/settingsmanager.h"
#include "projectmanager.h"
#include "../uimanager.h"

DynamicGrid::DynamicGrid(QWidget *parent) : QScrollArea(parent)
{
    this->parent = parent;

    setAlignment(Qt::AlignHCenter);

    gridWidget = new QWidget(this);
    gridWidget->setObjectName("gridWidget");
    setWidget(gridWidget);
    setStyleSheet("background: transparent");

    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    offset = 10;
    settings = SettingsManager::getDefaultManager();
    tileSize = sizeFromString(settings->getValue("tileSize", "Normal").toString());

    gridLayout = new QGridLayout(gridWidget);
//    gridLayout->setSpacing(20);
//    gridLayout->setSizeConstraint(QLayout::SetMinimumSize);
//    gridLayout->setRowMinimumHeight(0, offset);

    gridWidget->setLayout(gridLayout);
    gridLayout->setSpacing(12);

//    setStyleSheet("border: 1px solid yellow");
}

void DynamicGrid::addToGridView(ProjectTileData tileData, int count)
{
    bool highlight = UiManager::isSceneOpen && tileData.guid == Globals::project->getProjectGuid();
    ItemGridWidget *gameGridItem = new ItemGridWidget(tileData, tileSize, iconSize, gridWidget, highlight);

    originalItems.push_back(gameGridItem);

    gameGridItem->setContextMenuPolicy(Qt::CustomContextMenu);

    // remember that parent is the ProjectManager, we delegate or emit up
    connect(gameGridItem,   SIGNAL(openFromWidget(ItemGridWidget*, bool)),
            parent,         SLOT(openProjectFromWidget(ItemGridWidget*, bool)));

    connect(gameGridItem,   SIGNAL(closeFromWidget(ItemGridWidget*)),
            parent,         SLOT(closeProjectFromWidget(ItemGridWidget*)));

    connect(gameGridItem,   SIGNAL(remove(ItemGridWidget*)),
            parent,         SLOT(deleteProjectFromWidget(ItemGridWidget*)));

    connect(gameGridItem,   SIGNAL(exportFromWidget(ItemGridWidget*)),
            parent,         SLOT(exportProjectFromWidget(ItemGridWidget*)));

    connect(gameGridItem,   &ItemGridWidget::doubleClicked, parent, [this](ItemGridWidget *item) {
        static_cast<ProjectManager*>(parent)->openProjectFromWidget(item, false);
    });

    connect(gameGridItem,   SIGNAL(renameFromWidget(ItemGridWidget*)),
            parent,         SLOT(renameProjectFromWidget(ItemGridWidget*)));

    connect(gameGridItem,   SIGNAL(deleteFromWidget(ItemGridWidget*)),
            parent,         SLOT(deleteProjectFromWidget(ItemGridWidget*)));

    int columnCount = viewport()->width() / (tileSize.width());

    if (columnCount == 0) columnCount = 1;

    gridLayout->addWidget(gameGridItem, count / columnCount + 1, count % columnCount + 1);
    gridWidget->adjustSize();
}

void DynamicGrid::scaleTile(QString scale)
{
    QSize size = sizeFromString(scale);
    tileSize.setWidth(size.width());
    tileSize.setHeight(size.height());

    int columnCount = lastWidth / (tileSize.width());

    int count = 0;
    foreach(ItemGridWidget *gridItem, originalItems) {
        gridItem->setTileSize(tileSize, iconSize);
        gridLayout->addWidget(gridItem, count / columnCount + 1, count % columnCount + 1);
        count++;
    }

    gridWidget->adjustSize();
}

void DynamicGrid::searchTiles(QString searchString)
{
    int columnCount = lastWidth / (tileSize.width());

    int count = 0;
    if (!searchString.isEmpty()) {
        foreach(ItemGridWidget *gridItem, originalItems) {
            if (gridItem->tileData.name.toLower().contains(searchString)) {
                gridItem->setVisible(true);
                gridItem->setTileSize(tileSize, iconSize);
                gridLayout->addWidget(gridItem, count / columnCount + 1, count % columnCount + 1);
                count++;
            } else {
                gridItem->setVisible(false);
            }
        }
    } else {
        foreach (ItemGridWidget *gridItem, originalItems) {
            gridItem->setVisible(true);
            gridItem->setTileSize(tileSize, iconSize);
            gridLayout->addWidget(gridItem, count / columnCount + 1, count % columnCount + 1);
            count++;
        }
    }

    gridWidget->adjustSize();
}

bool DynamicGrid::containsTiles()
{
    if (gridLayout->count()) return true;
    return false;
}

/**
 * Helper function. Deletes all child widgets of the given layout @a item.
 */
void deleteChildWidgets(QLayoutItem *item) {
    if (item->layout()) {
        // Process all child items recursively.
        for (int i = 0; i < item->layout()->count(); i++) {
            deleteChildWidgets(item->layout()->itemAt(i));
        }
    }

    // delete item->widget();
    item->widget()->deleteLater();
}

void DynamicGrid::deleteTile(ItemGridWidget *widget)
{
    int index = gridLayout->indexOf(widget);
    if (index != -1) {
        int row, col, col_span, row_span;
        gridLayout->getItemPosition(index, &row, &col, &col_span, &row_span);

        auto w = gridLayout->itemAtPosition(row, col)->widget();
        auto idx = gridLayout->layout()->indexOf(w);
        auto item = gridLayout->takeAt(idx);
        deleteChildWidgets(item);
        item->widget()->deleteLater();

        originalItems.removeOne(widget);
        updateGridColumns(lastWidth);
    }
}

void DynamicGrid::updateTile(const QString &id, const QByteArray &arr)
{
	foreach(ItemGridWidget *gridItem, originalItems) {
		if (gridItem->tileData.guid == id) {
			gridItem->updateTile(arr);
			break;
		}
	}
}

void DynamicGrid::resetView()
{
    QLayoutItem *gridItem;
    while ((gridItem = gridLayout->takeAt(0)) != Q_NULLPTR) {
        delete gridItem->widget();
        delete gridItem;
    }

    originalItems.clear();
}

void DynamicGrid::resizeEvent(QResizeEvent *event)
{
    lastWidth = event->size().width();

//    gridWidget->setMinimumWidth(viewport()->width());
//    gridWidget->setMaximumWidth(viewport()->width());

    int check = event->size().width() / (tileSize.width());
    bool autoAdjustColumns = true;

    if (autoAdjustColumns && check != autoColumnCount && check != 0) {
        autoColumnCount = check;
        updateGridColumns(event->size().width());
    } else
        QScrollArea::resizeEvent(event);
}

void DynamicGrid::updateGridColumns(int width)
{
    int columnCount = width / (tileSize.width());

    int count = 0;
    foreach(ItemGridWidget *gridItem, originalItems) {
        gridItem->setTileSize(tileSize, iconSize);
        gridLayout->addWidget(gridItem, count / columnCount + 1, count % columnCount + 1);
        count++;
    }

    gridWidget->adjustSize();
}

QSize DynamicGrid::sizeFromString(QString size)
{
    if (size == "Small") {
        iconSize = QSize(22, 22);
    } else if (size == "Large") {
        iconSize = QSize(32, 32);
    } else if (size == "Huge") {
        iconSize = QSize(36, 36);
    } else {
        iconSize = QSize(28, 28);
    }

    if (size == "Small") {
        return Constants::TILE_SIZE * 0.4;
    } else if (size == "Large") {
        return Constants::TILE_SIZE * 0.8;
    } else if (size == "Huge") {
        return Constants::TILE_SIZE;
    } else {
        return Constants::TILE_SIZE * 0.6;
    }
}
