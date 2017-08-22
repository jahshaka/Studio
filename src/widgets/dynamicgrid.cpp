#include "dynamicgrid.h"
#include "gridwidget.h"
#include <QGraphicsDropShadowEffect>
#include <QGridLayout>
#include <QLabel>
#include <QResizeEvent>

DynamicGrid::DynamicGrid(QWidget *parent) : QScrollArea(parent)
{
    this->parent = parent;

    setAlignment(Qt::AlignHCenter);

    gridWidget = new QWidget(this);
    gridWidget->setObjectName("gridWidget");
    setWidget(gridWidget);
//    setStyleSheet("background: #1e1e1e");

    gridLayout = new QGridLayout(gridWidget);
    gridLayout->setSizeConstraint(QLayout::SetMinAndMaxSize);
    gridLayout->setRowMinimumHeight(0, 10);

    gridWidget->setLayout(gridLayout);
}

void DynamicGrid::addToGridView(GridWidget *item, int count)
{
    QWidget *gameGridItem = new QWidget(gridWidget);
    gameGridItem->setMinimumWidth(200);
    gameGridItem->setMaximumWidth(200);
    gameGridItem->setContextMenuPolicy(Qt::CustomContextMenu);


//    QGraphicsDropShadowEffect *shadow = new QGraphicsDropShadowEffect;

////    if (active) {
//        shadow->setBlurRadius(8.0);
//        shadow->setColor(Qt::black);
//        shadow->setOffset(0);
//    gameGridItem->setGraphicsEffect(shadow);
//    }

    //Assign ROM data to widget for use in click events
//    gameGridItem->setProperty("fileName", currentRom->fileName);
//    gameGridItem->setProperty("directory", currentRom->directory);
//    if (currentRom->goodName == getTranslation("Unknown ROM") ||
//        currentRom->goodName == getTranslation("Requires catalog file"))
//        gameGridItem->setProperty("search", currentRom->internalName);
//    else
//        gameGridItem->setProperty("search", currentRom->goodName);
//    gameGridItem->setProperty("romMD5", currentRom->romMD5);
//    gameGridItem->setProperty("zipFile", currentRom->zipFile);

    QGridLayout *gameGridLayout = new QGridLayout(gameGridItem);
    gameGridLayout->setColumnStretch(0, 1);
    gameGridLayout->setColumnStretch(3, 1);
    gameGridLayout->setRowMinimumHeight(1, QSize(200, 200).height());

    QLabel *gridImageLabel = new QLabel(gameGridItem);
    gridImageLabel->setMinimumHeight(QSize(200, 200).height());
    gridImageLabel->setMinimumWidth(QSize(200, 200).width());
    QPixmap image;

//    if (currentRom->imageExists) {
        //Use uniform aspect ratio to account for fluctuations in TheGamesDB box art
        Qt::AspectRatioMode aspectRatioMode = Qt::IgnoreAspectRatio;

        //Don't warp aspect ratio though if image is too far away from standard size (JP box art)
        float aspectRatio = float(item->image.width()) / item->image.height();

        if (aspectRatio < 1.1 || aspectRatio > 1.8)
            aspectRatioMode = Qt::KeepAspectRatio;

        image = item->image.scaled(QSize(200, 200), Qt::KeepAspectRatio, Qt::SmoothTransformation);
//    } else {
//        if (ddEnabled && count == 0)
//            image = QPixmap(":/images/no-cart.png").scaled(getImageSize("Grid"), Qt::IgnoreAspectRatio,
//                                                             Qt::SmoothTransformation);
//        else
//            image = QPixmap(":/images/not-found.png").scaled(getImageSize("Grid"), Qt::IgnoreAspectRatio,
//                                                             Qt::SmoothTransformation);
//    }

    gridImageLabel->setPixmap(image);
    gridImageLabel->setAlignment(Qt::AlignCenter);
    gameGridLayout->addWidget(gridImageLabel, 1, 1);

    gameGridItem->setLayout(gameGridLayout);

    gameGridItem->setMinimumHeight(gameGridItem->sizeHint().height());

    int columnCount;
//    if (SETTINGS.value("Grid/autocolumns","true").toString() == "true")
        columnCount = viewport()->width() / (200 + 10);
//    else
//        columnCount = SETTINGS.value("Grid/columncount", "4").toInt();

    if (columnCount == 0) columnCount = 1;

    gridLayout->addWidget(gameGridItem, count / columnCount + 1, count % columnCount + 1);
    gridWidget->adjustSize();
}

void DynamicGrid::resizeEvent(QResizeEvent *event)
{
    int check = event->size().width() / (200 + 10);
    bool autoAdjustColumns = true;

    if (autoAdjustColumns && check != autoColumnCount && check != 0) {
        autoColumnCount = check;
        updateGridColumns(event->size().width());
    } else
        QScrollArea::resizeEvent(event);
}

void DynamicGrid::updateGridColumns(int width)
{
    int columnCount = width / (200 + 10);

    int gridCount = gridLayout->count();
    QList<QWidget*> gridItems;
    for (int count = 0; count < gridCount; count++)
        gridItems << gridLayout->takeAt(0)->widget();

    int count = 0;
    foreach(QWidget *gridItem, gridItems)
    {
        gridLayout->addWidget(gridItem, count / columnCount + 1, count % columnCount + 1);
        count++;
    }

    gridWidget->adjustSize();
}
