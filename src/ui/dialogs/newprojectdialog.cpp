/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include <QCheckBox>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QShowEvent>

#include "irisgl/core/irisutils.h"
#include "data/settingsmanager.h"
#include "services/apppaths.h"
#include "ui/style/stylesheet.h"
#include "ui/style/thememanager.h"
#include "ui/dialogs/newprojectdialog.h"

#include <QStandardPaths>
#include <QMessageBox>

#include "data/constants.h"

NewProjectDialog::NewProjectDialog(QWidget *parent) : QDialog(parent)
{

	setAttribute(Qt::WA_MacShowFocusRect, false);
	setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint | Qt::FramelessWindowHint);

	scene = new QLabel("Name");
	path = new QLabel("Location");
	projectPathEdit = new QLineEdit();
	projectNameEdit = new QLineEdit();
	// BROWSE (owner review R1d). The location was a disabled read-out of one
	// fixed folder; the button is what makes it a choice. Narrow and beside
	// the field, so the field still shows the start of the path.
	browse = new QPushButton(tr("Browse…"));
	browse->setObjectName(QStringLiteral("browseLocation"));
	browse->setAutoDefault(false);
	browse->setDefault(false);
	// EMPTY SCENE (owner review R1a / answer Q1): off = the template (ground,
	// sun, Sky Light, the realistic sky); on = a blank world.
	emptyScene = new QCheckBox(tr("Empty scene"));
	emptyScene->setObjectName(QStringLiteral("emptyScene"));
	emptyScene->setToolTip(tr("Start with a blank world — no ground, no lights, no sky.\n"
	                          "Off: the default template (ground, sun, sky light, real-time sky)."));
	cancel = new QPushButton("Cancel");
	create = new QPushButton("Create");

	create->setAutoDefault(true);
	create->setDefault(true);
    projectNameEdit->setAttribute(Qt::WA_MacShowFocusRect, false);

    settingsManager = SettingsManager::getDefaultManager();

	// READ-ONLY, NOT DISABLED. The path is chosen with Browse, never typed —
	// but a disabled QLineEdit cannot be selected or copied, and this is the
	// one place a user is told where their work is going to live.
	projectPathEdit->setReadOnly(true);

    connect(create, SIGNAL(pressed()), SLOT(confirmProjectCreation()));
    connect(cancel, SIGNAL(pressed()), SLOT(close()));
    connect(browse, SIGNAL(pressed()), SLOT(setProjectPath()));

    // services/apppaths.h: the data root when a run forces one, the
    // `default_directory` preference otherwise (S-extra2). This is the
    // Jahshaka documents folder the owner asked for as the default, and it is
    // what Browse opens AT (setProjectPath passes the field's current value as
    // the file dialog's starting directory).
    //
    // AND CHOOSING A LOCATION HERE IS PER PROJECT (SMALL-UI-A fix round F2).
    // This dialog never writes `default_directory` — the one writer of that
    // preference in the whole tree is Preferences > World Settings
    // (ui/dialogs/preferences/worldsettingswidget.cpp), which is where a user
    // changes where their projects live FROM NOW ON. Putting one project on an
    // external drive must not move every future project there too, and the
    // chosen root is recorded with the project row instead
    // (Database::setProjectLocation; ProjectService::projectFolderFor reads it).
    projectPath = AppPaths::projectsRoot(
        settingsManager->get(settingkeys::defaultDirectory),
        Constants::PROJECT_FOLDER);

	setProjectLocation(projectPath);

	auto title = new QLabel("Create Scene");
	title->setAlignment(Qt::AlignCenter);
	title->setStyleSheet(StyleSheet::QLabelWhite());

	auto grid = new QVBoxLayout;
	this->setLayout(grid);

	grid->addWidget(title);
	grid->addSpacing(4);

	grid->addWidget(scene);
	grid->addWidget(projectNameEdit);
	grid->addWidget(path);
	// The location row is FIELD + BROWSE, one line.
	auto locationRow = new QWidget;
	auto locationLayout = new QHBoxLayout;
	locationRow->setLayout(locationLayout);
	locationLayout->setContentsMargins(0, 0, 0, 0);
	locationLayout->addWidget(projectPathEdit);
	locationLayout->addWidget(browse);
	grid->addWidget(locationRow);

	grid->addSpacing(6);
	grid->addWidget(emptyScene);

	auto wid = new QWidget;
	auto layout = new QHBoxLayout;
	wid->setLayout(layout);
	layout->addStretch();
	layout->addWidget(cancel);
	layout->addWidget(create);
	layout->setContentsMargins(0, 0, 0, 0);

	grid->addSpacing(20);
	grid->addWidget(wid);


	// WIDTH. 288 px was the 2026-09-07 narrowing, taken when the location row
	// was a disabled read-out nobody could act on; with a Browse button beside
	// the field that leaves the field about 150 px, so the dialog is "a little
	// wider to fit the new location button" (owner review R1e) — 360, which is
	// still well under the 410 the narrowing came from. The field is allowed to
	// shrink and shows the START of the path; the full value is in the tooltip
	// and is what gets used either way (projectPath, not the edit's text).
	setFixedWidth(360);
	projectPathEdit->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
	projectPathEdit->setMinimumWidth(0);
	setStyleSheet(StyleSheet::QWidgetDark());

	projectNameEdit->setStyleSheet(StyleSheet::QLineEdit());
	projectPathEdit->setStyleSheet(StyleSheet::QLineEdit());
	scene->setStyleSheet(StyleSheet::QLabelWhite());
	path->setStyleSheet(StyleSheet::QLabelWhite());
	if (ThemeManager::classicActive()) emptyScene->setStyleSheet(StyleSheet::QCheckBox());
	// THE PRIMARY ACTION IS "CREATE". Classic said so through its own two
	// sheets; under Qlementine both getters return "" and the theme painted
	// Cancel as the accented button and Create as the plain one — the owner
	// read it, correctly, as the colours being swapped. The house chrome
	// getters state it explicitly instead of relying on which button the
	// style thinks is default.
	cancel->setStyleSheet(ThemeManager::classicActive()
	                          ? StyleSheet::QPushButtonGreyscaleBig()
	                          : ThemeManager::chromeButtonSheet());
	create->setStyleSheet(ThemeManager::classicActive()
	                          ? StyleSheet::QPushButtonBlueBig()
	                          : ThemeManager::chromeAccentButtonSheet());
	browse->setStyleSheet(ThemeManager::classicActive()
	                          ? StyleSheet::QPushButtonGreyscaleBig()
	                          : ThemeManager::chromeButtonSheet());
	// ...and no auto-default on Cancel, so Return still creates.
	cancel->setAutoDefault(false);
	cancel->setDefault(false);
}

NewProjectDialog::~NewProjectDialog()
{
}

ProjectInfo NewProjectDialog::getProjectInfo()
{
	ProjectInfo pInfo = { projectName, projectPath, emptyScene->isChecked() };
	return pInfo;
}

void NewProjectDialog::setProjectLocation(const QString &path)
{
	if (path.isEmpty()) return;
	projectPath = path;
	projectPathEdit->setText(projectPath);
	projectPathEdit->setToolTip(projectPath);
	projectPathEdit->setCursorPosition(0);
}

void NewProjectDialog::setProjectPath()
{
	// Opens ON the folder the field currently shows, so Browse starts where
	// the user is rather than at the process's cwd. A cancelled dialog returns
	// an empty string and setProjectLocation leaves the field alone.
	setProjectLocation(QFileDialog::getExistingDirectory(this, tr("Choose a location"),
	                                                     projectPath));
}

void NewProjectDialog::createNewProject()
{
    projectName = projectNameEdit->text();
}

void NewProjectDialog::confirmProjectCreation()
{
    createNewProject();
    this->close();
    emit accepted();
}

void NewProjectDialog::showEvent(QShowEvent *event)
{
	QDialog::showEvent(event);
	centreOnParent();
}

void NewProjectDialog::centreOnParent()
{
	if (centred) return;
	// A frameless dialog gets no help from the window manager, so the position
	// is ours to set: the centre of the parent WINDOW (not the parent widget —
	// a dialog opened from a page inside the editor belongs to the window the
	// user is looking at). With no parent at all there is nothing to centre on
	// and the platform's own placement stands.
	const QWidget *anchor = parentWidget() ? parentWidget()->window() : nullptr;
	if (!anchor) return;
	centred = true;
	const QRect frame = anchor->frameGeometry();
	move(frame.center() - QPoint(width() / 2, height() / 2));
}
