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
    projectPathEdit->setStyleSheet("background: #303030; color: #888;");

    connect(create, SIGNAL(pressed()), SLOT(confirmProjectCreation()));
    connect(cancel, SIGNAL(pressed()), SLOT(close()));

    auto pathText = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + Constants::PROJECT_FOLDER;
	projectPath = settingsManager->getValue("default_directory", pathText).toString();

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


	setMinimumWidth(410);
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
