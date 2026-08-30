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
#include "core/project.h"

class QGridLayout;
class SettingsManager;
class ItemGridWidget;


class DynamicGrid : public QScrollArea
{
    Q_OBJECT

public:
    // Desktops (DESKTOPS_SPEC.md): Rows is the classic sequential grid; Freeform lets
    // tiles sit anywhere on the desktop canvas at their stored normalized positions.
    enum class LayoutMode { Rows, Freeform };

    explicit DynamicGrid(QWidget *parent = Q_NULLPTR);
    void addToGridView(ProjectTileData tileData, int count);

    void setLayoutMode(LayoutMode mode);
    LayoutMode layoutMode() const { return mode; }
    void setCurrentDesktop(int desktop) { currentDesktop = desktop; }
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

private:
    void updateGridColumns(int width);
    int autoColumnCount;
    QSize sizeFromString(QString);

    // freeform helpers
    void applyFreeformLayout();                 // size the canvas + place every tile
    void placeFreeformTile(ItemGridWidget*);    // position one tile (cascade if unplaced)
    QPoint pixelPosFor(ItemGridWidget*) const;  // normalized -> canvas pixels

    QWidget *parent;
    QWidget *gridWidget;
    QGridLayout *gridLayout;
    SettingsManager *settings;

    LayoutMode mode = LayoutMode::Rows;
    int currentDesktop = 1;
};

#endif // DYNAMICGRID_H
