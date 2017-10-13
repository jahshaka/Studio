#include "itemgridwidget.hpp"
#include <QDebug>
#include <QFileInfo>
#include <QGraphicsDropShadowEffect>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>

#include "../dialogs/renameprojectdialog.h"

ItemGridWidget::ItemGridWidget(ProjectTileData tileData, QSize size, QSize iSize, QWidget *parent) : QWidget(parent)
{
    this->parent = parent;
    setParent(parent);

    tileSize = size;
    iconSize = iSize;

    this->tileData = tileData;

    setMinimumWidth(tileSize.width());
    setMaximumWidth(tileSize.width());

    setMouseTracking(true);

    gameGridLayout = new QGridLayout(this);
    gameGridLayout->setVerticalSpacing(5);

    gridImageLabel = new QLabel(this);
    gridImageLabel->setObjectName("image");

    gridTextLabel = new QLabel(this);

    // TODO - don't allow label to be wider than image
    gridTextLabel->setText(tileData.name);

    gridTextLabel->setStyleSheet("QLabel { font-weight: bold; color: #ddd; font-size: 12px; }");
    gridTextLabel->setWordWrap(true);
    gridTextLabel->setAlignment(Qt::AlignHCenter | Qt::AlignTop);


    QPixmap pixmap;
    if (!tileData.thumbnail.isEmpty() || !tileData.thumbnail.isNull()) {
        QPixmap cachedPixmap;
        if (cachedPixmap.loadFromData(tileData.thumbnail, "PNG")) {
            pixmap = cachedPixmap;
        } else {
            pixmap = QPixmap::fromImage(QImage(":/images/preview.png"));
        }
    } else {
        pixmap = QPixmap::fromImage(QImage(":/images/preview.png"));
    }

    oimage = pixmap;
    image = pixmap.scaled(tileSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    gridImageLabel->setPixmap(image);
    gridImageLabel->setAlignment(Qt::AlignCenter);

    options = new QWidget(this);

    QVBoxLayout *vlayout = new QVBoxLayout();

    QHBoxLayout *olayout = new QHBoxLayout();
    olayout->setMargin(0);
    olayout->setSpacing(0);

    playButton = new QPushButton();
    playButton->installEventFilter(this);
    playButton->setObjectName("playButton");
    playButton->setToolTipDuration(0);
    playButton->setToolTip("Play world fullscreen");
    playButton->setCursor(Qt::PointingHandCursor);
    playButton->setIconSize(iconSize);
    playButton->setIcon(QIcon(":/icons/tplay_alpha.svg"));
    playButton->setStyleSheet("QPushButton { background: transparent; font-weight: bold; color: white } QToolTip { padding: 2px; }");
    olayout->addWidget(playButton);

    auto spacer = new QLabel("");
    spacer->setMaximumWidth(10);
    spacer->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    spacer->setStyleSheet("background: transparent; color: white");
    olayout->addWidget(spacer);

    editButton = new QPushButton();
    editButton->installEventFilter(this);
    editButton->setObjectName("editButton");
    editButton->setToolTipDuration(0);
    editButton->setToolTip("Open world in editor");
    editButton->setCursor(Qt::PointingHandCursor);
    editButton->setIconSize(iconSize);
    editButton->setIcon(QIcon(":/icons/tedit_alpha.svg"));
    editButton->setStyleSheet("QPushButton { background: transparent; font-weight: bold; color: white } QToolTip { padding: 2px; }");
    olayout->addWidget(editButton);

    controls = new QWidget();
    controls->setStyleSheet("background: rgba(32, 32, 32, 190); border-radius: 4px;");
    controls->setContentsMargins(iconSize.width() / 2, iconSize.width() / 2, iconSize.width() / 2, iconSize.width() / 2);
    controls->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    controls->setLayout(olayout);

    vlayout->addWidget(controls);
    vlayout->setAlignment(controls, Qt::AlignHCenter);

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

    connect(this, SIGNAL(hovered()), SLOT(showControls()));
    connect(this, SIGNAL(left()), SLOT(hideControls()));

    setCursor(Qt::PointingHandCursor);
    setContextMenuPolicy(Qt::CustomContextMenu);

//    setStyleSheet("border: 1px solid red");

    connect(this, SIGNAL(customContextMenuRequested(const QPoint&)), SLOT(projectContextMenu(QPoint)));
}

void ItemGridWidget::setTileSize(QSize size, QSize iSize)
{
    tileSize = size;
    iconSize = iSize;

    controls->setContentsMargins(iconSize.width() / 2, iconSize.width() / 2, iconSize.width() / 2, iconSize.width() / 2);
    playButton->setIconSize(iconSize);
    editButton->setIconSize(iconSize);

    setMinimumWidth(tileSize.width());
    setMaximumWidth(tileSize.width());

    auto img = oimage.scaled(tileSize, Qt::KeepAspectRatio, Qt::FastTransformation);

    gridImageLabel->setPixmap(img);
    gridImageLabel->setAlignment(Qt::AlignCenter);

    gridTextLabel->setWordWrap(true);
    gridTextLabel->setAlignment(Qt::AlignHCenter | Qt::AlignTop);

    setMinimumHeight(this->sizeHint().height());
}

void ItemGridWidget::updateLabel(QString text)
{
    this->gridTextLabel->setText(text);
    tileData.name = text;
}

bool ItemGridWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == playButton) {
        switch (event->type()) {
            case QEvent::Enter: {
                playButton->setIcon(QIcon(":/icons/tplay.svg"));
                break;
            }

            case QEvent::Leave: {
                playButton->setIcon(QIcon(":/icons/tplay_alpha.svg"));
                break;
            }

            default: break;
        }
    }

    if (watched == editButton) {
        switch (event->type()) {
            case QEvent::Enter: {
                editButton->setIcon(QIcon(":/icons/tedit.svg"));
                break;
            }

            case QEvent::Leave: {
                editButton->setIcon(QIcon(":/icons/tedit_alpha.svg"));
                break;
            }

            default: break;
        }
    }

    return QObject::eventFilter(watched, event);
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
    QWidget::enterEvent(event);
    emit hovered();
}

void ItemGridWidget::leaveEvent(QEvent *event)
{
    QWidget::leaveEvent(event);
    emit left();
}

void ItemGridWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton || event->button() == Qt::RightButton) {
        emit singleClicked(this);
    }
}


void ItemGridWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) emit doubleClicked(this);
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
