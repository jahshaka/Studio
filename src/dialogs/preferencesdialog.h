/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef PREFERENCESDIALOG_H
#define PREFERENCESDIALOG_H

#include <QDialog>
#include <QCloseEvent>
#include <QDebug>

namespace Ui {
    class PreferencesDialog;
}

class QListWidgetItem;
class SettingsManager;
class WorldSettings;
class Database;

class PreferencesDialog : public QDialog
{
    Q_OBJECT
    SettingsManager* settings;

signals:
    void PreferencesDialogClosed();

protected:
    void closeEvent(QCloseEvent *event) {
        emit PreferencesDialogClosed();
        event->accept();
    }

public:
    explicit PreferencesDialog(Database *db, SettingsManager* settings);
    ~PreferencesDialog();

    WorldSettings* worldSettings;
	Database *db;

private:
    void setupPages();

private slots:
    void saveSettings();

private:
    Ui::PreferencesDialog *ui;
};

#endif // PREFERENCESDIALOG_H
