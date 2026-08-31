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

ProgressDialog::ProgressDialog(QDialog *parent) : QDialog(parent), ui(new Ui::ProgressDialog)
{
    ui->setupUi(this);
    this->setWindowFlags(Qt::FramelessWindowHint);
	setWindowModality(Qt::WindowModal);

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

void ProgressDialog::setLabelText(const QString &text)
{
    ui->label->setText(text);
    QApplication::processEvents();
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
	QApplication::processEvents();
}

void ProgressDialog::setValueAndText(int value, QString text)
{
	setValue(value);
	setLabelText(text);
}

