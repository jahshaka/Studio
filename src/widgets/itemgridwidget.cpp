/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "itemgridwidget.hpp"
#include <QDebug>
#include <QFileInfo>
#include <QGraphicsDropShadowEffect>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QApplication>

#include "../dialogs/renameprojectdialog.h"

ItemGridWidget::ItemGridWidget(ProjectTileData tileData,
                               QSize size,
                               QSize iSize,
                               QWidget *parent,
                               bool highlight) : QWidget(parent)
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

    // TODO - don't allow label to be wider than image
    gridTextLabel = new QLabel(this);

    if (highlight) {
        if (devicePixelRatio() > 1) {
            gridImageLabel->setStyleSheet("border: 3px dashed #3498db");
        } else {
            gridImageLabel->setStyleSheet("border: 5px dashed #3498db");
        }
        gridTextLabel->setText(tileData.name + " [ Open ]");
    } else {
        if (devicePixelRatio() > 1) {
            gridImageLabel->setStyleSheet("border: 3px solid rgba(0, 0, 0, 10%)");
        } else {
            gridImageLabel->setStyleSheet("border: 5px solid rgba(0, 0, 0, 10%)");
        }
        gridTextLabel->setText(tileData.name);
    }

    gridImageLabel->setObjectName("image");

    // make things bigger at lower resolutions
    if (devicePixelRatio() > 1) {
        gridTextLabel->setStyleSheet("color: #ddd; font-size: 12px;");
    } else {
        gridTextLabel->setStyleSheet("color: #ddd; font-size: 15px;");
    }
    gridTextLabel->setWordWrap(true);
    gridTextLabel->setAlignment(Qt::AlignHCenter | Qt::AlignTop);


    QPixmap pixmap;
    if (!tileData.thumbnail.isEmpty() || !tileData.thumbnail.isNull()) {
        QPixmap cachedPixmap;
        if (cachedPixmap.loadFromData(tileData.thumbnail, "PNG")) {
            pixmap = cachedPixmap;
        } else {
            pixmap = QPixmap(":/images/preview.png");
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
    playButton->setObjectName("playButton");
    playButton->setToolTipDuration(0);
    playButton->setToolTip("Play world fullscreen");
    playButton->setCursor(Qt::PointingHandCursor);
    playButton->setIconSize(iconSize);
    playButton->setIcon(QIcon(":/icons/tplay_alpha.svg"));
    playButton->setStyleSheet("QPushButton { background: transparent; font-weight: bold; color: white }"
                              "QToolTip { padding: 2px; }");

    spacer = new QLabel("");
    spacer->setMaximumWidth(10);
    spacer->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    spacer->setStyleSheet("background: transparent; color: white");

    editButton = new QPushButton();
    editButton->setObjectName("editButton");
    editButton->setToolTipDuration(0);
    editButton->setToolTip("Open world in editor");
    editButton->setCursor(Qt::PointingHandCursor);
    editButton->setIconSize(iconSize);
    editButton->setIcon(QIcon(":/icons/tedit_alpha.svg"));
    editButton->setStyleSheet("QPushButton { background: transparent; font-weight: bold; color: white }"
                              "QToolTip { padding: 2px; }");

    closeButton = new QPushButton();
    closeButton->setObjectName("closeButton");
    closeButton->setToolTipDuration(0);
    closeButton->setToolTip("Close open world");
    closeButton->setCursor(Qt::PointingHandCursor);
    closeButton->setIconSize(iconSize);
    closeButton->setIcon(QIcon(":/icons/error_alpha.svg"));
    closeButton->setStyleSheet("QPushButton { background: transparent; font-weight: bold; color: white }"
                               "QToolTip { padding: 2px; }");

    playContainer = new QWidget;
    auto l = new QVBoxLayout;
    l->setSpacing(0);
    l->setMargin(0);
    playText = new QLabel("PLAY");
    playText->setAlignment(Qt::AlignHCenter);
    l->addWidget(playButton);
    l->addWidget(playText);
    playContainer->setLayout(l);
    playContainer->installEventFilter(this);

    editContainer = new QWidget;
    l = new QVBoxLayout;
    l->setSpacing(0);
    l->setMargin(0);
    editText = new QLabel("EDIT");
    editText->setAlignment(Qt::AlignHCenter);
    l->addWidget(editButton);
    l->addWidget(editText);
    editContainer->setLayout(l);
    editContainer->installEventFilter(this);

    closeContainer = new QWidget;
    l = new QVBoxLayout;
    l->setSpacing(0);
    l->setMargin(0);
    closeText = new QLabel("CLOSE");
    closeText->setAlignment(Qt::AlignHCenter);
    l->addWidget(closeButton);
    l->addWidget(closeText);
    closeContainer->setLayout(l);
    closeContainer->installEventFilter(this);

    if (highlight) {
        playContainer->setVisible(false);
        spacer->setVisible(false);
        editContainer->setVisible(false);
    } else {
        closeContainer->setVisible(false);
    }

    olayout->addWidget(playContainer);
    olayout->addWidget(spacer);
    olayout->addWidget(editContainer);
    olayout->addWidget(closeContainer);
#ifdef BUILD_PLAYER_ONLY
    editContainer->setVisible(false);
#endif // !BUILD_PLAYER_ONLY


    controls = new QWidget();
    controls->setObjectName("fresh");
    controls->setStyleSheet("#fresh { background: rgba(32, 32, 32, 190); border-radius: 4px; }"
                            "QLabel { font-weight: bold; font-size: 12px }");
    controls->setContentsMargins(iconSize.width() / 2,
                                 iconSize.width() / 2,
                                 iconSize.width() / 2,
                                 iconSize.width() / 2);
    controls->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    controls->setLayout(olayout);

    vlayout->addWidget(controls);
    vlayout->setAlignment(controls, Qt::AlignHCenter);

    options->setLayout(vlayout);
    options->hide();

    gameGridLayout->addWidget(gridImageLabel, 0, 0);
    gameGridLayout->addWidget(options, 0, 0);
    gameGridLayout->addWidget(gridTextLabel, 1, 0);

//    QGraphicsDropShadowEffect *shadow = new QGraphicsDropShadowEffect;
//    shadow->setColor(Qt::black);
//    shadow->setOffset(0);
//    shadow->setBlurRadius(12.f);
//    setGraphicsEffect(shadow);

    setLayout(gameGridLayout);
    setMinimumHeight(this->sizeHint().height());

    connect(playButton, SIGNAL(pressed()), SLOT(playProject()));
    connect(editButton, SIGNAL(pressed()), SLOT(editProject()));
    connect(closeButton, SIGNAL(pressed()), SLOT(closeProject()));

    connect(this, SIGNAL(hovered()), SLOT(showControls()));
    connect(this, SIGNAL(left()), SLOT(hideControls()));

    setCursor(Qt::PointingHandCursor);
    setContextMenuPolicy(Qt::CustomContextMenu);

//    setStyleSheet("border: 1px solid red");

    connect(this, SIGNAL(customContextMenuRequested(const QPoint&)), SLOT(projectContextMenu(QPoint)));
}

void ItemGridWidget::updateTile(const QByteArray &arr)
{
	QPixmap pixmap;
	QPixmap cachedPixmap;
	if (cachedPixmap.loadFromData(tileData.thumbnail, "PNG")) {
		pixmap = cachedPixmap;
	}
	else {
		pixmap = QPixmap(":/images/preview.png");
	}

	oimage = pixmap;
	image = pixmap.scaled(tileSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);

	gridImageLabel->setPixmap(image);
	gridImageLabel->update();
	update();
	QApplication::processEvents();
	gridImageLabel->setAlignment(Qt::AlignCenter);

	tileData.thumbnail = arr;
}

void ItemGridWidget::setTileSize(QSize size, QSize iSize)
{
    tileSize = size;
    iconSize = iSize;

    controls->setContentsMargins(iconSize.width() / 2,
                                 iconSize.width() / 2,
                                 iconSize.width() / 2,
                                 iconSize.width() / 2);
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

void ItemGridWidget::removeHighlight()
{
    if (devicePixelRatio() > 1) {
        gridImageLabel->setStyleSheet("border: 3px solid rgba(0, 0, 0, 10%)");
    } else {
        gridImageLabel->setStyleSheet("border: 5px solid rgba(0, 0, 0, 10%)");
    }
    gridTextLabel->setText(tileData.name);

    playContainer->setVisible(true);
    spacer->setVisible(true);

#ifdef BUILD_PLAYER_ONLY
    editContainer->setVisible(false);
#else
    editContainer->setVisible(true);
#endif // !BUILD_PLAYER_ONLY

    closeContainer->setVisible(false);
}

bool ItemGridWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == playContainer) {
        switch (event->type()) {
            case QEvent::Enter: {
                playButton->setIcon(QIcon(":/icons/tplay.svg"));
                playText->setStyleSheet("color: white");
                break;
            }

            case QEvent::Leave: {
                playButton->setIcon(QIcon(":/icons/tplay_alpha.svg"));
                playText->setStyleSheet("color: rgba(255, 255, 255, 50%)");
                break;
            }

            default: break;
        }
    }

    if (watched == editContainer) {
        switch (event->type()) {
            case QEvent::Enter: {
                editButton->setIcon(QIcon(":/icons/tedit.svg"));
                editText->setStyleSheet("color: white");
                break;
            }

            case QEvent::Leave: {
                editButton->setIcon(QIcon(":/icons/tedit_alpha.svg"));
                editText->setStyleSheet("color: rgba(255, 255, 255, 50%)");
                break;
            }

            default: break;
        }
    }

    if (watched == closeContainer) {
        switch (event->type()) {
            case QEvent::Enter: {
                closeButton->setIcon(QIcon(":/icons/error.svg"));
                closeText->setStyleSheet("color: white");
                break;
            }

            case QEvent::Leave: {
                closeButton->setIcon(QIcon(":/icons/error_alpha.svg"));
                closeText->setStyleSheet("color: rgba(255, 255, 255, 50%)");
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

void ItemGridWidget::closeProject()
{
    this->removeHighlight();
    emit closeFromWidget(this);
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
    menu.setStyleSheet(
		"QMenu { background-color: #1A1A1A; color: #EEE; padding: 0; margin: 0; }"
		"QMenu::item { background-color: #1A1A1A; padding: 6px 8px; margin: 0; }"
		"QMenu::item:selected { background-color: #3498db; color: #EEE; padding: 6px 8px; margin: 0; }"
		"QMenu::item : disabled { color: #555; }"
	);

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

void ItemGridWidget::deleteProject()
{
    emit deleteFromWidget(this);
}

void ItemGridWidget::renameFromWidgetStr(QString text)
{
    this->labelText = text;
    emit renameFromWidget(this);
}
