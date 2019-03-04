/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "worldsettingswidget.h"
#include "ui_worldsettings.h"

#include "irisgl/src/core/irisutils.h"

#include <QFileDialog>
#include <QListView>
#include <QStandardPaths>
#include <QStyledItemDelegate>
#include <QButtonGroup>

#include "core/database/database.h"
#include "core/settingsmanager.h"
#include "widgets/sceneviewwidget.h"

#include "constants.h"
#include "globals.h"
#include "uimanager.h"

WorldSettingsWidget::WorldSettingsWidget(Database *handle, SettingsManager* settings) :
    QWidget(nullptr)
{

	db = handle;
    this->settings = settings;

	//ui->author->setText(db->getAuthorName());
	//ui->cc->setItemDelegate(new QStyledItemDelegate(ui->cc));

	//auto lv = new QListView();
	//ui->cc->setView(lv);


	viewport = new QPushButton("Viewport");
	editor = new QPushButton("Editor");
	content = new QPushButton("Content");
	mining = new QPushButton("Mining");
	help = new QPushButton("Help");
	about = new QPushButton("About");
	shortcuts = new QPushButton("Shortcuts");
	database = new QPushButton("Database");

	viewportWidget	= new QWidget;
	editorWidget 	= new QWidget;
	contentWidget	= new QWidget;
	miningWidget	= new QWidget;
	helpWidget		= new QWidget;
	aboutWidget		= new QWidget;
	shortcutsWidget	= new QWidget;
	databaseWidget	= new QWidget;

	stack = new QStackedWidget;

	auto mainLayout = new QVBoxLayout;
	auto leftWidget = new QWidget;
	auto splitter = new QSplitter(Qt::Horizontal);

	setLayout(mainLayout);
	mainLayout->addWidget(splitter);

	splitter->addWidget(leftWidget);
	splitter->addWidget(stack);

	auto buttonLayout = new QVBoxLayout;
	buttonLayout->addWidget(viewport);
	buttonLayout->addWidget(editor);
	buttonLayout->addWidget(content);
	buttonLayout->addWidget(mining);
	buttonLayout->addWidget(help);
	buttonLayout->addWidget(about);
	buttonLayout->addWidget(shortcuts);
	buttonLayout->addWidget(database);
	leftWidget->setLayout(buttonLayout);
	buttonLayout->setSpacing(0);
	buttonLayout->setContentsMargins(0, 0, 0, 0);

	stack->addWidget(viewportWidget);
	stack->addWidget(editorWidget);
	stack->addWidget(contentWidget);
	stack->addWidget(miningWidget);
	stack->addWidget(helpWidget);
	stack->addWidget(aboutWidget);
	stack->addWidget(shortcutsWidget);
	stack->addWidget(databaseWidget);

	configureViewport();
	configureEditor();
	configureContent();
	configureAbout();
	configureShortcuts();
	configureDatabaseWidget();

	connect(viewport, &QPushButton::clicked, [=]() { stack->setCurrentIndex(0); });
	connect(editor, &QPushButton::clicked, [=]() { stack->setCurrentIndex(1); });
	connect(content, &QPushButton::clicked, [=]() { stack->setCurrentIndex(2); });
	connect(mining, &QPushButton::clicked, [=]() { stack->setCurrentIndex(3); });
	connect(help, &QPushButton::clicked, [=]() { stack->setCurrentIndex(4); });
	connect(about, &QPushButton::clicked, [=]() { stack->setCurrentIndex(5); });
	connect(shortcuts, &QPushButton::clicked, [=]() { stack->setCurrentIndex(6); });
	connect(database, &QPushButton::clicked, [=]() { stack->setCurrentIndex(7); });


 //   connect(ui->browseProject,  SIGNAL(pressed()),              SLOT(changeDefaultDirectory()));
 //   connect(ui->projectDefault, SIGNAL(textChanged(QString)),   SLOT(projectDirectoryChanged(QString)));
 //   connect(ui->browseEditor,   SIGNAL(pressed()),              SLOT(changeEditorPath()));
 //   connect(ui->editorPath,     SIGNAL(textChanged(QString)),   SLOT(editorPathChanged(QString)));
 //   connect(ui->outlineWidth,   SIGNAL(valueChanged(double)),   SLOT(outlineWidthChanged(double)));
 //   connect(ui->outlineColor,   SIGNAL(onColorChanged(QColor)), SLOT(outlineColorChanged(QColor)));
	//connect(ui->showFPS,		SIGNAL(toggled(bool)),			SLOT(showFpsChanged(bool)));
	////connect(ui->showPL,			SIGNAL(toggled(bool)),			SLOT(setShowPerspectiveLabel(bool)));
	//connect(ui->autoSave,       SIGNAL(toggled(bool)),          SLOT(enableAutoSave(bool)));
	//connect(ui->openInPlayer,   SIGNAL(toggled(bool)),          SLOT(enableOpenInPlayer(bool)));
	//connect(ui->mouseControls,	SIGNAL(currentTextChanged(const QString&)),	SLOT(mouseControlChanged(const QString&)));

	//QButtonGroup *buttonGroup = new QButtonGroup;
	////ui->showPL->setChecked(SettingsManager::getDefaultManager()->getValue("show_PL", true).toBool());

	//buttonGroup->addButton(ui->viewport_2);
	//buttonGroup->addButton(ui->editor_2);
	//buttonGroup->addButton(ui->content_2);
	//buttonGroup->addButton(ui->mining_2);
	//buttonGroup->addButton(ui->help);
	//buttonGroup->addButton(ui->donate);
	//buttonGroup->addButton(ui->about);
	//buttonGroup->addButton(ui->shortcutss);

	//// hide these for now
	//ui->mining_2->hide();
	//ui->donate->hide();

 //   //QPixmap p = IrisUtils::getAbsoluteAssetPath("app/images/mascot.png");

 //   //ui->logo->setPixmap(p.scaled(128, 128, Qt::KeepAspectRatio, Qt::SmoothTransformation));
 //   //ui->logo->setAlignment(Qt::AlignCenter | Qt::AlignBottom);

	//connect(buttonGroup,
	//	static_cast<void(QButtonGroup::*)(QAbstractButton *, bool)>(&QButtonGroup::buttonToggled),
	//	[](QAbstractButton *button, bool checked)
	//{
	//	QString style = checked ? "background: #3498db" : "background: #1E1E1E";
	//	button->setStyleSheet(style);
	//});

	//connect(ui->viewport_2, &QPushButton::pressed, [this]() { ui->stackedWidget->setCurrentIndex(0); });
	//connect(ui->editor_2,   &QPushButton::pressed, [this]() { ui->stackedWidget->setCurrentIndex(1); });
	//connect(ui->content_2,  &QPushButton::pressed, [this]() { ui->stackedWidget->setCurrentIndex(2); });
	//connect(ui->mining_2,   &QPushButton::pressed, [this]() { ui->stackedWidget->setCurrentIndex(3); });
	//connect(ui->help,       &QPushButton::pressed, [this]() { ui->stackedWidget->setCurrentIndex(4); });
	//connect(ui->about,		&QPushButton::pressed, [this]() { ui->stackedWidget->setCurrentIndex(5); });
	//connect(ui->shortcutss,	&QPushButton::pressed, [this]() { ui->stackedWidget->setCurrentIndex(6); });


	//showFps = settings->getValue("show_fps", false).toBool();
	//ui->showFPS->setChecked(showFps);

	//autoSave = settings->getValue("auto_save", true).toBool();
	//ui->autoSave->setChecked(autoSave);
	//
	//openInPlayer = settings->getValue("open_in_player", false).toBool();


	//autoUpdate = settings->getValue("automatic_updates", true).toBool();
	//ui->checkUpdates->setChecked(autoUpdate);
	//ui->stackedWidget->setCurrentIndex(0);
}




void WorldSettingsWidget::changeDefaultDirectory()
{
    QFileDialog projectDir;
    //defaultProjectDirectory = projectDir.getExistingDirectory(nullptr, "Select project dir", defaultProjectDirectory);
    //if (!defaultProjectDirectory.isNull())
    //    ui->projectDefault->setText(defaultProjectDirectory);
}

void WorldSettingsWidget::outlineWidthChanged(double width)
{
    settings->setValue("outline_width", (int) width);
    outlineWidth = width;
}

void WorldSettingsWidget::outlineColorChanged(QColor color)
{
    settings->setValue("outline_color", color.name());
    outlineColor = color;
}

void WorldSettingsWidget::showFpsChanged(bool show)
{
    showFps = show;
    if (UiManager::sceneViewWidget) UiManager::sceneViewWidget->setShowFps(show);
}

void WorldSettingsWidget::setShowPerspectiveLabel(bool show)
{
	if (UiManager::sceneViewWidget) UiManager::sceneViewWidget->setShowPerspeciveLabel(show);
}

void WorldSettingsWidget::enableAutoSave(bool state)
{
	settings->setValue("auto_save", autoSave = state);
}

void WorldSettingsWidget::enableOpenInPlayer(bool state)
{
	settings->setValue("open_in_player", openInPlayer = state);
}

void WorldSettingsWidget::enableAutoUpdate(bool state)
{
	settings->setValue("open_in_player", autoUpdate = state);
}

void WorldSettingsWidget::mouseControlChanged(const QString& value)
{
	if (value == "Jahshaka")
		settings->setValue("mouse_controls", "jahshaka");
	else
		settings->setValue("mouse_controls", "default");
}

void WorldSettingsWidget::projectDirectoryChanged(QString path)
{
    settings->setValue("default_directory", path);
    defaultProjectDirectory = path;
}

void WorldSettingsWidget::changeEditorPath()
{
    QFileDialog editorBrowser;
    defaultEditorPath = editorBrowser.getExistingDirectory(Q_NULLPTR, "Select Editor", defaultProjectDirectory);
    //if (!defaultEditorPath.isNull())
    //    ui->editorPath->setText(defaultEditorPath);
}

void WorldSettingsWidget::editorPathChanged(QString path)
{
    settings->setValue("editor_path", path);
    defaultEditorPath = path;
}

void WorldSettingsWidget::saveSettings()
{
	//if (!ui->author->text().isEmpty()) {
	//	db->updateAuthorInfo(ui->author->text());
	//}
}

WorldSettingsWidget::~WorldSettingsWidget()
{
  //  delete ui;
}
//
//void WorldSettingsWidget::setupDirectoryDefaults()
//{
//    auto path = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
//                + Constants::PROJECT_FOLDER;
//    defaultProjectDirectory = settings->getValue("default_directory", path).toString();
//
//   // ui->projectDefault->setText(defaultProjectDirectory);
//}

void WorldSettingsWidget::configureViewport()
{
	auto layout = new QVBoxLayout;
	viewportWidget->setLayout(layout);

	auto selectionOutlineWidth = new QLabel("Selection Outline Width :");
	auto selectionOutlineColor = new QLabel("Selection Outline Color :");
	auto enableAutoSave = new QLabel("Enable Autosave :");

	setSizePolicyForWidgets(selectionOutlineColor);
	setSizePolicyForWidgets(selectionOutlineWidth);
	setSizePolicyForWidgets(enableAutoSave);

	auto spinbox = new QDoubleSpinBox;
	auto colorPicker = new ColorPickerWidget;
	auto checkbox = new QCheckBox;

	auto h1 = new QHBoxLayout;
	auto h2 = new QHBoxLayout;
	auto h3 = new QHBoxLayout;

	h1->addWidget(selectionOutlineWidth);
	h1->addWidget(spinbox);

	h2->addWidget(selectionOutlineColor);
	h2->addWidget(colorPicker);

	h3->addWidget(enableAutoSave);
	h3->addWidget(checkbox);

	layout->addLayout(h1);
	layout->addLayout(h2);
	layout->addLayout(h3);
	layout->addStretch();

	spinbox->setValue(settings->getValue("outline_width", 6).toInt());
	colorPicker->setColor(settings->getValue("outline_color", "#3498db").toString());
	checkbox->setChecked(settings->getValue("auto_save", true).toBool());

	connect(spinbox, SIGNAL(valueChanged(double)), this, SLOT(outlineWidthChanged(double)));
	connect(colorPicker, SIGNAL(onColorChanged(QColor)), this, SLOT(outlineColorChanged(QCOlor)));
	connect(checkbox, SIGNAL(toggled(bool)), this, SLOT(enableAutoSave(bool)));

}

void WorldSettingsWidget::configureEditor()
{
	auto layout = new QVBoxLayout;
	editorWidget->setLayout(layout);

	auto showFPS = new QLabel("Show FPS :");
	auto showViewportProjection = new QLabel("Show Viewport Projection:");
	auto openImportedWorldsInPlayer = new QLabel("Open Imported Worlds In Player :");
	auto autoCheckUpdates = new QLabel("Automatically Check For Updates :");
	auto mouseControls = new QLabel("EMouse Controls :");

	setSizePolicyForWidgets(showFPS);
	setSizePolicyForWidgets(showViewportProjection);
	setSizePolicyForWidgets(showViewportProjection);
	setSizePolicyForWidgets(autoCheckUpdates);
	setSizePolicyForWidgets(mouseControls);

	auto fps = new QCheckBox;
	auto viewportProjection = new QCheckBox;
	auto openInPlayer = new QCheckBox;
	auto autoUpdates = new QCheckBox;
	auto mouseCon = new QComboBox;

	auto h1 = new QHBoxLayout;
	auto h2 = new QHBoxLayout;
	auto h3 = new QHBoxLayout;
	auto h4 = new QHBoxLayout;
	auto h5 = new QHBoxLayout;

	h1->addWidget(showFPS);
	h1->addWidget(fps);
	h2->addWidget(showViewportProjection);
	h2->addWidget(viewportProjection);
	h3->addWidget(openImportedWorldsInPlayer);
	h3->addWidget(openInPlayer);
	h4->addWidget(autoCheckUpdates);
	h4->addWidget(autoUpdates);
	h5->addWidget(mouseControls);
	h5->addWidget(mouseCon);

	layout->addLayout(h1);
	layout->addLayout(h2);
	layout->addLayout(h3);
	layout->addLayout(h4);
	layout->addLayout(h5);

	fps->setChecked(settings->getValue("show_fps", false).toBool());
	openInPlayer->setChecked(settings->getValue("open_in_player", false).toBool());
	autoUpdates->setChecked(settings->getValue("automatic_updates", true).toBool());
	viewportProjection->setChecked( UiManager::sceneViewWidget ? UiManager::sceneViewWidget->showFps : false);
	
	// mouse control options
	QStringList list;
	list << "Jahshaka" << "Default";
	mouseCon->addItems(list);

	auto mode = settings->getValue("mouse_controls", "jahshaka").toString();
	if (mode == "jahshaka")		mouseCon->setCurrentIndex(0);
	else						mouseCon->setCurrentIndex(1);

	connect(fps, SIGNAL(toggled(bool)), this, SLOT(showFpsChanged(bool)));
	connect(viewportProjection, SIGNAL(toggled(bool)), this, SLOT(setShowPerspectiveLabel(bool)));
	connect(openInPlayer, SIGNAL(toggled(bool)), this, SLOT(enableOpenInPlayer(bool)));
	connect(autoUpdates, SIGNAL(toggled(bool)), this, SLOT(enableAutoUpdate(bool)));
	connect(mouseCon, SIGNAL(currentTextChanged(const QString&)), SLOT(mouseControlChanged(const QString&)));

}

void WorldSettingsWidget::configureContent()
{
	auto layout = new QVBoxLayout;
	contentWidget->setLayout(layout);

	auto l1 = new QLabel("Default Project Directory");
	auto l2 = new QLabel("Preferred Text Editor");
	auto l3 = new QLabel("Author");
	auto l4 = new QLabel("Default License");

	auto projectDir = new QLineEdit;
	auto textEdit = new QLineEdit;
	auto author = new QLineEdit;
	auto license = new QComboBox;

	setSizePolicyForWidgets(l1);
	setSizePolicyForWidgets(l2);
	setSizePolicyForWidgets(l3);
	setSizePolicyForWidgets(l4);
	setSizePolicyForWidgets(projectDir);
	setSizePolicyForWidgets(textEdit);
	setSizePolicyForWidgets(author);
	setSizePolicyForWidgets(license);

	auto browse1 = new QPushButton("...");
	auto browse2 = new QPushButton("...");

	auto h1 = new QHBoxLayout;
	auto h2 = new QHBoxLayout;
	auto h3 = new QHBoxLayout;
	auto h4 = new QHBoxLayout;

	h1->addWidget(l1);
	h1->addWidget(projectDir);
	h1->addWidget(browse1);

	h2->addWidget(l2);
	h2->addWidget(textEdit);
	h2->addWidget(browse2);

	h3->addWidget(l3);
	h3->addWidget(author);

	h4->addWidget(l4);
	h4->addWidget(license);

	layout->addLayout(h1);
	layout->addLayout(h2);
	layout->addLayout(h3);
	layout->addLayout(h4);

	// set default directory 
	auto path = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
		+ Constants::PROJECT_FOLDER;
	defaultProjectDirectory = settings->getValue("default_directory", path).toString();

	projectDir->setText(defaultProjectDirectory);
	//set editor path

	QStringList list;
	list << "CC0 - NO restrictions";

	license->addItems(list);

	connect(browse1, &QPushButton::clicked, [=]() {
		QFileDialog pather;
		defaultProjectDirectory = pather.getExistingDirectory(nullptr, "Select project dir", defaultProjectDirectory);
		if (!defaultProjectDirectory.isNull()) {
			projectDir->setText(defaultProjectDirectory);
			projectDirectoryChanged(defaultProjectDirectory);
		}
	});
	connect(browse2, &QPushButton::clicked, [=]() {
		QFileDialog editorBrowser;
		defaultEditorPath = editorBrowser.getExistingDirectory(Q_NULLPTR, "Select Editor", defaultProjectDirectory);
		if (!defaultEditorPath.isNull()) {
			textEdit->setText(defaultEditorPath);
			editorPathChanged(defaultEditorPath);
		}
	});

}

void WorldSettingsWidget::configureAbout()
{
	auto layout = new QVBoxLayout;
	aboutWidget->setLayout(layout);

	auto view = new QTextBrowser;
	view->setHtml(QString("<!DOCTYPE HTML PUBLIC '-//W3C//DTD HTML 4.0//EN' 'http://www.w3.org/TR/REC-html40/strict.dtd'>\n<html><head><meta name = 'qrichtext' content = '1' / ><style type = 'text/css'>\np, li{ white - space: pre - wrap; }\n< / style>< / head><body style = ' font-family:'MS Shell Dlg 2'; font-size:12pt; font-weight:400; font-style:normal;'>\n<p style = ' margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;'><span style = ' font-size:8.25pt;'>Jahshaka is bringing you the future of immersive digital content creation.It delivers a media management and playback platform the is accentuated by compositing, editing and effects modules.Jahshaka is free software, developed as an open source project under the GPL licence, and is designed to be compiled for Windows, OsX and many distributions of Linux.< / span>< / p>\n<p style = '-qt-paragraph-type:empty; margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; font-size:8.25pt;'><br / >< / p>\n<p style = '-qt-paragraph-type:empty; margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; font-size:8.25pt;'><br / >< / p>\n<p style = ' margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;'><span style = ' font-weight:600;'>Core Jahshaka Features< / span>< / p>\n<p style = '-qt-paragraph-type:empty; margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; font-size:8.25pt;'><br / >< / p>\n<p style = ' margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;'> < span style = ' font-size:8.25pt;'>2D and 3D animation &amp; compositing< / span>< / p>\n<p style = ' margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;'><span style = ' font-size:8.25pt;'>Media and asset management< / span>< / p>\n<p style = ' margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;'> < span style = ' font-size:8.25pt;'>2D and 3D playback< / span>< / p>\n<p style = ' margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;'><span style = ' font-size:8.25pt;'>Colour Correction< / span>< / p>\n<p style = ' margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;'><span style = ' font-size:8.25pt;'>Editing &amp; Effects< / span>< / p>\n<p style = '-qt-paragraph-type:empty; margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; font-size:8.25pt;'><br / >< / p>\n<p style = '-qt-paragraph-type:empty; margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; font-size:8.25pt;'><br / >< / p>\n<p style = ' margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;'><span style = ' font-weight:600;'>Support Jahshaka< / span>< / p>\n<p style = '-qt-paragraph-type:empty; margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; font-size:8.25pt;'><br / >< / p>\n<p style = ' margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;'><span style = ' font-size:8.25pt;'>There are many ways you can support Jahshaka, the main thing is to just get involved.You can follow us on twitter for news and updates and join our community at facebook.< / span>< / p>\n<p style = '-qt-paragraph-type:empty; margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; font-size:8.25pt;'><br / >< / p>\n<p style = '-qt-paragraph-type:empty; margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; font-size:8.25pt;'><br / >< / p>\n<p style = '-qt-paragraph-type:empty; margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; font-size:8.25pt;'><br / >< / p>\n<p style = '-qt-paragraph-type:empty; margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; font-size:8.25pt;'><br / >< / p>\n<p style = ' margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;'><span style = ' font-weight:600;'>Jahshaka is, and will always remain, free!< / span>< / p>\n<p style = '-qt-paragraph-type:empty; margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px; font-size:8.25pt;'><br / >< / p>\n<p style = ' margin-top:0px; margin-bottom:0px; margin-left:0px; margin-right:0px; -qt-block-indent:0; text-indent:0px;'><span style = ' font-size:8.25pt;'>Jahshaka, the industrys leading free, open source digital content creation platform, is back!Stay tuned for news, updates, a new jahshaka site and a powerful new release!< / span>< / p>< / body>< / html>"));
	setSizePolicyForWidgets(view);

	layout->addWidget(view);
}

void WorldSettingsWidget::configureShortcuts()
{
	auto layout = new QGridLayout;
	shortcutsWidget->setLayout(layout);

	auto scrollArea = new QScrollArea;
	auto widget = new QWidget;
	auto holderLayout = new QVBoxLayout;

	scrollArea->setWidget(widget);
	widget->setLayout(holderLayout);

	auto k1 = new QLabel("T");
	auto v1 = new QLabel("Switch to translate tool");
	auto k2 = new QLabel("R");
	auto v2 = new QLabel("Switch to rotate tool");
	auto k3 = new QLabel("S");
	auto v3 = new QLabel("Switch to scale tool");
	auto k4 = new QLabel("CRTL + S");
	auto v4 = new QLabel("Saves scene");
	auto k5 = new QLabel("CRTL (hold)");
	auto v5 = new QLabel("Hold while moving objects to snap to grid");
	auto k6 = new QLabel("X");
	auto v6 = new QLabel("View from left");
	auto k7 = new QLabel("CRTL + X");
	auto v7 = new QLabel("View from right");
	auto k8 = new QLabel("Y");
	auto v8 = new QLabel("View from top");
	auto k9 = new QLabel("CRTL + Y");
	auto v9 = new QLabel("View from botom");
	auto k10 = new QLabel("Z");
	auto v10 = new QLabel("View from front");
	auto k11 = new QLabel("CRTL + Z");
	auto v11 = new QLabel("View from back");
	
	layout->addWidget(k1, 0, 0);
	layout->addWidget(v1, 0, 1);
	layout->addWidget(k2, 1, 0);
	layout->addWidget(v2, 1, 1);
	layout->addWidget(k3, 2, 0);
	layout->addWidget(v3, 2, 1);
	layout->addWidget(k4, 3, 0);
	layout->addWidget(v4, 3, 1);
	layout->addWidget(k5, 4, 0);
	layout->addWidget(v5, 4, 1);
	layout->addWidget(k6, 5, 0);
	layout->addWidget(v6, 5, 1);
	layout->addWidget(k7, 6, 0);
	layout->addWidget(v7, 6, 1);
	layout->addWidget(k8, 7, 0);
	layout->addWidget(v8, 7, 1);
	layout->addWidget(k9, 8, 0);
	layout->addWidget(v9, 8, 1);
	layout->addWidget(k10, 9, 0);
	layout->addWidget(v10, 9, 1);
	layout->addWidget(k11, 10, 0);
	layout->addWidget(v11, 10, 1);

	layout->setRowStretch(11, 100);
}

void WorldSettingsWidget::configureDatabaseWidget()
{
	auto layout = new QGridLayout;
	databaseWidget->setLayout(layout);

}

void WorldSettingsWidget::setSizePolicyForWidgets(QWidget *widget)
{
	widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
}
