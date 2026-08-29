/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

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
    lastWidth = 0;
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

    connect(gameGridItem,   SIGNAL(moveToDesktopFromWidget(ItemGridWidget*, int)),
            parent,         SLOT(moveProjectToDesktop(ItemGridWidget*, int)));

    connect(gameGridItem,   &ItemGridWidget::tileMoved,
            this,           &DynamicGrid::tilePositionChanged);

    // desktops state: which desktop this grid shows + the tile's stored freeform position
    gameGridItem->currentDesktop = currentDesktop;
    gameGridItem->hasFreeformPos = tileData.hasPosition;
    gameGridItem->normX = tileData.posX;
    gameGridItem->normY = tileData.posY;

    if (mode == LayoutMode::Freeform) {
        gameGridItem->freeformDraggable = true;
        gridWidget->resize(viewport()->size().expandedTo(gridWidget->size()));
        placeFreeformTile(gameGridItem);
        gameGridItem->show();
        return;
    }

    int columnCount = viewport()->width() / (tileSize.width());

    if (columnCount == 0) columnCount = 1;

    gridLayout->addWidget(gameGridItem, count / columnCount + 1, count % columnCount + 1);
    gridWidget->adjustSize();
}

void DynamicGrid::setLayoutMode(LayoutMode newMode)
{
    mode = newMode;

    // detach every tile from the flow layout (keep the widgets)
    QLayoutItem *item;
    while ((item = gridLayout->takeAt(0)) != Q_NULLPTR) delete item;

    if (mode == LayoutMode::Freeform) {
        foreach (ItemGridWidget *gridItem, originalItems) gridItem->freeformDraggable = true;
        applyFreeformLayout();
    } else {
        // Rows ignores stored positions: pure sequence, top-left to bottom-right.
        // The freeform positions are kept (not cleared) for the next freeform show.
        foreach (ItemGridWidget *gridItem, originalItems) gridItem->freeformDraggable = false;
        updateGridColumns(qMax(lastWidth, tileSize.width()));
    }
}

void DynamicGrid::applyFreeformLayout()
{
    // the desktop canvas fills the viewport; positions are normalized to it so
    // resizes keep relative placement
    gridWidget->resize(viewport()->size().expandedTo(QSize(tileSize.width(), tileSize.height())));

    // two passes: settle every placed tile first so the cascade for unplaced ones
    // tests against real positions, not stale ones
    foreach (ItemGridWidget *gridItem, originalItems) {
        if (gridItem->hasFreeformPos) placeFreeformTile(gridItem);
        gridItem->show();
    }
    foreach (ItemGridWidget *gridItem, originalItems) {
        if (!gridItem->hasFreeformPos) placeFreeformTile(gridItem);
    }
}

QPoint DynamicGrid::pixelPosFor(ItemGridWidget *widget) const
{
    const int availW = qMax(1, gridWidget->width() - widget->width());
    const int availH = qMax(1, gridWidget->height() - widget->height());
    return QPoint(qRound(widget->normX * availW), qRound(widget->normY * availH));
}

void DynamicGrid::placeFreeformTile(ItemGridWidget *widget)
{
    if (widget->hasFreeformPos) {
        widget->move(pixelPosFor(widget));
        return;
    }

    // Never placed: new tiles land at the top-left; when the spot is taken, cascade
    // by 10% of the tile size (like window cascading) until a free spot.
    // Before the canvas is realized (first populate happens pre-show, when the
    // viewport is still tiny) defer: assigning + persisting positions against a
    // degenerate canvas would pile every tile onto one spot. The first real
    // resize triggers applyFreeformLayout, which lands here again.
    if (gridWidget->width() < widget->width() * 2 || gridWidget->height() < widget->height()) {
        widget->move(offset, offset);
        return;
    }

    const int stepX = qMax(1, widget->width() / 10);
    const int stepY = qMax(1, widget->height() / 10);
    QPoint candidate(offset, offset);

    const int maxX = qMax(0, gridWidget->width() - widget->width());
    const int maxY = qMax(0, gridWidget->height() - widget->height());

    for (int tries = 0; tries < 512; ++tries) {
        bool occupied = false;
        foreach (ItemGridWidget *other, originalItems) {
            if (other == widget || !other->hasFreeformPos) continue;
            if (qAbs(other->pos().x() - candidate.x()) < stepX &&
                qAbs(other->pos().y() - candidate.y()) < stepY) {
                occupied = true;
                break;
            }
        }
        if (!occupied) break;

        candidate += QPoint(stepX, stepY);
        if (candidate.x() > maxX || candidate.y() > maxY) break; // clamp at the far corner
    }

    candidate.setX(qBound(0, candidate.x(), maxX));
    candidate.setY(qBound(0, candidate.y(), maxY));

    widget->normX = qBound(0.0, qreal(candidate.x()) / qMax(1, gridWidget->width() - widget->width()), 1.0);
    widget->normY = qBound(0.0, qreal(candidate.y()) / qMax(1, gridWidget->height() - widget->height()), 1.0);
    widget->hasFreeformPos = true;
    widget->move(candidate);

    emit tilePositionChanged(widget);   // persist the assigned position
}

void DynamicGrid::scaleTile(QString scale)
{
    QSize size = sizeFromString(scale);
    tileSize.setWidth(size.width());
    tileSize.setHeight(size.height());

    if (mode == LayoutMode::Freeform) {
        foreach (ItemGridWidget *gridItem, originalItems) gridItem->setTileSize(tileSize, iconSize);
        applyFreeformLayout();
        return;
    }

    int columnCount = qMax(1, lastWidth / tileSize.width());

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
    if (mode == LayoutMode::Freeform) {
        // freeform keeps positions; searching only filters visibility
        foreach (ItemGridWidget *gridItem, originalItems) {
            gridItem->setVisible(searchString.isEmpty()
                                 || gridItem->tileData.name.toLower().contains(searchString));
        }
        return;
    }

    int columnCount = qMax(1, lastWidth / tileSize.width());

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
    // freeform tiles are not layout-managed, so count the tiles themselves
    return !originalItems.isEmpty();
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
    if (index == -1) {
        // freeform tiles are free children of the canvas, not layout items
        originalItems.removeOne(widget);
        widget->deleteLater();
        return;
    }
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
        originalItems.removeOne(static_cast<ItemGridWidget*>(gridItem->widget()));
        delete gridItem->widget();
        delete gridItem;
    }

    // freeform tiles never entered the layout; delete the stragglers
    foreach (ItemGridWidget *item, originalItems) delete item;

    originalItems.clear();
}

void DynamicGrid::resizeEvent(QResizeEvent *event)
{
    lastWidth = event->size().width();

    if (mode == LayoutMode::Freeform) {
        QScrollArea::resizeEvent(event);
        applyFreeformLayout();  // normalized positions -> new canvas size
        return;
    }

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
    int columnCount = qMax(1, width / tileSize.width());

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
