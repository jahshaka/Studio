/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/
#include "infodialog.h"
#include "ui_infodialog.h"
#include "../core/settingsmanager.h"
#include "../mainwindow.h"

#include <QListWidget>
#include <QDesktopServices>
#include <QUrl>
#include <QFileInfo>

InfoDialog::InfoDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::InfoDialog)
{
    ui->setupUi(this);
    this->setWindowTitle("Jahshaka VR");

    /*
     * List Widget for sample files
     * */
/*
    ui->listWidget->setViewMode(QListWidget::IconMode);
    ui->listWidget->setIconSize(QSize(200,90));
    ui->listWidget->setResizeMode(QListWidget::Adjust);
    ui->listWidget->setMovement(QListView::Static);
    ui->listWidget->setSelectionBehavior(QAbstractItemView::SelectItems);
    ui->listWidget->setSelectionMode(QAbstractItemView::SingleSelection);
*/
    //ui->listWidget->addItem(new QListWidgetItem(QIcon(":/images/3D-Planet-Earth-Wallpaper-HD-Widescreen.jpg"),"3D Planet Earth"));
    //ui->listWidget->addItem(new QListWidgetItem(QIcon(":/images/3D-Wallpaper-9-610x381.jpg"),"A New World"));
    //ui->listWidget->addItem(new QListWidgetItem(QIcon(":/images/computer-hd-wallpaper-1366x768-3D.jpg"),"Blocky Design"));


    connect(ui->pushButton,SIGNAL(pressed()),this,SLOT(onClose()));
    connect(ui->checkBox,SIGNAL(stateChanged(int)),this,SLOT(onShowDialogCheckbox(int)));

    connect(ui->visitWebsite,SIGNAL(pressed()),this,SLOT(visitWebsite()));
    connect(ui->viewTutorials,SIGNAL(pressed()),this,SLOT(visitTutorials()));

    //samples
    //connect(ui->listWidget,SIGNAL(itemClicked(QListWidgetItem*)),this,SLOT(openSample(QListWidgetItem*)));

    //recent projects
    connect(ui->recentProjects,SIGNAL(itemClicked(QListWidgetItem*)),this,SLOT(openProject(QListWidgetItem*)));

    this->loadRecentScenes();
}

void InfoDialog::loadRecentScenes()
{
    auto recent = SettingsManager::getDefaultManager()->getRecentlyOpenedScenes();
    for(int i=0;i<recent.size();i++)
    {
        auto filename = recent[i];
        auto item = new QListWidgetItem();
        //item->setText();

        QFileInfo info(filename);
        item->setText(info.baseName()+ " - "+filename);
        item->setData(Qt::UserRole,filename);

        ui->recentProjects->addItem(item);
    }

}
/*
void InfoDialog::addSample(QString title,QString imagePath,QString filePath)
{
    auto item = new QListWidgetItem();
    item->setText(title);
    item->setIcon(QIcon(imagePath));
    item->setData(Qt::UserRole,QVariant::fromValue(filePath));

    ui->listWidget->addItem(item);
}
*/
InfoDialog::~InfoDialog()
{
    delete ui;
}

void InfoDialog::onClose()
{
    if(ui->checkBox->checkState()==Qt::Checked)
    {
        SettingsManager::getDefaultManager()->setValue("show_info_dialog_on_start",QVariant::fromValue(true));
    }
    else
    {
        SettingsManager::getDefaultManager()->setValue("show_info_dialog_on_start",QVariant::fromValue(false));
    }

    this->close();
}

void InfoDialog::onShowDialogCheckbox(int state)
{
    if(state==Qt::Checked)
    {
        SettingsManager::getDefaultManager()->setValue("show_info_dialog_on_start",QVariant::fromValue(true));
    }
    else
    {
        SettingsManager::getDefaultManager()->setValue("show_info_dialog_on_start",QVariant::fromValue(false));
    }

}

void InfoDialog::visitWebsite()
{
    QDesktopServices::openUrl(QUrl("http://www.jahshaka.com/"));
}

void InfoDialog::visitTutorials()
{
    QDesktopServices::openUrl(QUrl("http://www.jahshaka.com/category/blog/"));
}

void InfoDialog::openRecentProject()
{

}

void InfoDialog::openSample(QListWidgetItem* item)
{
    auto filePath = item->data(Qt::UserRole).value<QString>();
    window->openProject(filePath);
}

void InfoDialog::openProject(QListWidgetItem* item)
{
    auto filePath = item->data(Qt::UserRole).value<QString>();
    window->openProject(filePath);

    this->close();
}
