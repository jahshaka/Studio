/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/panels/presets/skypresets.h"
#include "ui_skypresets.h"

#include "shell/mainwindow.h"
#include "irisgl/core/logger.h"
#include "services/shippedassets.h"

SkyPresets::SkyPresets(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SkyPresets)
{
    ui->setupUi(this);

    mainWindow = nullptr;

    ui->skyList->setAttribute(Qt::WA_MacShowFocusRect, false);
    ui->skyList->setViewMode(QListWidget::IconMode);
    ui->skyList->setIconSize(QSize(64, 64));
    ui->skyList->setResizeMode(QListWidget::Adjust);
    ui->skyList->setMovement(QListView::Static);
    ui->skyList->setSelectionBehavior(QAbstractItemView::SelectItems);
    ui->skyList->setSelectionMode(QAbstractItemView::SingleSelection);

    // The shipped cube skies, from the one list world.skyPresets reads too.
    for (const ShippedAssets::SkyPreset &preset : ShippedAssets::skyPresets())
        addCubeSky(preset.thumbnail(), preset.name);

    connect(ui->skyList,    SIGNAL(itemClicked(QListWidgetItem*)),
            this,           SLOT(applyCubeSky(QListWidgetItem*)));
}

SkyPresets::~SkyPresets()
{
    delete ui;
}

void SkyPresets::addCubeSky(const QString &thumbnail, const QString &name)
{
    auto item = new QListWidgetItem(QIcon(thumbnail), name);
    item->setData(Qt::UserRole, name);
    ui->skyList->addItem(item);
}

void SkyPresets::applyCubeSky(QListWidgetItem* item)
{
    if (!mainWindow || !item) return;

    // THE SAME DOOR AS world.skyPreset (services/shippedassets.h, plan item
    // 15c): the six faces become library textures through the one import
    // pipeline — the first time any project uses them — and are pinned into
    // this project, and the sky panel's cubemap slots receive their GUIDS.
    //
    // This used to copy the six files into the project folder as
    // "<sky>_<face>.<ext>" under bare catalog rows (no stored bytes, so a
    // project export left the sky behind), after first asking the catalog for
    // rows with those NAMES — the by-name lookup the whole round trip then
    // depended on. An equirect "addSky/applySky" pair sat beside it with no
    // caller and no data; it is gone too.
    QString error;
    const QStringList guids =
        ShippedAssets::pinSkyPreset(item->data(Qt::UserRole).toString(), db, project, &error);
    if (guids.size() != 6) {
        irisLog("sky preset: " + error);
        return;
    }
    emit changeSceneCubemap(guids);
}
