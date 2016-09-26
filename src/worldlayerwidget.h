/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Karsten Becker <jahshaka@gmail.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef WORLDLAYERWIDGET_H
#define WORLDLAYERWIDGET_H

#include <QWidget>

namespace Ui {
class WorldLayerWidget;
}

namespace Qt3DExtras
{
    class QForwardRenderer;
}

class SceneManager;
class QGridLayout;
class QHBoxLayout;
class QListWidgetItem;

class WorldLayerWidget : public QWidget
{
    Q_OBJECT
    SceneManager* scene;
    Qt3DExtras::QForwardRenderer* renderer;
    QGridLayout* grid;
    QHBoxLayout* hbox;
    QList<QString> presets;
public:
    explicit WorldLayerWidget(QWidget *parent = 0);
    ~WorldLayerWidget();

    void setScene(SceneManager* scene);
    void setRenderer(Qt3DExtras::QForwardRenderer* renderer);
    void addPresetSky(QString url);
    void setSky(QString file);
private slots:
    void setBackgroundR(int col);
    void setBackgroundG(int col);
    void setBackgroundB(int col);
    void chooseSky();
    void clearSky();
    void setColor(QColor color);
    void toggleGridVisbility(int);
    void listItemClicked(QListWidgetItem*);

    //fog
    void fogColorChanged(QColor color);
    void fogStartChanged(int fogStart);
    void fogEndChanged(int fogEnd);
    void fogEnabledChanged(bool enabled);

private:
    Ui::WorldLayerWidget *ui;
};

#endif // WORLDLAYERWIDGET_H
