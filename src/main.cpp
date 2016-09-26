/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Karsten Becker <jahshaka@gmail.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "mainwindow.h"
#include <QApplication>
#include <QPalette>
#include <QStyleFactory>
#include <QSplashScreen>
#include "dialogs/infodialog.h"
#include "settingsmanager.h"

//qt3d presentation
//https://prezi.com/u-ewejoqxqj2/qt3d-20/

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setWindowIcon(QIcon(":/images/logo.png"));

    QGuiApplication::setAttribute(Qt::AA_DisableHighDpiScaling);
    QApplication::setDesktopSettingsAware(false);

    //https://gist.github.com/skyrpex/5547015

    a.setStyle(QStyleFactory::create("fusion"));
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(83, 83, 83));
    palette.setColor(QPalette::WindowText, Qt::white);
    palette.setColor(QPalette::Base, QColor(64, 64, 64));
    palette.setColor(QPalette::AlternateBase, QColor(53,53,53));
    palette.setColor(QPalette::ToolTipBase, Qt::white);
    palette.setColor(QPalette::ToolTipText, Qt::white);
    palette.setColor(QPalette::Text, Qt::white);
    palette.setColor(QPalette::Button, QColor(53,53,53));
    palette.setColor(QPalette::ButtonText, Qt::white);
    palette.setColor(QPalette::BrightText, Qt::red);
    palette.setColor(QPalette::Highlight, QColor(30,144,255));
    palette.setColor(QPalette::HighlightedText, Qt::black);

    palette.setColor(QPalette::Inactive, QPalette::Link, QColor(135, 135, 135));
    palette.setColor(QPalette::Inactive, QPalette::Text, QColor(135, 135, 135));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(135, 135, 135));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(135, 135, 135));
    a.setPalette(palette);

    QSplashScreen *splash = new QSplashScreen;
    splash->setPixmap(QPixmap(":/images/splash screen v2.jpg"));
    splash->show();

    MainWindow w;
    //w.show();
    w.showMaximized();
    splash->hide();
    delete splash;

    auto settings = w.getSettingsManager();

    if(settings->getValue("show_info_dialog_on_start",QVariant::fromValue(true)).value<bool>() == true)
    {
        InfoDialog dialog;
        dialog.setMainWindow(&w);
        dialog.setModal(true);
        dialog.exec();
    }

    return a.exec();
}
