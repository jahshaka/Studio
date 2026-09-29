/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/dialogs/bundleexportdialog.h"

#include <QEventLoop>
#include <QFutureWatcher>
#include <QTimer>

#include "ui/dialogs/progressdialog.h"

namespace bundleexportdialog {

namespace {
/// How long a write may run before the dialog is worth showing: a small
/// export finishes inside it and never flashes a window.
constexpr int kShowAfterMs = 300;
/// How often the dialog reads the worker's counters.
constexpr int kPollMs = 50;
}   // namespace

assetshare::ExportResult run(QWidget *parent, assetshare::BundleStage stage,
                             const QString &destPath, const QString &title)
{
    if (!stage.ok()) {
        assetshare::ExportResult result;
        result.error = stage.error;
        return result;
    }

    assetshare::BundleExport job(std::move(stage), destPath);
    job.start();

    // The dialog is created now and shown only if the write is still running
    // after kShowAfterMs. It is MODAL while it is up: the one thing a person
    // can do during an export is cancel it.
    ProgressDialog dialog(parent);
    dialog.setWindowModality(Qt::ApplicationModal);
    dialog.setLabelText(title);
    dialog.setStageText(QObject::tr("Writing the share file…"));
    dialog.setRange(0, job.total());
    dialog.setCancelVisible(true);
    QObject::connect(&dialog, &ProgressDialog::canceled, &dialog, [&job]() { job.cancel(); });

    QEventLoop loop;
    QFutureWatcher<assetshare::ExportResult> watcher;
    QObject::connect(&watcher, &QFutureWatcherBase::finished, &loop, &QEventLoop::quit);
    watcher.setFuture(job.future());

    QTimer show;
    show.setSingleShot(true);
    QObject::connect(&show, &QTimer::timeout, &dialog, [&dialog]() { dialog.show(); });
    show.start(kShowAfterMs);

    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &dialog, [&dialog, &job]() {
        dialog.setRange(0, job.total());
        dialog.setValue(job.done());
        dialog.setStageText(QObject::tr("Writing the share file (%1/%2)…")
                                .arg(job.done()).arg(job.total()));
    });
    poll.start(kPollMs);

    if (!job.isFinished()) loop.exec();
    show.stop();
    poll.stop();
    dialog.hide();
    // Finished by now (the loop ends on the worker's finish); an application
    // quitting ends the loop early, and the write — bounded file I/O — is
    // joined rather than abandoned mid-rename.
    return job.wait();
}

}   // namespace bundleexportdialog
