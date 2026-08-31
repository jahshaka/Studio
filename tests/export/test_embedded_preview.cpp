/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// export.embed — EmbeddedWebPreview contract tests (WEB_EXPORT embed follow-up).
// What is honestly automatable headless: the precondition guards, every
// FAILURE path (dead browser, window never appears) resolving to failed() with
// no crash and no dialog, stop() reaping the child, the RE-ENTRY state machine
// (stop() -> Idle -> a fresh start on the same object; unique per-run Chrome
// profiles so successive runs can never singleton-collide; stale-profile
// purge; profile removal at stop), the per-project PublishRecord states
// (none / present / missing, deleted-folder degradation and re-Process heal),
// and the viewer<->host readiness-marker contract. The successful adoption
// itself needs a real Chrome + GPU and is verified on the rig (see the embed
// spike report). Convention from export.web holds: never spawn a real browser
// in tests — only fake stand-ins (/bin/true, a sleep script).

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QPointer>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <cstdio>

#include "export/embeddedpreview.h"
#include "modules/publish/publishrecord.h"

static int failures = 0;
#define CHECK(cond, what) \
    do { \
        if (cond) std::printf("ok  %s\n", what); \
        else { std::printf("FAIL %s\n", what); ++failures; } \
    } while (0)

// Run the loop until `sig` fires on `obj` or `ms` elapses; true when it fired.
template <typename Sender, typename Signal>
static bool waitForSignal(Sender *obj, Signal sig, int ms)
{
    QEventLoop loop;
    bool fired = false;
    QObject::connect(obj, sig, &loop, [&] { fired = true; loop.quit(); });
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
    return fired;
}

int main(int argc, char **argv)
{
    // xcb on purpose: platformSupported() and the failure paths must be
    // exercised on the platform the feature actually targets.
    qputenv("QT_QPA_PLATFORM", "xcb");
    QApplication app(argc, argv);

    QTemporaryDir tmp;
    const QString index = tmp.filePath("index.html");
    {
        QFile f(index);
        if (!f.open(QIODevice::WriteOnly)) { std::printf("FAIL temp index\n"); return 1; }
        f.write("<!doctype html><title>t</title>");
    }
    QWidget slot_;
    auto *lay = new QVBoxLayout(&slot_);
    Q_UNUSED(lay);

    // ---- platform ----
    CHECK(EmbeddedWebPreview::platformSupported(), "platformSupported on xcb");

    // ---- precondition guards: start() refuses without side effects ----
    {
        EmbeddedWebPreview p;
        CHECK(!p.start(QStringLiteral("/bin/true"), tmp.filePath("missing.html"), &slot_),
              "start refused: index.html missing");
        CHECK(!p.start(QString(), index, &slot_), "start refused: empty browser path");
        QWidget bare; // no layout
        CHECK(!p.start(QStringLiteral("/bin/true"), index, &bare),
              "start refused: slot without layout");
        CHECK(!p.start(QStringLiteral("/bin/true"), index, nullptr),
              "start refused: null slot");
    }

    // ---- failure path: browser exits immediately -> failed(), no crash ----
    {
        EmbeddedWebPreview p;
        QString reason;
        QObject::connect(&p, &EmbeddedWebPreview::failed,
                         [&reason](const QString &r) { reason = r; });
        CHECK(p.start(QStringLiteral("/bin/true"), index, &slot_),
              "start accepted a fake browser");
        CHECK(waitForSignal(&p, &EmbeddedWebPreview::failed, 5000),
              "dead browser -> failed() promptly");
        CHECK(!reason.isEmpty(), "failed() carries a reason");
        CHECK(!p.isEmbedded(), "never reported embedded");
        p.stop();
        p.stop(); // idempotent
        CHECK(true, "stop() is idempotent after failure");
    }

    // ---- failure path: browser alive but no window -> find timeout ----
    {
        const QString fake = tmp.filePath("fakebrowser.sh");
        {
            QFile f(fake);
            if (!f.open(QIODevice::WriteOnly)) { std::printf("FAIL temp script\n"); return 1; }
            f.write("#!/bin/bash\nsleep 30\n");
            f.setPermissions(f.permissions() | QFileDevice::ExeOwner);
        }
        EmbeddedWebPreview p;
        p.findTimeoutMs = 1200; // keep the suite fast
        QString reason;
        QObject::connect(&p, &EmbeddedWebPreview::failed,
                         [&reason](const QString &r) { reason = r; });
        CHECK(p.start(fake, index, &slot_), "start accepted the sleeping browser");
        CHECK(!p.start(fake, index, &slot_), "start refused while a run is in flight");
        CHECK(waitForSignal(&p, &EmbeddedWebPreview::failed, 6000),
              "no window within findTimeoutMs -> failed()");
        CHECK(reason.contains(QStringLiteral("not found")), "reason names the find timeout");
        QPointer<QProcess> child = p.process();
        CHECK(child && child->state() == QProcess::Running,
              "failed() leaves the child for the caller to stop");
        p.stop();
        CHECK(!child || child->state() == QProcess::NotRunning, "stop() kills the child");
        CHECK(p.process() == nullptr, "stop() clears the reaped child");
    }

    // ---- re-entry state machine: stop() -> Idle -> fresh start, unique
    // ---- per-run profiles, stale-profile purge, profile removal at stop ----
    {
        // A leftover profile dir from a "previous run" (or a crashed app):
        // start() must purge it before launching, or its Chrome could
        // singleton-capture the new launch (the double-preview root cause).
        const QString staleDir =
            tmp.filePath(".preview-profile-embedded-stalerun");
        QDir().mkpath(staleDir);
        QFile staleFile(staleDir + QStringLiteral("/SingletonLock"));
        staleFile.open(QIODevice::WriteOnly);
        staleFile.close();

        EmbeddedWebPreview p;
        CHECK(p.start(QStringLiteral("/bin/true"), index, &slot_),
              "run 1 started on a fresh object");
        CHECK(!QDir(staleDir).exists(), "start() purged the stale profile dir");
        const QString profile1 = p.profileDirPath();
        CHECK(profile1.contains(QStringLiteral(".preview-profile-embedded-")),
              "run 1 uses a namespaced profile dir");
        bool argsCarryProfile = false;
        if (p.process())
            for (const QString &a : p.process()->arguments())
                if (a == QStringLiteral("--user-data-dir=%1").arg(profile1))
                    argsCarryProfile = true;
        CHECK(argsCarryProfile, "run 1 passes its unique profile to the browser");
        CHECK(waitForSignal(&p, &EmbeddedWebPreview::failed, 5000),
              "run 1 resolved (dead fake browser)");

        // Re-entry WITHOUT an explicit stop: a finished (Done) run must accept
        // a fresh start and clean the old one up itself.
        CHECK(p.start(QStringLiteral("/bin/true"), index, &slot_),
              "run 2 accepted after run 1 finished");
        const QString profile2 = p.profileDirPath();
        CHECK(!profile2.isEmpty() && profile2 != profile1,
              "run 2 got a DIFFERENT profile dir (no singleton crosstalk)");
        CHECK(waitForSignal(&p, &EmbeddedWebPreview::failed, 5000),
              "run 2 resolved");

        // Chrome would have created the profile dir; simulate it, then stop():
        // the dir must be gone — nothing left for a later run to collide with.
        QDir().mkpath(p.profileDirPath());
        p.stop();
        CHECK(!QDir(profile2).exists(), "stop() removed this run's profile dir");
        CHECK(p.profileDirPath().isEmpty(), "stop() cleared the profile path");
        CHECK(!p.isEmbedded(), "Idle after stop()");

        // Re-entry AFTER stop() — the Close-preview -> Process path.
        CHECK(p.start(QStringLiteral("/bin/true"), index, &slot_),
              "run 3 accepted after stop()");
        p.stop();
        CHECK(true, "stop() during a live run is clean (no crash)");
    }

    // ---- PublishRecord: the per-project last-publish memory ----
    {
        QTemporaryDir projectDir;
        const QString folder = projectDir.path();
        const QString webDir = QDir(folder).filePath(QStringLiteral("exports/web"));

        CHECK(PublishRecord::filePath(QString()).isEmpty(),
              "record path empty without a project folder");

        // Never published: no record, no export -> the empty state.
        PublishRecord none = PublishRecord::load(folder, webDir);
        CHECK(none.state() == PublishRecord::State::None,
              "never-published project reads as None");

        // First Process: export lands, record saved -> Present with the when.
        QDir().mkpath(webDir);
        {
            QFile f(QDir(webDir).filePath(QStringLiteral("index.html")));
            f.open(QIODevice::WriteOnly);
            f.write("<!doctype html>");
        }
        const QDateTime when = QDateTime::fromString(
            QStringLiteral("2026-08-31T12:00:00"), Qt::ISODate);
        CHECK(PublishRecord::save(folder, webDir, when), "record saved");
        PublishRecord rec = PublishRecord::load(folder, webDir);
        CHECK(rec.state() == PublishRecord::State::Present,
              "publish + record reads as Present");
        CHECK(rec.dir == webDir, "record remembers the export dir");
        CHECK(rec.when == when, "record remembers when it was published");
        CHECK(rec.indexHtml() == QDir(webDir).filePath(QStringLiteral("index.html")),
              "record points at the export's index.html");

        // User deletes the export folder: record survives, state degrades
        // cleanly to Missing — never an error, never a crash.
        QDir(webDir).removeRecursively();
        PublishRecord gone = PublishRecord::load(folder, webDir);
        CHECK(gone.isValid() && gone.state() == PublishRecord::State::Missing,
              "deleted export dir reads as Missing (record kept)");

        // Re-Process heals: the export reappears at the same path -> Present.
        QDir().mkpath(webDir);
        {
            QFile f(QDir(webDir).filePath(QStringLiteral("index.html")));
            f.open(QIODevice::WriteOnly);
            f.write("<!doctype html>");
        }
        CHECK(PublishRecord::load(folder, webDir).state() == PublishRecord::State::Present,
              "re-Process at the same path heals to Present");

        // Backfill: a pre-record project with a finished export shows its
        // publish (index.html mtime) without a record file being written.
        QFile::remove(PublishRecord::filePath(folder));
        PublishRecord back = PublishRecord::load(folder, webDir);
        CHECK(back.state() == PublishRecord::State::Present,
              "pre-record export backfills to Present");
        CHECK(back.when.isValid(), "backfill carries the index.html mtime");
        CHECK(!QFile::exists(PublishRecord::filePath(folder)),
              "backfill never writes the record file");
    }

    // ---- viewer <-> host readiness-marker contract (drift guard) ----
    {
        QFile viewer(QStringLiteral(":/export/viewer.js"));
        CHECK(viewer.open(QIODevice::ReadOnly), "viewer.js resource opens");
        const QByteArray js = viewer.readAll();
        CHECK(js.contains("[jah-gpu-ready]"), "viewer emits the gpu-ready marker");
        CHECK(js.contains("[jah-no-gpu]"), "viewer emits the no-gpu marker");
        CHECK(js.contains("jahembed=1"), "markers gated on the jahembed query");
        // Unreal-parity blend modes ride extras.jah.blendMode (no core glTF
        // equivalent) — the viewer half must stay in place, and three only
        // implements MultiplyBlending on the premultiplied path.
        CHECK(js.contains("AdditiveBlending") && js.contains("MultiplyBlending"),
              "viewer patches three blending for jah blend modes");
        CHECK(js.contains("premultipliedAlpha"),
              "modulate sets premultipliedAlpha (three blends nothing without it)");
    }

    std::printf(failures ? "FAILED: %d checks\n" : "ALL OK\n", failures);
    return failures ? 1 : 0;
}
