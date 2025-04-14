/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef ASSETMATERIALPANEL_H
#define ASSETMATERIALPANEL_H

#include <QWidget>
#include <QListWidget>
#include <QListWidgetItem>

#include "assetpanel.h"
#include "core/project.h"
#include "core/database/database.h"
#include "core/materialpreset.h"

class MainWindow;
class MaterialPreset;

class AssetMaterialPanel : public AssetPanel
{
    Q_OBJECT

public:
    explicit AssetMaterialPanel(QWidget *parent = 0);
    ~AssetMaterialPanel();

    void setMainWindow(MainWindow* mainWindow) {
        this->mainWindow = mainWindow;
    }

    void setDatabaseHandle(Database *db) {
        this->handle = db;
        // objectAssets = handle->fetchAssetsByView(...);
        // objectAssets = handle->fetchFavorites();
    }

    // Add the default starter primitives
    void addDefaultItems();
    void removeFavorite(const QString &guid);
    void addNewItem(QListWidgetItem *item);
    void addFavorites();

    bool eventFilter(QObject *watched, QEvent *event);
    QVector<MaterialPreset> getDefaultMaterials() {
        return defaultMaterials;
    }
public slots:
    void showContextMenu(const QPoint &pos);
    void applyMaterialPreset(QListWidgetItem *item);

private:
    QVector<MaterialPreset> defaultMaterials;
    QVector<AssetRecord> objectAssets;
};

#endif // MATERIALSETS_H
