/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/services.h"
#include "services/projectservice.h"
#include "scripting/modules/appapi.h"

#include "shell/mainwindow.h"
#include "ui/pages/projectmanager.h"
#include "services/engineerrorpump.h"
#include "services/loadtimeline.h"
#include "services/mainthreadheartbeat.h"
#include "services/mainthreadwatchdog.h"
#include "bridge/enginehost.h"
#include "viewport/enginerenderdriver.h"
#include <QDir>
#include <QFileInfo>

QVector<VerbInfo> AppApi::verbs() const
{
    return {
        { "desktop", "app.desktop(n=0) -> current",
          "Switches to desktop 1-4; app.desktop() just returns the current one.",
          Needs::Window },
        { "space", "app.space(name) -> bool",
          "Switches the main window space: desktop, player, editor, materials, assets, publish, avatar. player and editor need an open project.",
          Needs::Window },
        { "openTimings", "app.openTimings() -> [{stage, ms, items?, label?}]",
          "The millisecond ledger of the most recent scene open (services/loadtimeline.h): one entry per stage, "
          "the first entry being {stage:'total', ms, label}, plus 'counter:*' entries for the work that "
          "accumulates inside the stages (assimp parses, database sweeps, the engine push). Empty before "
          "the first open of the session.",
          Needs::Document },
        { "heartbeat", "app.heartbeat(intervalMs=250) -> bool",
          "Starts (or, with 0, stops) a main-thread heartbeat probe: a timer that ticks on the UI thread and "
          "records the WORST gap between ticks. The measurable definition of 'the window stayed responsive' — "
          "a blocked UI thread cannot tick. Restarting resets the statistics.",
          Needs::Window },
        { "heartbeatStats", "app.heartbeatStats() -> {running, intervalMs, ticks, maxGapMs, sinceLastTickMs}",
          "The heartbeat probe's readings (see app.heartbeat). maxGapMs is the longest the UI thread went "
          "without servicing its event loop since the probe started.",
          Needs::Window },
        { "watchdogStats", "app.watchdogStats() -> {supported, running, enabled, stallMs, reports, lastStallMs}",
          "The main-thread watchdog (services/mainthreadwatchdog.h): a thread of our own that polls the "
          "heartbeat's last-tick atomic and, when the UI thread has not ticked for stallMs, makes THAT thread "
          "print its own backtrace (a watchdog thread calling backtrace() would photograph itself). 'reports' "
          "counts the stalls this session reported — at most one per stall, capped and cooled down. A "
          "DEVELOPMENT-BUILD feature: 'supported' is false in a release build, and a dev build can still turn "
          "it off with the watchdog_enabled preference or --watchdog=off.",
          Needs::Window },
        { "blockUiThread", "app.blockUiThread(ms) -> bool",
          "Blocks the UI thread for ms milliseconds WITHOUT servicing the event loop — a deliberate freeze, so "
          "the watchdog and the heartbeat can be tested through the registry instead of through a private "
          "sleep. Development builds only: returns false and does nothing in a release build. The verb itself "
          "does not return until the freeze is over, which is the point.",
          Needs::Window },
        { "shaderCache", "app.shaderCache() -> {enabled, dir, fingerprint, sizeBytes, files, "
                         "pipelineCacheLoaded, microcodeLoaded, microcodeEntries, hlmsCachesLoaded, "
                         "compiledThisRun, loadedThisRun, expectedShaders, lastSaved}",
          "The persistent shader cache (SHADER_CACHE_SPEC.md): what is on disk and what this run "
          "did with it. compiledThisRun counts shaders the compiler actually built; loadedThisRun "
          "counts shaders served straight from the cache, so a warm launch shows the second number "
          "high and the first near zero. expectedShaders is what the last saved run needed in "
          "total — the startup progress counter's denominator, 0 before any cache has been "
          "written. The counters work whether or not the cache itself is enabled.",
          Needs::Engine },
        { "clearShaderCache", "app.clearShaderCache() -> bool",
          "Deletes every cached shader artifact. The running session is unaffected (its shaders are "
          "already in memory); the NEXT launch is cold. Our r.InvalidateCachedShaders — the same "
          "thing --clear-shader-cache does before the engine starts.",
          Needs::Engine },
        { "saveShaderCache", "app.saveShaderCache() -> bool",
          "Writes the shader cache now instead of waiting for the burst-settle watchdog or a clean "
          "quit. A no-op returning true when nothing new has been compiled. Mostly for tests: the "
          "app saves on its own.",
          Needs::Engine },
        { "warmUpSet", "app.warmUpSet(action?) -> {path, exists, sizeBytes, recorded?, saved?, built?}",
          "The recorded warm-up set (SHADER_CACHE_SPEC.md §2.7b) — this machine's list of the "
          "shader permutations previous sessions actually used. Not shaders and not SPIR-V: a list "
          "of vertex formats, render queues and one representative material each, which is why it "
          "is tiny and why it is the only cached artifact that is platform- and driver-independent. "
          "With no argument it reports. 'record' adds every live scene to the set and writes it; "
          "'apply' replays it, compiling every permutation against degenerate 4-vertex buffers so "
          "nothing is loaded from disk, and reports how many shaders that built. The app records on "
          "quit and applies at startup on its own; these are for tests and for recording a set "
          "deliberately from a scene built for the purpose.",
          Needs::Engine },
        { "engineErrors", "app.engineErrors(reset?) -> {drains, recorded, suppressed, entries:[{message, count, suppressed, firstMs, lastMs}]}",
          "What the renderer refused to do, and did not otherwise tell anyone. The engine swallows "
          "failures by design (every backend call is wrapped and returns a refusal value instead of "
          "throwing), and almost nothing checks those return values — so a texture that will not "
          "decode, a mesh with no tangents or a full decal atlas draw the wrong picture in silence. "
          "EngineErrorPump drains the engine's error sink once per rendered frame and this is its "
          "record: the distinct messages, newest first, with how often each occurred and how many "
          "repeats the 5-second rate limiter swallowed. drains counts the pump calls themselves, so "
          "a zero there means the frame loop is not running, not that the renderer is happy. Pass "
          "true to clear the record after reading it.",
          Needs::Document },
        { "frameStats", "app.frameStats() -> {running, intervalMs, ticks, rendered, skipped, enabledViews}",
          "What the ONE render loop (EngineRenderDriver) has been doing, cumulatively since the engine "
          "started. `ticks` counts timer fires, `rendered` the ticks that actually called the engine, "
          "`skipped` the ticks that had no enabled View to draw and therefore submitted nothing — sitting "
          "on a page with no viewport should advance `skipped` and leave `rendered` still. `enabledViews` "
          "is the engine's live answer to the same question the loop asks each tick. Note the scripted "
          "stepping verb editor.frame(n) bypasses the driver entirely, so it moves none of these.",
          Needs::Document },
        { "quit", "app.quit() -> bool",
          "Closes the main window through the normal close path (autosave/unsaved-changes rules apply, background work is shut down). The verb returns before the window actually closes.",
          Needs::Window },
    };
}

QVariantList AppApi::openTimings()
{
    return LoadTimeline::lastRun();
}

bool AppApi::heartbeat(int intervalMs)
{
    if (intervalMs <= 0) { MainThreadHeartbeat::stop(); return true; }
    MainThreadHeartbeat::start(intervalMs);
    return true;
}

QVariantMap AppApi::heartbeatStats()
{
    return MainThreadHeartbeat::stats();
}

QVariantMap AppApi::watchdogStats()
{
    return MainThreadWatchdog::stats();
}

bool AppApi::blockUiThread(int ms)
{
    if (!MainThreadWatchdog::isSupported())
        return fail("app.blockUiThread: development builds only");
    if (ms <= 0) return fail("app.blockUiThread: a positive duration is required");
    return MainThreadWatchdog::blockCallingThread(ms);
}

QVariantMap AppApi::shaderCache()
{
    QVariantMap m;
    auto engine = EngineHost::instance().engine();
    if (!engine) { m["enabled"] = false; return m; }
    const jahshaka::engine::ShaderCacheStats s = engine->shaderCacheStats();
    m["enabled"]             = s.enabled;
    m["dir"]                 = QString::fromStdString(s.dir);
    m["fingerprint"]         = QString::fromStdString(s.fingerprint);
    m["sizeBytes"]           = QVariant::fromValue(qulonglong(s.sizeBytes));
    m["files"]               = s.files;
    m["pipelineCacheLoaded"] = s.pipelineCacheLoaded;
    m["microcodeLoaded"]     = s.microcodeLoaded;
    m["microcodeEntries"]    = s.microcodeEntries;
    m["hlmsCachesLoaded"]    = s.hlmsCachesLoaded;
    m["compiledThisRun"]     = s.compiledThisRun;
    m["loadedThisRun"]       = s.loadedThisRun;
    m["expectedShaders"]     = s.expectedShaders;
    m["lastSaved"]           = QVariant::fromValue(qlonglong(s.lastSavedUnixMs));
    return m;
}

bool AppApi::clearShaderCache()
{
    // Two halves on purpose: the engine drops what it wrote, and the host
    // removes the directory itself — clearing must work in a session whose
    // engine never started (a headless run) exactly as in one where it did.
    bool ok = EngineHost::clearShaderCacheOnDisk();
    if (auto engine = EngineHost::instance().engine()) ok = engine->clearShaderCache() && ok;
    return ok;
}

bool AppApi::saveShaderCache()
{
    auto engine = EngineHost::instance().engine();
    if (!engine) return fail("app.saveShaderCache: the engine is not running");
    return engine->saveShaderCache();
}

QVariantMap AppApi::warmUpSet(const QString &action)
{
    QVariantMap m;
    const QString path = EngineHost::warmUpSetPath();
    m["path"] = path;
    auto engine = EngineHost::instance().engine();
    const QString what = action.trimmed().toLower();
    if (!what.isEmpty() && !engine) { fail("app.warmUpSet: the engine is not running"); return m; }

    if (what == QLatin1String("record")) {
        const bool recorded = engine->recordWarmUpSet();
        m["recorded"] = recorded;
        if (recorded) {
            QDir().mkpath(QFileInfo(path).absolutePath());
            m["saved"] = engine->saveWarmUpSet(path.toStdString());
        }
    } else if (what == QLatin1String("apply")) {
        // Needs a scene to host the degenerate renderables. The editor's own
        // scene is the natural one and leaves nothing behind (createWarmUp /
        // destroyWarmUp are paired inside the engine).
        m["built"] = 0u;
        if (!QFileInfo::exists(path)) return fail("app.warmUpSet: no set recorded yet"), m;
        m["built"] = engine->applyWarmUpSet(path.toStdString(), nullptr);
    } else if (!what.isEmpty()) {
        return fail(QStringLiteral("app.warmUpSet: unknown action '%1' (record, apply)").arg(action)), m;
    }

    const QFileInfo info(path);
    m["exists"] = info.exists();
    m["sizeBytes"] = QVariant::fromValue(qulonglong(info.exists() ? info.size() : 0));
    return m;
}

bool AppApi::quit()
{
    if (!host.mainWindow) return fail("app: not available in this session");
    // Deferred: let the calling script (and its undo macro) finish first.
    QMetaObject::invokeMethod(host.mainWindow, [w = host.mainWindow]() { w->close(); },
                              Qt::QueuedConnection);
    return true;
}

int AppApi::desktop(int n)
{
    if (!host.projectManager) { fail("app: not available in this session"); return 0; }
    if (n >= 1) host.projectManager->switchDesktop(n);
    return host.projectManager->getCurrentDesktop();
}

bool AppApi::space(const QString &name)
{
    if (!host.mainWindow) return fail("app: not available in this session");
    const QString s = name.trimmed().toLower();
    WindowSpaces space;
    if (s == "desktop")                          space = WindowSpaces::DESKTOP;
    else if (s == "player")                      space = WindowSpaces::PLAYER;
    else if (s == "editor")                      space = WindowSpaces::EDITOR;
    else if (s == "materials" || s == "effects") space = WindowSpaces::EFFECT;
    else if (s == "assets")                      space = WindowSpaces::ASSETS;
    else if (s == "publish")                     space = WindowSpaces::PUBLISH;
    else if (s == "avatar")                      space = WindowSpaces::AVATAR;
    else return fail(QStringLiteral("app.space: unknown space '%1' (desktop, player, editor, materials, assets, publish, avatar)").arg(name));

    const bool sceneOpen = host.services && host.services->project && host.services->project->isSceneOpen();
    if ((space == WindowSpaces::PLAYER || space == WindowSpaces::EDITOR) && !sceneOpen)
        return fail(QStringLiteral("app.space: '%1' needs an open project").arg(s));

    host.mainWindow->switchSpace(space);

    // audit D15: the verb once reported success while the page stayed put —
    // never claim a switch the window didn't make
    if (host.mainWindow->getWindowSpace() != space)
        return fail(QStringLiteral("app.space: the window refused to switch to '%1'").arg(s));
    return true;
}

QVariantMap AppApi::engineErrors(bool reset)
{
    // No engine guard: the pump's record outlives any particular engine and the
    // most useful moment to read it is exactly when the renderer has fallen
    // over. Needs::Document for the same reason.
    const QVariantMap out = EngineErrorPump::instance().report();
    if (reset) EngineErrorPump::instance().reset();
    return out;
}

QVariantMap AppApi::frameStats()
{
    // No engine guard, same reasoning as engineErrors: "the loop is not running"
    // is one of the answers this verb exists to give, so it must be readable
    // when there is no engine at all.
    QVariantMap out;
    EngineRenderDriver *driver = EngineHost::instance().driver();
    const EngineRenderDriver::Stats s = driver ? driver->stats() : EngineRenderDriver::Stats{};
    out.insert("running", driver ? driver->isRunning() : false);
    out.insert("intervalMs", driver ? driver->intervalMs() : 0);
    out.insert("ticks", QVariant::fromValue(s.ticks));
    out.insert("rendered", QVariant::fromValue(s.rendered));
    out.insert("skipped", QVariant::fromValue(s.skipped));
    auto engine = EngineHost::instance().engine();
    out.insert("enabledViews", engine ? engine->hasEnabledViews() : false);
    return out;
}
