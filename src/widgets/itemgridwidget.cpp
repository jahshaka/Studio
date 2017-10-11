#include "itemgridwidget.hpp"
#include <QDebug>
#include <QFileInfo>
#include <QGraphicsDropShadowEffect>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>

#include "../dialogs/renameprojectdialog.h"

ItemGridWidget::ItemGridWidget(ProjectTileData tileData, QSize size, QWidget *parent) : QWidget(parent)
{
    this->parent = parent;
    setParent(parent);

    tileSize = size;

    this->tileData = tileData;

    setMinimumWidth(tileSize.width());
    setMaximumWidth(tileSize.width());

    gameGridLayout = new QGridLayout(this);
    gameGridLayout->setVerticalSpacing(5);
//    gameGridLayout->setColumnStretch(0, 1);
//    gameGridLayout->setColumnStretch(3, 1);
//    gameGridLayout->setRowMinimumHeight(1, tileSize.height());

    gridImageLabel = new QLabel(this);
    gridImageLabel->setObjectName("image");
//    gridImageLabel->setMinimumHeight(tileSize.height());
//    gridImageLabel->setMinimumWidth(tileSize.width());

    gridTextLabel = new QLabel(this);

//    //Don't allow label to be wider than image
//    gridTextLabel->setMaximumWidth(tileSize.width());
    gridTextLabel->setText(tileData.name);

////    QString textHex = getColor(SETTINGS.value("Grid/labelcolor","White").toString()).name();
////    int fontSize = getGridSize("font");

    gridTextLabel->setStyleSheet("QLabel { font-weight: bold; color: #ddd; font-size: 12px; }");
    gridTextLabel->setWordWrap(true);
    gridTextLabel->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
////    gridTextLabel->setTextInteractionFlags(Qt::TextEditorInteraction);


////    QPixmap pixmap;
////    if (!tileData.thumbnail.isEmpty() || !tileData.thumbnail.isNull()) {
        QPixmap cachedPixmap;
        cachedPixmap.loadFromData(tileData.thumbnail, "PNG");
////    } else {
////        pixmap = QPixmap::fromImage(QImage(":/images/preview.png"));
////    }

    oimage = cachedPixmap;
    image = cachedPixmap.scaled(tileSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    gridImageLabel->setPixmap(image);
    gridImageLabel->setAlignment(Qt::AlignCenter);

    options = new QWidget(this);
//    // check these constants
//    options->setStyleSheet("background: red");
//    options->setMinimumWidth(tileSize.width());
//    options->setMaximumWidth(tileSize.width());

    QVBoxLayout *vlayout = new QVBoxLayout();
//    vlayout->setMargin(0);
//    vlayout->setSpacing(0);
    auto padding = new QWidget();

    QHBoxLayout *olayout = new QHBoxLayout();
    olayout->setMargin(0);
    olayout->setSpacing(0);

    auto playButton = new QPushButton("Play");
    playButton->setObjectName("playButton");
    playButton->setCursor(Qt::PointingHandCursor);
    playButton->setStyleSheet("QPushButton { background: transparent; font-weight: bold; color: white }");
    olayout->addWidget(playButton);

//    auto spacer2 = new QLabel("|");
//    spacer2->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
//    spacer2->setStyleSheet("background: transparent; color: white");
//    olayout->addWidget(spacer2);

    auto editButton = new QPushButton("Edit");
    editButton->setObjectName("editButton");
    editButton->setCursor(Qt::PointingHandCursor);
    editButton->setStyleSheet("QPushButton { background: transparent; font-weight: bold; color: white }");
    olayout->addWidget(editButton);

//    auto spacer = new QLabel("|");
//    spacer->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
//    spacer->setStyleSheet("background: transparent; color: white");
//    olayout->addWidget(spacer);

//    auto deleteButton = new QPushButton("Delete");
//    deleteButton->setCursor(Qt::PointingHandCursor);
//    deleteButton->setStyleSheet("QPushButton { background: transparent; font-weight: bold;  color: white}");
//    olayout->addWidget(deleteButton);

    auto controls = new QWidget();
    controls->setStyleSheet("background: #2980b9; border-radius: 1px");
    controls->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    controls->setFixedHeight(32);
    controls->setLayout(olayout);

    vlayout->addWidget(padding);
    vlayout->addWidget(controls);

    options->setLayout(vlayout);
    options->hide();

    gameGridLayout->addWidget(gridImageLabel, 0, 0);
    gameGridLayout->addWidget(options, 0, 0);
    gameGridLayout->addWidget(gridTextLabel, 1, 0);

    QGraphicsDropShadowEffect *shadow = new QGraphicsDropShadowEffect;
    shadow->setColor(Qt::black);
    shadow->setOffset(0);
    shadow->setBlurRadius(12.f);
    setGraphicsEffect(shadow);

    setLayout(gameGridLayout);
    setMinimumHeight(this->sizeHint().height());

    connect(playButton, SIGNAL(pressed()), SLOT(playProject()));
    connect(editButton, SIGNAL(pressed()), SLOT(editProject()));
//    connect(deleteButton, SIGNAL(pressed()), SLOT(removeProject()));

    connect(this, SIGNAL(hovered()), SLOT(showControls()));
    connect(this, SIGNAL(left()), SLOT(hideControls()));

    setCursor(Qt::PointingHandCursor);
    setContextMenuPolicy(Qt::CustomContextMenu);

//    setStyleSheet("border: 1px solid red");

    connect(this, SIGNAL(customContextMenuRequested(const QPoint&)), SLOT(projectContextMenu(QPoint)));

//    connect(this, SIGNAL(singleClicked(QWidget*)), this, SLOT(highlightGridWidget(QWidget*)));
//    connect(this, SIGNAL(doubleClicked(QWidget*)), parent, SLOT(openProjectFromWidget(QWidget*)));
//    connect(gameGridItem, SIGNAL(customContextMenuRequested(const QPoint &)), parent, SLOT(showRomMenu(const QPoint &)));
}

void ItemGridWidget::setTileSize(QSize size)
{
    tileSize = size;

    setMinimumWidth(tileSize.width());
    setMaximumWidth(tileSize.width());

//    gameGridLayout->setColumnStretch(0, 1);
//    gameGridLayout->setColumnStretch(3, 1);
//    gameGridLayout->setRowMinimumHeight(1, tileSize.height());

//    gridImageLabel->setMinimumHeight(tileSize.height());
//    gridImageLabel->setMinimumWidth(tileSize.width());

    auto img = oimage.scaled(tileSize, Qt::KeepAspectRatio, Qt::FastTransformation);

    gridImageLabel->setPixmap(img);
    gridImageLabel->setAlignment(Qt::AlignCenter);

//    gridTextLabel->setMaximumWidth(tileSize.width());
    gridTextLabel->setWordWrap(true);
    gridTextLabel->setAlignment(Qt::AlignHCenter | Qt::AlignTop);

//    options->setMaximumWidth(tileSize.width());

    setMinimumHeight(this->sizeHint().height());
}

void ItemGridWidget::updateImage()
{

}

void ItemGridWidget::updateLabel(QString text)
{
    this->gridTextLabel->setText(text);
}

void ItemGridWidget::showControls()
{
    options->show();
}

void ItemGridWidget::hideControls()
{
    options->hide();
}

void ItemGridWidget::removeProject()
{
    emit remove(this);
}

void ItemGridWidget::editProject()
{
    emit openFromWidget(this, false);
}

void ItemGridWidget::enterEvent(QEvent *event)
{
    // this->setStyleSheet("#image { border: 2px solid #3498db }");
    QWidget::enterEvent(event);
    emit hovered();
}

void ItemGridWidget::leaveEvent(QEvent *event)
{
    // this->setStyleSheet("#image { border: none }");
    QWidget::leaveEvent(event);
    emit left();
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

void ItemGridWidget::projectContextMenu(const QPoint &pos)
{
    QMenu menu("Context Menu", this);

    QAction open("Open", this);
    connect(&open, SIGNAL(triggered()), this, SLOT(openProject()));
    menu.addAction(&open);

    QAction exportProj("Export", this);
    connect(&exportProj, SIGNAL(triggered()), this, SLOT(exportProject()));
    menu.addAction(&exportProj);

    QAction rename("Rename", this);
    connect(&rename, SIGNAL(triggered()), this, SLOT(renameProject()));
    menu.addAction(&rename);

    QAction close("Close", this);
    connect(&close, SIGNAL(triggered()), this, SLOT(closeProject()));
    menu.addAction(&close);

//    menu.addSeparator();

    QAction del("Delete", this);
    connect(&del, SIGNAL(triggered()), this, SLOT(deleteProject()));
    menu.addAction(&del);

    menu.exec(mapToGlobal(pos));
}

void ItemGridWidget::playProject()
{
    emit openFromWidget(this, true);
}

void ItemGridWidget::exportProject()
{
    emit exportFromWidget(this);
}

void ItemGridWidget::openProject()
{
    emit openFromWidget(this, false);
}

void ItemGridWidget::renameProject()
{
    auto renameDialog = new RenameProjectDialog();

    connect(renameDialog, SIGNAL(newTextEmit(QString)), SLOT(renameFromWidgetStr(QString)));

    renameDialog->show();
}

void ItemGridWidget::closeProject()
{
    emit closeFromWidget(this);
}

void ItemGridWidget::deleteProject()
{
    emit deleteFromWidget(this);
}

void ItemGridWidget::renameFromWidgetStr(QString text)
{
    this->labelText = text;
    emit renameFromWidget(this);
}
