/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/controls/itemgridwidget.h"
#include <QDebug>
#include <QFileInfo>
#include <QGraphicsDropShadowEffect>
#include <QLineEdit>
#include <QMenu>
#include <QEnterEvent>
#include <QMouseEvent>
#include <QPushButton>
#include <QApplication>

#include <QPainter>
#include <QPainterPath>

#include "ui/dialogs/renameprojectdialog.h"
#include "ui/style/stylesheet.h"

// The tile reads as one rounded card: the image supplies the two top rounded
// corners (clipped here — a stylesheet border-radius does not clip a QLabel's
// pixmap), the black caption bar supplies the two bottom ones, and the seam
// between them stays square so they join seamlessly.
static const int kTileCornerRadius = 3;

static QPixmap roundTopCorners(const QPixmap &src, int radius)
{
    if (src.isNull()) return src;
    QPixmap out(src.size());
    out.setDevicePixelRatio(src.devicePixelRatio());
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect(QRectF(0, 0, src.width(), src.height()), radius, radius);
    path.addRect(QRectF(0, src.height() - radius, src.width(), radius));
    p.setClipPath(path.simplified());
    p.drawPixmap(0, 0, src);
    return out;
}

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
            gridImageLabel->setStyleSheet(StyleSheet::ItemGridTileBorderHighlight(3));
        } else {
            gridImageLabel->setStyleSheet(StyleSheet::ItemGridTileBorderHighlight(5));
        }
        gridTextLabel->setText(tileData.name + " [ Open ]");
    } else {
        if (devicePixelRatio() > 1) {
            gridImageLabel->setStyleSheet(StyleSheet::ItemGridTileBorder(3));
        } else {
            gridImageLabel->setStyleSheet(StyleSheet::ItemGridTileBorder(5));
        }
        gridTextLabel->setText(tileData.name);
    }

    gridImageLabel->setObjectName("image");

    // caption bar: black band spanning the full tile width, white name.
    // (make things bigger at lower resolutions, as before)
    const int captionFontSize = devicePixelRatio() > 1 ? 12 : 15;
    // owner-tuned: top of the bar hugs the text (perfect as-is); the bottom
    // gets two extra pixels of breathing room
    gridTextLabel->setStyleSheet(
        QString("background-color: black; color: white; font-size: %1px;"
                " padding-bottom: 2px;"
                " border-bottom-left-radius: %2px; border-bottom-right-radius: %2px;")
            .arg(captionFontSize)
            .arg(kTileCornerRadius));
    gridTextLabel->setWordWrap(true);
    gridTextLabel->setAlignment(Qt::AlignHCenter | Qt::AlignTop);

    // the bar sits verticalSpacing() below the image; give the tile the same
    // spacing BELOW the caption instead of the layout's larger default margin
    auto tileMargins = gameGridLayout->contentsMargins();
    tileMargins.setBottom(gameGridLayout->verticalSpacing());
    gameGridLayout->setContentsMargins(tileMargins);


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
    image = roundTopCorners(pixmap.scaled(tileSize, Qt::KeepAspectRatio, Qt::SmoothTransformation), kTileCornerRadius);

    gridImageLabel->setPixmap(image);
    gridImageLabel->setAlignment(Qt::AlignCenter);

    options = new QWidget(this);

    QVBoxLayout *vlayout = new QVBoxLayout();

    QHBoxLayout *olayout = new QHBoxLayout();
    olayout->setContentsMargins(0, 0, 0, 0);
    olayout->setSpacing(0);

    playButton = new QPushButton();
    playButton->setObjectName("playButton");
    playButton->setToolTipDuration(0);
    playButton->setToolTip("Play world fullscreen");
    playButton->setCursor(Qt::PointingHandCursor);
    playButton->setIconSize(iconSize);
    playButton->setIcon(QIcon(":/icons/tplay_alpha.svg"));
    playButton->setStyleSheet(StyleSheet::ItemGridTileButton());

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
    editButton->setStyleSheet(StyleSheet::ItemGridTileButton());

    closeButton = new QPushButton();
    closeButton->setObjectName("closeButton");
    closeButton->setToolTipDuration(0);
    closeButton->setToolTip("Close open world");
    closeButton->setCursor(Qt::PointingHandCursor);
    closeButton->setIconSize(iconSize);
    closeButton->setIcon(QIcon(":/icons/error_alpha.svg"));
    closeButton->setStyleSheet(StyleSheet::ItemGridTileButton());

    playContainer = new QWidget;
    auto l = new QVBoxLayout;
    l->setSpacing(0);
    l->setContentsMargins(0, 0, 0, 0);
    playText = new QLabel("PLAY");
    playText->setAlignment(Qt::AlignHCenter);
    l->addWidget(playButton);
    l->addWidget(playText);
    playContainer->setLayout(l);
    playContainer->installEventFilter(this);

    editContainer = new QWidget;
    l = new QVBoxLayout;
    l->setSpacing(0);
    l->setContentsMargins(0, 0, 0, 0);
    editText = new QLabel("EDIT");
    editText->setAlignment(Qt::AlignHCenter);
    l->addWidget(editButton);
    l->addWidget(editText);
    editContainer->setLayout(l);
    editContainer->installEventFilter(this);

    closeContainer = new QWidget;
    l = new QVBoxLayout;
    l->setSpacing(0);
    l->setContentsMargins(0, 0, 0, 0);
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


    controls = new QWidget();
    controls->setObjectName("fresh");
    controls->setStyleSheet(StyleSheet::ItemGridTileControls());
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
	image = roundTopCorners(pixmap.scaled(tileSize, Qt::KeepAspectRatio, Qt::SmoothTransformation), kTileCornerRadius);

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

    auto img = roundTopCorners(oimage.scaled(tileSize, Qt::KeepAspectRatio, Qt::FastTransformation), kTileCornerRadius);

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
        gridImageLabel->setStyleSheet(StyleSheet::ItemGridTileBorder(3));
    } else {
        gridImageLabel->setStyleSheet(StyleSheet::ItemGridTileBorder(5));
    }
    gridTextLabel->setText(tileData.name);

    playContainer->setVisible(true);
    spacer->setVisible(true);

    editContainer->setVisible(true);

    closeContainer->setVisible(false);
}

bool ItemGridWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == playContainer) {
        switch (event->type()) {
            case QEvent::Enter: {
                playButton->setIcon(QIcon(":/icons/tplay.svg"));
                playText->setStyleSheet(StyleSheet::ItemGridTileCaptionActive());
                break;
            }

            case QEvent::Leave: {
                playButton->setIcon(QIcon(":/icons/tplay_alpha.svg"));
                playText->setStyleSheet(StyleSheet::ItemGridTileCaptionIdle());
                break;
            }

            default: break;
        }
    }

    if (watched == editContainer) {
        switch (event->type()) {
            case QEvent::Enter: {
                editButton->setIcon(QIcon(":/icons/tedit.svg"));
                editText->setStyleSheet(StyleSheet::ItemGridTileCaptionActive());
                break;
            }

            case QEvent::Leave: {
                editButton->setIcon(QIcon(":/icons/tedit_alpha.svg"));
                editText->setStyleSheet(StyleSheet::ItemGridTileCaptionIdle());
                break;
            }

            default: break;
        }
    }

    if (watched == closeContainer) {
        switch (event->type()) {
            case QEvent::Enter: {
                closeButton->setIcon(QIcon(":/icons/error.svg"));
                closeText->setStyleSheet(StyleSheet::ItemGridTileCaptionActive());
                break;
            }

            case QEvent::Leave: {
                closeButton->setIcon(QIcon(":/icons/error_alpha.svg"));
                closeText->setStyleSheet(StyleSheet::ItemGridTileCaptionIdle());
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

void ItemGridWidget::enterEvent(QEnterEvent *event)
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

    // a press on a tile always starts a tile drag — never a row pan (sliders)
    if (event->button() == Qt::LeftButton && (freeformDraggable || sliderDraggable)) {
        dragging = false;
        dragStartGlobal = event->globalPosition().toPoint();
        dragStartTilePos = pos();
    }
}

void ItemGridWidget::mouseMoveEvent(QMouseEvent *event)
{
    QWidget::mouseMoveEvent(event);

    if (!(freeformDraggable || sliderDraggable) || !(event->buttons() & Qt::LeftButton)) return;

    const QPoint delta = event->globalPosition().toPoint() - dragStartGlobal;
    if (!dragging && delta.manhattanLength() < QApplication::startDragDistance()) return;

    if (!dragging) {
        dragging = true;
        raise();
    }

    // clamp inside the desktop canvas (our parent widget)
    QPoint target = dragStartTilePos + delta;
    QWidget *canvas = parentWidget();
    if (canvas) {
        target.setX(qBound(0, target.x(), qMax(0, canvas->width() - width())));
        target.setY(qBound(0, target.y(), qMax(0, canvas->height() - height())));
    }
    move(target);
}

void ItemGridWidget::mouseReleaseEvent(QMouseEvent *event)
{
    QWidget::mouseReleaseEvent(event);

    if (event->button() == Qt::LeftButton && dragging) {
        dragging = false;

        // sliders: the drop position decides {row, insert index}; DynamicGrid
        // resolves it. Never touch the freeform normX/normY — the freeform
        // layout must survive a stay in slider mode untouched (lossless rule).
        if (sliderDraggable) {
            emit tileMoved(this);
            return;
        }

        // store position normalized to the canvas so window resizes keep placement
        QWidget *canvas = parentWidget();
        if (canvas) {
            const int availW = qMax(1, canvas->width() - width());
            const int availH = qMax(1, canvas->height() - height());
            normX = qBound(0.0, qreal(x()) / availW, 1.0);
            normY = qBound(0.0, qreal(y()) / availH, 1.0);
            hasFreeformPos = true;
            emit tileMoved(this);
        }
    }
}

void ItemGridWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) emit doubleClicked(this);
}

void ItemGridWidget::projectContextMenu(const QPoint &pos)
{
    // Parent the menu to the top-level window, NOT the tile: DynamicGrid sets
    // a selector-less "background: transparent" stylesheet, and selector-less
    // declarations propagate to every descendant — a QMenu under it gets a
    // transparent background RULE, so QStyleSheetStyle paints nothing behind
    // the items (the see-through context menu). From the window, the menu
    // inherits no background rule and the theme paints its panel normally.
    QMenu menu("Context Menu", window());
    menu.setStyleSheet(StyleSheet::QMenuDarkGrid());

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

    // Desktops: re-file this project onto another desktop (current one disabled)
    QMenu *moveMenu = menu.addMenu("Move to");
    moveMenu->setStyleSheet(menu.styleSheet());
    for (int i = 1; i <= 4; ++i) {
        QAction *moveAction = moveMenu->addAction(QString("Desktop %1").arg(i));
        moveAction->setEnabled(i != currentDesktop);
        connect(moveAction, &QAction::triggered, this, [this, i]() {
            emit moveToDesktopFromWidget(this, i);
        });
    }

    // Sliders (DESKTOP_SLIDER_SPEC.md): re-file this tile onto another
    // filmstrip row (current row disabled). Only offered in slider mode.
    if (sliderRowCount > 0) {
        QMenu *rowMenu = menu.addMenu("Move to row");
        rowMenu->setStyleSheet(menu.styleSheet());
        for (int r = 0; r < sliderRowCount; ++r) {
            QAction *rowAction = rowMenu->addAction(QString("Row %1").arg(r + 1));
            rowAction->setEnabled(!(hasSliderPos && r == sliderRow));
            connect(rowAction, &QAction::triggered, this, [this, r]() {
                emit moveToRowFromWidget(this, r);
            });
        }
    }

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
