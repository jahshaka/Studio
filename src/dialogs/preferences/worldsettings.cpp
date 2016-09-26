/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Karsten Becker <jahshaka@gmail.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/
#include "worldsettings.h"
#include "ui_worldsettings.h"
#include "../../settingsmanager.h"

WorldSettings::WorldSettings(SettingsManager* settings) :
    QWidget(nullptr),
    ui(new Ui::WorldSettings)
{
    ui->setupUi(this);

    this->settings = settings;

    connect(ui->matrixRadio,SIGNAL(toggled(bool)),this,SLOT(onDefaultSceneChosen()));
    connect(ui->gridRadio,SIGNAL(toggled(bool)),this,SLOT(onDefaultSceneChosen()));

    connect(ui->comboBox,SIGNAL(currentIndexChanged(int)),this,SLOT(onGizmoOptionChosen(int)));

    setupDefaultSceneOptions();
    setupGizmoOptions();
}

void WorldSettings::setupGizmoOptions()
{
    auto value = settings->getValue("gizmo_style",0);
    auto index = value.toString().toInt();

    ui->comboBox->addItem("Thick");
    ui->comboBox->addItem("Slim");
    ui->comboBox->addItem("VR(Experimental)");

    ui->comboBox->setCurrentIndex(index);
}

void WorldSettings::setupDefaultSceneOptions()
{
    auto defaultScene = settings->getValue("default_scene","matrix").toString();
    if(defaultScene=="matrix")
    {
        ui->matrixRadio->setChecked(true);
    }
    else
    {
        ui->gridRadio->setChecked(true);
    }
}

void WorldSettings::onGizmoOptionChosen(int index)
{
    //auto index = this->ui->comboBox->currentIndex();
    settings->setValue("gizmo_style",index);

    //todo: modify appearance in scene
}

void WorldSettings::onDefaultSceneChosen()
{
    if(ui->matrixRadio->isChecked())
        settings->setValue("default_scene","matrix");
    else
        settings->setValue("default_scene","grid");
}

WorldSettings::~WorldSettings()
{
    delete ui;
}
