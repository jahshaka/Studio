/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/controls/dynamicgrid.h"
#include <QGraphicsDropShadowEffect>
#include <QGridLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QScrollBar>
#include <QTimer>
#include <QWheelEvent>

#include <QDebug>

#include "ui/controls/itemgridwidget.h"
#include "data/constants.h"
#include "data/settingsmanager.h"
#include "ui/pages/projectmanager.h"

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

    // sliders: empty-space press pans a row, wheel over a row slides it —
    // both arrive on the canvas (tiles swallow their own presses first)
    gridWidget->installEventFilter(this);

//    setStyleSheet("border: 1px solid yellow");
}

void DynamicGrid::addToGridView(ProjectTileData tileData, int count, bool highlight)
{
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

    connect(gameGridItem,   &ItemGridWidget::tileMoved, this, [this](ItemGridWidget *widget) {
        if (mode == LayoutMode::Sliders) handleSliderDrop(widget);
        else emit tilePositionChanged(widget);
    });

    connect(gameGridItem,   &ItemGridWidget::moveToRowFromWidget,
            this,           [this](ItemGridWidget *widget, int row) {
        moveTileToRow(widget, row);     // append at the end of the target row
    });

    // desktops state: which desktop this grid shows + the tile's stored freeform position
    gameGridItem->currentDesktop = currentDesktop;
    gameGridItem->hasFreeformPos = tileData.hasPosition;
    gameGridItem->normX = tileData.posX;
    gameGridItem->normY = tileData.posY;
    gameGridItem->hasSliderPos = tileData.hasSliderPos;
    gameGridItem->sliderRow = tileData.sliderRow;
    gameGridItem->sliderIndex = tileData.sliderIndex;

    if (mode == LayoutMode::Sliders) {
        gameGridItem->show();
        // Coalesce: populateDesktop adds tiles one by one; seeding must see them
        // all at once or round-robin would pile every new tile onto row 0.
        scheduleSliderRelayout();
        return;
    }

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
    const LayoutMode prevMode = mode;
    mode = newMode;

    // detach every tile from the flow layout (keep the widgets)
    QLayoutItem *item;
    while ((item = gridLayout->takeAt(0)) != Q_NULLPTR) delete item;

    if (mode == LayoutMode::Freeform) {
        foreach (ItemGridWidget *gridItem, originalItems) {
            gridItem->freeformDraggable = true;
            gridItem->sliderDraggable = false;
            gridItem->sliderRowCount = 0;
        }
        applyFreeformLayout();
    } else if (mode == LayoutMode::Sliders) {
        // seed unassigned tiles from the mode we are leaving: freeform maps
        // y-bands to rows, anything else round-robins in rows order (lossless
        // switching — DESKTOP_SLIDER_SPEC.md; stored assignments always win)
        rebuildSliderModel(prevMode);
        applySliderLayout();
    } else {
        // Rows ignores stored positions: pure sequence, top-left to bottom-right.
        // The freeform positions are kept (not cleared) for the next freeform show.
        foreach (ItemGridWidget *gridItem, originalItems) {
            gridItem->freeformDraggable = false;
            gridItem->sliderDraggable = false;
            gridItem->sliderRowCount = 0;
        }
        updateGridColumns(qMax(lastWidth, tileSize.width()));
    }

    // panning affordance on the canvas itself (tiles keep their own cursors)
    if (mode == LayoutMode::Sliders) gridWidget->setCursor(Qt::OpenHandCursor);
    else gridWidget->unsetCursor();
    rowPanning = false;
    panRow = -1;
}

// ===== Sliders (DESKTOP_SLIDER_SPEC.md): N filmstrip rows over the same tiles =====

ItemGridWidget *DynamicGrid::tileByGuid(const QString &guid) const
{
    foreach (ItemGridWidget *gridItem, originalItems)
        if (gridItem->tileData.guid == guid) return gridItem;
    return Q_NULLPTR;
}

void DynamicGrid::scheduleSliderRelayout()
{
    if (sliderRelayoutPending) return;
    sliderRelayoutPending = true;
    QTimer::singleShot(0, this, [this]() {
        sliderRelayoutPending = false;
        if (mode != LayoutMode::Sliders) return;
        rebuildSliderModel(LayoutMode::Sliders);    // no mode change: rows-order seed
        applySliderLayout();
    });
}

void DynamicGrid::rebuildSliderModel(LayoutMode seedFrom)
{
    // "Slider rows" is a user setting (Settings -> Desktop), not per desktop
    sliderRows = qBound(2, settings->getValue("slider_rows", 6).toInt(), 10);

    QVector<SliderTileInfo> infos;
    foreach (ItemGridWidget *gridItem, originalItems) {
        SliderTileInfo info;
        info.guid = gridItem->tileData.guid;
        info.hasSlider = gridItem->hasSliderPos;
        info.row = gridItem->sliderRow;
        info.index = gridItem->sliderIndex;
        info.hasFreeform = gridItem->hasFreeformPos;
        info.normX = gridItem->normX;
        info.normY = gridItem->normY;
        infos.push_back(info);
    }

    sliderModel.build(infos, sliderRows,
                      seedFrom == LayoutMode::Freeform ? SliderLayoutModel::Seed::FreeformBands
                                                       : SliderLayoutModel::Seed::RowsOrder);
    syncSliderAssignments();
}

void DynamicGrid::syncSliderAssignments()
{
    foreach (ItemGridWidget *gridItem, originalItems) {
        gridItem->freeformDraggable = false;
        gridItem->sliderDraggable = true;
        gridItem->sliderRowCount = sliderRows;

        const SliderLayoutModel::Pos pos = sliderModel.posOf(gridItem->tileData.guid);
        if (!pos.valid()) continue;
        if (!gridItem->hasSliderPos || gridItem->sliderRow != pos.row
                                    || gridItem->sliderIndex != pos.index) {
            gridItem->hasSliderPos = true;
            gridItem->sliderRow = pos.row;
            gridItem->sliderIndex = pos.index;
            emit tileSliderPositionChanged(gridItem);   // persist {row, orderIndex}
        }
    }
}

int DynamicGrid::sliderRowHeight() const
{
    // a usable strip: the tile plus its label, never squeezed below that —
    // when sliderRows * rowHeight exceeds the viewport the desktop scrolls
    // vertically instead (the scroll area's vertical bar stays enabled)
    int h = tileSize.height() + 36;
    foreach (ItemGridWidget *gridItem, originalItems)
        h = qMax(h, gridItem->sizeHint().height());
    return h + 12;
}

int DynamicGrid::sliderRowAt(int y) const
{
    const int rowH = qMax(1, sliderRowHeight());
    return qBound(0, (y - offset) / rowH, sliderRows - 1);
}

void DynamicGrid::applySliderLayout()
{
    if (sliderRows < 1) return;

    const int rowH = sliderRowHeight();
    const int contentH = offset + sliderRows * rowH + offset;
    gridWidget->resize(qMax(viewport()->width(), tileSize.width()),
                       qMax(viewport()->height(), contentH));

    for (int r = 0; r < sliderRows; ++r) positionSliderRow(r);

    // park any tile the model does not know (shouldn't happen; stay visible)
    foreach (ItemGridWidget *gridItem, originalItems)
        if (!sliderModel.posOf(gridItem->tileData.guid).valid())
            gridItem->move(offset, offset);
}

void DynamicGrid::positionSliderRow(int row)
{
    if (row < 0 || row >= sliderModel.rowCount()) return;

    const int rowH = sliderRowHeight();
    const int gap = 12;
    const int y = offset + row * rowH;

    // content width of the strip (visible tiles only — search filtering compacts)
    int contentW = 0;
    QVector<ItemGridWidget*> strip;
    foreach (const QString &guid, sliderModel.rows()[row]) {
        ItemGridWidget *tile = tileByGuid(guid);
        if (!tile || tile->isHidden()) continue;
        strip.push_back(tile);
        contentW += tile->width() + gap;
    }
    if (contentW > 0) contentW -= gap;

    // clamp the filmstrip offset: slide freely, but keep the strip reachable
    const int avail = gridWidget->width() - 2 * offset;
    const qreal minOffset = qMin<qreal>(0.0, avail - contentW);
    const qreal off = qBound(minOffset, sliderModel.rowOffset(row), 0.0);
    sliderModel.setRowOffset(row, off);

    int x = offset + qRound(off);
    foreach (ItemGridWidget *tile, strip) {
        const int tileH = tile->height() > 0 ? tile->height() : tile->sizeHint().height();
        tile->move(x, y + qMax(0, (rowH - tileH) / 2));
        x += tile->width() + gap;
    }
}

void DynamicGrid::handleSliderDrop(ItemGridWidget *widget)
{
    // drop x decides the insert position; drop y decides the row
    const QPoint center = widget->pos() + QPoint(widget->width() / 2, widget->height() / 2);
    const int row = sliderRowAt(center.y());

    int index = 0;
    if (row < sliderModel.rowCount()) {
        foreach (const QString &guid, sliderModel.rows()[row]) {
            if (guid == widget->tileData.guid) continue;
            ItemGridWidget *other = tileByGuid(guid);
            if (!other || other->isHidden()) continue;
            if (other->pos().x() + other->width() / 2 < center.x()) ++index;
        }
    }

    moveTileToRow(widget, row, index);
}

void DynamicGrid::moveTileToRow(ItemGridWidget *widget, int row, int index)
{
    if (mode != LayoutMode::Sliders || !widget) return;

    sliderModel.moveTile(widget->tileData.guid, row, index);
    syncSliderAssignments();    // reindexes both rows; persists what changed
    applySliderLayout();
}

bool DynamicGrid::eventFilter(QObject *watched, QEvent *event)
{
    if (mode == LayoutMode::Sliders && watched == gridWidget) {
        switch (event->type()) {
        case QEvent::MouseButtonPress: {
            auto *me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                // empty-space grab: 1:1, momentum-free row pan
                rowPanning = true;
                panRow = sliderRowAt(int(me->position().y()));
                panStartX = me->globalPosition().toPoint().x();
                panStartOffset = sliderModel.rowOffset(panRow);
                gridWidget->setCursor(Qt::ClosedHandCursor);
                return true;
            }
            break;
        }
        case QEvent::MouseMove: {
            auto *me = static_cast<QMouseEvent*>(event);
            if (rowPanning && (me->buttons() & Qt::LeftButton)) {
                const int dx = me->globalPosition().toPoint().x() - panStartX;
                sliderModel.setRowOffset(panRow, panStartOffset + dx);
                positionSliderRow(panRow);  // clamps + moves the strip
                return true;
            }
            break;
        }
        case QEvent::MouseButtonRelease: {
            auto *me = static_cast<QMouseEvent*>(event);
            if (rowPanning && me->button() == Qt::LeftButton) {
                rowPanning = false;
                panRow = -1;
                gridWidget->setCursor(Qt::OpenHandCursor);
                return true;
            }
            break;
        }
        case QEvent::Wheel: {
            // wheel over a row slides it horizontally (tiles propagate the
            // wheel up to the canvas, so this covers tile-hover too)
            auto *we = static_cast<QWheelEvent*>(event);
            const int row = sliderRowAt(int(we->position().y()));
            const int delta = we->angleDelta().y() != 0 ? we->angleDelta().y()
                                                        : we->angleDelta().x();
            sliderModel.setRowOffset(row, sliderModel.rowOffset(row) + delta);
            positionSliderRow(row);
            return true;
        }
        default:
            break;
        }
    }

    return QScrollArea::eventFilter(watched, event);
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

    if (mode == LayoutMode::Sliders) {
        foreach (ItemGridWidget *gridItem, originalItems) gridItem->setTileSize(tileSize, iconSize);
        applySliderLayout();
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

    if (mode == LayoutMode::Sliders) {
        // assignments are kept; the strips compact around the visible tiles
        foreach (ItemGridWidget *gridItem, originalItems) {
            gridItem->setVisible(searchString.isEmpty()
                                 || gridItem->tileData.name.toLower().contains(searchString));
        }
        applySliderLayout();
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
        // freeform/slider tiles are free children of the canvas, not layout items
        originalItems.removeOne(widget);
        if (mode == LayoutMode::Sliders) {
            sliderModel.removeTile(widget->tileData.guid);
            widget->deleteLater();
            applySliderLayout();    // close the gap in its strip
            return;
        }
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

    // freeform/slider tiles never entered the layout; delete the stragglers
    foreach (ItemGridWidget *item, originalItems) delete item;

    originalItems.clear();
    sliderModel.clear();    // row offsets are session state; a repopulate resets them
}

void DynamicGrid::resizeEvent(QResizeEvent *event)
{
    lastWidth = event->size().width();

    if (mode == LayoutMode::Freeform) {
        QScrollArea::resizeEvent(event);
        applyFreeformLayout();  // normalized positions -> new canvas size
        return;
    }

    if (mode == LayoutMode::Sliders) {
        QScrollArea::resizeEvent(event);
        applySliderLayout();    // re-clamp offsets; vertical scroll if rows overflow
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
