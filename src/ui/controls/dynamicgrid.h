/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef DYNAMICGRID_H
#define DYNAMICGRID_H

#include <QScrollArea>
#include "data/project.h"
#include "ui/controls/sliderlayoutmodel.h"

class QGridLayout;
class SettingsManager;
class ItemGridWidget;


class DynamicGrid : public QScrollArea
{
    Q_OBJECT

public:
    // Desktops (DESKTOPS_SPEC.md): Rows is the classic sequential grid; Freeform lets
    // tiles sit anywhere on the desktop canvas at their stored normalized positions.
    // Sliders (DESKTOP_SLIDER_SPEC.md) stacks N filmstrip rows that slide
    // left/right independently (grab empty space to pan, wheel slides too).
    enum class LayoutMode { Rows, Freeform, Sliders };

    explicit DynamicGrid(QWidget *parent = Q_NULLPTR);
    void addToGridView(ProjectTileData tileData, int count, bool highlight = false);

    void setLayoutMode(LayoutMode mode);
    LayoutMode layoutMode() const { return mode; }
    void setCurrentDesktop(int desktop) { currentDesktop = desktop; }

    // sliders: insert-at semantics (index < 0 appends); relayouts + persists
    void moveTileToRow(ItemGridWidget *widget, int row, int index = -1);
    const SliderLayoutModel &sliderLayoutModel() const { return sliderModel; }
    int activeSliderRows() const { return sliderRows; }

    bool eventFilter(QObject *watched, QEvent *event) override;
    QSize tileSize;
    QSize iconSize;
    QSize baseSize;
    int lastWidth;
    QList<ItemGridWidget*> originalItems;
    int scale;
    int offset;
    float scl;
    void scaleTile(QString);
    void searchTiles(QString);
    bool containsTiles();
    void deleteTile(ItemGridWidget*);

	void updateTile(const QString &id, const QByteArray &arr);

    void resetView();

protected:
    void resizeEvent(QResizeEvent *event);

signals:
    // freeform: a tile was dragged, or the cascade rule assigned a first position —
    // the ProjectManager persists widget->normX/normY for widget->tileData.guid
    void tilePositionChanged(ItemGridWidget *widget);

    // sliders: the tile's {row, orderIndex} changed (drop, move-to-row, or the
    // seeding on first show) — the ProjectManager persists sliderRow/sliderIndex
    void tileSliderPositionChanged(ItemGridWidget *widget);

private:
    void updateGridColumns(int width);
    int autoColumnCount;
    QSize sizeFromString(QString);

    // freeform helpers
    void applyFreeformLayout();                 // size the canvas + place every tile
    void placeFreeformTile(ItemGridWidget*);    // position one tile (cascade if unplaced)
    QPoint pixelPosFor(ItemGridWidget*) const;  // normalized -> canvas pixels

    // slider helpers (DESKTOP_SLIDER_SPEC.md)
    void rebuildSliderModel(LayoutMode seedFrom);   // widgets -> model (+seed unassigned)
    void syncSliderAssignments();                   // model -> widgets, emit changes
    void applySliderLayout();                       // size the canvas + place every strip
    void positionSliderRow(int row);                // lay one strip at its clamped offset
    void scheduleSliderRelayout();                  // coalesce per-add rebuilds
    int sliderRowHeight() const;
    int sliderRowAt(int y) const;                   // canvas y -> row
    void handleSliderDrop(ItemGridWidget*);         // drop pos -> {row, insert index}
    ItemGridWidget *tileByGuid(const QString &guid) const;

    QWidget *parent;
    QWidget *gridWidget;
    QGridLayout *gridLayout;
    SettingsManager *settings;

    LayoutMode mode = LayoutMode::Rows;
    int currentDesktop = 1;

    // slider state (per-session; assignments persist via tileSliderPositionChanged)
    SliderLayoutModel sliderModel;
    int sliderRows = 6;                 // live "slider_rows" setting (2..10)
    bool sliderRelayoutPending = false; // a coalesced rebuild is queued
    bool rowPanning = false;            // grab-empty-space pan in progress
    int panRow = -1;
    int panStartX = 0;
    qreal panStartOffset = 0.0;
};

#endif // DYNAMICGRID_H
