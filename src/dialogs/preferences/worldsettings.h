/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/
#ifndef WORLDSETTINGS_H
#define WORLDSETTINGS_H

#include <QWidget>

namespace Ui {
    class WorldSettings;
}

class SettingsManager;

class WorldSettings : public QWidget
{
    Q_OBJECT
    SettingsManager* settings;

public:
    explicit WorldSettings(SettingsManager* settings);
    ~WorldSettings();

    int outlineWidth;
    QColor outlineColor;

    void setupDefaultSceneOptions();
    void setupGizmoOptions();
    void setupOutline();

private slots:
    void onGizmoOptionChosen(int index);
    void onDefaultSceneChosen();

    void outlineWidthChanged(int width);
    void outlineColorChanged(QColor color);

public:
    Ui::WorldSettings *ui;
};

#endif // WORLDSETTINGS_H
