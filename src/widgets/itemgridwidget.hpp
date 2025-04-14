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

#include "../core/project.h"

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
    void enterEvent(QEvent*);
    void leaveEvent(QEvent*);
    void mousePressEvent(QMouseEvent*);
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

private:
//    QWidget *gameGridItem;
    QWidget *options;
    QGridLayout *gameGridLayout;
    QLabel *gridImageLabel;
    QLabel *gridTextLabel;
    QPixmap image;
    QPixmap oimage;
    QWidget *parent;
};

#endif // ITEMGRIDWIDGET_HPP
