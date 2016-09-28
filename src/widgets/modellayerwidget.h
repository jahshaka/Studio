/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef MODELLAYERWIDGET_H
#define MODELLAYERWIDGET_H

#include <QWidget>

class MeshNode;

namespace Ui {
class ModelLayerWidget;
}

class ModelLayerWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ModelLayerWidget(QWidget *parent = 0);
    ~ModelLayerWidget();


    MeshNode* mesh;
    void setMesh(MeshNode* node);

public slots:
    void loadMesh();

private:
    Ui::ModelLayerWidget *ui;
};

#endif // MODELLAYERWIDGET_H
