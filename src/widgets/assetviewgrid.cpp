#include "assetviewgrid.h"
#include "assetgriditem.h"
#include "assetview.h"

AssetViewGrid::AssetViewGrid(QWidget *parent) : QScrollArea(parent) {
	this->parent = parent;
	gridWidget = new QWidget(this);
	fastGrid = new QGridLayout();
	fastGrid->setMargin(0);
	fastGrid->setSpacing(12);
	gridWidget->setLayout(fastGrid);
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

	connect(sampleWidget, &AssetGridItem::singleClicked, parent, [this](AssetGridItem *item) {
		qobject_cast<AssetView*>(parent)->fetchMetadata(item);
	});

	fastGrid->addWidget(sampleWidget, count / columnCount + 1, count % columnCount + 1);
	gridWidget->adjustSize();

	emit gridCount(fastGrid->count());
}

void AssetViewGrid::resizeEvent(QResizeEvent *event)
{
	lastWidth = event->size().width();
	int check = event->size().width() / (180 + 10);
	//gridWidget->setMinimumWidth(viewport()->width());

	if (check != 0) {
		updateGridColumns(event->size().width());
	}
	else
		QScrollArea::resizeEvent(event);
}

void AssetViewGrid::updateGridColumns(int width)
{
	int columnCount = width / (180 + 10);
	if (columnCount == 0) columnCount = 1;

	int gridCount = fastGrid->count();
	QList<AssetGridItem*> gridItems;
	for (int count = 0; count < gridCount; count++)
		gridItems << static_cast<AssetGridItem*>(fastGrid->takeAt(0)->widget());

	int count = 0;
	foreach(AssetGridItem *gridItem, gridItems) {
		fastGrid->addWidget(gridItem, count / columnCount + 1, count % columnCount + 1);
		count++;
	}

	// gridWidget->setMinimumWidth(gridCount * (180 + 10));
	gridWidget->adjustSize();
}