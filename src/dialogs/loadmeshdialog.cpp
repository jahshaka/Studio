/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "loadmeshdialog.h"
#include "ui_loadmeshdialog.h"
#include <QFileDialog>

LoadMeshDialog::LoadMeshDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::LoadMeshDialog)
{
    ui->setupUi(this);

    connect(ui->loadMesh,SIGNAL(pressed()),this,SLOT(loadMesh()));
    connect(ui->loadTexture,SIGNAL(pressed()),this,SLOT(loadTexture()));
}

LoadMeshDialog::~LoadMeshDialog()
{
    delete ui;
}


QString LoadMeshDialog::getTextureFile()
{
    return texFile;
}

QString LoadMeshDialog::getMeshFile()
{
    return meshFile;
}

void LoadMeshDialog::loadMesh()
{
    meshFile = QFileDialog::getOpenFileName();
    this->ui->meshEdit->setText(meshFile);
}

void LoadMeshDialog::loadTexture()
{
    texFile = QFileDialog::getOpenFileName();
    this->ui->texEdit->setText(texFile);
}
