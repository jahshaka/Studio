#include "itemgridwidget.hpp"
#include "gridwidget.h"
#include <QDebug>
#include <QGraphicsDropShadowEffect>
ItemGridWidget::ItemGridWidget(GridWidget *item, QSize size, QWidget *parent) : QWidget(parent)
{
    tileSize = size;
    this->parent = parent;

    setParent(parent);

    setMinimumWidth(tileSize.width());
    setMaximumWidth(tileSize.width());

    gameGridLayout = new QGridLayout(this);
    gameGridLayout->setColumnStretch(0, 1);
    gameGridLayout->setColumnStretch(3, 1);
    gameGridLayout->setRowMinimumHeight(1, tileSize.height());

    gridImageLabel = new QLabel(this);
    gridImageLabel->setMinimumHeight(tileSize.height());
    gridImageLabel->setMinimumWidth(tileSize.width());

    oimage = item->image;
    image = item->image.scaled(tileSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    gridImageLabel->setPixmap(image);
    gridImageLabel->setAlignment(Qt::AlignCenter);

    gameGridLayout->addWidget(gridImageLabel, 1, 1);

    QGraphicsDropShadowEffect *shadow = new QGraphicsDropShadowEffect;
    shadow->setColor(Qt::black);
    shadow->setOffset(0);
    shadow->setBlurRadius(10.f);
    setGraphicsEffect(shadow);

    setLayout(gameGridLayout);
    setMinimumHeight(this->sizeHint().height());
}

void ItemGridWidget::setTileSize(QSize size)
{
    tileSize = size;

    setMinimumWidth(tileSize.width());
    setMaximumWidth(tileSize.width());

    gameGridLayout->setColumnStretch(0, 1);
    gameGridLayout->setColumnStretch(3, 1);
    gameGridLayout->setRowMinimumHeight(1, tileSize.height());

    gridImageLabel->setMinimumHeight(tileSize.height());
    gridImageLabel->setMinimumWidth(tileSize.width());

//    qDebug() << tileSize;

    auto img = oimage.scaled(tileSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    gridImageLabel->setPixmap(img);
    gridImageLabel->setAlignment(Qt::AlignCenter);

//    gameGridLayout->addWidget(gridImageLabel, 1, 1);

    setMinimumHeight(this->sizeHint().height());
}

void ItemGridWidget::updateImage()
{

}

QWidget *ItemGridWidget::get()
{
    return gameGridItem;
}
