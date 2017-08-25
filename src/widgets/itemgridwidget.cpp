#include "itemgridwidget.hpp"
#include "gridwidget.h"
#include <QDebug>
#include <QFileInfo>
#include <QGraphicsDropShadowEffect>
#include <QMouseEvent>
#include <QPushButton>
ItemGridWidget::ItemGridWidget(GridWidget *item, QSize size, QWidget *parent) : QWidget(parent)
{
    tileSize = size;
    this->parent = parent;

    projectName = item->projectName;
    auto fileName = QFileInfo(projectName);

    setParent(parent);

    setMinimumWidth(tileSize.width() + 10);
    setMaximumWidth(tileSize.width() + 10);

    gameGridLayout = new QGridLayout(this);
    gameGridLayout->setColumnStretch(0, 1);
    gameGridLayout->setColumnStretch(3, 1);
    gameGridLayout->setRowMinimumHeight(1, tileSize.height());

    gridImageLabel = new QLabel(this);
    gridImageLabel->setMinimumHeight(tileSize.height());
    gridImageLabel->setMinimumWidth(tileSize.width());

    gridTextLabel = new QLabel(this);

    //Don't allow label to be wider than image
    gridTextLabel->setMaximumWidth(tileSize.width());
    gridTextLabel->setText(fileName.baseName());

//    QString textHex = getColor(SETTINGS.value("Grid/labelcolor","White").toString()).name();
//    int fontSize = getGridSize("font");

    gridTextLabel->setStyleSheet("QLabel { font-weight: bold; color: white; font-size: 11px; }");
    gridTextLabel->setWordWrap(true);
    gridTextLabel->setAlignment(Qt::AlignHCenter | Qt::AlignTop);

    gameGridLayout->addWidget(gridTextLabel, 2, 1);

    oimage = item->image;
    image = item->image.scaled(tileSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    gridImageLabel->setPixmap(image);
    gridImageLabel->setAlignment(Qt::AlignCenter);

    gameGridLayout->addWidget(gridImageLabel, 1, 1);

    options = new QWidget(this);
    // check these constants
//    options->setMaximumHeight(32);
    options->setStyleSheet("background: red");
    options->setMinimumWidth(tileSize.width());
    options->setMaximumWidth(tileSize.width());

    QVBoxLayout *vlayout = new QVBoxLayout();
    auto padding = new QWidget(this);
    padding->setMaximumHeight(64);

    QHBoxLayout *olayout = new QHBoxLayout();
    auto editButton = new QPushButton("Edit");
    editButton->setCursor(Qt::PointingHandCursor);
    olayout->addWidget(editButton);
    auto spacer = new QLabel("|");
    spacer->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    olayout->addWidget(spacer);
    auto deleteButton = new QPushButton("Delete");
    deleteButton->setCursor(Qt::PointingHandCursor);
    olayout->addWidget(deleteButton);

    options->setLayout(olayout);

//    vlayout->addWidget(padding);
//    vlayout->addWidget(options);

    gameGridLayout->addWidget(options, 1, 1);

    QGraphicsDropShadowEffect *shadow = new QGraphicsDropShadowEffect;
    shadow->setColor(Qt::black);
    shadow->setOffset(0);
    shadow->setBlurRadius(10.f);
    setGraphicsEffect(shadow);

    setLayout(gameGridLayout);
    setMinimumHeight(this->sizeHint().height());

//    connect(this, SIGNAL(singleClicked(QWidget*)), this, SLOT(highlightGridWidget(QWidget*)));
//    connect(this, SIGNAL(doubleClicked(QWidget*)), parent, SLOT(openProjectFromWidget(QWidget*)));
//    connect(gameGridItem, SIGNAL(customContextMenuRequested(const QPoint &)), parent, SLOT(showRomMenu(const QPoint &)));
}

void ItemGridWidget::setTileSize(QSize size)
{
    tileSize = size;

    setMinimumWidth(tileSize.width() + 10);
    setMaximumWidth(tileSize.width() + 10);

    gameGridLayout->setColumnStretch(0, 1);
    gameGridLayout->setColumnStretch(3, 1);
    gameGridLayout->setRowMinimumHeight(1, tileSize.height());

    gridImageLabel->setMinimumHeight(tileSize.height());
    gridImageLabel->setMinimumWidth(tileSize.width());

    auto img = oimage.scaled(tileSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    gridImageLabel->setPixmap(img);
    gridImageLabel->setAlignment(Qt::AlignCenter);

    gridTextLabel->setMaximumWidth(tileSize.width());
    gridTextLabel->setWordWrap(true);
    gridTextLabel->setAlignment(Qt::AlignHCenter | Qt::AlignTop);

    setMinimumHeight(this->sizeHint().height());
}

void ItemGridWidget::updateImage()
{

}

QWidget *ItemGridWidget::get()
{
//    return gameGridItem;
}

//void ItemGridWidget::keyPressEvent(QKeyEvent *event)
//{
//    if (event->key() == Qt::Key_Up)
//        emit arrowPressed(this, "UP");
//    else if (event->key() == Qt::Key_Down)
//        emit arrowPressed(this, "DOWN");
//    else if (event->key() == Qt::Key_Left)
//        emit arrowPressed(this, "LEFT");
//    else if (event->key() == Qt::Key_Right)
//        emit arrowPressed(this, "RIGHT");
//    else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
//        emit enterPressed(this);
//    else
//        QWidget::keyPressEvent(event);
//}


void ItemGridWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton || event->button() == Qt::RightButton)
        emit singleClicked(this);
}


void ItemGridWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        emit doubleClicked(this);
}
