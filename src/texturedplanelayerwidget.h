/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Karsten Becker <jahshaka@gmail.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef TEXTUREDPLANELAYERWIDGET_H
#define TEXTUREDPLANELAYERWIDGET_H

#include <QWidget>

namespace Ui {
class TexturedPlaneLayerWidget;
}

class SceneNode;
class TexturedPlaneNode;

class TexturedPlaneLayerWidget : public QWidget
{
    Q_OBJECT
    TexturedPlaneNode* node;
public:
    explicit TexturedPlaneLayerWidget(QWidget *parent = 0);
    ~TexturedPlaneLayerWidget();

    void setPlane(TexturedPlaneNode* node);
private slots:
    void chooseTexture();

private:
    Ui::TexturedPlaneLayerWidget *ui;
};

#endif // TEXTUREDPLANELAYERWIDGET_H
