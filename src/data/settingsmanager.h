/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SETTINGSMANAGER_H
#define SETTINGSMANAGER_H

#include <QCoreApplication>
#include <QSettings>
#include <QVariant>
#include <QDir>
#include <QStandardPaths>
#include <QApplication>

#include "services/apppaths.h"

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

    // WHERE THE SETTINGS FILE IS, in one place: AppPaths::settingsFilePath.
    //
    // The four #ifdef branches this replaced were two distinct answers written
    // twice each (BUILD_AS_LIB chose between QApplication:: and
    // QCoreApplication::applicationDirPath — the same function), and NONE of
    // them could be overridden. Under QT_DEBUG the file lives beside the
    // BINARY, so every run of a build tree — the owner's, and every suite that
    // spawns the app — writes the SAME jahsettings.ini; a scratch HOME moves
    // the library and the asset store but cannot move a path derived from the
    // executable's location. That is how a gate run came to re-roll the
    // owner's `[assets] storeId` (ENGINEERING_DEBT_SPEC ADDENDUM 6).
    //
    // With no override the location is bit-for-bit what it was. With
    // `--data-root` / `JAHSHAKA_DATA_ROOT` the settings file moves WITH the
    // library and the store, which is the whole point: one flag, one hermetic
    // run (services/apppaths.h).
    SettingsManager(QString fileName = "jahsettings.ini") {
        recentlyOpenedFilesSize = 9;
        loadSettings(AppPaths::settingsFilePath(fileName));
    }

    void loadSettings(QString path) {
        settings = new QSettings(path, QSettings::IniFormat);
    }

    void setValue(QString name, QVariant value) {
        settings->setValue(name, value);
    }

    QVariant getValue(QString name, QVariant def) {
        return settings->value(name,def);
    }

    QStringList getRecentlyOpenedScenes() {
        return settings->value("recent_files", QStringList()).toStringList();
    }

    void removeRecentlyOpenedEntry(const QString &entry) {
        auto list = settings->value("recent_files", QStringList()).toStringList();

        if (list.contains(entry)) {
            list.removeAt(list.indexOf(entry));
        }

        if (list.count()) {
            settings->setValue("recent_files", list);
        } else {
            settings->remove("recent_files");
        }
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
