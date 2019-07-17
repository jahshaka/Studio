/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>

#include "irisgl/src/core/irisutils.h"
#include "../core/settingsmanager.h"
#include "..//misc/stylesheet.h"
#include "newprojectdialog.h"
#include "ui_newprojectdialog.h"

#include <QStandardPaths>
#include <QMessageBox>

#include "constants.h"

NewProjectDialog::NewProjectDialog(QDialog *parent) : QDialog(parent)
{

	setAttribute(Qt::WA_MacShowFocusRect, false);
	setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint | Qt::FramelessWindowHint);

	scene = new QLabel("Scene Name");
	path = new QLabel("Location");
	projectPathEdit = new QLineEdit();
	projectNameEdit = new QLineEdit();
	cancel = new QPushButton("Cencel");
	create = new QPushButton("Create World");

	create->setAutoDefault(true);
	create->setDefault(true);
    projectNameEdit->setAttribute(Qt::WA_MacShowFocusRect, false);

    settingsManager = SettingsManager::getDefaultManager();
	
	projectPathEdit->setDisabled(true);
    projectPathEdit->setStyleSheet("background: #303030; color: #888;");

    connect(create, SIGNAL(pressed()), SLOT(confirmProjectCreation()));
    connect(cancel, SIGNAL(pressed()), SLOT(close()));

    auto pathe = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + Constants::PROJECT_FOLDER;
    auto projectPathText = settingsManager->getValue("default_directory", pathe).toString();

	projectPathEdit->setText(projectPathText);

	auto grid = new QGridLayout;
	this->setLayout(grid);

	grid->addWidget(scene, 0, 0);
	grid->addWidget(projectNameEdit, 0, 1);
	grid->addWidget(path, 1, 0);
	grid->addWidget(projectPathEdit, 1, 1);

	auto wid = new QWidget;
	auto layout = new QHBoxLayout;
	wid->setLayout(layout);
	layout->addWidget(cancel);
	layout->addWidget(create);
	layout->setContentsMargins(0, 0, 0, 0);


	grid->addWidget(wid, 2, 0, 1, 2);


	setMinimumWidth(460);
	setStyleSheet(StyleSheet::QWidgetDark());

	projectNameEdit->setStyleSheet(StyleSheet::QLineEdit());
	projectPathEdit->setStyleSheet(StyleSheet::QLineEdit());
	scene->setStyleSheet(StyleSheet::QLabelWhite());
	path->setStyleSheet(StyleSheet::QLabelWhite());
	cancel->setStyleSheet(StyleSheet::QPushButtonGreyscaleBig());
	create->setStyleSheet(StyleSheet::QPushButtonBlueBig());
	

	

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
