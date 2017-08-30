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
    scl = scale / 10.f;
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

    originalItems.push_back(gameGridItem);

    gameGridItem->setContextMenuPolicy(Qt::CustomContextMenu);

    connect(gameGridItem, SIGNAL(edit(ItemGridWidget*)), parent, SLOT(openProjectFromWidget(ItemGridWidget*)));
    connect(gameGridItem, SIGNAL(remove(ItemGridWidget*)), parent, SLOT(deleteProjectFromWidget(ItemGridWidget*)));
    connect(gameGridItem, SIGNAL(doubleClicked(ItemGridWidget*)), parent, SLOT(openProjectFromWidget(ItemGridWidget*)));
    connect(gameGridItem, SIGNAL(openFromWidget(ItemGridWidget*)), parent, SLOT(openProjectFromWidget(ItemGridWidget*)));
    connect(gameGridItem, SIGNAL(renameFromWidget(ItemGridWidget*)), parent, SLOT(renameProjectFromWidget(ItemGridWidget*)));
    connect(gameGridItem, SIGNAL(deleteFromWidget(ItemGridWidget*)), parent, SLOT(deleteProjectFromWidget(ItemGridWidget*)));

    int columnCount = viewport()->width() / (tileSize.width());

    if (columnCount == 0) columnCount = 1;

    gridLayout->addWidget(gameGridItem, count / columnCount + 1, count % columnCount + 1);
    gridWidget->adjustSize();
}

void DynamicGrid::scaleTile(QString scale)
{
    if (scale == "Small") {
        scl = 3 / 10.f;
    } else if (scale == "Large") {
        scl = 8 / 10.f;
    } else if (scale == "Huge") {
        scl = 1.f;
    } else {
        scl = 6 / 10.f;
    }

    QSize s = baseSize * (scl);

    tileSize.setWidth(s.width());
    tileSize.setHeight(s.height());

    int columnCount = lastWidth / (tileSize.width());

//    int gridCount = gridLayout->count();
//    QList<ItemGridWidget*> gridItems;
//    for (int count = 0; count < gridCount; count++)
//        gridItems << static_cast<ItemGridWidget*>(gridLayout->takeAt(0)->widget());

    int count = 0;
    foreach(ItemGridWidget *gridItem, originalItems) {
        gridItem->setTileSize(tileSize);
        gridLayout->addWidget(gridItem, count / columnCount + 1, count % columnCount + 1);
        count++;
    }

    gridWidget->adjustSize();
}

void DynamicGrid::searchTiles(QString searchString)
{
    // TODO - constant settings size
    QSize s = baseSize * (scl);

    tileSize.setWidth(s.width());
    tileSize.setHeight(s.height());

    int columnCount = lastWidth / (tileSize.width());

//    QLayoutItem *gridItem;
//    while ((gridItem = gridLayout->takeAt(0)) != NULL)
//    {
//        delete gridItem->widget();
//        delete gridItem;
//    }

//    gridLayout = new QGridLayout(gridWidget);
//    gridLayout->setSizeConstraint(QLayout::SetMinAndMaxSize);
//    gridLayout->setRowMinimumHeight(0, offset);

//    gridWidget->setLayout(gridLayout);

//    int gridCount = gridLayout->count();
//    QList<ItemGridWidget*> gridItems;
//    for (int count = 0; count < gridCount; count++) {
//        gridItems << static_cast<ItemGridWidget*>(gridLayout->takeAt(0)->widget());
//    }

//    QList<ItemGridWidget*> displayItems;

    int count = 0;
    if (!searchString.isEmpty()) {
        foreach(ItemGridWidget *gridItem, originalItems) {
            if (gridItem->name.toLower().contains(searchString)) {
//                qDebug() << "matched " << gridItem->name;
                gridItem->setVisible(true);
                gridItem->setTileSize(tileSize);
                gridLayout->addWidget(gridItem, count / columnCount + 1, count % columnCount + 1);
                count++;
            } else {
                gridItem->setVisible(false);
            }
        }
    } else {
        foreach(ItemGridWidget *gridItem, originalItems) {
            if (gridItem->name.toLower().contains(searchString)) {
//                qDebug() << "matched " << gridItem->name;
                gridItem->setVisible(true);
                gridItem->setTileSize(tileSize);
                gridLayout->addWidget(gridItem, count / columnCount + 1, count % columnCount + 1);
                count++;
            }
        }
    }

    gridWidget->adjustSize();
}

void DynamicGrid::deleteTile(ItemGridWidget *widget)
{
//    QLayoutItem *gridItem;
//    while ((gridItem = gridLayout->takeAt(0)) != NULL) {
//        delete gridItem->widget();
//        delete gridItem;
//    }

    QMutableListIterator<ItemGridWidget*> it(originalItems);
    while (it.hasNext()) {
        if (it.next()->projectName == widget->projectName) it.remove();
    }

    updateGridColumns(lastWidth);

////    QList<ItemGridWidget*> gridItems;
//    for (auto item : originalItems) {
//        if (item->projectName == widget->projectName) {
////            item->deleteLater();
//            originalItems.removeOne(item);
//        }
////        gridItems << static_cast<ItemGridWidget*>(gridLayout->takeAt(0)->widget());
//    }
}

void DynamicGrid::resizeEvent(QResizeEvent *event)
{
    lastWidth = event->size().width();

    int check = event->size().width() / (tileSize.width());
    bool autoAdjustColumns = true;

    if (autoAdjustColumns && check != autoColumnCount && check != 0) {
        autoColumnCount = check;
        updateGridColumns(event->size().width());
    } else
        QScrollArea::resizeEvent(event);
}

void DynamicGrid::updateGridColumns(int width)
{
    int columnCount = width / (tileSize.width());

//    int gridCount = gridLayout->count();
//    QList<ItemGridWidget*> gridItems;
//    for (int count = 0; count < gridCount; count++)
//        gridItems << static_cast<ItemGridWidget*>(gridLayout->takeAt(0)->widget());

    int count = 0;
    foreach(ItemGridWidget *gridItem, originalItems) {
        gridLayout->addWidget(gridItem, count / columnCount + 1, count % columnCount + 1);
        count++;
    }

    gridWidget->adjustSize();
}
