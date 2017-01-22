/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef SETTINGSMANAGER_H
#define SETTINGSMANAGER_H

#include <QSettings>
#include <QVariant>

class SettingsManager
{
    static SettingsManager* defaultSettings;

public:
    static SettingsManager* getDefaultManager() {
        if (defaultSettings == nullptr) {
            defaultSettings = new SettingsManager();
        }

        return defaultSettings;
    }

    QSettings* settings;

    int recentlyOpenedFilesSize;

    SettingsManager(QString fileName = "settings.ini") {
        recentlyOpenedFilesSize = 5;
        auto path = fileName;
        loadSettings(path);
    }

    void loadSettings(QString path) {
        settings = new QSettings(path,QSettings::IniFormat);
    }

    void setValue(QString name, QVariant value) {
        settings->setValue(name, value);
    }

    QVariant getValue(QString name, QVariant def) {
        return settings->value(name,def);
    }

    QStringList getRecentlyOpenedScenes() {
        return settings->value("recent_files",QStringList()).toStringList();
    }

    void addRecentlyOpenedScene(QString path) {
        auto list = settings->value("recent_files", QStringList()).toStringList();

        // if it already exists, remove it from the list
        // it will be added back to the top
        if (list.contains(path)) {
            list.removeAt(list.indexOf(path));
        }

        // prevents list from adding too much
        while (list.size() > recentlyOpenedFilesSize - 1) {
            list.removeLast();
        }

        list.push_front(path);

        settings->setValue("recent_files", list);
    }
};

#endif // SETTINGSMANAGER_H
