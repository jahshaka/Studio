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
#include <QtConcurrent>

// needs to be included near the top before
// anything includes inttypes before it
#ifdef USE_BREAKPAD
#include "breakpad/breakpad.h"
#endif

#include "mainwindow.h"
#include "dialogs/infodialog.h"
#include "core/settingsmanager.h"
#include "globals.h"
#include "constants.h"
#include "misc/updatechecker.h"
#include "misc/upgrader.h"
#include "dialogs/softwareupdatedialog.h"
#include "helpers/tooltip.h"


// Hints that a dedicated GPU should be used whenever possible
// https://stackoverflow.com/a/39047129/991834
#ifdef Q_OS_WIN
extern "C"
{
  __declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
  __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

inline void GetGitCommitHash()
{
#ifndef GIT_COMMIT_HASH && GIT_COMMIT_DATE
#define GIT_COMMIT_HASH "0000" // means uninitialized
#endif
}

int main(int argc, char *argv[])
{
    GetGitCommitHash();
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
	QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
    QApplication::setDesktopSettingsAware(false);
    QApplication app(argc, argv);

#ifdef USE_BREAKPAD
	initializeBreakpad();
#endif
	
	/*
	QtConcurrent::run([&updateChecker]() {
		updateChecker.checkForUpdate();
	});
	*/

	Upgrader upgrader;
	upgrader.checkIfDeprecatedVersion();

    app.setWindowIcon(QIcon(":/images/icon.ico"));
#ifdef BUILD_PLAYER_ONLY
    app.setApplicationName("JahPlayer");
#else
    app.setApplicationName("Jahshaka");
#endif // BUILD_PLAYER_ONLY


    auto dataPath = QStandardPaths::writableLocation(QStandardPaths::DataLocation);
    QDir dataDir(dataPath);
    if (!dataDir.exists()) dataDir.mkpath(dataPath);

    auto assetPath = QStandardPaths::writableLocation(QStandardPaths::DataLocation) + Constants::ASSET_FOLDER;
    QDir assetDir(assetPath);
    if (!assetDir.exists()) assetDir.mkpath(assetPath);

// use nicer font on platforms with poor defaults, Mac has really nice font rendering (iKlsR)
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
    int id = QFontDatabase::addApplicationFont(":/fonts/DroidSans.ttf");
    if (id != -1) {
        QString family = QFontDatabase::applicationFontFamilies(id).at(0);
        QFont monospace(family, Constants::UI_FONT_SIZE);
        monospace.setStyleStrategy(QFont::PreferAntialias);
        QApplication::setFont(monospace);
    }
#endif

#ifndef BUILD_PLAYER_ONLY
    QSplashScreen splash;
    splash.setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnBottomHint);
    auto pixmap = QPixmap(":/images/splashv3.png");
    splash.setPixmap(pixmap.scaled(900, 506, Qt::KeepAspectRatio, Qt::SmoothTransformation));
#ifdef QT_DEBUG
#ifdef GIT_COMMIT_HASH
    if (GIT_COMMIT_HASH != "0000")
        splash.showMessage(QString("Revision - %1 %2").arg(GIT_COMMIT_HASH).arg(GIT_COMMIT_DATE),
                           Qt::AlignBottom | Qt::AlignLeft, QColor(255, 255, 255));
#endif // GIT_COMMIT_HASH
#endif // QT_DEBUG
    splash.show();
#endif // BUILD_PLAYER_ONLY

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

#ifndef BUILD_PLAYER_ONLY
    splash.finish(&window);
#endif

#if !defined(QT_DEBUG) && !defined(BUILD_PLAYER_ONLY)
	UpdateChecker updateChecker;
	QObject::connect(&updateChecker, &UpdateChecker::updateNeeded,
        [&updateChecker](QString nextVersion, QString versionNotes, QString downloadLink)
	{
		// show update dialog
		auto dialog = new SoftwareUpdateDialog();
		dialog->setVersionNotes(versionNotes);
		dialog->setDownloadUrl(downloadLink);
		dialog->show();
	});

    if (SettingsManager::getDefaultManager()->getValue("automatic_updates", true).toBool()) {
		updateChecker.checkForUpdate();
    }
#endif // QT_DEBUG

	app.installEventFilter(new ToolTipHelper());
    return app.exec();
}
