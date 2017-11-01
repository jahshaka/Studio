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
#include <QFontDatabase>

#include "mainwindow.h"
#include "dialogs/infodialog.h"
#include "core/settingsmanager.h"
#include "globals.h"
#include "constants.h"

// Hints that a dedicated GPU should be used whenever possible
// https://stackoverflow.com/a/39047129/991834
#ifdef Q_OS_WIN
extern "C"
{
  __declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
  __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

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
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
    QApplication::setDesktopSettingsAware(false);
    QApplication app(argc, argv);
    app.setWindowIcon(QIcon(":/images/logo.png"));
    app.setApplicationName("Jahshaka");

    auto dataPath = QStandardPaths::writableLocation(QStandardPaths::DataLocation);
    QDir dataDir(dataPath);
    if (!dataDir.exists()) dataDir.mkpath(dataPath);

// use nicer font on platforms with poor defaults, Mac has really nice font rendering (iKlsR)
#if defined(Q_OS_WIN) || defined(Q_OS_UNIX)
    int id = QFontDatabase::addApplicationFont(":/fonts/DroidSans.ttf");
    if (id != -1) {
        QString family = QFontDatabase::applicationFontFamilies(id).at(0);
        QFont monospace(family, Constants::UI_FONT_SIZE);
        monospace.setStyleStrategy(QFont::PreferAntialias);
        QApplication::setFont(monospace);
    }
#endif

    // TODO - try to get rid of this in the future https://gist.github.com/skyrpex/5547015 (iKlsR)
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
    auto pixmap = QPixmap(":/images/splashv3.png");
    splash.setPixmap(pixmap.scaled(815, 480, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    splash.show();

    Globals::appWorkingDir = QApplication::applicationDirPath();
    app.processEvents();
    //app.setOverrideCursor( QCursor( Qt::BlankCursor ) );

    // Create our main app window but hide it at the same time while showing the EDITOR first
    // Set the attribute to render invisible while running as normal then hiding it after
    // This is all to make SceneViewWidget's initializeGL trigger OR a way to force the UI to
    // update when hidden, either way we want the Desktop to be the opening widget (iKlsR)
    MainWindow window;
    window.setAttribute(Qt::WA_DontShowOnScreen);
    window.show();
    window.grabOpenGLContextHack();
    window.hide();

    // Make our window render as normal going forward
    window.setAttribute(Qt::WA_DontShowOnScreen, false);
    window.goToDesktop();

    splash.finish(&window);

    return app.exec();
}
