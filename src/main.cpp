/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "mainwindow.h"
#include <QApplication>
#include <QPalette>
#include <QStyleFactory>
#include <QSplashScreen>
#include <QSurfaceFormat>
#include "dialogs/infodialog.h"
#include "core/settingsmanager.h"

int main(int argc, char *argv[])
{
    // fixes issue on osx where the SceneView widget shows up blank
    // causes freezing on linux for some reason
#ifdef Q_OS_MAC
    QSurfaceFormat format;
    format.setDepthBufferSize(32);
    format.setMajorVersion(3);
    format.setMinorVersion(2);
    format.setProfile(QSurfaceFormat::CoreProfile);

    QSurfaceFormat::setDefaultFormat(format);
#endif

    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication app(argc, argv);
    app.setWindowIcon(QIcon(":/app/images/logo.png"));

    QGuiApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setDesktopSettingsAware(false);

    //https://gist.github.com/skyrpex/5547015
    app.setStyle(QStyleFactory::create("fusion"));
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(48, 48, 48));
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
    app.setPalette(palette);

    //splah screen
    QSplashScreen splash;
    auto image = QPixmap(":/app/images/splashv2.jpg");
    splash.setPixmap(image);
    splash.show();

    app.processEvents();

    //draw main window maximized
    MainWindow window;
    window.showMaximized();
    splash.hide();

    //show info dialog is settings stipulate so
    /*
    auto settings = window.getSettingsManager();
    if(settings->getValue("show_info_dialog_on_start",QVariant::fromValue(true)).value<bool>() == true)
    {
        InfoDialog dialog;
        dialog.setMainWindow(&window);
        dialog.setModal(true);
        dialog.exec();
    }
    */
    return app.exec();
}
