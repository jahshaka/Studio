/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#include "ui/dialogs/aboutdialog.h"
#include "ui_aboutdialog.h"
#include "ui/style/thememanager.h"

AboutDialog::AboutDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::AboutDialog)
{
    ui->setupUi(this);
    // Qlementine owns this subtree: drop the .ui-embedded classic sheets right
    // here, before any runtime sheet is applied, so the QStyle paints instead of
    // dark-on-dark #212121 blocks nothing can reach (VISUAL_PARITY re-audit F3;
    // no-op under the Classic theme, which those sheets ARE).
    ThemeManager::clearClassicSheets(this);
    this->setWindowTitle("About");

    connect(ui->okButton,SIGNAL(clicked(bool)),this,SLOT(close()));
}

AboutDialog::~AboutDialog()
{
    delete ui;
}
