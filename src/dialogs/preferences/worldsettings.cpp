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

#include "irisgl/src/core/irisutils.h"

#include <QFileDialog>
#include <QListView>
#include <QStandardPaths>
#include <QStyledItemDelegate>

#include "core/database/database.h"
#include "core/settingsmanager.h"
#include "widgets/sceneviewwidget.h"

#include "constants.h"
#include "globals.h"
#include "uimanager.h"

WorldSettings::WorldSettings(Database *handle, SettingsManager* settings) :
    QWidget(nullptr),
    ui(new Ui::WorldSettings)
{
    ui->setupUi(this);
	db = handle;

    this->settings = settings;

	ui->author->setText(db->getAuthorName());

	ui->cc->setItemDelegate(new QStyledItemDelegate(ui->cc));

	auto lv = new QListView();
	ui->cc->setView(lv);

    connect(ui->browseProject,  SIGNAL(pressed()),              SLOT(changeDefaultDirectory()));
    connect(ui->projectDefault, SIGNAL(textChanged(QString)),   SLOT(projectDirectoryChanged(QString)));
    connect(ui->browseEditor,   SIGNAL(pressed()),              SLOT(changeEditorPath()));
    connect(ui->editorPath,     SIGNAL(textChanged(QString)),   SLOT(editorPathChanged(QString)));
    connect(ui->outlineWidth,   SIGNAL(valueChanged(double)),   SLOT(outlineWidthChanged(double)));
    connect(ui->outlineColor,   SIGNAL(onColorChanged(QColor)), SLOT(outlineColorChanged(QColor)));
	connect(ui->showFPS,		SIGNAL(toggled(bool)),			SLOT(showFpsChanged(bool)));
	connect(ui->showPL,			SIGNAL(toggled(bool)),			SLOT(setShowPerspectiveLabel(bool)));
	connect(ui->autoSave,       SIGNAL(toggled(bool)),          SLOT(enableAutoSave(bool)));
	connect(ui->openInPlayer,   SIGNAL(toggled(bool)),          SLOT(enableOpenInPlayer(bool)));
	connect(ui->mouseControls,	SIGNAL(currentTextChanged(const QString&)),	SLOT(mouseControlChanged(const QString&)));

	QButtonGroup *buttonGroup = new QButtonGroup;
	ui->showPL->setChecked(SettingsManager::getDefaultManager()->getValue("show_PL", true).toBool());

	buttonGroup->addButton(ui->viewport_2);
	buttonGroup->addButton(ui->editor_2);
	buttonGroup->addButton(ui->content_2);
	buttonGroup->addButton(ui->mining_2);
	buttonGroup->addButton(ui->help);
	buttonGroup->addButton(ui->donate);
	buttonGroup->addButton(ui->about);
	buttonGroup->addButton(ui->shortcutss);

	// hide these for now
	ui->mining_2->hide();
	ui->donate->hide();

    //QPixmap p = IrisUtils::getAbsoluteAssetPath("app/images/mascot.png");

    //ui->logo->setPixmap(p.scaled(128, 128, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    //ui->logo->setAlignment(Qt::AlignCenter | Qt::AlignBottom);

	connect(buttonGroup,
		static_cast<void(QButtonGroup::*)(QAbstractButton *, bool)>(&QButtonGroup::buttonToggled),
		[](QAbstractButton *button, bool checked)
	{
		QString style = checked ? "background: #3498db" : "background: #1E1E1E";
		button->setStyleSheet(style);
	});

	connect(ui->viewport_2, &QPushButton::pressed, [this]() { ui->stackedWidget->setCurrentIndex(0); });
	connect(ui->editor_2,   &QPushButton::pressed, [this]() { ui->stackedWidget->setCurrentIndex(1); });
	connect(ui->content_2,  &QPushButton::pressed, [this]() { ui->stackedWidget->setCurrentIndex(2); });
	connect(ui->mining_2,   &QPushButton::pressed, [this]() { ui->stackedWidget->setCurrentIndex(3); });
	connect(ui->help,       &QPushButton::pressed, [this]() { ui->stackedWidget->setCurrentIndex(4); });
	connect(ui->about,		&QPushButton::pressed, [this]() { ui->stackedWidget->setCurrentIndex(5); });
	connect(ui->shortcutss,	&QPushButton::pressed, [this]() { ui->stackedWidget->setCurrentIndex(6); });

    setupDirectoryDefaults();
    setupOutline();
	setupMouseControls();

	showFps = settings->getValue("show_fps", false).toBool();
	ui->showFPS->setChecked(showFps);

	autoSave = settings->getValue("auto_save", true).toBool();
	ui->autoSave->setChecked(autoSave);
#ifdef BUILD_PLAYER_ONLY
	openInPlayer = true;
    ui->openInPlayer->setChecked(true);
    ui->openInPlayer->setEnabled(false);
#else
	openInPlayer = settings->getValue("open_in_player", false).toBool();
#endif


	autoUpdate = settings->getValue("automatic_updates", true).toBool();
	ui->checkUpdates->setChecked(autoUpdate);
	ui->stackedWidget->setCurrentIndex(0);
}

void WorldSettings::setupOutline()
{
    outlineWidth = settings->getValue("outline_width", 6).toInt();
    outlineColor = settings->getValue("outline_color", "#3498db").toString();

    ui->outlineWidth->setValue(outlineWidth);
    ui->outlineColor->setColor(outlineColor);
}

void WorldSettings::setupMouseControls()
{
	// mouse control options
	QList<QString> mouseControlModes;
	mouseControlModes.append("Jahshaka");
	mouseControlModes.append("Default");

	ui->mouseControls->blockSignals(true);
	ui->mouseControls->addItems(mouseControlModes);
	ui->mouseControls->blockSignals(false);

	auto mode = settings->getValue("mouse_controls", "jahshaka").toString();
	if (mode == "jahshaka")
		ui->mouseControls->setCurrentIndex(0);
	else
		ui->mouseControls->setCurrentIndex(1);
}

void WorldSettings::changeDefaultDirectory()
{
    QFileDialog projectDir;
    defaultProjectDirectory = projectDir.getExistingDirectory(nullptr, "Select project dir", defaultProjectDirectory);
    if (!defaultProjectDirectory.isNull())
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

void WorldSettings::showFpsChanged(bool show)
{
    showFps = show;
    if (UiManager::sceneViewWidget) UiManager::sceneViewWidget->setShowFps(show);
}

void WorldSettings::setShowPerspectiveLabel(bool show)
{
	if (UiManager::sceneViewWidget) UiManager::sceneViewWidget->setShowPerspeciveLabel(show);
}

void WorldSettings::enableAutoSave(bool state)
{
	settings->setValue("auto_save", autoSave = state);
}

void WorldSettings::enableOpenInPlayer(bool state)
{
	settings->setValue("open_in_player", openInPlayer = state);
}

void WorldSettings::enableAutoUpdate(bool state)
{
	settings->setValue("open_in_player", autoUpdate = state);
}

void WorldSettings::mouseControlChanged(const QString& value)
{
	if (value == "Jahshaka")
		settings->setValue("mouse_controls", "jahshaka");
	else
		settings->setValue("mouse_controls", "default");
}

void WorldSettings::projectDirectoryChanged(QString path)
{
    settings->setValue("default_directory", path);
    defaultProjectDirectory = path;
}

void WorldSettings::changeEditorPath()
{
    QFileDialog editorBrowser;
    defaultEditorPath = editorBrowser.getExistingDirectory(Q_NULLPTR, "Select Editor", defaultProjectDirectory);
    if (!defaultEditorPath.isNull())
        ui->editorPath->setText(defaultEditorPath);
}

void WorldSettings::editorPathChanged(QString path)
{
    settings->setValue("editor_path", path);
    defaultEditorPath = path;
}

void WorldSettings::saveSettings()
{
	if (!ui->author->text().isEmpty()) {
		db->updateAuthorInfo(ui->author->text());
	}
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
