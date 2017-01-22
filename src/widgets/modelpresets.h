/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef MODELPRESETS_H
#define MODELPRESETS_H

#include <QWidget>
#include <QModelIndex>

namespace Ui {
class ModelPresets;
}

class MainWindow;

class ModelPresets : public QWidget
{
    Q_OBJECT

public:
    explicit ModelPresets(QWidget *parent = 0);
    ~ModelPresets();

    void setMainWindow(MainWindow* mainWindow);

protected slots:
    void onPrimitiveSelected(QModelIndex itemIndex);

private:
    void addItem(QString name,QString path);

    MainWindow* mainWindow;
    Ui::ModelPresets *ui;
};

#endif // MODELPRESETS_H
