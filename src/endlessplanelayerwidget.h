/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Karsten Becker <jahshaka@gmail.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef ENDLESSPLANELAYERWIDGET_H
#define ENDLESSPLANELAYERWIDGET_H

#include <QWidget>

namespace Ui {
class EndlessPlaneLayerWidget;
}

class EndlessPlaneNode;
class WorldNode;
class EndlessPlaneLayerWidget : public QWidget
{
    Q_OBJECT
    EndlessPlaneNode* plane;
    WorldNode* world;

public:
    explicit EndlessPlaneLayerWidget(QWidget *parent = 0);
    ~EndlessPlaneLayerWidget();

    void setPlane(EndlessPlaneNode* node);
    void setWorld(WorldNode* node);


private slots:
    void setTextureScale(int value);
    void chooseTexture();
    void changeVisibility();

private:
    Ui::EndlessPlaneLayerWidget *ui;
};

#endif // ENDLESSPLANELAYERWIDGET_H
