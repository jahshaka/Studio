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
	/// The last width a resizeEvent reported. ZERO UNTIL THE FIRST ONE, and
	/// that is a value this class must survive: the tray is POPULATED BEFORE
	/// IT IS SHOWN, so a search, a filter or a tile removal can run before any
	/// resize has happened. It used to be uninitialised, which made the column
	/// count whatever the heap held (UNINIT-SWEEP-1 A1-4).
	int lastWidth = 0;
	/// THE COLUMN COUNT, in ONE place. It was written out three times and only
	/// updateGridColumns() guarded the zero — so with `lastWidth` honest at 0,
	/// searchTiles() and filterAssets() divided by it. One tile column is what
	/// a grid too narrow for one tile has always laid out.
	static int columnsFor(int width) {
		const int c = width / (128 + 10);
		return c > 0 ? c : 1;
	}
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
	/// Every tile (visible or filtered out) — the list view renders from the
	/// same set the grid owns.
	const QList<AssetGridItem*> &tiles() const { return originalItems; }
	/// Programmatic equivalents of the tile gestures, so the list view's rows
	/// drive the very same selection/preview plumbing.
	void selectTile(AssetGridItem *item) { emit selectedTile(item); }
	void lightSelectTile(AssetGridItem *item);
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
	/// The "nothing is selected" payload for a click on empty canvas.
	/// selectedTile carries a tile pointer, so the empty case needs an object;
	/// mousePressEvent used to `new` a throwaway AssetGridItem per click and
	/// leak it. One parented, hidden instance, reused.
	AssetGridItem *emptySelection = nullptr;
	AssetGridItem *emptySelectionTile();

signals:
	void gridCount(int);
    void selectedTile(AssetGridItem*);
	/// Plain click: the tile becomes current for pane/button actions
	/// WITHOUT loading anything into the preview (the tile flip kept
	/// loading on double-click only).
	void lightSelectedTile(AssetGridItem*);
	void selectedTileToAdd(AssetGridItem*);
};

#endif // ASSETVIEWGRID_HPP