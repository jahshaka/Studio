/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "bridge/enginehost.h"
#include "viewport/ieditorviewport.h"
#include <QImage>
#include <QColor>
#include <QFile>
#include <QElapsedTimer>
#include <QThread>
#include <cstdio>
#include <QApplication>
#include <QMessageBox>
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
#include "app/firstrun.h"
#include "ui/dialogs/donatedialog.h"
#include "services/assetstorepaths.h"
#include "services/assetmetadata.h"
#include "services/assetstore.h"
#include "services/meshbakestore.h"
#include "data/settingsmanager.h"
#include "app/cli/scriptrunner.h"
#include "app/cli/selftestrunner.h"
#include "scripting/scriptengine.h"
#include "data/constants.h"
#include "app/updatechecker.h"
#include "services/librarygeneration.h"
#include "ui/pages/assetview.h"
#include <QTimer>
#include "irisgl/core/irisutils.h"
#include "irisgl/core/logger.h"
#include "ui/dialogs/softwareupdatedialog.h"
#include "ui/controls/tooltip.h"
#include "app/versionsplashscreen.h"
#include "services/livecompiles.h"
#include "jah_provenance.h"   // GIT_COMMIT_HASH / _DATE (generated; SPEED-CPU B2)
#include "app/shaderbuildgate.h"
#include "ui/style/thememanager.h"
#include "services/framepacing.h"
#include "services/jahlog.h"
#include "services/testtier.h"
#include "services/worldmodes.h"
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

int main(int argc, char *argv[])
{
    const CliOptions cli = CliOptions::parse(argc, argv);
    // --help / --version ANSWER AND EXIT HERE (lane HELP-FLAG-1): before the
    // refusals below, before QApplication, the data root, the settings file, the
    // session log, the database or a window — so asking the binary what it
    // accepts can never boot the editor on whatever display and data root the
    // shell has (two agents once did exactly that, on the owner's display).
    if (cli.help || cli.version) {
        if (cli.help) std::fputs(qPrintable(CliOptions::usageText()), stdout);
        else {
#ifdef JAHSHAKA_VERSION
            std::printf("Jahshaka %s (%s %s)\n", JAHSHAKA_VERSION, GIT_COMMIT_HASH, GIT_COMMIT_DATE);
#else
            std::printf("Jahshaka (%s %s)\n", GIT_COMMIT_HASH, GIT_COMMIT_DATE);
#endif
        }
        std::fflush(stdout);
        return 0;
    }
    // A COMMAND LINE THE APP CANNOT HONOUR STOPS HERE (ledger 150): before
    // QApplication, before the log, before a window — loudly, on stderr, with a
    // non-zero exit code. The alternative is what `--mcp-port=8716336` used to
    // do: get silently truncated to 48, boot the whole editor, fail to bind a
    // protected port and quit again, which is a worse answer AND was the
    // trigger for the unordered-teardown SIGSEGV.
    if (!cli.errors.isEmpty()) {
        for (const QString &e : cli.errors)
            std::fprintf(stderr, "Jahshaka: %s\n", qPrintable(e));
        return 2;
    }
    // THE PROCESS'S TEST TIER (TEST-TIER-1, services/testtier.h): the flag, else
    // JAHSHAKA_TEST_TIER; validated here, where the World Mode names are known,
    // and refused like any other argument the app cannot honour.
    {
        const QString tier = !cli.testTier.isEmpty()
            ? cli.testTier
            : QString::fromLocal8Bit(qgetenv(testtier::kEnvVar)).trimmed();
        if (!tier.isEmpty()) {
            bool ok = false;
            const worldmodes::Mode m = worldmodes::modeFromName(tier, &ok);
            if (!ok || m == worldmodes::Mode::Custom) {
                std::fprintf(stderr, "Jahshaka: --test-tier/%s: '%s' is not a World Mode (%s)\n",
                             testtier::kEnvVar, qPrintable(tier),
                             qPrintable(worldmodes::modeNames().join(QStringLiteral(", "))));
                return 2;
            }
            testtier::set(worldmodes::modeName(m));
        }
        // ...AND WHAT IT NEEDS (TEST-NEEDS-1): JAHSHAKA_TEST_NEEDS names the switchable
        // features the process keeps (testtier::switchable(); `none` = none of them). A list
        // the app cannot honour — an unknown word, `none` beside another, a list with no tier
        // to apply it to — is refused here, never silently booted as something else.
        const QStringList needs = testtier::needs();
        if (!needs.isEmpty()) {
            QString err = testtier::needsError(needs);
            if (err.isEmpty() && !testtier::active())
                err = QStringLiteral("a needs list applies to a test tier, and there is none "
                                     "(--test-tier / %1)").arg(QLatin1String(testtier::kEnvVar));
            if (!err.isEmpty()) {
                std::fprintf(stderr, "Jahshaka: %s: %s\n", testtier::kNeedsEnvVar, qPrintable(err));
                return 2;
            }
        }
    }
    cli.applyPlatformPolicy();
    // WHETHER A PERSON IS LOOKING AT THIS PROCESS, recorded once for everything
    // that has no CliOptions to ask with (app/firstrun.h): an error box raised
    // deep in a page is unanswerable in a driven run and spends the run's whole
    // budget waiting to be dismissed.
    FirstRun::rememberDriven(FirstRun::isDrivenSession(cli));

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
    // library database, the asset store, the shader cache — asks
    // AppPaths, so `--data-root <dir>` / `JAHSHAKA_DATA_ROOT` redirects the
    // whole set together. With neither given, every path is exactly where it
    // has always been.
    //
    // It has to be after QApplication (settingsFilePath reads
    // applicationDirPath) and before the log.
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
    // THE --no-ray-query OVERRIDE (SPECS/PHOTON_SPEC.md §7 R1), before any
    // engine exists. It sets the ENGINE CONFIG for this run through a process
    // latch EngineHost::resolveConfig reads — not the persisted preference,
    // which is the user's and which a test run must not rewrite (the engine
    // carries the config to the device itself).
    if (cli.noRayQuery) setCliNoRayQuery(true);
    // THE VR LAUNCH LATCH (SPECS/VR_SPEC.md §4.1, VR-START-1), before any engine
    // exists, like the ray latch above. THE ONE PRECEDENCE: --vr / --no-vr, then
    // JAHSHAKA_VR=1 / 0 (the runners' form — every non-VR test launcher sets 0),
    // then the two VR preferences (Start in VR, ON by default; the headset
    // runtime, WiVRn by default). The boot is the same either way.
    {
        SettingsManager *sm = SettingsManager::getDefaultManager();
        const CliOptions::VrChoice vr = CliOptions::resolveVr(
            cli.vr, cli.noVr, qgetenv("JAHSHAKA_VR"), sm->get(settingkeys::startInVr));
        VrLaunch launch;
        launch.enabled = vr.enabled;
        launch.anyRuntime = vr.anyRuntime;
        launch.startCheck = vr.startCheck;
        launch.source = vr.source;
        launch.headsetRuntime = sm->get(settingkeys::vrHeadsetRuntime);
        setVrLaunch(launch);
    }
    // The funnel is what makes the ~61 existing qDebug/qWarning call sites land
    // in the file with zero edits to any of them — LoadTimeline's open profile,
    // the slow-frame warning, the watchdog's stall line, SceneMirror's skeleton
    // diagnostics. It CHAINS to the previous handler, so stderr is unchanged.
    JahLog::installQtMessageHandler();
    // And this is fork F5-A: every irisLog() call site gains a timestamp, a
    // category and rotation without one of them being edited.
    JahLog::absorbIrisLogger();

    // THE BAKE SEAM (SHIPPED-BAKES-1, services/assetmetadata.h): a library
    // row's metadata backfill describes its current BAKE — a model's carries
    // the facts of the import's parse, a clip's its clip table — and never
    // parses. The lookup is a catalog query that lives in MeshBakeStore; wired
    // here so the describe service stays linkable on its own.
    AssetMetadata::setBakePathResolver(&MeshBakeStore::currentBakePath);

    // Apply the app theme (Qlementine Dark by default, archived Classic on
    // request) BEFORE any widget exists. See THEME_AUDIT.md §4.
    ThemeManager::applyAtStartup(app);

    app.setWindowIcon(QIcon(":/images/icon.ico"));

    auto dataPath = AppPaths::dataRoot();
    QDir dataDir(dataPath);
    if (!dataDir.exists()) dataDir.mkpath(dataPath);

    // --clear-shader-cache: our r.InvalidateCachedShaders. It runs HERE, before
    // MainWindow starts the engine, and the run then continues normally — which
    // is exactly what a cold-start benchmark needs and what a user with a
    // suspect cache needs (SHADER_CACHE_SPEC §4.5).
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

    // THE LIBRARY GENERATION (FORWARD-ONLY-1, services/librarygeneration.h).
    // There are no migrations: a library that is not this build's (its
    // generation or its tables) is WIPED through the one reset, after the store
    // bootstrap above so the reset removes the store this run uses. A data root
    // another instance holds is REFUSED: this process exits rather than delete
    // that instance's files.
    {
        const librarygeneration::Result gen = librarygeneration::checkAndWipe(
            IrisUtils::join(AppPaths::dataRoot(), Constants::JAH_DATABASE), AppPaths::dataRoot(),
            SettingsManager::getDefaultManager());
        if (gen.outcome == librarygeneration::Outcome::Refused
            || gen.outcome == librarygeneration::Outcome::Failed) {
            std::fprintf(stderr, "Jahshaka: the library cannot be opened: %s\n",
                         qUtf8Printable(gen.reason));
            irisLog(QStringLiteral("library: REFUSED — %1").arg(gen.reason));
            // ON SCREEN for a person (D6); never a modal on a driven/rig run.
            if (!FirstRun::isDrivenSession(cli))
                QMessageBox::critical(nullptr, QObject::tr("Jahshaka"),
                                      librarygeneration::refusalText(gen));
            JahLog::stop(QStringLiteral("library refused, exit code 4"));
            return 4;
        }
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
        // Every key through its ONE owner (STUDIO-CRUD-1 item 10): this block
        // used to re-type six keys and their defaults.
        auto yesNo = [](bool on) { return on ? QStringLiteral("true") : QStringLiteral("false"); };
        r << SessionHeader::Row { QStringLiteral("theme"),
                                  sm->getValue(ThemeManager::settingsKey(),
                                               ThemeManager::defaultThemeId()).toString() };
        r << SessionHeader::Row { QStringLiteral("pacing"),
                                  sm->getValue(QString::fromLatin1(framepacing::settingsKey()),
                                               framepacing::modeName(framepacing::Mode::Display))
                                      .toString() };
        r << SessionHeader::Row { QStringLiteral("watchdog"),
                                  yesNo(sm->get(settingkeys::watchdogEnabled)) };
        r << SessionHeader::Row { QStringLiteral("shadowMeshOptimization"),
                                  yesNo(sm->get(settingkeys::shadowMeshOptimization)) };
        r << SessionHeader::Row { QStringLiteral("shaderWarmupSamples"),
                                  QString::number(sm->get(settingkeys::shaderWarmupSamples)) };
        r << SessionHeader::Row { QStringLiteral("shaderWarmupShadows"),
                                  yesNo(sm->get(settingkeys::shaderWarmupShadows)) };
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

    splash.showMessage(QString("Revision - %1 %2").arg(GIT_COMMIT_HASH).arg(GIT_COMMIT_DATE),
                       Qt::AlignBottom | Qt::AlignLeft, QColor(255, 255, 255));

    splash.updateVersion(Constants::CONTENT_VERSION);

    splash.show();

    app.processEvents();

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
    holdSplashForShaderBuild(app, splash, window.database());
    // ...and from here a compile on the UI thread outside a compile dialog is a
    // named defect (services/livecompiles.h, SHADER-WARM-2).
    if (auto engine = EngineHost::instance().engine()) {
        livecompiles::arm([weak = std::weak_ptr<jahshaka::engine::Engine>(engine)]() {
            auto e = weak.lock();
            if (!e) return 0u;
            // ONLY WHAT A FRAME WAITED FOR (ASYNC-SHADERS-1): the shaders compiled off the
            // background compiler's threads, ONE counter read once. It used to be compiled -
            // compiledInBackground, two loads that a service-thread compile could land between
            // (the difference dipped by one and came back: a phantom live compile, measured as
            // shader.sample_opens_quiet's "1 shader(s)" under load).
            return e->asyncShaderStats().compiledInForeground;
        });
        // AFTER THE STARTUP GATE, THE EDITOR COMPILES IN THE BACKGROUND (ASYNC-SHADERS-1):
        // an interactive session's view never waits for a shader outside an open's dialog.
        // A scripted run, an MCP session (the suites and the rig drive the app through it)
        // and the engine selftest keep every frame a complete picture: their pixel reads
        // must never meet a placeholder (app.sky_swap_presented did). A session asks for
        // the background path with app.setAsyncShaders(true); the selftest asserts it drew
        // no placeholder.
        livecompiles::setAsyncPolicy(!cli.isScriptRun() && cli.selftestPng.isEmpty() && !cli.mcpServe);
    }
    // From now on any NEW compile burst (a scene open, a material edit) is
    // written to disk a few seconds after it settles, so a crash costs at most
    // that much recompilation.
    EngineHost::instance().startShaderCacheWatchdog();

    // Hide the splash as soon as the window shows — including the CLI paths
    // below (script/MCP runs show the window themselves). A splash left
    // visible counts as a window and blocks quitOnLastWindowClosed: the app
    // then survives its own main window (the headless-zombie bug family).
    splash.finish(&window);

    // --engine-selftest releases the engine host itself, through the RAII guard
    // in selftestrunner.cpp, and writes its own close bracket here.
    if (!cli.selftestPng.isEmpty()) {
        const int rc = runEngineSelftest(window, app, cli.selftestPng);
        JahLog::stop(QStringLiteral("engine selftest, exit code %1").arg(rc));
        return rc;
    }

    // --dump-api-docs GOES THROUGH THE ORDERED EXIT (ledger 150, round 2). It
    // used to return straight out with a live EngineHost, leaving the engine to
    // be torn down by the exit-handler chain — after Qt, after the database and
    // after the engine library's own statics, which is the shape that killed a
    // run in TextureCache::save. finalizeAppExit records step 4, stops the
    // watchdog and the monitor, releases the host and writes the close bracket.
    if (!cli.dumpDocsPath.isEmpty())
        return finalizeAppExit(runDumpApiDocs(window, cli.dumpDocsPath));

    if (!cli.scriptPath.isEmpty())
        return runScriptFile(window, app, cli.scriptPath, cli.headlessScript, cli.liveScript);

    if (!cli.poolScripts.isEmpty())
        return runScriptPool(window, app, cli.poolScripts, cli.poolName, cli.poolArms, cli.poolBaseline,
                             cli.headlessScript, cli.liveScript);

    if (cli.mcpServe)
        return runMcpServe(window, app, cli.mcpPort, cli.headlessScript);

    window.goToDesktop();   // splash.finish above hides the splash here

    // THE WIPE IS SAID ON SCREEN, once (FORWARD-ONLY-1): the projects the user
    // had are gone from the desktop, and a log line is not where they look.
    // Only the ordinary windowed launch reaches here (every CLI path returned
    // above); the scripted surface is app.libraryGeneration().
    // (Belt and braces with the CLI returns above: the ordinary windowed run a
    // rig starts with --data-root is DRIVEN too — D7 — and gets no modal.)
    if (librarygeneration::wipedAtStartup() && !FirstRun::isDrivenSession(cli)) {
        QMessageBox::information(&window, QObject::tr("Library reset"),
                                 librarygeneration::noticeText());
        // THE KEPT STORAGES' TILES COME BACK BY THEMSELVES (ASSETS-HOME-1): a
        // thumbnail is not in a sidecar, so the rows the bump rebuilt have none.
        // The library's own missing-thumbnail sweep runs once, queued after the
        // window is up, the event loop turning between assets as it always does.
        if (librarygeneration::lastResult().keptRows > 0)
            QTimer::singleShot(0, &window, [&window]() {
                if (AssetView *page = window.assetsPage()) page->rebuildMissingThumbnails(true);
            });
    }

    // FIRST LAUNCH, ONCE: the donate greeting (owner decision D3, 2026-09-12).
    // It used to run modally inside MainWindow::closeEvent — the last thing a
    // user saw on the way out, and a nested event loop inside the quit path.
    // It runs HERE instead: the window is up, nothing is closing, and every
    // DRIVEN way of starting this application (a suite, a script, MCP, the
    // selftest, an offscreen run) is excluded by ONE predicate in
    // app/firstrun.h. Note the CLI paths above all `return` before this line,
    // so the predicate is belt and braces for them — and load-bearing for the
    // ordinary windowed run a rig starts with `--data-root`.
    //
    // The dialog's CONTENT is untouched by this move.
    if (FirstRun::shouldGreet(
            cli, SettingsManager::getDefaultManager()
                     ->getValue("ddialog_seen", "false").toBool())) {
        DonateDialog greeting(&window);
        greeting.updateVersion(Constants::CONTENT_VERSION);
        greeting.exec();
        // Shown is seen, whether or not the user ticked the box: a greeting
        // that returns every launch until it is acknowledged is nagware.
        SettingsManager::getDefaultManager()->setValue("ddialog_seen", true);
    }

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


	// GATED, AND OFF BY DEFAULT (owner, 2026-09-18: "leave auto updates for
	// when we have an update server"). This ran on EVERY launch — a request to
	// Constants::UPDATE_CHECK_URL before there is a server to answer it —
	// while the Preferences checkbox that claims to govern it wrote a
	// different key entirely and governed nothing (SMOKE-FIX-1's fix round).
	// The decision is one function on the checker so that "does a default
	// launch touch the network?" has one answer and a test.
	updateChecker.checkForAppUpdateIfEnabled(
	    SettingsManager::getDefaultManager()->getValue(
	        UpdateChecker::kAutomaticChecksKey, UpdateChecker::kAutomaticChecksDefault));

	// Tooltips: Classic's own popup (ToolTipHelper), or the Qlementine style's
	// native tooltip with "Header | body" rendered as rich text.
	if (ThemeManager::classicActive())
		app.installEventFilter(new ToolTipHelper());
	else
		app.installEventFilter(new NativeToolTipFormatter(&app));

    const int rc = app.exec();
    return finalizeAppExit(rc);
}
