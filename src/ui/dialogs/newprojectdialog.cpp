/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>

#include "irisgl/core/irisutils.h"
#include "data/settingsmanager.h"
#include "services/apppaths.h"
#include "ui/style/stylesheet.h"
#include "ui/style/thememanager.h"
#include "ui/dialogs/newprojectdialog.h"

#include <QStandardPaths>
#include <QMessageBox>

#include "data/constants.h"

NewProjectDialog::NewProjectDialog(QDialog *parent) : QDialog(parent)
{

	setAttribute(Qt::WA_MacShowFocusRect, false);
	setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint | Qt::FramelessWindowHint);

	scene = new QLabel("Name");
	path = new QLabel("Location");
	projectPathEdit = new QLineEdit();
	projectNameEdit = new QLineEdit();
	cancel = new QPushButton("Cancel");
	create = new QPushButton("Create");

	create->setAutoDefault(true);
	create->setDefault(true);
    projectNameEdit->setAttribute(Qt::WA_MacShowFocusRect, false);

    settingsManager = SettingsManager::getDefaultManager();
	
	projectPathEdit->setDisabled(true);
    projectPathEdit->setStyleSheet(StyleSheet::QLineEditDisabled());

    connect(create, SIGNAL(pressed()), SLOT(confirmProjectCreation()));
    connect(cancel, SIGNAL(pressed()), SLOT(close()));

    // services/apppaths.h: the data root when a run forces one, the
    // `default_directory` preference otherwise (S-extra2).
    projectPath = AppPaths::projectsRoot(
        settingsManager->getValue("default_directory", QString()).toString(),
        Constants::PROJECT_FOLDER);

	projectPathEdit->setText(projectPath);

	auto title = new QLabel("Create Scene");
	title->setAlignment(Qt::AlignCenter);
	title->setStyleSheet(StyleSheet::QLabelWhite());
	//projectNameEdit->setPlaceholderText("Scene Name");
	//projectPathEdit->setPlaceholderText("Location");

	auto grid = new QVBoxLayout;
	this->setLayout(grid);

	grid->addWidget(title);
	grid->addSpacing(4);

	grid->addWidget(scene);
	grid->addWidget(projectNameEdit);
	grid->addWidget(path);
	grid->addWidget(projectPathEdit);

	auto wid = new QWidget;
	auto layout = new QHBoxLayout;
	wid->setLayout(layout);
	layout->addStretch();
	layout->addWidget(cancel);
	layout->addWidget(create);
	layout->setContentsMargins(0, 0, 0, 0);

	grid->addSpacing(20);
	grid->addWidget(wid);


	// WIDTH (owner ask 2026-09-07, ~30% narrower): the dialog asks for a name,
	// and 410px of it was mostly empty. The location row is what used to blow
	// it out further — a QLineEdit sized to a long absolute path — so it is
	// allowed to shrink and shows the start of the path; the full value is in
	// the tooltip and is what gets used either way (projectPath, not the
	// edit's text).
	setFixedWidth(288);
	projectPathEdit->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
	projectPathEdit->setMinimumWidth(0);
	projectPathEdit->setToolTip(projectPath);
	projectPathEdit->setCursorPosition(0);
	setStyleSheet(StyleSheet::QWidgetDark());

	projectNameEdit->setStyleSheet(StyleSheet::QLineEdit());
	projectPathEdit->setStyleSheet(StyleSheet::QLineEdit());
	scene->setStyleSheet(StyleSheet::QLabelWhite());
	path->setStyleSheet(StyleSheet::QLabelWhite());
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
	// ...and no auto-default on Cancel, so Return still creates.
	cancel->setAutoDefault(false);
	cancel->setDefault(false);
	

	

}

NewProjectDialog::~NewProjectDialog()
{
}

ProjectInfo NewProjectDialog::getProjectInfo()
{
	ProjectInfo pInfo = { projectName, projectPath };
	return pInfo;
}

void NewProjectDialog::setProjectPath()
{
    QFileDialog projectDir;
	projectPath = projectDir.getExistingDirectory(nullptr, "Select project dir", lastValue);
    projectPathEdit->setText(projectPath);
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
