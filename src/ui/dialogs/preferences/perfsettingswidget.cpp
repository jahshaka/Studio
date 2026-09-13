/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/dialogs/preferences/perfsettingswidget.h"

#include <QDesktopServices>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QUrl>
#include <QVBoxLayout>

#include "data/settingsmanager.h"
#include "services/framemonitor.h"

PerfSettingsWidget::PerfSettingsWidget(SettingsManager *settings, QWidget *parent)
    : QWidget(parent), mSettings(settings)
{
    auto *layout = new QVBoxLayout(this);

    auto *intro = new QLabel(
        tr("Ctrl+F4 records what the renderer is doing for a few seconds and writes it to a "
           "folder, so a problem you can see can be looked at afterwards. Nothing is shown on "
           "screen while it records, and nothing is measured when it is not recording."),
        this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto *form = new QFormLayout;

    mSeconds = new QDoubleSpinBox(this);
    mSeconds->setRange(1.0, 600.0);
    mSeconds->setDecimals(0);
    mSeconds->setSuffix(tr(" s"));
    mSeconds->setValue(FrameMonitor::preferredSeconds());
    mSeconds->setToolTip(tr("How long one Ctrl+F4 records for. Press it again to stop early; "
                            "what it has by then is written out either way."));
    form->addRow(tr("Record for"), mSeconds);

    // THE ROOT. Empty = the default, which is this run's own data directory —
    // and saying "empty means default" in the placeholder is what stops a user
    // from having to know the path in order to get it back.
    mRoot = new QLineEdit(this);
    mRoot->setPlaceholderText(tr("Default — this installation's data folder"));
    mRoot->setText(mSettings ? mSettings->getValue(QStringLiteral("perf/captureRoot"), QString())
                                   .toString()
                             : QString());
    auto *browse = new QPushButton(tr("Browse…"), this);
    auto *openFolder = new QPushButton(tr("Open folder"), this);
    auto *rootRow = new QHBoxLayout;
    rootRow->addWidget(mRoot, 1);
    rootRow->addWidget(browse);
    rootRow->addWidget(openFolder);
    form->addRow(tr("Save recordings in"), rootRow);

    mResolved = new QLabel(this);
    mResolved->setTextInteractionFlags(Qt::TextSelectableByMouse);
    mResolved->setWordWrap(true);
    form->addRow(tr("Right now"), mResolved);

    // HOUSEKEEPING. A 20 s recording of a busy scene is tens of megabytes, and
    // the sweep runs when the NEXT recording starts — never on the frame path.
    mKeepDays = new QSpinBox(this);
    mKeepDays->setRange(0, 3650);
    mKeepDays->setSuffix(tr(" days"));
    mKeepDays->setSpecialValueText(tr("keep forever"));
    mKeepDays->setValue(mSettings
                            ? mSettings->getValue(QStringLiteral("perf/keepDays"),
                                                  FrameMonitor::defaultKeepDays()).toInt()
                            : int(FrameMonitor::defaultKeepDays()));
    form->addRow(tr("Delete recordings after"), mKeepDays);

    mKeepGb = new QSpinBox(this);
    mKeepGb->setRange(0, 512);
    mKeepGb->setSuffix(tr(" GB"));
    mKeepGb->setSpecialValueText(tr("no limit"));
    const qint64 keepBytes =
        mSettings ? mSettings->getValue(QStringLiteral("perf/keepBytes"),
                                        QVariant::fromValue(FrameMonitor::defaultKeepBytes()))
                        .toLongLong()
                  : FrameMonitor::defaultKeepBytes();
    mKeepGb->setValue(int(keepBytes / (1024LL * 1024LL * 1024LL)));
    mKeepGb->setToolTip(tr("The oldest recordings are removed when the folder grows past this. "
                           "The newest one is never removed."));
    form->addRow(tr("...or when they exceed"), mKeepGb);

    layout->addLayout(form);
    layout->addStretch(1);

    connect(browse, &QPushButton::clicked, this, [this]() {
        const QString start = mRoot->text().trimmed().isEmpty() ? FrameMonitor::captureRoot()
                                                                : mRoot->text().trimmed();
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Save recordings in"), start);
        if (dir.isEmpty()) return;
        mRoot->setText(dir);
        refreshResolved();
    });
    connect(openFolder, &QPushButton::clicked, this, [this]() {
        const QString dir = mRoot->text().trimmed().isEmpty() ? FrameMonitor::captureRoot()
                                                              : mRoot->text().trimmed();
        if (!dir.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });
    connect(mRoot, &QLineEdit::textChanged, this, [this]() { refreshResolved(); });

    refreshResolved();
}

void PerfSettingsWidget::refreshResolved()
{
    // What the NEXT capture would actually use, given what is typed here —
    // including the environment variable, which beats both this page and the
    // default and would otherwise be invisible.
    const QByteArray env = qgetenv("JAHSHAKA_PERF_ROOT");
    const QString typed = mRoot ? mRoot->text().trimmed() : QString();
    if (!env.isEmpty())
        mResolved->setText(tr("%1 (set by JAHSHAKA_PERF_ROOT, which overrides this page)")
                               .arg(QString::fromLocal8Bit(env)));
    else if (!typed.isEmpty())
        mResolved->setText(typed);
    else
        mResolved->setText(FrameMonitor::captureRoot());
}

void PerfSettingsWidget::saveSettings()
{
    if (!mSettings) return;
    FrameMonitor::setPreferredSeconds(mSeconds->value());
    mSettings->setValue(QStringLiteral("perf/captureRoot"), mRoot->text().trimmed());
    mSettings->setValue(QStringLiteral("perf/keepDays"), double(mKeepDays->value()));
    mSettings->setValue(QStringLiteral("perf/keepBytes"),
                        QVariant::fromValue(qint64(mKeepGb->value()) * 1024LL * 1024LL * 1024LL));
}
