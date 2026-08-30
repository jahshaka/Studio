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
#include <QTabWidget>
#include "ui/dialogs/preferences/worldsettingswidget.h"
#include "ui/dialogs/preferences/mcpsettingswidget.h"
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
    mcpSettings = new McpSettingsWidget(settings);

    auto *tabs = new QTabWidget(this);
    tabs->addTab(worldSettings, "General");
    tabs->addTab(mcpSettings, "Claude / MCP");
    ui->worldLayout->addWidget(tabs);
}

void PreferencesDialog::saveSettings()
{
	worldSettings->saveSettings();
	if (mcpSettings) mcpSettings->saveSettings();
	close();
}

void PreferencesDialog::wireEditor(IEditorViewport *viewport, MainWindow *mainWindow)
{
    if (worldSettings) worldSettings->wireEditor(viewport, mainWindow);
}

void PreferencesDialog::wireMcp(McpServer *server, MainWindow *mainWindow)
{
    if (mcpSettings) mcpSettings->wireMcp(server, mainWindow);
}

void PreferencesDialog::wireShortcuts(ShortcutRegistry *registry)
{
    if (worldSettings) worldSettings->setShortcutRegistry(registry);
}

PreferencesDialog::~PreferencesDialog()
{
    delete ui;
}
