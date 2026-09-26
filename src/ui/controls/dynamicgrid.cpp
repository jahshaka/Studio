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
#include <QLayout>
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
#include "ui/style/stylesheet.h"
#include "ui/style/themeroles.h"


// THE ROWS GRID IS A LAYOUT THAT LAYS ITSELF OUT (DESKTOP-1, owner smoke C).
//
// The old grid placed tiles into a QGridLayout by hand and then SNAPSHOT the
// canvas size with `adjustSize()` — after each add, after a column change.
// At boot the window is shown before the grid is populated, so every tile was
// added to a VISIBLE parent, which Qt shows through a queued call: each
// snapshot ran while the newcomer was still hidden (a hidden widget is empty to
// a layout), the canvas stayed 32x32 and the tiles were clipped to a speck.
// Nothing re-took the snapshot when the tiles appeared: only a resize that
// changed the column count, an import or the layout-mode toggle did — which is
// why the toggle "fixed" it. An insert at the head had the same race (a canvas
// sized for N-1 tiles squeezing N: the overlap).
//
// This layout has no snapshot to forget. The canvas is the scroll area's
// RESIZABLE widget, its height is this layout's heightForWidth, and a tile
// that is shown, hidden or resized invalidates the layout like any child —
// so the first showing, a search, a resize and an insert all lay out through
// the one path Qt already runs. Columns count the spacing and the margins:
// (w - margins + spacing) / (tileW + spacing).
class TileFlowLayout : public QLayout
{
public:
    explicit TileFlowLayout(QWidget *parent) : QLayout(parent) {}
    ~TileFlowLayout() override { while (QLayoutItem *item = takeAt(0)) delete item; }

    void addItem(QLayoutItem *item) override { mItems.append(item); }
    int count() const override { return int(mItems.size()); }
    QLayoutItem *itemAt(int index) const override { return mItems.value(index); }
    QLayoutItem *takeAt(int index) override
    {
        if (index < 0 || index >= mItems.size()) return nullptr;
        return mItems.takeAt(index);
    }
    Qt::Orientations expandingDirections() const override { return {}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return arrange(QRect(0, 0, width, 0), false); }
    QSize minimumSize() const override
    {
        const QMargins m = contentsMargins();
        const QSize cell = cellSize();
        return QSize(cell.width() + m.left() + m.right(), cell.height() + m.top() + m.bottom());
    }
    QSize sizeHint() const override { return minimumSize(); }
    void setGeometry(const QRect &rect) override
    {
        QLayout::setGeometry(rect);
        arrange(rect, true);
    }

    /// The order the tiles flow in: `order`'s, each widget once. A widget the
    /// layout does not hold yet is added.
    void setOrder(const QList<ItemGridWidget *> &order)
    {
        QList<QLayoutItem *> sorted;
        for (ItemGridWidget *widget : order) {
            int at = -1;
            for (int i = 0; i < mItems.size(); ++i)
                if (mItems[i]->widget() == widget) { at = i; break; }
            if (at >= 0) sorted.append(mItems.takeAt(at));
            else { addChildWidget(widget); sorted.append(new QWidgetItem(widget)); }
        }
        sorted.append(mItems);          // anything not in `order` keeps its place at the end
        mItems = sorted;
        invalidate();
    }

    /// Columns at a canvas `width` — the ONE formula (freeform's first-entry
    /// slots read it too).
    int columnsFor(int width) const { return columnsFor(width, cellSize()); }
    int columnsFor(int width, const QSize &cell) const
    {
        const QMargins m = contentsMargins();
        const int avail = width - m.left() - m.right() + spacing();
        return qMax(1, avail / qMax(1, cell.width() + spacing()));
    }
    /// Where slot `index` sits on a canvas `width` wide, for tiles of `cell`.
    QPoint slotPos(int index, int width) const { return slotPos(index, width, cellSize()); }
    QPoint slotPos(int index, int width, const QSize &cell) const
    {
        const QMargins m = contentsMargins();
        const int cols = columnsFor(width, cell);
        const int used = cols * cell.width() + (cols - 1) * spacing();
        const int x0 = m.left() + qMax(0, (width - m.left() - m.right() - used) / 2);
        return QPoint(x0 + (index % cols) * (cell.width() + spacing()),
                      m.top() + (index / cols) * (cell.height() + spacing()));
    }
    QSize cellSize() const
    {
        QSize cell;
        for (QLayoutItem *item : mItems)
            if (!item->isEmpty()) cell = cell.expandedTo(item->sizeHint());
        return cell;
    }

private:
    /// Lays the SHOWN tiles out row-major in `rect` (when `apply`) and answers
    /// the height they need. A hidden tile (a search, a queued show) takes no
    /// slot; the moment it is shown the layout is invalidated and runs again.
    int arrange(const QRect &rect, bool apply) const
    {
        const QMargins m = contentsMargins();
        const QSize cell = cellSize();
        int shown = 0;
        for (QLayoutItem *item : mItems) {
            if (item->isEmpty()) continue;
            if (apply) {
                const QPoint p = slotPos(shown, rect.width()) + rect.topLeft();
                item->setGeometry(QRect(p, cell));
            }
            ++shown;
        }
        if (shown == 0) return m.top() + m.bottom();
        const int rows = (shown + columnsFor(rect.width()) - 1) / columnsFor(rect.width());
        return m.top() + rows * cell.height() + (rows - 1) * spacing() + m.bottom();
    }

    QList<QLayoutItem *> mItems;
};

DynamicGrid::DynamicGrid(QWidget *parent) : QScrollArea(parent)
{
    this->parent = parent;

    gridWidget = new QWidget(this);
    gridWidget->setObjectName("gridWidget");
    // THE CANVAS SIZES ITSELF (DESKTOP-1): resizable, so the scroll area keeps
    // it as wide as the viewport and as tall as the flow layout's
    // heightForWidth — re-asked whenever the layout is invalidated.
    setWidgetResizable(true);
    setWidget(gridWidget);
    setStyleSheet(StyleSheet::BackgroundTransparent());
    // Qlementine: the desktop shows through the grid, no frame around it
    ThemeRoles::setFrame(this, QFrame::NoFrame);
    ThemeRoles::clearBackground(viewport());

    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    offset = 10;
    settings = SettingsManager::getDefaultManager();
    tileSize = sizeFromString(settings->get(settingkeys::tileSize));

    gridLayout = new TileFlowLayout(gridWidget);
    gridLayout->setSpacing(12);
    gridLayout->setContentsMargins(16, 16, 16, 16);

    // sliders: empty-space press pans a row, wheel over a row slides it —
    // both arrive on the canvas (tiles swallow their own presses first)
    gridWidget->installEventFilter(this);

}

void DynamicGrid::addToGridView(ProjectTileData tileData, bool highlight)
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

    // By NAME, like every connection above: the grid knows its owner only as
    // the QObject that answers these slots (desktops.grid_layout builds the grid
    // without a ProjectManager).
    connect(gameGridItem,   &ItemGridWidget::doubleClicked, parent, [this](ItemGridWidget *item) {
        QMetaObject::invokeMethod(parent, "openProjectFromWidget",
                                  Q_ARG(ItemGridWidget*, item), Q_ARG(bool, false));
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

    // Rows: into the flow, in order. No size is taken here — the layout lays
    // the tile out when it is SHOWN (a tile added to a visible grid is shown by
    // Qt one turn later), and sizes the canvas with it.
    gridLayout->addWidget(gameGridItem);
}

void DynamicGrid::setLayoutMode(LayoutMode newMode)
{
    const LayoutMode prevMode = mode;
    mode = newMode;

    // detach every tile from the flow layout (keep the widgets)
    QLayoutItem *item;
    while ((item = gridLayout->takeAt(0)) != Q_NULLPTR) delete item;
    // Only Sliders asks the canvas to be taller than the viewport.
    gridWidget->setMinimumHeight(0);

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
        gridLayout->setOrder(originalItems);
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

int DynamicGrid::setSliderRowCount(int rows)
{
    const int clamped = qBound(2, rows, 10);
    settings->set(settingkeys::sliderRows, clamped);
    if (clamped == sliderRows) return clamped;
    if (mode != LayoutMode::Sliders) {
        // Not showing filmstrips: rebuildSliderModel reads the setting when the
        // mode is next entered. Keep the field in step anyway so
        // activeSliderRows() never lies about what the desktop would build.
        sliderRows = clamped;
        return clamped;
    }
    // Rows-order seed: the stored per-tile assignments still win, and tiles
    // whose row no longer exists fold into the rows that do.
    rebuildSliderModel(LayoutMode::Sliders);
    applySliderLayout();
    return clamped;
}

void DynamicGrid::rebuildSliderModel(LayoutMode seedFrom)
{
    // "Slider rows" is a user setting (Settings -> Desktop), not per desktop
    sliderRows = qBound(2, settings->get(settingkeys::sliderRows), 10);

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
    // The strips are not in the flow, so the canvas's height is ASKED for:
    // the resizable scroll area honours the minimum (vertical scroll when the
    // rows overflow) and sizes the canvas now.
    gridWidget->setMinimumHeight(contentH);
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

    // FREEFORM FOLLOWS ITS CANVAS: the scroll area resizes the canvas on its
    // own (a scroll bar coming or going changes the viewport without resizing
    // this widget), and the stored positions are fractions of the canvas.
    if (mode == LayoutMode::Freeform && watched == gridWidget && event->type() == QEvent::Resize) {
        foreach (ItemGridWidget *gridItem, originalItems)
            if (gridItem->hasFreeformPos) gridItem->move(pixelPosFor(gridItem));
    }

    return QScrollArea::eventFilter(watched, event);
}

void DynamicGrid::applyFreeformLayout()
{
    // the desktop canvas fills the viewport; positions are normalized to it so
    // resizes keep relative placement
    gridWidget->resize(viewport()->size().expandedTo(QSize(tileSize.width(), tileSize.height())));

    // Placed tiles first; then the never-placed ones take THE ROWS GRID'S
    // SLOTS (DESKTOP-1) — the first entry into Freeform shows the desktop as
    // the grid had it, instead of cascading every tile onto one pile at the
    // top-left and persisting the pile.
    int slot = 0;
    const bool realized = gridWidget->width() >= tileSize.width() * 2
                          && gridWidget->height() >= tileSize.height();
    foreach (ItemGridWidget *gridItem, originalItems) {
        if (gridItem->hasFreeformPos) placeFreeformTile(gridItem);
        gridItem->show();
    }
    foreach (ItemGridWidget *gridItem, originalItems) {
        if (gridItem->hasFreeformPos) { ++slot; continue; }
        if (!realized) { placeFreeformTile(gridItem); ++slot; continue; }   // deferred, as before
        const QSize cell = gridItem->size();
        const QPoint at = gridLayout->slotPos(slot, gridWidget->width(), cell);
        const int availW = qMax(1, gridWidget->width() - cell.width());
        const int availH = qMax(1, gridWidget->height() - cell.height());
        gridItem->normX = qBound(0.0, qreal(at.x()) / availW, 1.0);
        gridItem->normY = qBound(0.0, qreal(at.y()) / availH, 1.0);
        gridItem->hasFreeformPos = true;
        gridItem->move(pixelPosFor(gridItem));
        emit tilePositionChanged(gridItem);     // persist the assigned position
        ++slot;
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

    // A NEW TILE SIZE is the one path that needs every full picture again
    // (the cache holds tile-sized ones): decoded in parallel here, so the
    // setTileSize calls below are all hits.
    QVector<ProjectTileData> rows;
    rows.reserve(originalItems.size());
    for (ItemGridWidget *gridItem : std::as_const(originalItems)) rows.append(gridItem->tileData);
    ItemGridWidget::prefetchThumbnails(rows, tileSize);

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

    // Rows: the new size is each tile's; the flow re-lays itself out.
    foreach (ItemGridWidget *gridItem, originalItems) gridItem->setTileSize(tileSize, iconSize);
    gridLayout->invalidate();
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

    // Rows: a hidden tile takes no slot in the flow, so the matches close up
    // on their own.
    foreach (ItemGridWidget *gridItem, originalItems)
        gridItem->setVisible(searchString.isEmpty()
                             || gridItem->tileData.name.toLower().contains(searchString));
}

bool DynamicGrid::containsTiles()
{
    // freeform tiles are not layout-managed, so count the tiles themselves
    return !originalItems.isEmpty();
}

void DynamicGrid::deleteTile(ItemGridWidget *widget)
{
    originalItems.removeOne(widget);
    // Rows: out of the flow (freeform/slider tiles are free children of the
    // canvas and never in it); the flow closes the gap itself.
    const int index = gridLayout->indexOf(widget);
    if (index >= 0) delete gridLayout->takeAt(index);
    if (mode == LayoutMode::Sliders) sliderModel.removeTile(widget->tileData.guid);
    widget->hide();
    widget->deleteLater();
    if (mode == LayoutMode::Sliders) applySliderLayout();    // close the gap in its strip
}

void DynamicGrid::updateTile(const QString &id, const QByteArray &arr)
{
    if (ItemGridWidget *gridItem = tileByGuid(id)) gridItem->setThumbnail(arr);
}

ItemGridWidget *DynamicGrid::tile(const QString &guid) const
{
    return tileByGuid(guid);
}

// ONE TILE, AT THE HEAD (CREATE-GAP-1): the grid shows the desktop newest-first
// (Database::fetchProjects orders by last_written), so a project that was just
// made or just written belongs where a rebuild would have put it — first — and
// nothing else is rebuilt: the flow layout re-places the existing widgets, the
// freeform canvas places the one newcomer, the filmstrips reseed once.
void DynamicGrid::insertTileAtHead(const ProjectTileData &tileData, bool highlight)
{
    addToGridView(tileData, highlight);
    originalItems.move(originalItems.size() - 1, 0);
    if (mode == LayoutMode::Rows) gridLayout->setOrder(originalItems);
}

void DynamicGrid::moveTileToHead(ItemGridWidget *widget)
{
    const int at = originalItems.indexOf(widget);
    if (at <= 0) return;
    originalItems.move(at, 0);
    if (mode == LayoutMode::Rows) gridLayout->setOrder(originalItems);
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
    // The scroll area sizes the canvas (Rows: its flow lays itself out at the
    // new width — there is no column count to compare and nothing to skip).
    QScrollArea::resizeEvent(event);
    if (mode == LayoutMode::Freeform) applyFreeformLayout();  // normalized positions -> new canvas
    else if (mode == LayoutMode::Sliders) applySliderLayout(); // re-clamp offsets; vertical scroll
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
