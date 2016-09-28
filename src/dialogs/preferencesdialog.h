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

namespace Ui {
class PreferencesDialog;
}

class QListWidgetItem;
class SettingsManager;

class PreferencesDialog : public QDialog
{
    Q_OBJECT
    SettingsManager* settings;

public:
    explicit PreferencesDialog(SettingsManager* settings);
    ~PreferencesDialog();

private:
    void setupList();
    void setupPages();

private slots:
    void pageChanged(QListWidgetItem* prev,QListWidgetItem* cur);
    void closeDialog();

private:
    Ui::PreferencesDialog *ui;
};

#endif // PREFERENCESDIALOG_H
