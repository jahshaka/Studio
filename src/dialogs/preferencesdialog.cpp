/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/
#include "preferencesdialog.h"
#include "ui_preferencesdialog.h"
#include <QListWidgetItem>
#include "preferences/worldsettings.h"
#include "preferences/worldsettingswidget.h"
#include "../core/settingsmanager.h"
#include "../core/database/database.h"
#include "aboutdialog.h"

PreferencesDialog::PreferencesDialog(QWidget* parent, Database *handle, SettingsManager* settings) :
    QDialog(parent),
    ui(new Ui::PreferencesDialog)
{
    ui->setupUi(this);

	this->setWindowFlags(Qt::FramelessWindowHint);
	setWindowModality(Qt::ApplicationModal);

	db = handle;
    this->settings = settings;

    setWindowTitle("Preferences");

    connect(ui->okButton, SIGNAL(clicked(bool)), this, SLOT(saveSettings()));
	connect(ui->cancelButton, &QPushButton::pressed, [this]() { this->close(); });

    setupPages();
}

void PreferencesDialog::setupPages()
{
    // can we elimate this to be more permanent? why (was/is) this dynamic really?
    worldSettings = new WorldSettingsWidget(db, settings);
    ui->worldLayout->addWidget(worldSettings);
}

void PreferencesDialog::saveSettings()
{
	worldSettings->saveSettings();
	close();
}

PreferencesDialog::~PreferencesDialog()
{
    delete ui;
}
