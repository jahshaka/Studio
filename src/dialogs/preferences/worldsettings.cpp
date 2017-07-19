/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/
#include "worldsettings.h"
#include "ui_worldsettings.h"
#include "../../core/settingsmanager.h"
#include "../../constants.h"
#include <QFileDialog>
#include <QStandardPaths>

WorldSettings::WorldSettings(SettingsManager* settings) :
    QWidget(nullptr),
    ui(new Ui::WorldSettings)
{
    ui->setupUi(this);

    this->settings = settings;

    connect(ui->browseProject, SIGNAL(pressed()), SLOT(changeDefaultDirectory()));

    connect(ui->outlineWidth, SIGNAL(valueChanged(double)),
            this, SLOT(outlineWidthChanged(double)));

    connect(ui->outlineColor, SIGNAL(onColorChanged(QColor)),
            this, SLOT(outlineColorChanged(QColor)));

    connect(ui->projectDefault, SIGNAL(textChanged(QString)),
            this, SLOT(projectDirectoryChanged(QString)));

    setupDirectoryDefaults();
    setupOutline();
}

void WorldSettings::setupOutline()
{
    outlineWidth = settings->getValue("outline_width", 6).toInt();
    outlineColor = settings->getValue("outline_color", "#3498db").toString();

    ui->outlineWidth->setValue(outlineWidth);
    ui->outlineColor->setColor(outlineColor);
}

void WorldSettings::changeDefaultDirectory()
{
    QFileDialog projectDir;
    defaultProjectDirectory = projectDir.getExistingDirectory(nullptr, "Select project dir", "");
    ui->projectDefault->setText(defaultProjectDirectory);
}

void WorldSettings::outlineWidthChanged(double width)
{
    settings->setValue("outline_width", (int) width);
    outlineWidth = width;
}

void WorldSettings::outlineColorChanged(QColor color)
{
    settings->setValue("outline_color", color.name());
    outlineColor = color;
}

void WorldSettings::projectDirectoryChanged(QString path)
{
    settings->setValue("default_directory", path);
    defaultProjectDirectory = path;
}

WorldSettings::~WorldSettings()
{
    delete ui;
}

void WorldSettings::setupDirectoryDefaults()
{
    auto path = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                + Constants::PROJECT_FOLDER;
    defaultProjectDirectory = settings->getValue("default_directory", path).toString();

    ui->projectDefault->setText(defaultProjectDirectory);
}
