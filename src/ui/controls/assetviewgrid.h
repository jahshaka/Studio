/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef ASSETVIEWGRID_HPP
#define ASSETVIEWGRID_HPP

#include <QScrollArea>
#include <QGridLayout>
#include <QJsonObject>
#include <QResizeEvent>

class AssetGridItem;

class AssetViewGrid : public QScrollArea
{
	Q_OBJECT

public:
	QGridLayout *_layout;
	int lastWidth;
	QWidget *parent;

	AssetViewGrid(QWidget *parent);

	void updateImage();

    bool containsTiles() {
        if (_layout->count()) return true;
        return false;
    }
	void addTo(AssetGridItem *widget, int count, bool select = false);
	void addTo(QJsonObject details, QImage image, int count, QJsonObject properties, QJsonObject tags, bool select = false);
	/// The tile showing this asset, or null.
	AssetGridItem *tileByGuid(const QString &guid);
	/// Rewrites tiles' drawer metadata after a drawer delete moved their
	/// assets to Uncategorized (ASSET_DRAWERS_SPEC §2).
	void reassignCollections(const QVector<int> &from, int to, const QString &toName);
	void resizeEvent(QResizeEvent *event);
	void mousePressEvent(QMouseEvent*);
	void updateGridColumns(int width);
	void deselectAll();
	void searchTiles(QString);
    void deleteTile(AssetGridItem *widget);
    void deleteChildWidgets(QLayoutItem *item);
	void filterAssets(int id);

private:
	int gridCounter;
	QWidget *gridWidget;
	QList<AssetGridItem*> originalItems;

signals:
	void gridCount(int);
    void selectedTile(AssetGridItem*);
	void contextSelected(AssetGridItem*);
	void selectedTileToAdd(AssetGridItem*);
};

#endif // ASSETVIEWGRID_HPP