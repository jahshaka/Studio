#include "dynamicgrid.h"
#include "gridwidget.h"
#include <QGraphicsDropShadowEffect>
#include <QGridLayout>
#include <QLabel>
#include <QResizeEvent>

#include <QDebug>

#include "itemgridwidget.hpp"

DynamicGrid::DynamicGrid(QWidget *parent) : QScrollArea(parent)
{
    this->parent = parent;

    setAlignment(Qt::AlignHCenter);

    gridWidget = new QWidget(parent);
    gridWidget->setObjectName("gridWidget");
    setWidget(gridWidget);
//    setStyleSheet("background: #1e1e1e");

    scale = 6;
    baseSize = QSize(460, 215);
    tileSize = baseSize * 0.6;

    offset = 10;

    gridLayout = new QGridLayout(gridWidget);
    gridLayout->setSizeConstraint(QLayout::SetMinAndMaxSize);
    gridLayout->setRowMinimumHeight(0, offset);

    gridWidget->setLayout(gridLayout);
}

void DynamicGrid::addToGridView(GridWidget *item, int count)
{
    ItemGridWidget *gameGridItem = new ItemGridWidget(item, tileSize, gridWidget);

    connect(gameGridItem, SIGNAL(doubleClicked(ItemGridWidget*)), parent, SLOT(openProjectFromWidget(ItemGridWidget*)));

    int columnCount = viewport()->width() / (tileSize.width() + offset);

    if (columnCount == 0) columnCount = 1;

    gridLayout->addWidget(gameGridItem, count / columnCount + 1, count % columnCount + 1);
    gridWidget->adjustSize();
}

void DynamicGrid::scaleTile(int scale)
{
    float scl = 0;
    scl = scale / 10.f;
//    auto w = baseSize.width() + (scale * 4);
//    auto h = baseSize.height() + (scale * 4);
    QSize s = baseSize * (scl);

    tileSize.setWidth(s.width());
    tileSize.setHeight(s.height());

    int columnCount = lastWidth / (tileSize.width() + offset);

    int gridCount = gridLayout->count();
    QList<ItemGridWidget*> gridItems;
    for (int count = 0; count < gridCount; count++)
        gridItems << static_cast<ItemGridWidget*>(gridLayout->takeAt(0)->widget());

    int count = 0;
    foreach(ItemGridWidget *gridItem, gridItems) {
        gridItem->setTileSize(tileSize);
        gridLayout->addWidget(gridItem, count / columnCount + 1, count % columnCount + 1);
        count++;
    }

    gridWidget->adjustSize();
}

void DynamicGrid::resizeEvent(QResizeEvent *event)
{
    lastWidth = event->size().width();

    int check = event->size().width() / (tileSize.width() + offset);
    bool autoAdjustColumns = true;

    if (autoAdjustColumns && check != autoColumnCount && check != 0) {
        autoColumnCount = check;
        updateGridColumns(event->size().width());
    } else
        QScrollArea::resizeEvent(event);
}

void DynamicGrid::updateGridColumns(int width)
{
    int columnCount = width / (tileSize.width() + offset);

    int gridCount = gridLayout->count();
    QList<ItemGridWidget*> gridItems;
    for (int count = 0; count < gridCount; count++)
        gridItems << static_cast<ItemGridWidget*>(gridLayout->takeAt(0)->widget());

    int count = 0;
    foreach(ItemGridWidget *gridItem, gridItems) {
        gridLayout->addWidget(gridItem, count / columnCount + 1, count % columnCount + 1);
        count++;
    }

    gridWidget->adjustSize();
}
