/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Karsten Becker <jahshaka@gmail.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "texturedplanelayerwidget.h"
#include "ui_texturedplanelayerwidget.h"
#include "scenenode.h"

#include <QFileDialog>
#include <QDir>

TexturedPlaneLayerWidget::TexturedPlaneLayerWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::TexturedPlaneLayerWidget)
{
    ui->setupUi(this);

    connect(ui->pushButton,SIGNAL(clicked(bool)),this,SLOT(chooseTexture()));
}

TexturedPlaneLayerWidget::~TexturedPlaneLayerWidget()
{
    delete ui;
}

void TexturedPlaneLayerWidget::setPlane(TexturedPlaneNode* node)
{
    this->node = node;
    ui->lineEdit->setText(node->texPath);
}

void TexturedPlaneLayerWidget::chooseTexture()
{
    auto path = QFileDialog::getOpenFileName(this,"Select Texture",QDir::currentPath(),"Image Files (*.jpg *.png *.bmp *.tga)");
    if(path.isEmpty())
        return;

    node->setTexture(path);
    ui->lineEdit->setText(path);
}

