/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// perf.capture_bundle — THE CAPTURE BUNDLE IS COMPLETE AND CORRECT
// (RENDER_LOOP_MONITOR_SPEC §4.8 and §7's rewritten acceptance).
//
// DATA INTEGRITY ONLY. Nothing here asserts a performance number, a budget or a
// verdict — the monitor judges nothing (owner D5, 2026-09-12) and neither does
// its suite. What IS asserted is everything a capture the lead reads days later
// has to be able to promise:
//
//   * every file of the bundle exists and PARSES (machine.json,
//     snapshot_start/end.json, frames.jsonl, events.jsonl, trace.json, ogre.log);
//   * each frame's PASS LIST SUMS to that frame's draw count, on every frame
//     that was not replayed (Ogre replays unchanged command buffers and only
//     feeds its metrics on the build path, so a run of zeroes is "replayed",
//     never "empty" — the bundle marks those and the sum is asserted where it
//     means something);
//   * the snapshots NAME EVERY LIVE WORKSPACE, with the scene each renders and
//     the passes of each node — the compositor graph, which nothing else in the
//     app can see;
//   * the host's own stage tree reached the records (a frame's stages include
//     the mirror and the gap), and the frame causes are real (a scripted frame
//     says so);
//   * events carry their CAUSE: the mark the script dropped, the GI refresh it
//     asked for and the shader compile the fog switch forced are all there;
//   * the TRUNCATION NOTE IS ACCURATE — this capture fits, so it must say
//     complete with zero dropped records, and the byte count it states must
//     match the files on disk;
//   * an EARLY STOP (the script never waits out its 30 s) still writes all of
//     the above.
//
// The app half is capture_bundle.js. The two are split because the scripting
// engine has no file access, on purpose.
//
// PHASE 2 (CLEANUP-1 item 1): A WINDOW CLOSE MID-CAPTURE STILL WRITES THE
// BUNDLE. The owner's whole workflow is "press Ctrl+F4, do the thing that feels
// wrong, tell the lead where the bundle is" — and closing the window was a way
// to lose it: FrameMonitor::stop() was called from exactly two places (the
// --script runner and the Ctrl+F4 toggle) and MainWindow::shutdownBackgroundWork
// was not one of them, so finish() never ran on the path a user actually takes.
// This phase spawns the real binary over MCP, starts a long capture, and quits
// through app.quit() — the ordinary close — then reads the bundle the same way
// phase 1 does.
#include "mcpharness.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QThread>
#include <cstdio>

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

namespace {

QJsonDocument readJson(const QString &path, bool *ok)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { *ok = false; return {}; }
    QJsonParseError err {};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    *ok = err.error == QJsonParseError::NoError;
    if (!*ok) std::printf("      parse error in %s: %s\n", qPrintable(path),
                          qPrintable(err.errorString()));
    return doc;
}

QList<QJsonObject> readJsonl(const QString &path, bool *ok)
{
    QList<QJsonObject> out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { *ok = false; return out; }
    *ok = true;
    while (!f.atEnd()) {
        const QByteArray line = f.readLine().trimmed();
        if (line.isEmpty()) continue;
        QJsonParseError err {};
        const QJsonDocument doc = QJsonDocument::fromJson(line, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            std::printf("      unparseable line in %s: %s\n", qPrintable(path),
                        qPrintable(err.errorString()));
            *ok = false;
            continue;
        }
        out.append(doc.object());
    }
    return out;
}

}   // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    const QString home = QDir::current().absoluteFilePath(QStringLiteral("e2e-home-capture_bundle"));
    const QString bundle = QStringLiteral(JAH_TEST_BUNDLE);
    QDir().mkpath(home + "/run");
    // A capture must be able to CREATE its bundle directory, so the previous
    // run's is removed rather than reused.
    QDir(bundle).removeRecursively();
    // A COLD SHADER CACHE, every run. The script forces a compile inside the
    // capture window (fog is an Hlms piece: switching it on recompiles every
    // material), and a warm cache from the previous run would load that
    // permutation instead of compiling it — the suite would then be asserting
    // the state of a cache rather than the contents of a bundle. This is the
    // "gate red, solo green can be cache state" class (CLAUDE.md, 2026-09-11)
    // closed at the source.
    QDir(bundle + "-capped").removeRecursively();
    QDir(home + "/.local/share/Jahshaka/shadercache").removeRecursively();
    QDir(home + "/cache").removeRecursively();

    QProcess proc;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("HOME", home);
    env.insert("JAHSHAKA_DATA_ROOT", home + "/.local/share/Jahshaka");
    env.insert("XDG_CACHE_HOME", home + "/cache");
    // THE CAPTURE ROOT for this run. `out` is confined to it (a capture creates
    // directories and truncates files in whatever it is pointed at), so a suite
    // that writes bundles into its own scratch home says where that is.
    env.insert("JAHSHAKA_PERF_ROOT", QFileInfo(bundle).absolutePath());
    proc.setProcessEnvironment(env);
    proc.setWorkingDirectory(home + "/run");
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start(QStringLiteral(JAHSHAKA_BINARY),
               QStringList() << "--script" << QStringLiteral(JAH_TEST_SCRIPT));
    if (!proc.waitForStarted(20000)) { std::printf("FAIL: app did not start\n"); return 1; }
    if (!proc.waitForFinished(300000)) {
        std::printf("FAIL: app did not exit\n");
        proc.kill(); proc.waitForFinished(5000);
        return 1;
    }
    const QString out = QString::fromUtf8(proc.readAll());
    CHECK(proc.exitCode() == 0, "the capture script ran to completion");

    QJsonObject expect;
    for (const QString &line : out.split('\n')) {
        const int at = line.indexOf(QStringLiteral("CAPTUREBUNDLE "));
        if (at >= 0) expect = QJsonDocument::fromJson(line.mid(at + 14).toUtf8()).object();
    }
    if (expect.isEmpty() || proc.exitCode() != 0)
        std::printf("---- app output ----\n%s\n--------------------\n", qPrintable(out));
    CHECK(!expect.isEmpty(), "the script reported what it recorded");

    // ---- every file present -------------------------------------------------
    const QStringList files { "machine.json", "snapshot_start.json", "snapshot_end.json",
                              "frames.jsonl", "events.jsonl", "trace.json", "ogre.log" };
    for (const QString &name : files) {
        const bool exists = QFile::exists(QDir(bundle).filePath(name));
        CHECK(exists, qPrintable(QStringLiteral("bundle has %1").arg(name)));
    }

    // ---- machine.json: provenance, and an HONEST cap ------------------------
    bool ok = false;
    const QJsonObject machine = readJson(QDir(bundle).filePath("machine.json"), &ok).object();
    CHECK(ok && !machine.isEmpty(), "machine.json parses");
    CHECK(!machine.value("studioCommit").toString().isEmpty(), "machine.json names the Studio commit");
    CHECK(!machine.value("irisglCommit").toString().isEmpty(), "machine.json names the irisgl commit");
    CHECK(!machine.value("ogrePin").toString().isEmpty(), "machine.json names the Ogre pin");
    CHECK(machine.value("ogrePatchStack").toArray().size() > 20,
          "machine.json lists the Ogre patch stack");
    CHECK(machine.value("session").toObject().contains(QStringLiteral("build id")),
          "machine.json carries the session header (build id)");
    CHECK(machine.value("display").toObject().value("width").toInt() > 0,
          "machine.json records the display size");
    const QJsonObject capture = machine.value("capture").toObject();
    CHECK(capture.value("stoppedEarly").toBool(), "the bundle records that the stop was early");
    CHECK(capture.value("framesWritten").toDouble() > 0.0, "machine.json counts the frames written");
    const QJsonObject truncation = machine.value("truncation").toObject();
    CHECK(truncation.value("complete").toBool(true),
          "the truncation note says the bundle is complete");
    CHECK(truncation.value("frameRecordsDropped").toDouble() == 0.0
              && truncation.value("eventRecordsDropped").toDouble() == 0.0
              && !truncation.value("ogreLogTruncated").toBool(),
          "nothing was dropped, and the note agrees");
    // AND THE NOTE IS TRUE, not just present: the bytes it claims are the bytes
    // on disk (the whole point of an honest cap).
    qint64 onDisk = 0;
    for (const QString &name : { QStringLiteral("frames.jsonl"), QStringLiteral("events.jsonl"),
                                 QStringLiteral("trace.json"), QStringLiteral("ogre.log") })
        onDisk += QFileInfo(QDir(bundle).filePath(name)).size();
    const double claimed = truncation.value("bytesWritten").toDouble();
    CHECK(claimed > 0 && qAbs(claimed - double(onDisk)) <= double(onDisk) * 0.05 + 256.0,
          qPrintable(QStringLiteral("the cap's byte count matches the files (%1 claimed, %2 on disk)")
                         .arg(claimed).arg(onDisk)));

    // ---- the snapshots: the compositor graph --------------------------------
    for (const QString &name : { QStringLiteral("snapshot_start.json"),
                                 QStringLiteral("snapshot_end.json") }) {
        const QJsonObject snap = readJson(QDir(bundle).filePath(name), &ok).object();
        CHECK(ok && !snap.isEmpty(), qPrintable(QStringLiteral("%1 parses").arg(name)));
        CHECK(!snap.value("scene").toString().isEmpty(),
              qPrintable(QStringLiteral("%1 names its scene").arg(name)));
        const QJsonArray workspaces = snap.value("workspaces").toArray();
        CHECK(!workspaces.isEmpty(),
              qPrintable(QStringLiteral("%1 lists the live workspaces (%2)")
                             .arg(name).arg(workspaces.size())));
        // EVERY workspace, not just some: a named workspace with no scene or no
        // passes is a hole in the graph, and the graph is what a capture is
        // read for.
        int complete = 0, withPasses = 0;
        for (const QJsonValue &v : workspaces) {
            const QJsonObject w = v.toObject();
            if (w.value("name").toString().isEmpty() || w.value("scene").toString().isEmpty())
                continue;
            ++complete;
            for (const QJsonValue &nv : w.value("nodes").toArray())
                if (!nv.toObject().value("passes").toArray().isEmpty()) { ++withPasses; break; }
        }
        CHECK(complete == workspaces.size(),
              qPrintable(QStringLiteral("%1: every workspace names itself and its scene").arg(name)));
        CHECK(withPasses == workspaces.size(),
              qPrintable(QStringLiteral("%1: every workspace's nodes carry passes").arg(name)));
        // The probes each own a workspace, so the graph must be at least as big
        // as the probe grid plus the view's own.
        const int probes = expect.value("probes").toInt();
        CHECK(workspaces.size() >= probes,
              qPrintable(QStringLiteral("%1 covers the %2 reflection probes").arg(name).arg(probes)));
        CHECK(!snap.value("shadow").toObject().isEmpty(),
              qPrintable(QStringLiteral("%1 carries the shadow setup").arg(name)));
        CHECK(snap.value("lights").toArray().size() > 0,
              qPrintable(QStringLiteral("%1 carries the light list").arg(name)));
    }

    // ---- frames.jsonl -------------------------------------------------------
    const QList<QJsonObject> frames = readJsonl(QDir(bundle).filePath("frames.jsonl"), &ok);
    CHECK(ok, "every line of frames.jsonl parses");
    CHECK(frames.size() > 5, qPrintable(QStringLiteral("frames.jsonl has records (%1)")
                                            .arg(frames.size())));
    CHECK(double(frames.size()) == capture.value("framesWritten").toDouble(),
          "machine.json's frame count is the number of records in frames.jsonl");

    int sumsChecked = 0, replayed = 0, scripted = 0, withStages = 0, withHostStage = 0;
    bool everyPassNamed = true, everySumRight = true, everyFrameHasReason = true;
    for (const QJsonObject &f : frames) {
        const QJsonArray passes = f.value("passes").toArray();
        int draws = 0;
        for (const QJsonValue &pv : passes) {
            const QJsonObject p = pv.toObject();
            draws += p.value("draws").toInt();
            if (p.value("workspace").toString().isEmpty() || !p.contains(QStringLiteral("bucket")))
                everyPassNamed = false;
        }
        if (f.value("replayed").toBool()) { ++replayed; continue; }
        if (draws != f.value("draws").toInt()) everySumRight = false;
        if (!passes.isEmpty()) ++sumsChecked;
        if (f.value("cause").toString() == QLatin1String("scripted")) ++scripted;
        if (!f.value("stages").toArray().isEmpty()) ++withStages;
        // THE STAGE TREE IS ONE TREE, which is the whole point of pushing the
        // host's stages through the engine: a frame record must carry the
        // host's own stages (the viewport's sync), the MIRROR's sub-stages
        // (which only the mirror can see) and the ENGINE's — in one list, in
        // order. A frame with only the engine's half is the defect this counts.
        bool host = false, mirror = false, engine = false;
        for (const QJsonValue &sv : f.value("stages").toArray()) {
            const QString name = sv.toObject().value("name").toString();
            if (name.startsWith(QLatin1String("host."))) host = true;
            else if (name.startsWith(QLatin1String("mirror."))) mirror = true;
            else if (name.startsWith(QLatin1String("engine."))) engine = true;
        }
        if (host && mirror && engine) ++withHostStage;
        if (!f.contains(QStringLiteral("cause"))) everyFrameHasReason = false;
        for (const QJsonValue &wv : f.value("cacheWork").toArray())
            if (wv.toObject().value("reason").toString().isEmpty()) everyFrameHasReason = false;
    }
    CHECK(everySumRight && sumsChecked > 0,
          qPrintable(QStringLiteral("every non-replayed frame's passes sum to its draw count "
                                    "(%1 frames checked, %2 replayed)")
                         .arg(sumsChecked).arg(replayed)));
    CHECK(everyPassNamed, "every pass names its workspace and its bucket");
    CHECK(scripted > 0, qPrintable(QStringLiteral("the scripted frames say so (%1)").arg(scripted)));
    CHECK(withStages > 0, "frames carry a stage tree");
    CHECK(withHostStage == sumsChecked,
          qPrintable(QStringLiteral("every frame carries host + mirror + engine stages in ONE "
                                    "tree (%1 of %2 frames)").arg(withHostStage).arg(sumsChecked)));
    CHECK(everyFrameHasReason, "every cache-work entry carries a reason");

    // ---- events.jsonl: the causes -------------------------------------------
    const QList<QJsonObject> events = readJsonl(QDir(bundle).filePath("events.jsonl"), &ok);
    CHECK(ok, "every line of events.jsonl parses");
    bool mark = false, gi = false, compile = false, toast = false, startEvent = false;
    for (const QJsonObject &e : events) {
        const QString kind = e.value("kind").toString();
        const QString label = e.value("label").toString();
        if (label == QLatin1String("mark")
            && e.value("detail").toString() == QLatin1String("bundle-marker")) mark = true;
        if (kind.startsWith(QLatin1String("gi."))) gi = true;
        if (kind == QLatin1String("shader.compile")) compile = true;
        // THE START TOAST'S LIFETIME (owner, 2026-09-12): analysis has to know
        // which frames the one visible thing overlapped.
        if (label == QLatin1String("toast") && e.value("ms").toDouble() > 0.0) toast = true;
        if (label == QLatin1String("capture.start")) startEvent = true;
    }
    CHECK(mark, "perf.mark is in events.jsonl");
    CHECK(gi, "the GI refresh is in events.jsonl");
    CHECK(compile, "the shader compile is in events.jsonl");
    CHECK(toast, "the start toast is logged with its lifetime");
    CHECK(startEvent, "the capture's own start is an event, naming the bundle");

    // ---- trace.json ---------------------------------------------------------
    const QJsonDocument trace = readJson(QDir(bundle).filePath("trace.json"), &ok);
    CHECK(ok && trace.isArray(), "trace.json parses as a Chrome/Perfetto trace array");
    const QJsonArray traceEvents = trace.array();
    CHECK(traceEvents.size() > frames.size(),
          qPrintable(QStringLiteral("the trace has at least one event per frame (%1 for %2 frames)")
                         .arg(traceEvents.size()).arg(frames.size())));
    // PER TRACK, which is what the format requires: "X" events on one thread
    // must nest, never overlap, and the three tracks (frames, the reconstructed
    // stage strip, the reconstructed pass strip) are laid independently.
    bool monotonic = true, haveFrameEvent = false;
    QHash<int, double> trackEnd;
    for (const QJsonValue &v : traceEvents) {
        const QJsonObject e = v.toObject();
        const QString ph = e.value("ph").toString();
        if (ph == QLatin1String("M") || ph == QLatin1String("C")) continue;
        const int tid = e.value("tid").toInt();
        const double ts = e.value("ts").toDouble();
        if (ph == QLatin1String("X")) {
            if (ts < trackEnd.value(tid, -1.0) - 1.0) monotonic = false;
            trackEnd[tid] = ts + e.value("dur").toDouble();
        }
        if (e.value("name").toString().startsWith(QLatin1String("frame "))) haveFrameEvent = true;
    }
    CHECK(monotonic, "the trace's spans do not overlap on their own track");
    CHECK(haveFrameEvent, "the trace has frame spans");

    // ---- THE CAP IS HONEST (review item, lane MON-P1b) ----------------------
    // The script also recorded a bundle with a 64 KB ceiling. Everything it
    // wrote must still be readable, it must COUNT what it cut, and it must say
    // it is incomplete — the assertion the suite was missing, because `complete`
    // had only ever been asserted true.
    {
        const QString cappedDir = bundle + QStringLiteral("-capped");
        const QJsonObject cm = readJson(QDir(cappedDir).filePath("machine.json"), &ok).object();
        CHECK(ok && !cm.isEmpty(), "the capped bundle's machine.json parses");
        const QJsonObject ct = cm.value("truncation").toObject();
        CHECK(!ct.value("complete").toBool(true),
              "the capped bundle says it is NOT complete");
        const double cut = ct.value("frameRecordsDropped").toDouble()
                           + ct.value("eventRecordsDropped").toDouble()
                           + ct.value("traceRecordsDropped").toDouble();
        CHECK(cut > 0.0,
              qPrintable(QStringLiteral("...and counts what it cut (%1 records)").arg(cut)));
        bool linesOk = false;
        const QList<QJsonObject> cappedFrames =
            readJsonl(QDir(cappedDir).filePath("frames.jsonl"), &linesOk);
        CHECK(linesOk, "every line the capped bundle DID write still parses");
        CHECK(!cappedFrames.isEmpty(), "the capped bundle wrote frames before the cap bit");
        bool traceOk = false;
        const QJsonDocument cappedTrace = readJson(QDir(cappedDir).filePath("trace.json"), &traceOk);
        CHECK(traceOk && cappedTrace.isArray(),
              "the capped trace.json is still valid JSON (the separator guard)");
        for (const QString &name : files)
            CHECK(QFile::exists(QDir(cappedDir).filePath(name)),
                  qPrintable(QStringLiteral("capped bundle has %1").arg(name)));
    }

    // ---- the two toasts -----------------------------------------------------
    // TWO PER CAPTURE, and the script records two (the main bundle and the
    // size-capped one) — the refused captures must show NONE, which is what
    // makes four the assertion rather than "at least two".
    const QJsonObject lastToast = expect.value("toast").toObject();
    CHECK(lastToast.value("shown").toInt() == 4,
          qPrintable(QStringLiteral("the shell showed two toasts per capture and none for a "
                                    "refusal (%1 for 2 captures + 2 refusals)")
                         .arg(lastToast.value("shown").toInt())));
    CHECK(lastToast.value("text").toString().contains(bundle),
          "the second toast names the bundle's path");

    // ---- PHASE 2: THE WINDOW CLOSES WHILE A CAPTURE IS RUNNING -------------
    {
        using namespace shutdownharness;
        const QString quitHome =
            QDir::current().absoluteFilePath(QStringLiteral("e2e-home-capture_quit"));
        const QString quitRoot = quitHome + QStringLiteral("/perf");
        const QString quitBundle = quitRoot + QStringLiteral("/quit");
        QDir(quitRoot).removeRecursively();
        QDir().mkpath(quitHome + "/run");
        QDir().mkpath(quitRoot);

        // The spawned app's settings file follows JAHSHAKA_DATA_ROOT, and
        // seedSettingsForSpawnedApp mirrors that precedence from OUR
        // environment — so the variable goes into this process too, or the
        // auto_save key lands in a file the child will never read and the quit
        // stops on a modal prompt.
        qputenv("JAHSHAKA_DATA_ROOT", (quitHome + "/.local/share/Jahshaka").toUtf8());
        QDir().mkpath(quitHome + "/.local/share/Jahshaka");
        seedSettings(QStringLiteral(JAHSHAKA_BINARY));

        QProcess app2;
        QProcessEnvironment env2 = QProcessEnvironment::systemEnvironment();
        env2.insert("HOME", quitHome);
        env2.insert("JAHSHAKA_DATA_ROOT", quitHome + "/.local/share/Jahshaka");
        env2.insert("XDG_CACHE_HOME", quitHome + "/cache");
        env2.insert("JAHSHAKA_PERF_ROOT", quitRoot);
        app2.setProcessEnvironment(env2);
        app2.setWorkingDirectory(quitHome + "/run");

        QString token;
        QByteArray log;
        const quint16 port = freePort();
        const bool booted = spawn(app2, port, &token, &log);
        CHECK(booted && !token.isEmpty(), "phase 2: the app booted with an MCP endpoint");
        if (booted && !token.isEmpty()) {
            McpClient mcp;
            mcp.url = QUrl(QStringLiteral("http://127.0.0.1:%1/mcp").arg(port));
            mcp.token = token;
            mcp.initialize();

            mcp.runScript(QStringLiteral("project.create('CaptureQuit')"));
            mcp.runScript(QStringLiteral("scene.addPrimitive('cube', {position:{x:0,y:1,z:0}})"));
            // LONG, so the capture cannot possibly have auto-stopped on its own:
            // the only thing that can write this bundle is the close path.
            const QJsonObject started = mcp.runScript(
                QStringLiteral("perf.capture({seconds: 300, out: '%1', label: 'quit'})")
                    .arg(quitBundle));
            CHECK(started.value("result").toObject().value("started").toBool(),
                  "phase 2: a 300 s capture is running");
            mcp.runScript(QStringLiteral("editor.frame(8)"));
            CHECK(mcp.runScript(QStringLiteral("perf.status()"))
                      .value("result").toObject().value("recording").toBool(),
                  "phase 2: ...and still recording when the window closes");

            mcp.runScript(QStringLiteral("app.quit()"));
            const bool exited = app2.waitForFinished(60000);
            log += app2.readAll();
            CHECK(exited, "phase 2: the app exited through the normal close path");
            if (!exited) { app2.kill(); app2.waitForFinished(5000); }

            // AND IT WAS WRITTEN WHILE THE APP WAS STILL WHOLE — which is the
            // actual contract, and the one that was broken. The only stop() on
            // this path used to be the one in finalizeAppExit, which runs after
            // app.exec() RETURNS: after the background-work step, after the
            // modules are down, and — this is the part that loses bundles —
            // after the two forced exits that step can take (the 3 s pool wait
            // and the 20 s teardown watchdog, mainwindow.cpp). Measured on the
            // binary without the fix: "[perf] capture written" lands after
            // "[shutdown] step 4/8"; with it, before step 3.
            const int writtenAt = log.indexOf("[perf] capture written");
            const int modulesAt = log.indexOf("[shutdown] step 3/8");
            std::printf("info: 'capture written' at %d, 'step 3/8' at %d\n", writtenAt, modulesAt);
            CHECK(writtenAt >= 0, "phase 2: the app logged that it wrote the capture");
            CHECK(writtenAt >= 0 && modulesAt >= 0 && writtenAt < modulesAt,
                  "phase 2: the capture was written INSIDE the background-work step, before the "
                  "modules and the engine went down");

            // THE BUNDLE IS WHOLE. Before the fix there was no machine.json at
            // all here, and trace.json kept its open bracket.
            bool qok = false;
            const QJsonObject qm = readJson(QDir(quitBundle).filePath("machine.json"), &qok).object();
            CHECK(qok && !qm.isEmpty(), "phase 2: machine.json exists and parses after the quit");
            const QJsonObject qcapture = qm.value("capture").toObject();
            CHECK(qcapture.value("framesWritten").toDouble() > 0.0,
                  "phase 2: the capture kept the frames it had recorded");
            CHECK(qcapture.value("stoppedEarly").toBool(),
                  "phase 2: ...and records that it was stopped early");
            CHECK(qm.value("truncation").toObject().value("complete").toBool(false),
                  "phase 2: the bundle says it is COMPLETE");
            bool qtok = false;
            const QJsonDocument qtrace = readJson(QDir(quitBundle).filePath("trace.json"), &qtok);
            CHECK(qtok && qtrace.isArray(),
                  "phase 2: trace.json parses (its closing bracket was written)");
            bool qframesOk = false;
            const QList<QJsonObject> qframes =
                readJsonl(QDir(quitBundle).filePath("frames.jsonl"), &qframesOk);
            CHECK(qframesOk && !qframes.isEmpty(), "phase 2: frames.jsonl parses and has records");

            // ---- THE MONITOR'S OWN HOST COST (item 6) ----------------------
            // `host.monitor` is the drain — two JSON records per pass per frame
            // plus three file writes, on the UI thread — which used to be
            // charged to nobody at all: it runs after a frame ends and before
            // the gap clock is re-armed, so it appeared in neither totalMs nor
            // gap.*. It lands on the FOLLOWING frame's record (the stage
            // contract), so the first frame of a capture cannot carry one.
            //
            // AND NEITHER CAN AN `offscreen` FRAME, which is why the count
            // below filters them out rather than asserting over everything: a
            // thumbnail, a preview or a screenshot renders THROUGH A THROWAWAY
            // VIEW, outside the render driver's tick entirely — there is no
            // noteTickStart/noteTickEnd around it, so there is no drain to
            // charge to it. (The quit path takes exactly such a frame: the
            // project's preview tile is re-rendered on the way out. Measured on
            // this capture: 10 driver/scripted frames and 2 offscreen ones.)
            int withMonitor = 0, withStartTag = 0, considered = 0, offscreen = 0;
            for (int i = 0; i < qframes.size(); ++i) {
                bool monitor = false, startTag = false;
                for (const QJsonValue &sv : qframes[i].value("stages").toArray()) {
                    const QString name = sv.toObject().value("name").toString();
                    if (name == QLatin1String("host.monitor")) monitor = true;
                    if (name == QLatin1String("host.capture_start")) startTag = true;
                }
                if (startTag) ++withStartTag;
                if (qframes[i].value("cause").toString() == QLatin1String("offscreen")) {
                    ++offscreen;
                    continue;
                }
                if (i == 0) continue;              // nothing has drained before it
                ++considered;
                if (monitor) ++withMonitor;
            }
            std::printf("info: %d frames, %d of them offscreen\n", int(qframes.size()), offscreen);
            CHECK(considered > 0 && withMonitor == considered,
                  qPrintable(QStringLiteral("phase 2: every DRIVEN frame after the first carries "
                                            "the host.monitor stage (%1 of %2)")
                                 .arg(withMonitor).arg(considered)));
            CHECK(withStartTag == 1,
                  qPrintable(QStringLiteral("phase 2: exactly one frame is tagged "
                                            "host.capture_start (%1)").arg(withStartTag)));

            // ---- THE CAPTURE ROOT FOLLOWS THE DATA ROOT (item 11) ----------
            // Asserted on a SECOND app, because this one's root came from
            // $JAHSHAKA_PERF_ROOT — the question is what the DEFAULT resolves to
            // when only --data-root is set. It used to be a compiled-in
            // ~/Developer/spikes/perf.
        }

        QProcess app3;
        QProcessEnvironment env3 = QProcessEnvironment::systemEnvironment();
        env3.insert("HOME", quitHome);
        env3.insert("JAHSHAKA_DATA_ROOT", quitHome + "/.local/share/Jahshaka");
        env3.insert("XDG_CACHE_HOME", quitHome + "/cache");
        env3.remove("JAHSHAKA_PERF_ROOT");
        app3.setProcessEnvironment(env3);
        app3.setWorkingDirectory(quitHome + "/run");
        QString token3;
        QByteArray log3;
        const quint16 port3 = freePort();
        if (spawn(app3, port3, &token3, &log3) && !token3.isEmpty()) {
            McpClient mcp3;
            mcp3.url = QUrl(QStringLiteral("http://127.0.0.1:%1/mcp").arg(port3));
            mcp3.token = token3;
            mcp3.initialize();
            const QString root = mcp3.runScript(QStringLiteral("perf.status()"))
                                     .value("result").toObject().value("root").toString();
            std::printf("info: default capture root = %s\n", qPrintable(root));
            const QString expected =
                QDir::cleanPath(quitHome + "/.local/share/Jahshaka/perf");
            CHECK(QDir::cleanPath(root) == expected,
                  qPrintable(QStringLiteral("the default capture root follows --data-root "
                                            "(%1)").arg(root)));
            mcp3.runScript(QStringLiteral("app.quit()"));
            if (!app3.waitForFinished(60000)) { app3.kill(); app3.waitForFinished(5000); }
        } else {
            CHECK(false, "the second app booted for the capture-root check");
        }
    }

    std::printf(failures ? "\nFAILURES: %d\n" : "\nall checks passed (%d failures)\n", failures);
    return failures ? 1 : 0;
}
