#include "assetviewgrid.h"
#include "assetgriditem.h"
#include "assetview.h"

AssetViewGrid::AssetViewGrid(QWidget *parent) : QScrollArea(parent) {
	this->parent = parent;
	gridWidget = new QWidget(this);
	_layout = new QGridLayout;
	_layout->setMargin(0);
	_layout->setSpacing(12);
	gridCounter = 0;
	gridWidget->setLayout(_layout);
	setAlignment(Qt::AlignHCenter);
	//setWidgetResizable(true);
	setWidget(gridWidget);
	setStyleSheet("background: transparent");
}

void AssetViewGrid::updateImage() {

}

// local
void AssetViewGrid::addTo(AssetGridItem *item, int count)
{
	int columnCount = viewport()->width() / (180 + 10);
	if (columnCount == 0) columnCount = 1;

	originalItems.push_back(item);

	connect(item, &AssetGridItem::singleClicked, [this](AssetGridItem *item) {
		emit selectedTile(item);
	});

	_layout->addWidget(item, count / columnCount + 1, count % columnCount + 1);
	gridWidget->adjustSize();

	emit gridCount(_layout->count());
}

void AssetViewGrid::addTo(QJsonObject details, QImage image, int count) {
	auto sampleWidget = new AssetGridItem(details, image);

	int columnCount = viewport()->width() / (180 + 10);
	if (columnCount == 0) columnCount = 1;

	originalItems.push_back(sampleWidget);

	connect(sampleWidget, &AssetGridItem::singleClicked, [this](AssetGridItem *item) {
		//qobject_cast<AssetView*>(parent)->fetchMetadata(item);
        emit selectedTile(item);
	});

	_layout->addWidget(sampleWidget, count / columnCount + 1, count % columnCount + 1);
	gridWidget->adjustSize();

	emit gridCount(_layout->count());
}

void AssetViewGrid::resizeEvent(QResizeEvent *event)
{
	lastWidth = event->size().width();
	int check = event->size().width() / (180 + 10);
	//gridWidget->setMinimumWidth(viewport()->width());

	if (check != 0) {
		updateGridColumns(event->size().width());
	}
	
	QScrollArea::resizeEvent(event);
}

void AssetViewGrid::mousePressEvent(QMouseEvent *event)
{
	if (event->button() == Qt::LeftButton) {
		emit selectedTile(new AssetGridItem(QJsonObject(), QImage()));
	}

	if (event->button() == Qt::RightButton) {
		emit contextSelected(new AssetGridItem(QJsonObject(), QImage()));
	}
}

void AssetViewGrid::deleteTile(AssetGridItem *widget)
{
    int index = _layout->indexOf(widget);
    if (index != -1) {
        int row, col, col_span, row_span;
        _layout->getItemPosition(index, &row, &col, &col_span, &row_span);

        auto w = _layout->itemAtPosition(row, col)->widget();
        auto idx = _layout->layout()->indexOf(w);
        auto item = _layout->takeAt(idx);
        deleteChildWidgets(item);
        item->widget()->deleteLater();

        originalItems.removeOne(widget);
        updateGridColumns(lastWidth);

        emit gridCount(_layout->count());
    }
}

void AssetViewGrid::deleteChildWidgets(QLayoutItem *item) {
    if (item->layout()) {
        // Process all child items recursively.
        for (int i = 0; i < item->layout()->count(); i++) {
            deleteChildWidgets(item->layout()->itemAt(i));
        }
    }

    // delete item->widget();
    item->widget()->deleteLater();
}

void AssetViewGrid::searchTiles(QString searchString)
{
	int columnCount = lastWidth / (180 + 10);

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
	int columnCount = lastWidth / (180 + 10);

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
	int columnCount = width / (180 + 10);
	if (columnCount == 0) columnCount = 1;

	int count = 0;
	foreach(auto gridItem, originalItems) {
		_layout->addWidget(gridItem, count / columnCount + 1, count % columnCount + 1);
		count++;
	}

	// gridWidget->setMinimumWidth(gridCount * (180 + 10));
	gridWidget->adjustSize();
}

void AssetViewGrid::deselectAll()
{
	foreach(AssetGridItem *gridItem, originalItems) {
		gridItem->selected = false;
		gridItem->highlight(false);
	}
}