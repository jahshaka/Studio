/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef MATERIALSETS_H
#define MATERIALSETS_H

#include <QWidget>
#include <QListWidgetItem>

namespace Ui {
class MaterialSets;
}

class MaterialPreset;
class MainWindow;

class MaterialSets : public QWidget
{
    Q_OBJECT

public:
    explicit MaterialSets(QWidget *parent = 0);
    ~MaterialSets();

    void setMainWindow(MainWindow* mainWindow)
    {
        this->mainWindow = mainWindow;
    }

private slots:
    void applyMaterialPreset(QListWidgetItem* item);

private:
    void loadPresets();

    void addPreset(MaterialPreset* preset);

private:
    Ui::MaterialSets *ui;
    QList<MaterialPreset*> presets;
    MainWindow* mainWindow;
};

#endif // MATERIALSETS_H
