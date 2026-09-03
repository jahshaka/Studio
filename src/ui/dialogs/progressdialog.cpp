/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/dialogs/progressdialog.h"
#include "ui_progressdialog.h"

#include <QApplication>
#include <QWindow>

ProgressDialog::ProgressDialog(QWidget *parent) : QDialog(parent), ui(new Ui::ProgressDialog)
{
    ui->setupUi(this);
    // Qt::Dialog keeps this a top-level window now that callers parent us
    // (bare FramelessWindowHint has no window-type bit — a parented dialog
    // would collapse into an embedded child widget). Parenting matters:
    // app teardown must be able to close and destroy every progress dialog,
    // or a "loading" window outlives the main window (owner-reported).
    this->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    // No modality: the old unparented dialog blocked nothing (WindowModal
    // with no parent is inert), and imports must keep the app interactive.

    ui->stageLabel->setVisible(false);
    // Opt-in: flows that honor the latch (the import batch) call
    // setCancelVisible(true); legacy progress rows keep their old look.
    ui->cancelButton->setVisible(false);
    connect(ui->cancelButton, &QPushButton::clicked, this, [this]() {
        if (mCanceled) return;
        mCanceled = true;
        ui->cancelButton->setEnabled(false);
        ui->cancelButton->setText(tr("Cancelling…"));
        emit canceled();
    });
}

void ProgressDialog::reject()
{
    // Esc = Cancel; the dialog stays up until the pipeline acknowledges
    // (rollback runs, the caller hides us) — never a silent close.
    ui->cancelButton->click();
}

void ProgressDialog::resetCancel()
{
    mCanceled = false;
    ui->cancelButton->setEnabled(true);
    ui->cancelButton->setText(tr("Cancel"));
}

void ProgressDialog::setCancelVisible(bool visible)
{
    ui->cancelButton->setVisible(visible);
}

void ProgressDialog::setStageText(const QString &text)
{
    ui->stageLabel->setVisible(!text.isEmpty());
    ui->stageLabel->setText(text);
}

ProgressDialog::~ProgressDialog()
{
    delete ui;
}

// NO QApplication::processEvents() in the setters. It was there for the old
// SYNCHRONOUS importers, which blocked the UI thread and needed a manual
// pump to repaint. Since the threaded import landed the event loop runs
// normally, and pumping from a setter is actively dangerous: a progress
// update emitted from inside AssetImportService::commit re-entered the loop,
// delivered the batch's finished() signal, and the handler's deleteLater
// destroyed the ImportBatchRunner *underneath the running commit* — the
// commit then emitted stageProgress on freed memory (exit-time SIGSEGV in
// QObjectPrivate::maybeSignalConnected, seen while quitting mid-import).
void ProgressDialog::setLabelText(const QString &text)
{
    ui->label->setText(text);
    if (mPumps) QApplication::processEvents();
}

void ProgressDialog::reset()
{
    ui->progressBar->reset();
}

void ProgressDialog::setRange(int min, int max)
{
    ui->progressBar->setRange(min, max);
}

void ProgressDialog::setValue(int val)
{
    ui->progressBar->setValue(val);
    if (mPumps) QApplication::processEvents();   // see setLabelText
}

void ProgressDialog::setValueAndText(int value, QString text)
{
	setValue(value);
	setLabelText(text);
}


void ProgressDialog::hideEvent(QHideEvent *event)
{
    QDialog::hideEvent(event);
    dropNativeWindow();
}

void ProgressDialog::closeEvent(QCloseEvent *event)
{
    QDialog::closeEvent(event);
    // close() on a dialog Qt ALREADY believes hidden delivers no hideEvent, so
    // the hide-path guard can be skipped entirely — the owner still saw ghosts
    // after it landed. Route both paths through the same teardown.
    dropNativeWindow();
}

void ProgressDialog::dropNativeWindow()
{
    // The page-switch native-window desync (AA_DontCreateNativeWidgetSiblings
    // family): Qt's visibility bookkeeping and the mapped X window disagree
    // after a mid-flow page switch, leaving a ghost dialog on the desktop that
    // only xdotool could dismiss. setVisible(false) trusts the bookkeeping —
    // destroy() does not: it unmaps and destroys the native window whatever
    // state Qt thinks it is in. show() recreates it cleanly next time.
    // (ENGINEERING_DEBT_SPEC addendum 2; second sighting 2026-09-03 night.)
    if (windowHandle()) windowHandle()->destroy();
}
