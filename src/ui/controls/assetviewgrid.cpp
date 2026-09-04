/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/controls/assetviewgrid.h"
#include "ui/controls/assetgriditem.h"
#include "ui/pages/assetview.h"

AssetViewGrid::AssetViewGrid(QWidget *parent) : QScrollArea(parent) {
	this->parent = parent;
	gridWidget = new QWidget(this);
	_layout = new QGridLayout;
	// Tiles clear the filter/search toolbar and the pane's left edge (owner
	// direction 2026-08-31) instead of running flush against both.
	_layout->setContentsMargins(12, 12, 12, 12);
	_layout->setSpacing(12);
	gridCounter = 0;
	gridWidget->setLayout(_layout);
	// File-browser flow (owner direction): rows fill left→right from the
	// top-left — the grid widget anchors to the viewport's top-left instead
	// of floating centered.
	setAlignment(Qt::AlignLeft | Qt::AlignTop);
	//setWidgetResizable(true);
	setWidget(gridWidget);
	// Frameless in both themes — the border:0 sheet alone doesn't stop
	// Qlementine drawing the default QFrame around the scroll area.
	setFrameShape(QFrame::NoFrame);
    setStyleSheet("background: #202020; border: 0");

	setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
}

void AssetViewGrid::updateImage() {

}

// local
void AssetViewGrid::addTo(AssetGridItem *item, int count, bool select)
{
	int columnCount = viewport()->width() / (128 + 10);
	if (columnCount == 0) columnCount = 1;

	originalItems.push_back(item);

	// Tile interaction flip (ASSET_DRAWERS_SPEC §1): a plain click is only a
	// subtle highlight; double-click selects and loads the preview.
	connect(item, &AssetGridItem::singleClicked, [this](AssetGridItem *item) {
		deselectAll();
		item->highlight(true);
		emit lightSelectedTile(item);
	});

	connect(item, &AssetGridItem::doubleClicked, [this](AssetGridItem *item) {
		emit selectedTile(item);
	});

	connect(item, &AssetGridItem::specialClicked, [this](AssetGridItem *item) {
		emit selectedTileToAdd(item);
	});

	if (select) emit selectedTile(item);

	_layout->addWidget(item, count / columnCount + 1, count % columnCount + 1);
	gridWidget->adjustSize();

	emit gridCount(_layout->count());
}

void AssetViewGrid::addTo(QJsonObject details, QImage image, int count, QJsonObject properties, QJsonObject tags, bool select) {
	auto sampleWidget = new AssetGridItem(details, image, properties, tags);

	int columnCount = viewport()->width() / (128 + 10);
	if (columnCount == 0) columnCount = 1;

	originalItems.push_back(sampleWidget);

	connect(sampleWidget, &AssetGridItem::singleClicked, [this](AssetGridItem *item) {
		deselectAll();
		item->highlight(true);
		emit lightSelectedTile(item);
	});

	connect(sampleWidget, &AssetGridItem::doubleClicked, [this](AssetGridItem *item) {
		emit selectedTile(item);
	});

	connect(sampleWidget, &AssetGridItem::specialClicked, [this](AssetGridItem *item) {
		emit selectedTileToAdd(item);
	});

	if (select) emit selectedTile(sampleWidget);

	_layout->addWidget(sampleWidget, count / columnCount + 1, count % columnCount + 1);
	gridWidget->adjustSize();

	emit gridCount(_layout->count());
}

void AssetViewGrid::resizeEvent(QResizeEvent *event)
{
	lastWidth = event->size().width();
	int check = event->size().width() / (128 + 10);
	//gridWidget->setMinimumWidth(viewport()->width());

	if (check != 0) {
		updateGridColumns(event->size().width());
	}
	
	QScrollArea::resizeEvent(event);
}

AssetGridItem *AssetViewGrid::emptySelectionTile()
{
	if (!emptySelection) {
		emptySelection = new AssetGridItem(QJsonObject(), QImage(), QJsonObject(), QJsonObject(), this);
		emptySelection->hide();
	}
	return emptySelection;
}

void AssetViewGrid::mousePressEvent(QMouseEvent *event)
{
	// A click on empty canvas means "nothing selected": receivers read the
	// (empty) metadata and clear their panes. One reused hidden tile — this
	// used to allocate a fresh AssetGridItem per click and drop it.
	if (event->button() == Qt::LeftButton) {
		emit selectedTile(emptySelectionTile());
	}
	// The RightButton branch emitted contextSelected — a signal with no
	// connection anywhere in the app — and leaked another tile doing it.
	// Removed with the signal.
}

void AssetViewGrid::deleteTile(AssetGridItem *widget)
{
    const int index = _layout->indexOf(widget);
    if (index == -1) return;

    // takeAt returns an OWNED QLayoutItem (the QWidgetItem wrapper): the old
    // code dropped it, and then called deleteLater() on the widget twice —
    // once through deleteChildWidgets and once directly.
    QLayoutItem *item = _layout->takeAt(index);
    if (item) {
        deleteChildWidgets(item);
        delete item;
    }

    originalItems.removeOne(widget);
    if (emptySelection == widget) emptySelection = nullptr;
    updateGridColumns(lastWidth);

    emit gridCount(_layout->count());
}

void AssetViewGrid::deleteChildWidgets(QLayoutItem *item) {
    if (!item) return;
    if (item->layout()) {
        // Process all child items recursively.
        for (int i = 0; i < item->layout()->count(); i++) {
            deleteChildWidgets(item->layout()->itemAt(i));
        }
    }

    // Spacers and stretches have no widget.
    if (QWidget *w = item->widget()) w->deleteLater();
}

void AssetViewGrid::searchTiles(QString searchString)
{
	int columnCount = lastWidth / (128 + 10);

	int count = 0;
	if (!searchString.isEmpty()) {
		foreach (AssetGridItem *gridItem, originalItems) {
			if (gridItem->textLabel->text().toLower().contains(searchString)) {
				gridItem->setVisible(true);
				_layout->addWidget(gridItem, count / columnCount + 1, count % columnCount + 1);
				count++;
			}
			else {
				gridItem->setVisible(false);
			}
		}
	}
	else {
		foreach(AssetGridItem *gridItem, originalItems) {
			gridItem->setVisible(true);
			_layout->addWidget(gridItem, count / columnCount + 1, count % columnCount + 1);
			count++;
		}
	}

	gridWidget->adjustSize();
}

void AssetViewGrid::filterAssets(int id)
{
	int columnCount = lastWidth / (128 + 10);

	int count = 0;
	if (id != -1) {
		foreach(AssetGridItem *gridItem, originalItems) {
			if (gridItem->metadata["collection"].toInt() == id) {
				gridItem->setVisible(true);
				_layout->addWidget(gridItem, count / columnCount + 1, count % columnCount + 1);
				count++;
			}
			else {
				gridItem->setVisible(false);
			}
		}
	}
	else {
		foreach(AssetGridItem *gridItem, originalItems) {
			gridItem->setVisible(true);
			_layout->addWidget(gridItem, count / columnCount + 1, count % columnCount + 1);
			count++;
		}
	}

	gridWidget->adjustSize();
}

void AssetViewGrid::updateGridColumns(int width)
{
	int columnCount = width / (128 + 10);
	if (columnCount == 0) columnCount = 1;

	int count = 0;
	foreach(auto gridItem, originalItems) {
		_layout->addWidget(gridItem, count / columnCount + 1, count % columnCount + 1);
		count++;
	}

	// gridWidget->setMinimumWidth(gridCount * (180 + 10));
	gridWidget->adjustSize();
}

AssetGridItem *AssetViewGrid::tileByGuid(const QString &guid)
{
	foreach(AssetGridItem *gridItem, originalItems) {
		if (gridItem->metadata["guid"].toString() == guid) return gridItem;
	}
	return nullptr;
}

void AssetViewGrid::reassignCollections(const QVector<int> &from, int to, const QString &toName)
{
	foreach(AssetGridItem *gridItem, originalItems) {
		if (from.contains(gridItem->metadata["collection"].toInt())) {
			gridItem->metadata["collection"] = to;
			gridItem->metadata["collection_name"] = toName;
		}
	}
}

void AssetViewGrid::lightSelectTile(AssetGridItem *item)
{
	if (!item) return;
	deselectAll();
	item->highlight(true);
	emit lightSelectedTile(item);
}

void AssetViewGrid::deselectAll()
{
	foreach(AssetGridItem *gridItem, originalItems) {
		gridItem->selected = false;
		gridItem->highlight(false);
	}
}