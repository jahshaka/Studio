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
#include <QVariant>

#include <cstdlib>
#include <QDir>
#include <QStandardPaths>
#include <QApplication>

#include "data/settingkeys.h"
#include "data/settingsstore.h"
#include "services/apppaths.h"

class SettingsManager
{
    static SettingsManager* defaultSettings;

public:
    static SettingsManager* getDefaultManager() {
        if (defaultSettings == nullptr) {
            defaultSettings = new SettingsManager();
            // THE LAST WRITE OF A SESSION REACHES THE FILE. The manager is
            // never destroyed (anything may read a preference until the
            // process ends — ~MainWindow saves the dock layout after main's
            // finalizeAppExit), so its store's writer is joined by nobody:
            // the exit handler waits for it instead. Registered after the
            // store's first QSettings, so it runs before Qt's own settings
            // statics are torn down.
            std::atexit([] { if (defaultSettings) defaultSettings->settings->flush(); });
        }

        return defaultSettings;
    }

    /// THE store (data/settingsstore.h): reads and writes are memory, the
    /// file is written on the store's own thread. Never a QSettings: a
    /// QSettings syncs on the UI thread.
    SettingsStore* settings = nullptr;

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
        loadSettings(AppPaths::settingsFilePath(fileName));
    }

    void loadSettings(QString path) {
        settings = new SettingsStore(path);
    }

    void setValue(QString name, QVariant value) {
        settings->setValue(name, value);
    }

    QVariant getValue(QString name, QVariant def) {
        return settings->value(name,def);
    }

    /// A declared key (data/settingkeys.h): its value, or its ONE default.
    template <typename T>
    T get(const SettingKey<T> &key) const { return read(settings, key); }
    QString get(const SettingKey<const char *> &key) const { return read(settings, key); }
    template <typename T, typename V>
    void set(const SettingKey<T> &key, const V &value) { write(settings, key, value); }

    /// The same, on a store a widget was handed directly (the Claude chat
    /// window takes one so its suite can point it at a scratch file).
    template <typename T>
    static T read(const SettingsStore *s, const SettingKey<T> &key) {
        return s ? s->value(QLatin1String(key.name), QVariant::fromValue(key.fallback))
                       .template value<T>()
                 : key.fallback;
    }
    static QString read(const SettingsStore *s, const SettingKey<const char *> &key) {
        return s ? s->value(QLatin1String(key.name), QString::fromUtf8(key.fallback)).toString()
                 : QString::fromUtf8(key.fallback);
    }
    template <typename T, typename V>
    static void write(SettingsStore *s, const SettingKey<T> &key, const V &value) {
        if (s) s->setValue(QLatin1String(key.name), QVariant::fromValue(value));
    }
};

#endif // SETTINGSMANAGER_H
