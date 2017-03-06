/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "materialsets.h"
#include "ui_materialsets.h"

#include <QDir>
#include <QFileInfo>
#include <QFileInfoList>
#include <QDebug>

#include "../io/materialpresetreader.h"
#include "../core/materialpreset.h"
#include "../irisgl/src/core/irisutils.h"

#include "../mainwindow.h"

MaterialSets::MaterialSets(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::MaterialSets)
{
    ui->setupUi(this);

    mainWindow = nullptr;

    ui->materialPresets->setViewMode(QListWidget::IconMode);
    ui->materialPresets->setIconSize(QSize(64, 64));
    ui->materialPresets->setResizeMode(QListWidget::Adjust);
    ui->materialPresets->setMovement(QListView::Static);
    ui->materialPresets->setSelectionBehavior(QAbstractItemView::SelectItems);
    ui->materialPresets->setSelectionMode(QAbstractItemView::SingleSelection);
//    ui->materialPresets->setSpacing(4);

    connect(ui->materialPresets,    SIGNAL(itemClicked(QListWidgetItem*)),
            this,                   SLOT(applyMaterialPreset(QListWidgetItem*)));

    loadPresets();
}

MaterialSets::~MaterialSets()
{
    delete ui;
    for (auto preset : presets) delete preset;
}

void MaterialSets::loadPresets()
{
    auto dir = QDir(IrisUtils::getAbsoluteAssetPath("app/content/materials"));
    auto files = dir.entryInfoList(QStringList(), QDir::Files);

    auto reader = new MaterialPresetReader();

    for (auto file : files) {
        auto preset = reader->readMaterialPreset(file.absoluteFilePath());
        addPreset(preset);
    }
}

void MaterialSets::addPreset(MaterialPreset* preset)
{
    presets.append(preset);

    auto item = new QListWidgetItem(QIcon(preset->icon), preset->name);
    item->setData(Qt::UserRole, presets.count() - 1);
    ui->materialPresets->addItem(item);
}

void MaterialSets::applyMaterialPreset(QListWidgetItem* item)
{
    if (!mainWindow) return;

    auto preset = presets[item->data(Qt::UserRole).toInt()];
    mainWindow->applyMaterialPreset(preset);
}
