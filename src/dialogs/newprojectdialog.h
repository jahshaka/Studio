/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef NEWPROJECTDIALOG_H
#define NEWPROJECTDIALOG_H

#include <QDialog>

namespace Ui {
    class NewProjectDialog;
}

struct ProjectInfo {
    QString projectName;
    QString projectPath;
};

class SettingsManager;

class NewProjectDialog : public QDialog
{
    Q_OBJECT

public:
    explicit NewProjectDialog(QDialog *parent = 0);
    ~NewProjectDialog();

    ProjectInfo getProjectInfo();

protected slots:
    void setProjectPath();
    void createNewProject();
    void confirmProjectCreation();

private:
    Ui::NewProjectDialog *ui;
    QString projectName,
            projectPath;
    SettingsManager *settingsManager;
    QString lastValue;
};

#endif // NEWPROJECTDIALOG_H
