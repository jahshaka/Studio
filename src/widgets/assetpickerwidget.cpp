/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "assetpickerwidget.h"
#include "ui_assetpickerwidget.h"
#include "../core/thumbnailmanager.h"
#include "../constants.h"

AssetPickerWidget::AssetPickerWidget(ModelTypes type, QDialog *parent) :
    QDialog(parent),
    ui(new Ui::AssetPickerWidget)
{
    ui->setupUi(this);

    setWindowTitle("Select Asset");
    ui->viewButton->setCheckable(true);
    ui->viewButton->setToolTip("Toggle icon view");

    connect(ui->assetView,  SIGNAL(itemDoubleClicked(QListWidgetItem*)),
            this,           SLOT(assetViewDblClicked(QListWidgetItem*)));

    connect(ui->scanBtn,    SIGNAL(pressed()), this, SLOT(refreshList()));
    connect(ui->viewButton, SIGNAL(toggled(bool)), this, SLOT(changeView(bool)));

    connect(ui->searchBar,  SIGNAL(textChanged(QString)),
            this,           SLOT(searchAssets(QString)));

    populateWidget();

    ui->assetView->setViewMode(QListWidget::ListMode);
    ui->assetView->setIconSize(QSize(32, 32));
    ui->assetView->setSpacing(4);

    this->show();
}

AssetPickerWidget::~AssetPickerWidget()
{
    delete ui;
}

void AssetPickerWidget::populateWidget(QString filter)
{
    for (auto asset : AssetManager::getAssets()) {
        QPixmap pixmap;

        if (asset->type == ModelTypes::Texture) {
            QFileInfo file(asset->fileName);
            auto item = new QListWidgetItem(asset->fileName);

            if (Constants::IMAGE_EXTS.contains(file.suffix())) {
                auto thumb = ThumbnailManager::createThumbnail(asset->path, 128, 128);
                pixmap = QPixmap::fromImage(*thumb->thumb);
                item->setIcon(QIcon(pixmap));
            }

            item->setData(Qt::UserRole, asset->path);
            item->setData(MODEL_GUID_ROLE, asset->assetGuid);

            if (filter.isEmpty()) {
                ui->assetView->addItem(item);
            } else {
                if (asset->fileName.contains(filter)) {
                    ui->assetView->addItem(item);
                }
            }
        }
    }
}

void AssetPickerWidget::assetViewDblClicked(QListWidgetItem *item)
{
    emit itemDoubleClicked(item);
    this->close();
}

void AssetPickerWidget::refreshList()
{
    ui->assetView->clear();
    populateWidget();
}

void AssetPickerWidget::changeView(bool toggle)
{
    if (toggle) {
        ui->assetView->setViewMode(QListWidget::IconMode);
        ui->assetView->setIconSize(QSize(88, 88));
        ui->assetView->setResizeMode(QListWidget::Adjust);
//        ui->assetView->setMovement(QListView::Static);
//        ui->assetView->setSelectionBehavior(QAbstractItemView::SelectItems);
        ui->assetView->setSelectionMode(QAbstractItemView::SingleSelection);

        for (int i = 0; i < ui->assetView->count(); ++i) {
            auto item = ui->assetView->item(i);
            item->setSizeHint(QSize(128, 128));
        }
    } else {
        ui->assetView->setViewMode(QListWidget::ListMode);
        ui->assetView->setIconSize(QSize(32, 32));
        ui->assetView->setSpacing(4);

        for (int i = 0; i < ui->assetView->count(); ++i) {
            auto item = ui->assetView->item(i);
            item->setSizeHint(QSize(32, 32));
        }
    }
}

void AssetPickerWidget::searchAssets(QString searchString)
{
    ui->assetView->clear();
    populateWidget(searchString);
}

bool AssetPickerWidget::eventFilter(QObject *watched, QEvent *event)
{
    return QObject::eventFilter(watched, event);
}
