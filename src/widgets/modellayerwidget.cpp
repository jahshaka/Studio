/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "modellayerwidget.h"
#include "ui_modellayerwidget.h"
#include "qfiledialog.h"
#include "../scenegraph/scenenodes.h"
#include "qmesh.h"

ModelLayerWidget::ModelLayerWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::ModelLayerWidget)
{
    ui->setupUi(this);

    connect(ui->loadMesh,SIGNAL(pressed()),this,SLOT(loadMesh()));
}

void ModelLayerWidget::loadMesh()
{
    if(mesh==nullptr)
        return;

    auto file = QFileDialog::getOpenFileName();
    if(file.isEmpty())
        return;

    mesh->setSource(file);
}

void ModelLayerWidget::setMesh(MeshNode* node)
{
    mesh = node;
    ui->meshPath->setText(node->mesh->source().toString());
}

ModelLayerWidget::~ModelLayerWidget()
{
    delete ui;
}
