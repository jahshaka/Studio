/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/panels/timeline/createanimationwidget.h"
#include "ui_createanimationwidget.h"

CreateAnimationWidget::CreateAnimationWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::CreateAnimationWidget)
{
    ui->setupUi(this);
}

CreateAnimationWidget::~CreateAnimationWidget()
{
    delete ui;
}

QPushButton *CreateAnimationWidget::getCreateButton()
{
    return ui->newAnimButton;
}

void CreateAnimationWidget::showButton()
{
    ui->newAnimButton->show();
}

void CreateAnimationWidget::hideButton()
{
    ui->newAnimButton->hide();
}

void CreateAnimationWidget::setButtonText(QString text)
{
    ui->newAnimButton->setText(text);
}
