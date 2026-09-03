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
#include "ui/dialogs/preferences/cachesettingswidget.h"
#include "ui/dialogs/preferences/assetssettingswidget.h"
#include "data/settingsmanager.h"
#include "data/database/database.h"
#include "ui/dialogs/aboutdialog.h"
#include "ui/style/stylesheet.h"

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
    assetsSettings = new AssetsSettingsWidget(settings, db);
    mcpSettings = new McpSettingsWidget(settings);
    cacheSettings = new CacheSettingsWidget(settings);

    auto *tabs = new QTabWidget(this);
    // The dark theme: a bare QTabWidget renders the platform-light pane over
    // the dark dialog (owner regression 2026-08-31) — the centralized sheet
    // covers the tab bar, pane, page backgrounds and text defaults.
    tabs->setStyleSheet(StyleSheet::PreferencesTabs());
    tabs->addTab(worldSettings, "General");
    tabs->addTab(assetsSettings, "Assets");
    tabs->addTab(cacheSettings, "Cache");
    tabs->addTab(mcpSettings, "Claude / MCP");
    ui->worldLayout->addWidget(tabs);
}

void PreferencesDialog::saveSettings()
{
	worldSettings->saveSettings();
	if (assetsSettings) assetsSettings->saveSettings();
	if (mcpSettings) mcpSettings->saveSettings();
	if (cacheSettings) cacheSettings->saveSettings();
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
