/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/dialogs/preferences/assetssettingswidget.h"

#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QStorageInfo>
#include <QVBoxLayout>

#include <QSqlDatabase>

#include "data/settingsmanager.h"
#include "services/assetgc.h"
#include "services/assetstore.h"
#include "services/assetstorepaths.h"
#include "ui/style/stylesheet.h"

AssetsSettingsWidget::AssetsSettingsWidget(SettingsManager *settings, Database *db,
                                           QWidget *parent)
    : QWidget(parent), mSettings(settings), mDb(db)
{
    auto *layout = new QVBoxLayout(this);

    auto *intro = new QLabel(
        "The asset store holds the library's files (models, textures, media). "
        "It can live on any drive — moving it copies the files and keeps the "
        "old copy until you delete it yourself.", this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto *form = new QFormLayout;
    mRoot = new QLabel(this);
    mRoot->setTextInteractionFlags(Qt::TextSelectableByMouse);
    mRoot->setWordWrap(true);
    form->addRow("Store location", mRoot);

    mFreeSpace = new QLabel(this);
    form->addRow("Free space", mFreeSpace);

    mStatus = new QLabel(this);
    mStatus->setWordWrap(true);
    form->addRow("Status", mStatus);
    layout->addLayout(form);

    auto *buttons = new QHBoxLayout;
    auto *moveButton = new QPushButton("Move Store…", this);
    auto *useButton = new QPushButton("Use Existing Store…", this);
    auto *resetButton = new QPushButton("Reset to Default", this);
    auto *reconnectButton = new QPushButton("Reconnect", this);
    auto *cleanButton = new QPushButton("Clean up Storage…", this);
    buttons->addWidget(moveButton);
    buttons->addWidget(useButton);
    buttons->addWidget(resetButton);
    buttons->addWidget(reconnectButton);
    buttons->addWidget(cleanButton);
    buttons->addStretch(1);
    layout->addLayout(buttons);

    auto *note = new QLabel(
        "When the store's drive is unplugged the library still opens — "
        "thumbnails, search and drawers keep working; only actions that need "
        "the files themselves wait for the store to come back.", this);
    note->setWordWrap(true);
    note->setStyleSheet(StyleSheet::MutedInfoText());
    layout->addWidget(note);
    layout->addStretch(1);

    StyleSheet::setStyle({ intro, mRoot, mFreeSpace, mStatus });

    connect(moveButton, &QPushButton::clicked, this, &AssetsSettingsWidget::moveStore);
    connect(useButton, &QPushButton::clicked, this, &AssetsSettingsWidget::useExistingStore);
    connect(resetButton, &QPushButton::clicked, this, &AssetsSettingsWidget::resetToDefault);
    connect(reconnectButton, &QPushButton::clicked, this, &AssetsSettingsWidget::refresh);
    connect(cleanButton, &QPushButton::clicked, this, &AssetsSettingsWidget::cleanUpStorage);

    refresh();
}

void AssetsSettingsWidget::refresh()
{
    const QString root = AssetStorePaths::root();
    mRoot->setText(QDir::toNativeSeparators(root));

    const QVariantMap st = AssetStoreService::status(mDb);
    if (st["online"].toBool()) {
        QStorageInfo storage(root);
        mFreeSpace->setText(storage.isValid()
            ? QLocale().formattedDataSize(storage.bytesAvailable())
            : QStringLiteral("—"));
        const int missing = st["missing"].toInt();
        mStatus->setText(missing > 0
            ? QStringLiteral("online — %1 library asset(s) have no folder in this store").arg(missing)
            : QStringLiteral("online"));
    }
    else {
        mFreeSpace->setText(QStringLiteral("—"));
        mStatus->setText(QStringLiteral("OFFLINE — %1 is unreachable. Reconnect the drive and press Reconnect.")
                             .arg(QDir::toNativeSeparators(root)));
    }
}

void AssetsSettingsWidget::applyRoot(const QString &path, bool move)
{
    QString error;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    bool ok = AssetStoreService::setRoot(path, move, /*force*/ false,
                                         mSettings, mDb, &error);
    QApplication::restoreOverrideCursor();

    if (!ok && !move) {
        // Use Existing sanity check failed — offer the force override.
        const auto answer = QMessageBox::question(this, "Use Existing Store",
            error + "\n\nUse this folder anyway?",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer == QMessageBox::Yes) {
            QApplication::setOverrideCursor(Qt::WaitCursor);
            ok = AssetStoreService::setRoot(path, false, /*force*/ true,
                                            mSettings, mDb, &error);
            QApplication::restoreOverrideCursor();
        } else {
            refresh();
            return;
        }
    }

    if (!ok) QMessageBox::warning(this, "Asset Store", error);
    refresh();
}

void AssetsSettingsWidget::moveStore()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, "Move asset store to…", QDir::homePath());
    if (dir.isEmpty()) return;
    applyRoot(dir, /*move*/ true);
}

void AssetsSettingsWidget::useExistingStore()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, "Use existing asset store…", QDir::homePath());
    if (dir.isEmpty()) return;
    applyRoot(dir, /*move*/ false);
}

void AssetsSettingsWidget::resetToDefault()
{
    applyRoot(QString(), /*move*/ false);
}

void AssetsSettingsWidget::cleanUpStorage()
{
    // DRY RUN FIRST, ALWAYS — the same contract the assets.gc verb has. The
    // summary below is what a real run would delete; nothing is unlinked
    // until the owner says yes to these exact numbers.
    const QString root = AssetStorePaths::root();

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const auto preview = AssetGc::sweep(QSqlDatabase::database(), root, /*dryRun*/ true);
    QApplication::restoreOverrideCursor();

    if (!preview.ok) {
        QMessageBox::warning(this, "Clean up Storage", preview.error);
        return;
    }
    if (preview.totalCount() == 0) {
        QMessageBox::information(this, "Clean up Storage",
            QStringLiteral("Nothing to clean up — %1 holds no orphaned files.")
                .arg(QDir::toNativeSeparators(root)));
        return;
    }

    const auto line = [](const char *label, const AssetGc::ClassReport &cls) {
        return cls.items.isEmpty()
            ? QString()
            : QStringLiteral("\n  %1: %2 (%3)").arg(QLatin1String(label))
                  .arg(cls.items.size()).arg(QLocale().formattedDataSize(cls.bytes));
    };

    const QString detail =
        QStringLiteral("%1 orphaned item(s), %2 in total:")
            .arg(preview.totalCount()).arg(QLocale().formattedDataSize(preview.totalBytes()))
        + line("unreferenced objects", preview.unreferencedObjects)
        + line("files the catalog never recorded", preview.strayObjects)
        + line("sidecars with no asset", preview.straySidecars)
        + line("old per-asset folders", preview.legacyFolders)
        + line("duplicate copies of stored files", preview.redundantLegacyFiles)
        + QStringLiteral("\n\nDelete them? Assets in the library, project pins and "
                        "copy-on-write edits are never touched.");

    if (QMessageBox::question(this, "Clean up Storage", detail,
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
        != QMessageBox::Yes) {
        return;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const auto done = AssetGc::sweep(QSqlDatabase::database(), root, /*dryRun*/ false);
    QApplication::restoreOverrideCursor();

    if (!done.failures.isEmpty()) {
        QMessageBox::warning(this, "Clean up Storage",
            QStringLiteral("Removed %1 of %2 item(s); %3 could not be removed:\n%4")
                .arg(done.removedCount()).arg(done.totalCount())
                .arg(done.failures.size()).arg(done.failures.join(QLatin1Char('\n'))));
    } else {
        QMessageBox::information(this, "Clean up Storage",
            QStringLiteral("Removed %1 item(s), freeing %2.")
                .arg(done.removedCount()).arg(QLocale().formattedDataSize(done.removedBytes())));
    }
    refresh();
}
