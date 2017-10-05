#include "dynamicgrid.h"
#include "gridwidget.h"
#include <QGraphicsDropShadowEffect>
#include <QGridLayout>
#include <QLabel>
#include <QResizeEvent>

#include <QDebug>

#include "itemgridwidget.hpp"
#include "../constants.h"
#include "../core/settingsmanager.h"
#include "projectmanager.h"

DynamicGrid::DynamicGrid(QWidget *parent) : QScrollArea(parent)
{
    this->parent = parent;

    setAlignment(Qt::AlignHCenter);

    gridWidget = new QWidget(parent);
    gridWidget->setObjectName("gridWidget");
    setWidget(gridWidget);
    setStyleSheet("background: transparent");

    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    offset = 10;
    settings = SettingsManager::getDefaultManager();
    tileSize = sizeFromString(settings->getValue("tileSize", "Normal").toString());

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

    // remember that parent is the ProjectManager, we delegate or emit up
    connect(gameGridItem,   SIGNAL(openFromWidget(ItemGridWidget*, bool)),
            parent,         SLOT(openProjectFromWidget(ItemGridWidget*, bool)));

    connect(gameGridItem,   SIGNAL(remove(ItemGridWidget*)),
            parent,         SLOT(deleteProjectFromWidget(ItemGridWidget*)));

    // lambdas! we can do this instead of using classic syntax & forcing dblClick to accept 2 args
    connect(gameGridItem,   &ItemGridWidget::doubleClicked, parent, [this](ItemGridWidget *item) {
        static_cast<ProjectManager*>(parent)->openProjectFromWidget(item, false);
    });

    connect(gameGridItem,   SIGNAL(renameFromWidget(ItemGridWidget*)),
            parent,         SLOT(renameProjectFromWidget(ItemGridWidget*)));

    connect(gameGridItem,   SIGNAL(deleteFromWidget(ItemGridWidget*)),
            parent,         SLOT(deleteProjectFromWidget(ItemGridWidget*)));

    int columnCount = viewport()->width() / (tileSize.width());

    if (columnCount == 0) columnCount = 1;

    gridLayout->addWidget(gameGridItem, count / columnCount + 1, count % columnCount + 1);
    gridWidget->adjustSize();
}

void DynamicGrid::scaleTile(QString scale)
{
    QSize size = sizeFromString(scale);
    tileSize.setWidth(size.width());
    tileSize.setHeight(size.height());

    int columnCount = lastWidth / (tileSize.width());

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
    int columnCount = lastWidth / (tileSize.width());

    int count = 0;
    if (!searchString.isEmpty()) {
        foreach(ItemGridWidget *gridItem, originalItems) {
            if (gridItem->name.toLower().contains(searchString)) {
                gridItem->setVisible(true);
                gridItem->setTileSize(tileSize);
                gridLayout->addWidget(gridItem, count / columnCount + 1, count % columnCount + 1);
                count++;
            } else {
                gridItem->setVisible(false);
            }
        }
    } else {
        foreach (ItemGridWidget *gridItem, originalItems) {
            gridItem->setVisible(true);
            gridItem->setTileSize(tileSize);
            gridLayout->addWidget(gridItem, count / columnCount + 1, count % columnCount + 1);
            count++;
        }
    }

    gridWidget->adjustSize();
}

void DynamicGrid::deleteTile(ItemGridWidget *widget)
{
    QMutableListIterator<ItemGridWidget*> it(originalItems);
    while (it.hasNext()) {
        if (it.next()->projectName == widget->projectName) it.remove();
    }

    updateGridColumns(lastWidth);
}

void DynamicGrid::resetView()
{
    QMutableListIterator<ItemGridWidget*> it(originalItems);
    while (it.hasNext()) {
        if (it.next()) {
            it.remove();
//            updateGridColumns(lastWidth);
        }
    }

    QLayoutItem *gridItem;
        while ((gridItem = gridLayout->takeAt(0)) != NULL)
        {
            delete gridItem->widget();
            delete gridItem;
        }

    originalItems.clear();
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

    int count = 0;
    foreach(ItemGridWidget *gridItem, originalItems) {
        gridLayout->addWidget(gridItem, count / columnCount + 1, count % columnCount + 1);
        count++;
    }

    gridWidget->adjustSize();
}

QSize DynamicGrid::sizeFromString(QString size)
{
    if (size == "Small") {
        return Constants::TILE_SIZE * 0.3;
    } else if (size == "Large") {
        return Constants::TILE_SIZE * 0.8;
    } else if (size == "Huge") {
        return Constants::TILE_SIZE;
    } else {
        return Constants::TILE_SIZE * 0.6;
    }
}
