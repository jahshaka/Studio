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
#include "newprojectdialog.h"
#include "ui_newprojectdialog.h"

#include <QStandardPaths>
#include <QMessageBox>

#include "constants.h"

NewProjectDialog::NewProjectDialog(QDialog *parent) : QDialog(parent), ui(new Ui::NewProjectDialog)
{
    ui->setupUi(this);

	setAttribute(Qt::WA_MacShowFocusRect, false);
	setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint | Qt::FramelessWindowHint);

    ui->createProject->setAutoDefault(true);
    ui->createProject->setDefault(true);
    ui->projectName->setAttribute(Qt::WA_MacShowFocusRect, false);

    settingsManager = SettingsManager::getDefaultManager();
	
	ui->projectPath->setDisabled(true);
    ui->projectPath->setStyleSheet("background: #303030; color: #888;");

    connect(ui->createProject, SIGNAL(pressed()), SLOT(confirmProjectCreation()));
    connect(ui->cancel, SIGNAL(pressed()), SLOT(close()));

    auto path = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + Constants::PROJECT_FOLDER;
    projectPath = settingsManager->getValue("default_directory", path).toString();

    ui->projectPath->setText(projectPath);
}

NewProjectDialog::~NewProjectDialog()
{
    delete ui;
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
    ui->projectPath->setText(projectPath);
}

void NewProjectDialog::createNewProject()
{
    projectName = ui->projectName->text();
}

void NewProjectDialog::confirmProjectCreation()
{
    createNewProject();
    this->close();
    emit accepted();
}
