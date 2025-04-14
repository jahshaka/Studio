/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef DYNAMICGRID_H
#define DYNAMICGRID_H

#include <QScrollArea>
#include "../core/project.h"

class QGridLayout;
class SettingsManager;
class ItemGridWidget;


class DynamicGrid : public QScrollArea
{
    Q_OBJECT

public:
    explicit DynamicGrid(QWidget *parent = Q_NULLPTR);
    void addToGridView(ProjectTileData tileData, int count);
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

private:
    void updateGridColumns(int width);
    int autoColumnCount;
    QSize sizeFromString(QString);

    QWidget *parent;
    QWidget *gridWidget;
    QGridLayout *gridLayout;
    SettingsManager *settings;
};

#endif // DYNAMICGRID_H
