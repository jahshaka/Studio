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
#include <QCache>
#include <QCoreApplication>
#include <QtConcurrent/QtConcurrentMap>

#include <QPainter>
#include <QPainterPath>

#include "ui/dialogs/renameprojectdialog.h"
#include "ui/style/stylesheet.h"
#include "ui/style/thememanager.h"

// The tile reads as one rounded card: the image supplies the two top rounded
// corners (clipped here — a stylesheet border-radius does not clip a QLabel's
// pixmap), the caption bar supplies the two bottom ones, and the seam between
// them stays square so they join seamlessly. The bar is black on an ordinary
// tile and the theme's dark blue on the OPEN project's tile
// (ThemeManager::tileCaptionBarColor).
static const int kTileCornerRadius = 3;

static QPainterPath topCornersClip(int w, int h, int radius)
{
    QPainterPath path;
    path.addRoundedRect(QRectF(0, 0, w, h), radius, radius);
    path.addRect(QRectF(0, h - radius, w, radius));
    return path.simplified();
}

// The card's two top corners, clipped on a QImage — callable on a worker
// (QPainter on a QImage is; on a QPixmap it is not).
static QImage roundTopCornersImage(const QImage &src, int radius)
{
    if (src.isNull()) return src;
    QImage out(src.size(), QImage::Format_ARGB32_Premultiplied);
    out.setDevicePixelRatio(src.devicePixelRatio());
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    p.setClipPath(topCornersClip(src.width(), src.height(), radius));
    p.drawImage(0, 0, src);
    return out;
}

// THE DESKTOP'S DECODED THUMBNAILS (CREATE-GAP-1). A tile used to inflate its
// PNG in its constructor, on the UI thread, every time the grid was built —
// ~11 ms a tile, 450 ms for a 40-tile desktop, paid again on every rebuild of
// thumbnails that had not changed. The cache is keyed by the project's guid and
// checked against a hash of the thumbnail BYTES, so a project whose thumbnail
// was re-saved decodes once more and a project whose thumbnail did not change
// never does. One entry per guid (a new thumbnail REPLACES its project's old
// entry).
//
// AN ENTRY IS THE TILE-SIZED PICTURE ONLY, and the bound charges exactly that
// (fix round): the full 920x430 decode is scaled and dropped. Charging the full
// decode (1.5 MB) held ~84 entries in 128 MB, and a desktop above that thrashed
// — the build's row 0 was evicted by the prefetch's last rows, and every tile
// then evicted the next one it needed. At the tile sizes (Normal 276x129,
// ~139 KB; Huge 460x215, ~386 KB) the same 128 MB holds ~940 projects at
// Normal and ~340 at Huge. A tile-size change is the one path that needs the
// full picture again: it re-decodes, in parallel (DynamicGrid::scaleTile ->
// prefetchThumbnails). GUI thread only (QPixmap).
namespace {

const int kThumbCacheKB = 128 * 1024;

struct CachedThumb
{
    size_t  hash = 0;
    QSize   tileSize;   // what `tile` was scaled for
    QPixmap tile;       // scaled + rounded for tileSize
};

struct ThumbCache
{
    QCache<QString, CachedThumb> entries{kThumbCacheKB};
    int decodes = 0;
};

ThumbCache &thumbCache()
{
    static ThumbCache *cache = [] {
        auto *c = new ThumbCache;
        // Emptied while the application still exists: a QPixmap outliving its
        // QGuiApplication is undefined behaviour on some platforms.
        qAddPostRoutine([] { thumbCache().entries.clear(); });
        return c;
    }();
    return *cache;
}

int costKB(const QPixmap &pm)
{
    return qMax(1, int(qint64(pm.width()) * pm.height() * 4 / 1024));
}

size_t thumbHash(const QByteArray &png)
{
    return qHash(png) ^ size_t(png.size());
}

QImage placeholderThumbnail()
{
    return QImage(QStringLiteral(":/images/preview.png"));
}

QImage scaledTile(const QImage &full, const QSize &size)
{
    return roundTopCornersImage(full.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation),
                                kTileCornerRadius);
}

// The tile's picture for (guid, png) at `size`: a hit when the bytes hash the
// same and the entry was scaled for this size; one decode (replacing the
// guid's entry) otherwise.
QPixmap thumbnailFor(const QString &guid, const QByteArray &png, const QSize &size)
{
    ThumbCache &cache = thumbCache();
    const size_t hash = thumbHash(png);
    if (!guid.isEmpty())
        if (CachedThumb *hit = cache.entries.object(guid))
            if (hit->hash == hash && hit->tileSize == size) return hit->tile;

    QImage full;
    if (!png.isEmpty() && full.loadFromData(png, "PNG")) ++cache.decodes;
    else full = placeholderThumbnail();
    const QPixmap tile = QPixmap::fromImage(scaledTile(full, size));
    if (!guid.isEmpty())
        cache.entries.insert(guid, new CachedThumb{ hash, size, tile }, costKB(tile));
    return tile;
}

}   // namespace

int ItemGridWidget::thumbnailDecodeCount()
{
    return thumbCache().decodes;
}

// A COLD DESKTOP IS DECODED IN PARALLEL (CREATE-GAP-1, built because it was
// measured: a first build of 43 tiles blocked 446 ms, 43 decodes, where the
// same build with the cache warm took 60). Every thumbnail the cache does not
// hold at `tileSize` is inflated AND scaled on the thread pool — QImage work,
// legal off the GUI thread — and only the QImage -> QPixmap conversion runs
// here. The calling thread takes part in the map rather than idling, so it
// cannot starve behind a busy pool. The tiles built next are all cache hits.
// Inserted LAST ROW FIRST, so the rows a build reaches first are the most
// recently used should a desktop ever outgrow the bound.
int ItemGridWidget::prefetchThumbnails(const QVector<ProjectTileData> &rows, const QSize &tileSize)
{
    struct Job
    {
        QString guid;
        QByteArray png;
        size_t hash = 0;
        QImage tile;
    };
    ThumbCache &cache = thumbCache();
    QVector<Job> jobs;
    for (const ProjectTileData &row : rows) {
        if (row.guid.isEmpty() || row.thumbnail.isEmpty()) continue;
        const size_t hash = thumbHash(row.thumbnail);
        if (CachedThumb *hit = cache.entries.object(row.guid))
            if (hit->hash == hash && hit->tileSize == tileSize) continue;
        jobs.append({ row.guid, row.thumbnail, hash, QImage() });
    }
    if (jobs.isEmpty()) return 0;

    QtConcurrent::blockingMap(jobs, [tileSize](Job &job) {
        QImage full;
        if (full.loadFromData(job.png, "PNG")) job.tile = scaledTile(full, tileSize);
    });

    int decoded = 0;
    for (auto it = jobs.crbegin(); it != jobs.crend(); ++it) {
        if (it->tile.isNull()) continue;    // undecodable: the tile's own path decides
        const QPixmap tile = QPixmap::fromImage(it->tile);
        cache.entries.insert(it->guid, new CachedThumb{ it->hash, tileSize, tile }, costKB(tile));
        ++cache.decodes;
        ++decoded;
    }
    return decoded;
}

void ItemGridWidget::setThumbnail(const QByteArray &png)
{
    tileData.thumbnail = png;
    image = thumbnailFor(tileData.guid, png, tileSize);
    gridImageLabel->setPixmap(image);
    gridImageLabel->setAlignment(Qt::AlignCenter);
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

    gridImageLabel->setObjectName("image");

    // caption bar: a band spanning the full tile width, white name. Its
    // colour (black, or dark blue for the open project) is pushed by
    // setOpenProject at the end of this constructor.
    gridTextLabel->setWordWrap(true);
    gridTextLabel->setAlignment(Qt::AlignHCenter | Qt::AlignTop);

    // the bar sits verticalSpacing() below the image; give the tile the same
    // spacing BELOW the caption instead of the layout's larger default margin
    auto tileMargins = gameGridLayout->contentsMargins();
    tileMargins.setBottom(gameGridLayout->verticalSpacing());
    gameGridLayout->setContentsMargins(tileMargins);
    // owner: the caption bar must sit flush against the image - no gap
    gameGridLayout->setVerticalSpacing(0);


    // Decoded at most once per (project, thumbnail) for the whole session —
    // the cache above.
    setThumbnail(tileData.thumbnail);

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
    spacer->setStyleSheet(StyleSheet::ItemGridTileSpacer());

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

    // the whole open/ordinary look in one call, now that every piece exists
    setOpenProject(highlight);

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


    connect(this, SIGNAL(customContextMenuRequested(const QPoint&)), SLOT(projectContextMenu(QPoint)));
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

    // The cached tile picture at this size (a relayout at an unchanged size
    // costs nothing; a tile-size change was prefetched by DynamicGrid).
    image = thumbnailFor(tileData.guid, tileData.thumbnail, tileSize);
    gridImageLabel->setPixmap(image);
    gridImageLabel->setAlignment(Qt::AlignCenter);

    gridTextLabel->setWordWrap(true);
    gridTextLabel->setAlignment(Qt::AlignHCenter | Qt::AlignTop);

    setMinimumHeight(this->sizeHint().height());
}

void ItemGridWidget::updateLabel(QString text)
{
    tileData.name = text;
    this->gridTextLabel->setText(isOpenProject ? text + " [ Open ]" : text);
}

void ItemGridWidget::applyCaptionBarStyle()
{
    // Pixels are device-independent in Qt; the old "smaller on a HiDPI
    // screen" branch compensated for a scaling that does not happen (platform
    // audit F-S4) — one size everywhere.
    const int captionFontSize = 15;
    gridTextLabel->setStyleSheet(
        ThemeManager::tileCaptionBarSheet(captionFontSize, kTileCornerRadius, isOpenProject));
}

void ItemGridWidget::setOpenProject(bool open)
{
    isOpenProject = open;

    const int borderWidth = 5;   // device-independent (F-S4, see applyCaptionBarStyle)
    gridImageLabel->setStyleSheet(open ? StyleSheet::ItemGridTileBorderHighlight(borderWidth)
                                       : StyleSheet::ItemGridTileBorder(borderWidth));
    gridTextLabel->setText(open ? tileData.name + " [ Open ]" : tileData.name);

    // the dark blue caption bar — the one marker that survives the Qlementine
    // theme, where the dashed accent border above is neutralized to nothing
    applyCaptionBarStyle();

    playContainer->setVisible(!open);
    spacer->setVisible(!open);
    editContainer->setVisible(!open);
    closeContainer->setVisible(open);
}

void ItemGridWidget::removeHighlight()
{
    setOpenProject(false);
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
    moveMenu->setStyleSheet(StyleSheet::QMenuDarkGrid());
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
        rowMenu->setStyleSheet(StyleSheet::QMenuDarkGrid());
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
