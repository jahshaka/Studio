/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef ASSETMATERIALPANEL_H
#define ASSETMATERIALPANEL_H

#include <QWidget>
#include <QListWidget>
#include <QListWidgetItem>

#include "ui/panels/presets/assetpanel.h"
#include "data/project.h"
#include "data/database/database.h"
#include "data/materialpreset.h"

struct StudioServices;

class AssetMaterialPanel : public AssetPanel
{
    Q_OBJECT

public:
    explicit AssetMaterialPanel(QWidget *parent = 0);
    ~AssetMaterialPanel();

    /// The service layer (§2.3's opportunistic rule). The tray's double-click
    /// is a LIBRARY drop onto the selection (SceneEditService::dropMaterial —
    /// the viewport's drop and `material.drop` make the same call; MATERIAL-DROP-1).
    void setServices(StudioServices *services) { this->services = services; }

    /// The favourites are listed HERE, not in the constructor: the panel is
    /// built before MainWindow has a database to give it, and reading the
    /// library through a pointer that does not exist yet is what the
    /// constructor's addFavorites() call was doing (CLOSE-2). Called once,
    /// like the wiring that calls it; addDefaultItems() stays in the
    /// constructor so the starter tiles still come first.
    void setDatabaseHandle(Database *db) {
        this->handle = db;
        addFavorites();
    }

    // Add the default starter primitives
    void addDefaultItems();
    void removeFavorite(const QString &guid);
    void addNewItem(QListWidgetItem *item);
    void addFavorites();

    /// THE DOUBLE-CLICK, by guid (editor.activateMaterialTile): what a person's
    /// double-click on that tile does. False when no tile carries `guid`.
    bool activateTile(const QString &guid);

    bool eventFilter(QObject *watched, QEvent *event);
public slots:
    void showContextMenu(const QPoint &pos);
    void applyMaterialPreset(QListWidgetItem *item);

private:
    StudioServices *services = nullptr;
    QVector<AssetRecord> objectAssets;
};

#endif // MATERIALSETS_H
