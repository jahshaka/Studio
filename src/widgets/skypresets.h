/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef SKYPRESETS_H
#define SKYPRESETS_H

#include <QWidget>

namespace Ui {
class SkyPresets;
}

class QListWidgetItem;
class MainWindow;

class SkyPresets : public QWidget
{
    Q_OBJECT

public:
    explicit SkyPresets(QWidget *parent = 0);
    ~SkyPresets();

    void addSky(QString path, QString name);
    void addCubeSky(QString path, QString name);

    void setMainWindow(MainWindow* mainWindow)
    {
        this->mainWindow = mainWindow;
    }

protected slots:
    void applySky(QListWidgetItem* item);
    void applyCubeSky(QListWidgetItem* item);

private:
    Ui::SkyPresets *ui;
    QList<QString> skies;
    QList<QString> alternativeSkies;
    MainWindow* mainWindow;
};

#endif // SKYPRESETS_H
