/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/controls/assetpickerwidget.h"
#include "ui_assetpickerwidget.h"
#include "services/thumbnailmanager.h"
#include "data/constants.h"
#include "ui/style/stylesheet.h"
#include <QFileDialog>
#include <QFileInfo>

AssetPickerWidget::AssetPickerWidget(ModelTypes type, QDialog *parent) :
    QDialog(parent),
    ui(new Ui::AssetPickerWidget)
{
    ui->setupUi(this);
    // assetpickerwidget.ui used to embed these (classic-only now; theme sweep)
    setStyleSheet(StyleSheet::AssetPickerRoot());
    ui->assetView->setStyleSheet(StyleSheet::AssetPickerAssetView());

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

    // A modeless dialog nobody owns: it deletes itself when closed (it used to
    // leak, one window per pick), and every connection INTO it is made with the
    // caller as context, so a closed picker and a retired caller are both safe.
    setAttribute(Qt::WA_DeleteOnClose);
    this->show();
}

AssetPickerWidget::~AssetPickerWidget()
{
    delete ui;
}

void AssetPickerWidget::setImportFromDisk(const ImportHandler &handler)
{
    importHandler = handler;
    if (!handler) {
        delete importButton;
        importButton = nullptr;
        return;
    }
    if (importButton) return;
    importButton = new QPushButton(tr("Import from disk…"), this);
    importButton->setToolTip(tr("Bring an image in from anywhere on disk. It is imported into "
                                "the library once, by its content, and pinned into this project."));
    ui->horizontalLayout->addWidget(importButton);
    connect(importButton, &QPushButton::clicked, this, [this] {
        if (!importHandler) return;
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Choose an image"), QString(),
            tr("Images (%1)").arg(QStringLiteral("*.") + Constants::IMAGE_EXTS.join(" *.")));
        if (path.isEmpty()) return;
        const QString guid = importHandler(path);
        if (guid.isEmpty()) return;   // a refusal: the dialog stays open
        // The imported image IS the pick — the user asked for that file, not
        // for a list to find it in again.
        auto *item = new QListWidgetItem(QFileInfo(path).fileName());
        item->setData(Qt::UserRole, path);
        item->setData(MODEL_GUID_ROLE, guid);
        emit itemDoubleClicked(item);
        close();
    });
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
                pixmap = QPixmap::fromImage(thumb->thumb);
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
