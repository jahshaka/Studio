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
#include "app/crashhandler.h"
#ifdef USE_BREAKPAD
#include "app/breakpad.h"
#endif

#include "shell/mainwindow.h"
#include "services/apppaths.h"
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
#include "app/shaderbuildgate.h"
#include "ui/style/thememanager.h"
#include "services/jahlog.h"
#include "services/sessionheader.h"


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

    // (AA_EnableHighDpiScaling / AA_UseHighDpiPixmaps were set here three times
    // between them. Both are no-ops in Qt 6 — high-DPI scaling is always on and
    // cannot be turned off by an attribute — so they were removed rather than
    // left to read as if this application had a HiDPI policy. It does not: the
    // engine's unit contract is in Engine.h (createView), and actually handling
    // scaled displays is its own program.)
    // The editor embeds a native render window (WA_NativeWindow). Without this
    // attribute Qt silently promotes EVERY sibling widget to a native X window, and
    // on xcb the page-switch mapping of those windows desyncs: QStackedWidget said
    // index 3 / shadergraph visible, while at X level the editor page stayed mapped
    // and the Materials page never appeared (verified with xwininfo map states).
    QApplication::setAttribute(Qt::AA_DontCreateNativeWidgetSiblings);
    QApplication::setDesktopSettingsAware(false);
    QApplication app(argc, argv);

    // WHERE THIS RUN KEEPS ITS DATA (services/apppaths.h), resolved BEFORE
    // anything reads a path. Everything downstream — the session log's release
    // directory, SettingsManager (which the very next block constructs), the
    // Upgrader, the library database, the asset store, the shader cache — asks
    // AppPaths, so `--data-root <dir>` / `JAHSHAKA_DATA_ROOT` redirects the
    // whole set together. With neither given, every path is exactly where it
    // has always been.
    //
    // It has to be after QApplication (settingsFilePath reads
    // applicationDirPath) and before the log, and the ordering below it —
    // Upgrader before AssetStoreService::bootstrapFromSettings — is unchanged.
    AppPaths::initialize(cli.dataRoot);

	installCrashHandler();   // STABILITY_AUDIT.md §5.1 — backtraces for every fatal
	                         // signal, ALWAYS on. Linux links -rdynamic so the
	                         // frames carry names; scripts/debug-crash.sh decodes.
	//
	// ORDER CAVEAT, for the day someone flips DISABLE_BREAKPAD=OFF: this used to
	// claim "breakpad chains behind it". It does not. The signal goes to the
	// LAST handler installed, so with the call below breakpad runs FIRST — and
	// on a successful dump it installs SIG_DFL instead of restoring what it
	// replaced (breakpad/src/client/linux/handler/exception_handler.cc:383-386),
	// which means no crash-*.log is written at all. The two lines want to be
	// swapped when breakpad goes live; see docs/BREAKPAD_BETA_PLAN.md §3.
	// Left as-is here because USE_BREAKPAD is off in every build we run and
	// swapping it untested buys nothing.
#ifdef USE_BREAKPAD
	initializeBreakpad();
#endif

    // ---- THE SESSION LOG (SPECS/SESSION_LOG_SPEC.md) -----------------------
    // Opened HERE: after QApplication (SettingsManager and applicationDirPath
    // need it) and after the crash handler, but before the theme, the upgrader
    // and the splash — the three phases that used to fail with no record at
    // all. One file per app RUN (fork F1-A) in <cwd>/logs (Debug) or
    // <AppDataLocation>/logs (release), with a latest.log pointer beside it.
    //
    // PRECEDENCE, in this exact order (spec §3.5, UE's rule verbatim):
    // compiled default -> ini -> command line -> runtime (the log.* verbs).
    {
        JahLog::Options logOpts =
            JahLog::optionsFromSettings(SettingsManager::getDefaultManager()->settings);
        if (!cli.logDir.isEmpty())  logOpts.dir = cli.logDir;
        if (!cli.logFile.isEmpty()) logOpts.file = cli.logFile;
        logOpts.disabled = cli.noLog;
        JahLog::start(logOpts);                    // applies the compiled defaults
        JahLog::applyIniLevels(SettingsManager::getDefaultManager()->settings);
        for (const QString &spec : cli.logLevels) JahLog::applyLevelSpec(spec);
        // The crash handler gets the path and the raw descriptor, and nothing
        // else (spec §3.8-4, §9-R3): it runs in signal context, so it may
        // write(2) into the log but may never CALL into it.
        crashHandlerSetSessionLog(qPrintable(JahLog::sessionFilePath()), JahLog::rawFd());
    }
    // The funnel is what makes the ~61 existing qDebug/qWarning call sites land
    // in the file with zero edits to any of them — LoadTimeline's open profile,
    // the slow-frame warning, the watchdog's stall line, SceneMirror's skeleton
    // diagnostics. It CHAINS to the previous handler, so stderr is unchanged.
    JahLog::installQtMessageHandler();
    // And this is fork F5-A: every irisLog() call site gains a timestamp, a
    // category and rotation without one of them being edited.
    JahLog::absorbIrisLogger();

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

    auto dataPath = AppPaths::dataRoot();
    QDir dataDir(dataPath);
    if (!dataDir.exists()) dataDir.mkpath(dataPath);

    // --clear-shader-cache: our r.InvalidateCachedShaders. It runs HERE, before
    // MainWindow starts the engine, and the run then continues normally — which
    // is exactly what a cold-start benchmark needs and what a user with a
    // suspect cache needs (SHADER_CACHE_SPEC §4.5).
    // --profile: recorded on the host BEFORE the engine exists; resolveConfig
    // folds it into EngineConfig::profile when MainWindow starts the engine.
    if (cli.profile) EngineHost::setProfilingRequested(true);

    if (cli.clearShaderCache) {
        const bool ok = EngineHost::clearShaderCacheOnDisk();
        std::fprintf(stderr, "shader cache: %s\n",
                     ok ? "cleared" : "could not be cleared");
    }

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

    // ---- The startup header (SESSION_LOG_SPEC §4) --------------------------
    // Emitted HERE rather than beside JahLog::start() for one reason: the asset
    // store's online/offline verdict and the engine config are two of the most
    // useful rows in it, and neither exists until the bootstrap above has run.
    // Everything before this point is still in the file — the open bracket, and
    // whatever the theme and the upgrader had to say.
    //
    // The GPU/device/driver rows are NOT here: no render system exists yet.
    // They arrive as their own block once the engine boots (phase 3).
    SessionHeader::addProvider(QStringLiteral("settings"), [] {
        SessionHeader::Rows r;
        SettingsManager *sm = SettingsManager::getDefaultManager();
        r << SessionHeader::Row { QStringLiteral("file"), sm->settings->fileName() };
        r << SessionHeader::Row { QStringLiteral("theme"),
                                  sm->getValue("appearance/theme", "qlementine-dark").toString() };
        r << SessionHeader::Row { QStringLiteral("pacing"),
                                  sm->getValue("viewport/pacing", "display").toString() };
        r << SessionHeader::Row { QStringLiteral("watchdog"),
                                  sm->getValue("watchdog_enabled", true).toString() };
        r << SessionHeader::Row { QStringLiteral("shadowMeshOptimization"),
                                  sm->getValue("shadow_mesh_optimization", true).toString() };
        r << SessionHeader::Row { QStringLiteral("shaderWarmupSamples"),
                                  sm->getValue("shader_warmup_samples", 1).toString() };
        r << SessionHeader::Row { QStringLiteral("shaderWarmupShadows"),
                                  sm->getValue("shader_warmup_shadows", true).toString() };
        return r;
    });
    SessionHeader::addProvider(QStringLiteral("assets"), [] {
        SessionHeader::Rows r;
        const QString root = AssetStorePaths::root();
        r << SessionHeader::Row { QStringLiteral("storeRoot"), root };
        r << SessionHeader::Row { QStringLiteral("defaultRoot"), AssetStorePaths::defaultRoot() };
        r << SessionHeader::Row { QStringLiteral("storeOnline"),
                                  QDir(root).exists() ? QStringLiteral("true")
                                                      : QStringLiteral("false (offline)") };
        return r;
    });
    SessionHeader::addProvider(QStringLiteral("engine"), [] {
        SessionHeader::Rows r;
        const auto cfg = EngineHost::resolveConfig();
        r << SessionHeader::Row { QStringLiteral("pluginDir"),
                                  QString::fromStdString(cfg.pluginDir) };
        r << SessionHeader::Row { QStringLiteral("hlmsMediaDir"),
                                  QString::fromStdString(cfg.hlmsMediaDir) };
        r << SessionHeader::Row { QStringLiteral("ogreLog"),
                                  QString::fromStdString(cfg.logFile) };
        r << SessionHeader::Row { QStringLiteral("optimizeShadowMeshes"),
                                  cfg.optimizeShadowMeshes ? QStringLiteral("true")
                                                           : QStringLiteral("false") };
        r << SessionHeader::Row { QStringLiteral("appBuildId"),
                                  QString::fromStdString(cfg.appBuildId) };
        r << SessionHeader::Row { QStringLiteral("shaderCacheEnabled"),
                                  EngineHost::shaderCacheEnabled() ? QStringLiteral("true")
                                                                   : QStringLiteral("false") };
        r << SessionHeader::Row { QStringLiteral("shaderCacheDir"),
                                  EngineHost::shaderCacheDirectory() };
        return r;
    });
    SessionHeader::addProvider(QStringLiteral("mcp"), [&cli] {
        SessionHeader::Rows r;
        r << SessionHeader::Row { QStringLiteral("port"),
                                  !cli.mcpServe ? QStringLiteral("(off for this run)")
                                  : cli.mcpPort  ? QString::number(cli.mcpPort)
                                                 : QStringLiteral("(ephemeral)") };
        return r;
    });
    SessionHeader::emitBlock();

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

    // THE STARTUP SHADER BUILD RUNS HERE, behind the splash (owner decision,
    // 2026-09-04). MainWindow's constructor has started the engine, created the
    // views and registered the Hlms; what it has NOT done is render, and the
    // Hlms compiles per renderable on first use. So the gate pumps frames with
    // the window still hidden, shows "Building shaders N/M" on the splash, and
    // returns when the count settles. A cold first launch pays for its whole
    // build here; a warm one races through cache hits.
    holdSplashForShaderBuild(app, splash);
    // From now on any NEW compile burst (a scene open, a material edit) is
    // written to disk a few seconds after it settles, so a crash costs at most
    // that much recompilation.
    EngineHost::instance().startShaderCacheWatchdog();

    // Hide the splash as soon as the window shows — including the CLI paths
    // below (script/MCP runs show the window themselves). A splash left
    // visible counts as a window and blocks quitOnLastWindowClosed: the app
    // then survives its own main window (the headless-zombie bug family).
    splash.finish(&window);

    // The two CLI paths that do NOT go through finalizeAppExit get their close
    // bracket here, so no exit route leaves a file that looks like a crash.
    if (!cli.selftestPng.isEmpty()) {
        const int rc = runEngineSelftest(window, app, cli.selftestPng);
        JahLog::stop(QStringLiteral("engine selftest, exit code %1").arg(rc));
        return rc;
    }

    if (!cli.dumpDocsPath.isEmpty()) {
        const int rc = runDumpApiDocs(window, cli.dumpDocsPath);
        JahLog::stop(QStringLiteral("dump-api-docs, exit code %1").arg(rc));
        return rc;
    }

    if (!cli.scriptPath.isEmpty())
        return runScriptFile(window, app, cli.scriptPath, cli.headlessScript);

    if (cli.mcpServe)
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

	// Tooltips: Classic's own popup (ToolTipHelper), or the Qlementine style's
	// native tooltip with "Header | body" rendered as rich text.
	if (ThemeManager::classicActive())
		app.installEventFilter(new ToolTipHelper());
	else
		app.installEventFilter(new NativeToolTipFormatter(&app));

    const int rc = app.exec();
    return finalizeAppExit(rc);
}
