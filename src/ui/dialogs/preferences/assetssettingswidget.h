/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef ASSETSSETTINGSWIDGET_H
#define ASSETSSETTINGSWIDGET_H

// The Assets page of the Preferences dialog (ASSET_PIPELINE_SPEC §3.1.1,
// phase 1): current store root + free-space readout, Move Store… (copy →
// verify → flip setting; old tree retained), Use Existing Store… (catalog
// sanity-checked), Reset to Default, Clean up Storage… (the assets.gc dry
// run, then the sweep on confirmation), Bake All Models (MESH_BAKE_SPEC
// phase 1 — the explicit form of the lazy bake every open already does) and
// the online/offline status with Reconnect. All actions run through AssetStoreService / AssetGc — the same
// implementations behind the assets.setStoreRoot/storeStatus/gc verbs.

#include <QWidget>

class QLabel;
class QPushButton;
class Database;
class SettingsManager;

class AssetsSettingsWidget : public QWidget
{
    Q_OBJECT
public:
    explicit AssetsSettingsWidget(SettingsManager *settings, Database *db,
                                  QWidget *parent = nullptr);

    /// Nothing deferred: root changes apply immediately (they move files);
    /// kept for the dialog's uniform page interface.
    void saveSettings() {}

private slots:
    void refresh();
    void moveStore();
    void useExistingStore();
    void resetToDefault();
    void cleanUpStorage();
    void bakeAllModels();

private:
    void applyRoot(const QString &path, bool move);

    SettingsManager *mSettings;
    Database *mDb;

    QLabel *mRoot;
    QLabel *mFreeSpace;
    QLabel *mStatus;
};

#endif // ASSETSSETTINGSWIDGET_H
