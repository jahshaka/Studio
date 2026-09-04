/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef CACHESETTINGSWIDGET_H
#define CACHESETTINGSWIDGET_H

// The Cache page of the Preferences dialog (SHADER_CACHE_SPEC.md §4.5, owner
// directed 2026-09-04). Three things, and nothing else:
//
//   (a) the cache DATA — total size on disk, what was loaded per layer, what
//       this run compiled versus took from the cache;
//   (b) WHERE it is — the resolved path, read only, with an open-folder button;
//   (c) "Rebuild all cached data" — clears every layer behind a confirm, and
//       re-warms immediately when there is something to warm.
//
// API-FIRST, strictly: every number on this page comes from app.shaderCache()
// and the button calls app.clearShaderCache(). The page never opens the cache
// directory itself, never deletes a file itself, and knows nothing about the
// container format. If a number is not exposed by the verb it does not appear
// here — that is the rule that keeps the verb the only capability surface.

#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QAbstractButton;
class SettingsManager;

class CacheSettingsWidget : public QWidget
{
    Q_OBJECT
public:
    explicit CacheSettingsWidget(SettingsManager *settings, QWidget *parent = nullptr);

    /// Persists shader_cache_enabled. Takes effect on the next launch: the
    /// directory is resolved once, when the engine starts.
    void saveSettings();

private slots:
    /// Re-reads app.shaderCache() and repaints every field.
    void refresh();

private:
    SettingsManager *mSettings;

    QAbstractButton *mEnabled;
    QAbstractButton *mWarmUpOnOpen;
    QLineEdit *mLocation;
    QLabel *mSize;
    QLabel *mLayers;
    QLabel *mThisRun;
    QLabel *mLastSaved;
    QLabel *mFingerprint;
    QPushButton *mRebuild;
};

#endif // CACHESETTINGSWIDGET_H
