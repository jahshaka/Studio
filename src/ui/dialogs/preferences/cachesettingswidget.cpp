/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/dialogs/preferences/cachesettingswidget.h"

#include <QCheckBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>
#include <oclero/qlementine/widgets/Switch.hpp>

#include "bridge/enginehost.h"
#include "data/settingsmanager.h"
#include "ui/style/thememanager.h"

using namespace jahshaka::engine;

namespace {

/// The page reads exactly what `app.shaderCache()` reads and clears exactly
/// what `app.clearShaderCache()` clears — one implementation, two front doors
/// (SCRIPTING_SPEC §2.3). Nothing here touches the cache directory itself.
ShaderCacheStats currentStats()
{
    if (auto engine = EngineHost::instance().engine()) return engine->shaderCacheStats();
    ShaderCacheStats s;
    s.dir = EngineHost::shaderCacheDirectory().toStdString();
    return s;
}

QString humanBytes(unsigned long long n)
{
    return QLocale().formattedDataSize(qint64(n), 2, QLocale::DataSizeTraditionalFormat);
}

}  // namespace

CacheSettingsWidget::CacheSettingsWidget(SettingsManager *settings, QWidget *parent)
    : QWidget(parent), mSettings(settings)
{
    auto *layout = new QVBoxLayout(this);

    auto *intro = new QLabel(
        tr("Jahshaka compiles a shader for every combination of material, light and effect a "
           "scene uses. The shader cache keeps those results between launches, so only the "
           "first launch after an update pays for them.\n\n"
           "Everything here is derived data: deleting it costs one slow launch and nothing else."),
        this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    if (ThemeManager::classicActive())
        mEnabled = new QCheckBox(tr("Keep compiled shaders between launches"), this);
    else {
        auto *sw = new oclero::qlementine::Switch(this);
        sw->setText(tr("Keep compiled shaders between launches"));
        mEnabled = sw;
    }
    mEnabled->setChecked(mSettings->getValue("shader_cache_enabled", true).toBool());
    layout->addWidget(mEnabled);

    auto *form = new QFormLayout;

    // (b) WHERE it is. Read-only on purpose: the location is derived from the
    // platform's application-data directory, and a second way to set it would
    // be a second way to lose track of it.
    mLocation = new QLineEdit(this);
    mLocation->setReadOnly(true);
    mLocation->setCursorPosition(0);
    auto *openFolder = new QPushButton(tr("Open folder"), this);
    auto *locationRow = new QHBoxLayout;
    locationRow->addWidget(mLocation, 1);
    locationRow->addWidget(openFolder);
    form->addRow(tr("Location"), locationRow);

    // (a) the cache DATA.
    mSize        = new QLabel(this);
    mLayers      = new QLabel(this);
    mThisRun     = new QLabel(this);
    mLastSaved   = new QLabel(this);
    mFingerprint = new QLabel(this);
    mFingerprint->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(tr("On disk"),       mSize);
    form->addRow(tr("Loaded"),        mLayers);
    form->addRow(tr("This session"),  mThisRun);
    form->addRow(tr("Last written"),  mLastSaved);
    form->addRow(tr("Fingerprint"),   mFingerprint);
    layout->addLayout(form);

    // (c) the destructive button, behind a confirm.
    mRebuild = new QPushButton(tr("Rebuild all cached data"), this);
    mRebuild->setToolTip(tr("Deletes every cached shader and writes back what this session "
                            "still has in memory. Anything that cannot be written back is "
                            "rebuilt on the next launch, which then takes a few seconds longer."));
    auto *buttonRow = new QHBoxLayout;
    buttonRow->addStretch(1);
    buttonRow->addWidget(mRebuild);
    layout->addLayout(buttonRow);
    layout->addStretch(1);

    connect(openFolder, &QPushButton::clicked, this, [this]() {
        const QString dir = QString::fromStdString(currentStats().dir);
        if (!dir.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });

    connect(mRebuild, &QPushButton::clicked, this, [this]() {
        const auto answer = QMessageBox::question(
            this, tr("Rebuild cached data"),
            tr("Delete every cached shader?\n\n"
               "Nothing in your projects is affected. The next launch has to compile the "
               "shaders again, so it will take a few seconds longer than usual."),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) return;

        bool ok = EngineHost::clearShaderCacheOnDisk();
        if (auto engine = EngineHost::instance().engine()) {
            ok = engine->clearShaderCache() && ok;
            // Re-warm immediately: whatever this session already has compiled
            // goes straight back to disk, so a user who rebuilds with a scene
            // open is not punished with a cold next launch for no reason.
            engine->saveShaderCache();
        }
        if (!ok)
            QMessageBox::warning(this, tr("Rebuild cached data"),
                                 tr("The cache directory could not be fully removed. "
                                    "It will be rebuilt on the next launch anyway."));
        refresh();
    });

    refresh();
}

void CacheSettingsWidget::refresh()
{
    const ShaderCacheStats s = currentStats();

    mLocation->setText(QString::fromStdString(s.dir));
    mLocation->setCursorPosition(0);

    if (!s.enabled) {
        mSize->setText(tr("disabled — shaders are recompiled every launch"));
        mLayers->setText(QStringLiteral("-"));
        mLastSaved->setText(QStringLiteral("-"));
        mFingerprint->setText(QStringLiteral("-"));
    } else {
        mSize->setText(s.files == 0
            ? tr("empty — nothing has been written yet")
            : tr("%1 in %n file(s)", "", int(s.files)).arg(humanBytes(s.sizeBytes)));
        // Per LAYER, because they fail independently and a user reporting "the
        // cache does nothing" needs to be able to say which one.
        mLayers->setText(tr("pipelines: %1   ·   shader binaries: %2   ·   shader sources: %3")
            .arg(s.pipelineCacheLoaded ? tr("yes") : tr("no"),
                 s.microcodeLoaded ? tr("%1 entries").arg(s.microcodeEntries) : tr("no"),
                 s.hlmsCachesLoaded ? QString::number(s.hlmsCachesLoaded) : tr("no")));
        mLastSaved->setText(s.lastSavedUnixMs
            ? QDateTime::fromMSecsSinceEpoch(qint64(s.lastSavedUnixMs)).toString(Qt::TextDate)
            : tr("never"));
        mFingerprint->setText(QString::fromStdString(s.fingerprint));
    }

    // Always meaningful: the counters run whether or not anything is persisted,
    // and "compiled 0, reused 66" IS the proof the cache did its job.
    mThisRun->setText(tr("%1 compiled, %2 reused from the cache")
                          .arg(s.compiledThisRun).arg(s.loadedThisRun));
    mRebuild->setEnabled(s.enabled);
}

void CacheSettingsWidget::saveSettings()
{
    // Next-launch only, and deliberately not hidden behind a restart prompt: the
    // directory is resolved once, when the engine starts, and re-resolving it
    // under a live Hlms would mean tearing the engine down.
    mSettings->setValue("shader_cache_enabled", mEnabled->isChecked());
}
