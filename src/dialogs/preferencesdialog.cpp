/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/
#include "preferencesdialog.h"
#include "ui_preferencesdialog.h"
#include <QListWidgetItem>
#include "preferences/worldsettings.h"
#include "../core/settingsmanager.h"

PreferencesDialog::PreferencesDialog(SettingsManager* settings) :
    QDialog(nullptr),
    ui(new Ui::PreferencesDialog)
{
    ui->setupUi(this);

    this->settings = settings;

    setWindowTitle("Preferences");
    ui->cancelButton->hide();

    connect(ui->okButton, SIGNAL(clicked(bool)), this, SLOT(close()));

    setupList();
    setupPages();
}

void PreferencesDialog::setupList()
{
    auto list = ui->listWidget;
    list->setFlow(QListView::TopToBottom);
    list->setViewMode(QListView::IconMode);
    list->setMovement(QListView::Static);

    QListWidgetItem *item = new QListWidgetItem(ui->listWidget);
    item->setIcon(QIcon(":/app/icons/world.svg"));
    item->setText(tr("Editor"));
    item->setSizeHint(QSize(80, 80));
    item->setTextAlignment(Qt::AlignHCenter);
    item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);

    connect(ui->listWidget, SIGNAL(currentItemChanged(QListWidgetItem*, QListWidgetItem*)),
            this, SLOT(pageChanged(QListWidgetItem*, QListWidgetItem*)));
}

void PreferencesDialog::setupPages()
{
    // can we elimate this to be more permanent? why (was/is) this dynamic really?
    worldSettings = new WorldSettings(settings);
    ui->stackedWidget->addWidget(worldSettings);
}

void PreferencesDialog::pageChanged(QListWidgetItem* prev,QListWidgetItem* cur)
{
    Q_UNUSED(prev);
    ui->stackedWidget->setCurrentIndex(ui->listWidget->row(cur));
}

void PreferencesDialog::closeDialog()
{
    this->close();
}

PreferencesDialog::~PreferencesDialog()
{
    delete ui;
}
