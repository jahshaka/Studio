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
#include "ui/dialogs/infodialog.h"
#include "scripting/scriptengine.h"
#include "data/constants.h"
#include "app/updatechecker.h"
#include "app/upgrader.h"
#include "ui/dialogs/softwareupdatedialog.h"
#include "ui/controls/tooltip.h"
#include "app/versionsplashscreen.h"


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

// --engine-selftest <out.png>
//
// The self-verification path for the engine viewport (VIEWPORT_MIGRATION_PLAN.md
// step 6): MainWindow built normally in engine mode, its default scene created and
// set on the viewport exactly as newScene() does at runtime, ~30 frames pumped, one
// offscreen screenshot saved. Exit 0 iff the image exists and its centre pixel is
// not the clear colour (0.10, 0.11, 0.14) — i.e. the ground plane rendered.
static int runEngineSelftest(MainWindow &window, QApplication &app, const QString &outPng)
{
    window.show();
    app.processEvents();

    QString why;
    if (!window.beginEngineSelftest(why)) {
        std::fprintf(stderr, "engine-selftest: %s\n", qPrintable(why));
        return 1;
    }

    // Pump the render loop for ~30 frames (the driver ticks every 16 ms).
    QElapsedTimer clock;
    clock.start();
    // Resize twice on the way (the layout does this to the viewport in real use):
    // the engine must survive a swapchain rebuild without a stale depth buffer.
    for (int frame = 0; frame < 40; ++frame) {
        if (frame == 10) window.resize(1100, 760);
        if (frame == 25) window.resize(700, 520);
        app.processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(16);
    }
    app.processEvents();

    QImage img = window.viewport()->takeScreenshot(256, 256);
    if (img.isNull()) {
        std::fprintf(stderr, "engine-selftest: takeScreenshot returned a null image after %lld ms\n",
                     static_cast<long long>(clock.elapsed()));
        return 1;
    }
    if (!img.save(outPng, "PNG")) {
        std::fprintf(stderr, "engine-selftest: could not save %s\n", qPrintable(outPng));
        return 1;
    }
    const QColor centre = img.pixelColor(img.width() / 2, img.height() / 2);
    const QColor clear = QColor::fromRgbF(0.10f, 0.11f, 0.14f);
    const int tolerance = 2;
    const bool differs = qAbs(centre.red() - clear.red()) > tolerance ||
                         qAbs(centre.green() - clear.green()) > tolerance ||
                         qAbs(centre.blue() - clear.blue()) > tolerance;
    std::fprintf(stderr, "engine-selftest: %dx%d image, centre pixel (%d,%d,%d), clear (%d,%d,%d) -> %s\n",
                 img.width(), img.height(), centre.red(), centre.green(), centre.blue(),
                 clear.red(), clear.green(), clear.blue(), differs ? "PASS" : "FAIL");
    window.endEngineSelftest();
    EngineHost::instance().shutdown();
    return differs ? 0 : 1;
}

// --script <file.js> [--headless]
//
// SCRIPTING_SPEC §3.2: the CLI script runner, cloned from the selftest boot.
// Engine mode (default): MainWindow shown, editor page + default scene up
// exactly as beginEngineSelftest does, a few frames pumped, then the script
// runs with the full verb surface. --headless: offscreen QPA, no engine —
// Document-class verbs only (Engine verbs throw catchable errors).
// console.log goes to stdout; errors go to stderr as file.js:line.
// Exit code: 1 on script error; else the script's numeric completion value
// (clamped 0-255) or 0.
static int runScriptFile(MainWindow &window, QApplication &app, const QString &path, bool headless)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        std::fprintf(stderr, "script: cannot open %s\n", qPrintable(path));
        return 1;
    }
    const QString source = QString::fromUtf8(file.readAll());

    window.show();
    app.processEvents();

    if (!headless) {
        QString why;
        if (!window.beginEngineSelftest(why)) {
            std::fprintf(stderr, "script: %s\n", qPrintable(why));
            return 1;
        }
        // Let the engine settle (swapchain, first frames) like the selftest does.
        for (int frame = 0; frame < 10; ++frame) {
            app.processEvents(QEventLoop::AllEvents, 50);
            QThread::msleep(16);
        }
    }

    ScriptEngine *engine = window.scripting();
    QObject::connect(engine, &ScriptEngine::consoleOutput, [](const QString &t) {
        std::fprintf(stdout, "%s\n", qPrintable(t));
        std::fflush(stdout);
    });

    const ScriptResult result = engine->evaluate(source, path);

    int rc = 0;
    if (!result.ok) {
        std::fprintf(stderr, "%s\n", qPrintable(result.toString()));
        if (!result.stack.isEmpty()) std::fprintf(stderr, "%s\n", qPrintable(result.stack));
        rc = 1;
    } else {
        const int typeId = result.value.typeId();
        if (typeId == QMetaType::Int || typeId == QMetaType::Double || typeId == QMetaType::LongLong)
            rc = qBound(0, result.value.toInt(), 255);
    }

    if (!headless) window.endEngineSelftest();
    EngineHost::instance().shutdown();
    return rc;
}

int main(int argc, char *argv[])
{
    GetGitCommitHash();

    // --engine-preview: start ONLY the engine preview dialog, no MainWindow.
    bool enginePreviewOnly = false;
    // --engine-selftest <out.png>: engine viewport, default scene, one screenshot, exit.
    QString selftestPng;
    // --script <file.js> [--headless]: run a script and exit (SCRIPTING_SPEC §3.2).
    // --dump-api-docs <file.md>: write the registry-generated verb reference and exit.
    QString scriptPath, dumpDocsPath;
    bool headlessScript = false;
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--engine-preview") == 0) enginePreviewOnly = true;
        else if (qstrcmp(argv[i], "--engine-selftest") == 0 && i + 1 < argc) selftestPng = QString::fromLocal8Bit(argv[++i]);
        else if (qstrcmp(argv[i], "--script") == 0 && i + 1 < argc) scriptPath = QString::fromLocal8Bit(argv[++i]);
        else if (qstrcmp(argv[i], "--headless") == 0) headlessScript = true;
        else if (qstrcmp(argv[i], "--dump-api-docs") == 0 && i + 1 < argc) dumpDocsPath = QString::fromLocal8Bit(argv[++i]);
        else if (qstrncmp(argv[i], "--viewport", 10) == 0) {
            // Accepted for compatibility; the engine viewport is the only
            // renderer since the legacy GL viewport was deleted (step 14).
            if (qstrcmp(argv[i], "--viewport=legacy") == 0)
                qWarning("--viewport=legacy: the legacy GL viewport was removed; using the engine viewport.");
        }
    }

    if ((headlessScript && !scriptPath.isEmpty()) || !dumpDocsPath.isEmpty())
        qputenv("QT_QPA_PLATFORM", "offscreen");

    // The engine (Ogre-Next) has no Wayland backend: xcb, always — unless the
    // user chose a platform themselves (or a headless run went offscreen above).
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "xcb");

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

    app.processEvents();
    //app.setOverrideCursor( QCursor( Qt::BlankCursor ) );

    // Create our main app window but hide it at the same time while showing the EDITOR first
    // Set the attribute to render invisible while running as normal then hiding it after
    // This is all to make SceneViewWidget's initializeGL trigger OR a way to force the UI to
    // update when hidden, either way we want the Desktop to be the opening widget (iKlsR)
    MainWindow window;

    if (!selftestPng.isEmpty())
        return runEngineSelftest(window, app, selftestPng);

    if (!dumpDocsPath.isEmpty()) {
        QFile docs(dumpDocsPath);
        if (!docs.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            std::fprintf(stderr, "dump-api-docs: cannot write %s\n", qPrintable(dumpDocsPath));
            return 1;
        }
        docs.write(window.scripting()->registry().markdown().toUtf8());
        std::fprintf(stderr, "dump-api-docs: wrote %s\n", qPrintable(dumpDocsPath));
        return 0;
    }

    if (!scriptPath.isEmpty())
        return runScriptFile(window, app, scriptPath, headlessScript);

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

    const int rc = app.exec();
    // The engine borrows Qt's X display: release it before QApplication goes away.
    EngineHost::instance().shutdown();
    return rc;
}
