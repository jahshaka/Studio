/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/dialogs/ogrepreviewdialog.h"
#include "bridge/enginehost.h"
#include "viewport/ieditorviewport.h"
#include <QImage>
#include <QColor>
#include <QFile>
#include <QElapsedTimer>
#include <QThread>
#include <cstdio>
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
#include "app/breakpad.h"
#endif

#include "shell/mainwindow.h"
#include "app/cli/clioptions.h"
#include "services/assetstorepaths.h"
#include "services/assetstore.h"
#include "data/settingsmanager.h"
#include "app/cli/scriptrunner.h"
#include "app/cli/selftestrunner.h"
#include "ui/dialogs/infodialog.h"
#include "scripting/scriptengine.h"
#include "data/constants.h"
#include "app/updatechecker.h"
#include "app/upgrader.h"
#include "ui/dialogs/softwareupdatedialog.h"
#include "ui/controls/tooltip.h"
#include "app/versionsplashscreen.h"
#include "ui/style/thememanager.h"


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

    const CliOptions cli = CliOptions::parse(argc, argv);
    cli.applyPlatformPolicy();

    // Pin the application identity instead of letting Qt infer it from the
    // executable's file name — that inference is what a renamed or bundled
    // binary silently changes, and QStandardPaths::AppDataLocation (the library
    // DB, the asset store, the settings file in non-Debug builds) is derived
    // from it. "Jahshaka" is exactly what the inference already produced, so
    // this pins today's location rather than moving it.
    //
    // setOrganizationName is deliberately NOT called: QStandardPaths appends the
    // organization ABOVE the application name on every platform, so setting it
    // would relocate AppDataLocation for every existing user, and no QSettings
    // in this tree uses the default constructor anyway (they all take an
    // explicit path — src/data/settingsmanager.h:62).
    QCoreApplication::setApplicationName(QStringLiteral("Jahshaka"));
#ifdef JAHSHAKA_VERSION
    QCoreApplication::setApplicationVersion(QStringLiteral(JAHSHAKA_VERSION));
#endif

	QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    // The editor embeds a native render window (WA_NativeWindow). Without this
    // attribute Qt silently promotes EVERY sibling widget to a native X window, and
    // on xcb the page-switch mapping of those windows desyncs: QStackedWidget said
    // index 3 / shadergraph visible, while at X level the editor page stayed mapped
    // and the Materials page never appeared (verified with xwininfo map states).
    QApplication::setAttribute(Qt::AA_DontCreateNativeWidgetSiblings);
    QApplication::setDesktopSettingsAware(false);
    QApplication app(argc, argv);

#ifdef USE_BREAKPAD
	initializeBreakpad();
#endif

    // Apply the app theme (Qlementine Dark by default, archived Classic on
    // request) BEFORE any widget exists — the Upgrader dialog and the engine
    // preview dialog are the first widgets alive. See THEME_AUDIT.md §4.
    ThemeManager::applyAtStartup(app);

    if (cli.enginePreviewOnly) {
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

    // Relocatable store root (ASSET_PIPELINE_SPEC §3.1.1): point the path
    // authority at the assets/storeRoot setting before anything derives a
    // store path. Only the DEFAULT root is ever created implicitly — a
    // missing custom root means the store is OFFLINE (§3.1.2), and mkpath-ing
    // a dead mount point would fake an empty-but-online store.
    AssetStoreService::bootstrapFromSettings(SettingsManager::getDefaultManager());
    if (AssetStorePaths::root() == AssetStorePaths::defaultRoot()) {
        QDir assetDir(AssetStorePaths::defaultRoot());
        if (!assetDir.exists()) assetDir.mkpath(AssetStorePaths::defaultRoot());
    }

    // Fonts are a theme decision now: Classic sets DroidSans inside
    // ThemeManager::applyAtStartup; Qlementine Dark uses the theme's own
    // typography (Inter/Roboto Mono, bundled and applied by the style).

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

    app.processEvents();
    //app.setOverrideCursor( QCursor( Qt::BlankCursor ) );

    // Create our main app window but hide it at the same time while showing the EDITOR first
    // Set the attribute to render invisible while running as normal then hiding it after
    // This is all to make SceneViewWidget's initializeGL trigger OR a way to force the UI to
    // update when hidden, either way we want the Desktop to be the opening widget (iKlsR)
    MainWindow window;

    // Hide the splash as soon as the window shows — including the CLI paths
    // below (script/MCP runs show the window themselves). A splash left
    // visible counts as a window and blocks quitOnLastWindowClosed: the app
    // then survives its own main window (the headless-zombie bug family).
    splash.finish(&window);

    if (!cli.selftestPng.isEmpty())
        return runEngineSelftest(window, app, cli.selftestPng);

    if (!cli.dumpDocsPath.isEmpty())
        return runDumpApiDocs(window, cli.dumpDocsPath);

    if (!cli.scriptPath.isEmpty())
        return runScriptFile(window, app, cli.scriptPath, cli.headlessScript);

    if (cli.mcpPort > 0)
        return runMcpServe(window, app, cli.mcpPort, cli.headlessScript);

    //window.setAttribute(Qt::WA_DontShowOnScreen);
    //window.show();
    //window.grabOpenGLContextHack();
    //window.hide();

    // Make our window render as normal going forward
    //window.setAttribute(Qt::WA_DontShowOnScreen, false);
    window.goToDesktop();   // splash.finish above hides the splash here

	UpdateChecker updateChecker;
	QObject::connect(&updateChecker, &UpdateChecker::updateNeeded,
        [&window](QString nextVersion, QString versionNotes, QString downloadLink)
	{
		// show update dialog (parented: no orphanable top-level windows)
		auto dialog = new SoftwareUpdateDialog(&window);
		dialog->setVersionNotes(versionNotes);
		dialog->setDownloadUrl(downloadLink);
		dialog->show();
	});

    
    updateChecker.checkForAppUpdate();

	app.installEventFilter(new ToolTipHelper());

    const int rc = app.exec();
    return finalizeAppExit(rc);
}
