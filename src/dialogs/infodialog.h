/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef INFODIALOG_H
#define INFODIALOG_H

#include <QDialog>


namespace Ui {
class InfoDialog;
}


class QListWidgetItem;
class MainWindow;

class InfoDialog : public QDialog
{
    Q_OBJECT

    MainWindow* window;

public:
    explicit InfoDialog(QWidget *parent = 0);
    ~InfoDialog();

    void loadRecentScenes();
    void addSample(QString title,QString imagePath,QString filePath);
    void setMainWindow(MainWindow* window)
    {
        this->window = window;
    }

private slots:
    void onClose();
    void onShowDialogCheckbox(int state);

    void visitWebsite();
    void visitTutorials();

    void openRecentProject();
    void openSample(QListWidgetItem*);
    void openProject(QListWidgetItem*);

private:
    Ui::InfoDialog *ui;
};

#endif // INFODIALOG_H
