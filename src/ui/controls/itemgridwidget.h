/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef ITEMGRIDWIDGET_HPP
#define ITEMGRIDWIDGET_HPP

#include <QWidget>
#include <QLabel>
#include <QGridLayout>
#include <QPushButton>
#include <QLineEdit>

#include "data/project.h"

class ItemGridWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ItemGridWidget(ProjectTileData tileData,
                            QSize size,
                            QSize iSize,
                            QWidget *parent = Q_NULLPTR,
                            bool highlight = false);
    QSize tileSize;
    QSize iconSize;
    QPushButton *playButton;
    QWidget *playContainer;
    QPushButton *editButton;
    QWidget *editContainer;
    QLabel *spacer;
    QLabel *playText;
    QLabel *editText;
    QLabel *closeText;
    QPushButton *closeButton;
    QWidget *closeContainer;
    QWidget *controls;
    ProjectTileData tileData;

    // THE OPEN PROJECT (owner request 2026-09-08): this tile is the project
    // currently open in the editor. Drives the dashed border, the "[ Open ]"
    // caption suffix, the Close-instead-of-Play/Edit controls and the DARK BLUE
    // caption bar that makes the tile spottable on a full desktop. Read back by
    // desktop.tiles() as the `open` field.
    bool isOpenProject = false;

    // Desktops (DESKTOPS_SPEC.md): the desktop this tile's grid is showing (to disable
    // the current entry in the Move-to submenu) and the freeform layout state.
    int currentDesktop = 1;
    bool freeformDraggable = false; // set by DynamicGrid when the desktop is in freeform mode
    bool hasFreeformPos = false;    // normalized position assigned (from DB or cascade)
    qreal normX = 0.0;              // 0..1 across the desktop canvas minus the tile size
    qreal normY = 0.0;

    // Slider mode (DESKTOP_SLIDER_SPEC.md): filmstrip state, kept in sync by
    // DynamicGrid. sliderRowCount > 0 means the desktop is in Sliders mode
    // (enables the Move-to-row submenu); a press on the tile starts a tile
    // drag — never a row pan (that is the canvas's empty-space gesture).
    bool sliderDraggable = false;
    bool hasSliderPos = false;      // {row, index} assignment exists (DB or seeded)
    int  sliderRow = 0;             // 0-based filmstrip row
    int  sliderIndex = 0;           // 0-based order within the row
    int  sliderRowCount = 0;

    /// Shows `png` as this tile's thumbnail (a save's new one). Decoded through
    /// the session's thumbnail cache: bytes this project has already shown are
    /// never inflated twice (CREATE-GAP-1).
    void setThumbnail(const QByteArray &png);
    /// PNG decodes the thumbnail cache has performed this session (the suites
    /// read it to prove a rebuild decodes nothing it has seen).
    static int thumbnailDecodeCount();
    /// Decodes (and scales to `tileSize`) every thumbnail of `rows` the cache
    /// does not hold, in parallel on the thread pool, before a desktop builds
    /// its tiles; returns how many it decoded.
    static int prefetchThumbnails(const QVector<ProjectTileData> &rows, const QSize &tileSize);

    void setTileSize(QSize size, QSize iSize);
    void updateLabel(QString);

    // Switches the tile between "ordinary" and "the open project" — every
    // piece of the open look in one place, and the ONLY path: the desktop no
    // longer rebuilds its tiles on open/close (ProjectManager::refreshOpenTiles
    // calls this on the live tiles).
    void setOpenProject(bool open);
    void removeHighlight();   // setOpenProject(false), kept for its call sites
    QString labelText;

    bool eventFilter(QObject *watched, QEvent *event);

protected slots:
    void showControls();
    void hideControls();
    void removeProject();
    void editProject();
    void projectContextMenu(const QPoint&);

    void playProject();
    void openProject();
    void exportProject();
    void renameProject();
    void closeProject();
    void deleteProject();

    void renameFromWidgetStr(QString);

protected:
    void enterEvent(QEnterEvent*);
    void leaveEvent(QEvent*);
    void mousePressEvent(QMouseEvent*);
    void mouseMoveEvent(QMouseEvent*);
    void mouseReleaseEvent(QMouseEvent*);
    void mouseDoubleClickEvent(QMouseEvent*);

signals:
    void hovered();
    void left();
    void remove(ItemGridWidget*);
    void singleClicked(ItemGridWidget*);
    void doubleClicked(ItemGridWidget*);

    void openFromWidget(ItemGridWidget*, bool playMode);
    void exportFromWidget(ItemGridWidget*);
    void renameFromWidget(ItemGridWidget*);
    void closeFromWidget(ItemGridWidget*);
    void deleteFromWidget(ItemGridWidget*);
    void moveToDesktopFromWidget(ItemGridWidget*, int desktop);
    void moveToRowFromWidget(ItemGridWidget*, int row);     // sliders: 0-based target row
    void tileMoved(ItemGridWidget*);    // freeform drag ended (normX/normY updated) or slider drop

private:
//    QWidget *gameGridItem;
    QWidget *options;
    QGridLayout *gameGridLayout;
    QLabel *gridImageLabel;
    QLabel *gridTextLabel;
    void applyCaptionBarStyle();

    QPixmap image;
    QWidget *parent;

    // freeform drag state
    bool dragging = false;
    QPoint dragStartGlobal;
    QPoint dragStartTilePos;
};

#endif // ITEMGRIDWIDGET_HPP
