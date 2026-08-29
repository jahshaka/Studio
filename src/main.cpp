/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "dialogs/ogrepreviewdialog.h"
#include <QApplication>
#include <QPalette>
#include <QStyleFactory>
#include <QSplashScreen>
#include <QSurfaceFormat>
#include <QFontDatabase>
#include <QtConcurrent>
#include <QStandardPaths>

// needs to be included near the top before
// anything includes inttypes before it
#ifdef USE_BREAKPAD
#include "breakpad/breakpad.h"
#endif

#include "mainwindow.h"
#include "dialogs/infodialog.h"
#include "globals.h"
#include "constants.h"
#include "misc/updatechecker.h"
#include "misc/upgrader.h"
#include "dialogs/softwareupdatedialog.h"
#include "helpers/tooltip.h"
#include "widgets/versionsplashscreen.h"


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
// NB: `#ifndef A && B` is not valid conditional logic - #ifndef takes a single
// identifier and everything after it is ignored, so this only ever tested
// GIT_COMMIT_HASH. Spelled out with #if !defined(...) || !defined(...).
#if !defined(GIT_COMMIT_HASH) || !defined(GIT_COMMIT_DATE)
#define GIT_COMMIT_HASH "0000" // means uninitialized
#endif
}

int main(int argc, char *argv[])
{
    GetGitCommitHash();

    // --engine-preview: start ONLY the new engine, skipping the legacy Qt-GL editor.
    //
    // The two renderers need different Qt platforms and cannot coexist yet:
    // the legacy viewport requires wayland (xcb gives it no GL context and the app
    // dies with "versionFunctions: No OpenGL context"), while Ogre-Next has no
    // Wayland backend and needs xcb. This mode is the transition path — it becomes
    // the normal startup once the editor viewport moves onto the engine.
    bool enginePreviewOnly = false;
    for (int i = 1; i < argc; ++i)
        if (qstrcmp(argv[i], "--engine-preview") == 0) enginePreviewOnly = true;
    if (enginePreviewOnly)
        qputenv("QT_QPA_PLATFORM", "xcb");

#ifdef Q_OS_LINUX
    // Only force xcb if the user hasn't chosen a platform, and use EGL rather
    // than GLX: under GLX, making an offscreen context current on a background
    // thread (the thumbnail render thread) fails with the NVIDIA driver.
    // Don't force xcb. On this stack QOpenGLWidget only renders under the
    // native wayland platform: xcb+GLX fails to make the context current,
    // and xcb+EGL makes it current but renders nothing.
    if (!enginePreviewOnly && qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        if (!qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY"))
            qputenv("QT_QPA_PLATFORM", "wayland");
        else
            qputenv("QT_QPA_PLATFORM", "xcb");
    }
#endif

    // Fixes issue on osx where the SceneView widget shows up blank
    // Causes freezing on linux for some reason (Nick)
#if defined(Q_OS_MAC) || defined(Q_OS_LINUX)
    QSurfaceFormat format;
    format.setDepthBufferSize(32);
    format.setMajorVersion(3);
    format.setMinorVersion(2);
    format.setProfile(QSurfaceFormat::CoreProfile);
    // Without this the EGL paths hand back an OpenGL ES context and the
    // 3.2 Core function resolver returns null.
    format.setRenderableType(QSurfaceFormat::OpenGL);
    QSurfaceFormat::setDefaultFormat(format);
#endif
	QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
	QApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
	QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QApplication::setDesktopSettingsAware(false);
    QApplication app(argc, argv);

#ifdef USE_BREAKPAD
	initializeBreakpad();
#endif

    if (enginePreviewOnly) {
        // No MainWindow, no IrisGL, no legacy GL context.
        OgrePreviewDialog preview;
        preview.setAttribute(Qt::WA_QuitOnClose, true);
        preview.show();
        return app.exec();
    }
	
	/*
	QtConcurrent::run([&updateChecker]() {
		updateChecker.checkForUpdate();
	});
	*/

	Upgrader upgrader;
	//upgrader.checkIfDeprecatedVersion();
	upgrader.checkIfSchemaNeedsUpdating();

    app.setWindowIcon(QIcon(":/images/icon.ico"));

    auto dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir dataDir(dataPath);
    if (!dataDir.exists()) dataDir.mkpath(dataPath);

    auto assetPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + Constants::ASSET_FOLDER;
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

    VersionSplashScreen splash;

    splash.setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    auto pixmap = QPixmap(":/images/splashv3.png");
    splash.setPixmap(pixmap.scaled(900, 506, Qt::KeepAspectRatio, Qt::SmoothTransformation));

//#ifdef QT_DEBUG
#ifdef GIT_COMMIT_HASH
    if (GIT_COMMIT_HASH != "0000")
        splash.showMessage(QString("Revision - %1 %2").arg(GIT_COMMIT_HASH).arg(GIT_COMMIT_DATE),
                           Qt::AlignBottom | Qt::AlignLeft, QColor(255, 255, 255));
#endif // GIT_COMMIT_HASH
//#endif // QT_DEBUG

    splash.updateVersion(Constants::CONTENT_VERSION);

    splash.show();

    Globals::appWorkingDir = QApplication::applicationDirPath();
    app.processEvents();
    //app.setOverrideCursor( QCursor( Qt::BlankCursor ) );

    // Create our main app window but hide it at the same time while showing the EDITOR first
    // Set the attribute to render invisible while running as normal then hiding it after
    // This is all to make SceneViewWidget's initializeGL trigger OR a way to force the UI to
    // update when hidden, either way we want the Desktop to be the opening widget (iKlsR)
    MainWindow window;
    //window.setAttribute(Qt::WA_DontShowOnScreen);
    //window.show();
    //window.grabOpenGLContextHack();
    //window.hide();

    // Make our window render as normal going forward
    //window.setAttribute(Qt::WA_DontShowOnScreen, false);
    window.goToDesktop();
    splash.finish(&window);

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

    
    updateChecker.checkForAppUpdate();

	app.installEventFilter(new ToolTipHelper());

    return app.exec();
}
