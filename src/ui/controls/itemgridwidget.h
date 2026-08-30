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

	void updateTile(const QByteArray &arr);

    void setTileSize(QSize size, QSize iSize);
    void updateImage();
    void updateLabel(QString);
    void removeHighlight();
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
//    void keyPressEvent(QKeyEvent* event);
    void enterEvent(QEnterEvent*);
    void leaveEvent(QEvent*);
    void mousePressEvent(QMouseEvent*);
    void mouseMoveEvent(QMouseEvent*);
    void mouseReleaseEvent(QMouseEvent*);
    void mouseDoubleClickEvent(QMouseEvent*);

signals:
//    void arrowPressed(QWidget *current, QString keypress);
//    void enterPressed(QWidget *current);
    void hovered();
    void left();
    //void edit(ItemGridWidget*, bool playMode);
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
    QPixmap image;
    QPixmap oimage;
    QWidget *parent;

    // freeform drag state
    bool dragging = false;
    QPoint dragStartGlobal;
    QPoint dragStartTilePos;
};

#endif // ITEMGRIDWIDGET_HPP
