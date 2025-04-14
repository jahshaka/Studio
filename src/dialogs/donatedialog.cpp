/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "donatedialog.h"
#include "ui_donate.h"
#include "../core/settingsmanager.h"

DonateDialog::DonateDialog(QDialog *parent) : QDialog(parent), ui(new Ui::DonateDialog)
{
    ui->setupUi(this);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint | Qt::FramelessWindowHint);

    connect(ui->checkBox, SIGNAL(pressed()), this, SLOT(saveAndClose()));
    connect(ui->close, SIGNAL(pressed()), this, SLOT(close()));

    settingsManager = SettingsManager::getDefaultManager();
}

DonateDialog::~DonateDialog()
{
    delete ui;
}

void DonateDialog::saveAndClose()
{
    settingsManager->setValue("ddialog_seen", true);
}
