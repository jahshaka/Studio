/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef ASSETMODELPANEL_H
#define ASSETMODELPANEL_H

#include <QWidget>
#include <QListWidget>
#include <QListWidgetItem>

#include "assetpanel.h"
#include "core/project.h"
#include "core/database/database.h"

class MainWindow;

class AssetModelPanel : public AssetPanel
{
    Q_OBJECT

public:
    explicit AssetModelPanel(QWidget *parent = 0);
    ~AssetModelPanel();

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

    void addDefaultItem(const AssetRecord &asset);
    void addNewItem(QListWidgetItem *item);
    void removeFavorite(const QString &guid);
    void addFavorites();

    bool eventFilter(QObject *watched, QEvent *event);

public slots:
    void showContextMenu(const QPoint &pos);
    void addObjectToScene(QModelIndex itemIndex);

private:
    QVector<DefaultModel> defaultModels;
    QVector<AssetRecord> objectAssets;
};

#endif // MATERIALSETS_H
