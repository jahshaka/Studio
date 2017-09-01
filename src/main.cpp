/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include <QApplication>
#include <QPalette>
#include <QStyleFactory>
#include <QSplashScreen>
#include <QSurfaceFormat>

#include "mainwindow.h"
#include "dialogs/infodialog.h"
#include "core/settingsmanager.h"
#include "globals.h"

int main(int argc, char *argv[])
{
    // Fixes issue on osx where the SceneView widget shows up blank
    // Causes freezing on linux for some reason (Nick)
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
    app.setWindowIcon(QIcon(":/images/logo.png"));

    QGuiApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setDesktopSettingsAware(false);

    // TODO - try to get rid of this in the future (iKlsR)
    // https://gist.github.com/skyrpex/5547015
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

    QSplashScreen splash;
    splash.setPixmap(QPixmap(":/images/splashv2.jpg"));
    splash.show();

    Globals::appWorkingDir = QApplication::applicationDirPath();
    app.processEvents();

    // Create our main app window but hide it at the same time while showing the Desktop first
    // Set the attribute to render invisible while running as normal then hiding it after
    // This is all to make SceneViewWidget's initializeGL trigger OR a way to force the UI to
    // update when hidden, either way we want the Desktop to be the opening widget for now (iKlsR)
    // Logic seems weird but works thanks to https://stackoverflow.com/a/8768138/996468
    MainWindow window;
    window.setAttribute(Qt::WA_DontShowOnScreen);
    window.show();
    window.hide();
    window.showProjectManager();

    // Make our window render as normal going forward
    window.setAttribute(Qt::WA_DontShowOnScreen, false);

    splash.finish(&window);

    return app.exec();
}
