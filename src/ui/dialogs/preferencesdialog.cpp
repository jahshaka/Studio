/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#include "ui/dialogs/preferencesdialog.h"
#include "ui_preferencesdialog.h"
#include <QListWidgetItem>
#include "ui/dialogs/preferences/worldsettings.h"
#include "ui/dialogs/preferences/worldsettingswidget.h"
#include "data/settingsmanager.h"
#include "data/database/database.h"
#include "ui/dialogs/aboutdialog.h"

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
