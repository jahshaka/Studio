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