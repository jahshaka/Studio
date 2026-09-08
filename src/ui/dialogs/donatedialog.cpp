/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/dialogs/donatedialog.h"
#include "ui_donate.h"
#include "data/settingsmanager.h"
#include "ui/style/thememanager.h"

DonateDialog::DonateDialog(QDialog *parent) : QDialog(parent), ui(new Ui::DonateDialog)
{
    ui->setupUi(this);
    // Qlementine owns this subtree: drop the .ui-embedded classic sheets right
    // here, before any runtime sheet is applied, so the QStyle paints instead of
    // dark-on-dark #212121 blocks nothing can reach (VISUAL_PARITY re-audit F3;
    // no-op under the Classic theme, which those sheets ARE).
    ThemeManager::clearClassicSheets(this);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint | Qt::FramelessWindowHint);

    connect(ui->checkBox, SIGNAL(released()), this, SLOT(saveAndClose()));
    connect(ui->close, SIGNAL(pressed()), this, SLOT(close()));

    settingsManager = SettingsManager::getDefaultManager();
}

DonateDialog::~DonateDialog()
{
    delete ui;
}

void DonateDialog::updateVersion(const QString &version)
{
    ui->label->setText(version);
}

void DonateDialog::saveAndClose()
{
    settingsManager->setValue("ddialog_seen", ui->checkBox->isChecked());
}
