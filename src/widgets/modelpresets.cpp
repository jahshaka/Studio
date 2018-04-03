/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "modelpresets.h"
#include "ui_modelpresets.h"
#include <QListWidgetItem>
#include "../irisgl/src/core/irisutils.h"
#include "../mainwindow.h"

ModelPresets::ModelPresets(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::ModelPresets)
{
    ui->setupUi(this);
    mainWindow = nullptr;

    ui->modelsSets->setViewMode(QListWidget::IconMode);
    ui->modelsSets->setIconSize(QSize(88,88));
    ui->modelsSets->setResizeMode(QListWidget::Adjust);
    ui->modelsSets->setMovement(QListView::Static);
    ui->modelsSets->setSelectionBehavior(QAbstractItemView::SelectItems);
    ui->modelsSets->setSelectionMode(QAbstractItemView::SingleSelection);

    connect(ui->modelsSets, SIGNAL(doubleClicked(QModelIndex)), SLOT(onPrimitiveSelected(QModelIndex)));

    // addItem("IcoSphere","app/modelpresets/icosphere.png");
    // addItem("Cone",     "app/modelpresets/cone.png");
    addItem("Plane",    "app/modelpresets/plane.png");
    addItem("Cube",     "app/modelpresets/cube.png");
    addItem("Cylinder", "app/modelpresets/cylinder.png");
    addItem("Sphere",   "app/modelpresets/sphere.png");
    addItem("Torus",    "app/modelpresets/torus.png");
    addItem("Capsule",  "app/modelpresets/capsule.png");

}

void ModelPresets::setMainWindow(MainWindow* mainWindow)
{
    this->mainWindow = mainWindow;
}

void ModelPresets::onPrimitiveSelected(QModelIndex itemIndex)
{
    if (mainWindow == Q_NULLPTR) return;

    auto item = ui->modelsSets->item(itemIndex.row());
    auto text = item->text();

    // if (text == "IcoSphere") mainWindow->addIcoSphere();
    if (text == "Plane")    mainWindow->addPlane();
    if (text == "Cone") mainWindow->addCone();
    if (text == "Cube")     mainWindow->addCube();
    if (text == "Cylinder") mainWindow->addCylinder();
    if (text == "Sphere")   mainWindow->addSphere();
    if (text == "Torus")    mainWindow->addTorus();
    if (text == "Capsule")  mainWindow->addCapsule();
}

void ModelPresets::addItem(QString name, QString path)
{
    QListWidgetItem* item = new QListWidgetItem(QIcon(IrisUtils::getAbsoluteAssetPath(path)),name);
    ui->modelsSets->addItem(item);
}

ModelPresets::~ModelPresets()
{
    delete ui;
}
