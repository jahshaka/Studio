/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/dialogs/renameprojectdialog.h"
#include "ui_renameprojectdialog.h"
#include "ui/style/thememanager.h"

RenameProjectDialog::RenameProjectDialog(QDialog *parent) : QDialog(parent), ui(new Ui::RenameProjectDialog)
{
    ui->setupUi(this);
    // Qlementine owns this subtree: drop the .ui-embedded classic sheets right
    // here, before any runtime sheet is applied, so the QStyle paints instead of
    // dark-on-dark #212121 blocks nothing can reach (VISUAL_PARITY re-audit F3;
    // no-op under the Classic theme, which those sheets ARE).
    ThemeManager::clearClassicSheets(this);
    setWindowTitle("Rename Project");
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

	ui->ok->setAutoDefault(true);
	ui->ok->setDefault(true);

    ui->lineEdit->setAttribute(Qt::WA_MacShowFocusRect, false);

    connect(ui->ok, SIGNAL(pressed()), SLOT(newText()));
    connect(ui->cancel, &QPushButton::pressed, [this]() { close(); });
}
 
RenameProjectDialog::~RenameProjectDialog()
{
    delete ui;
}

void RenameProjectDialog::newText()
{
    emit newTextEmit(ui->lineEdit->text());
    this->close();
}
