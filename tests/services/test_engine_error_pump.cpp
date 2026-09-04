// engine.error_pump — STABILITY_PROGRAM_SPEC.md Lane 1.
//
// The engine refuses rather than throws: every backend virtual is wrapped in
// `JAH_TRY { … } JAH_CATCH(mError, <refusal>)`, recording a reason in one
// process-wide string and returning 0/false. Until this lane NOTHING read that
// string, and SceneMirror ignores the return value of nearly every call it
// makes — so ~86 catch sites produced a wrong picture and not one log line.
//
// Two halves, both here because they are one claim:
//   1. the BOUNDARY: Engine::takeLastError() returns AND clears, so a poller
//      can tell a fresh failure from a stale one. Proved against the real
//      failure the spec picked as the exemplar — a file that exists but does
//      not decode, i.e. exactly what a torn CAS object is (§1.4, Lane 2).
//   2. the PUMP: EngineErrorPump's rate limiter. It runs at 60Hz against a
//      sink that can produce the same message every frame, and (as the first
//      real catch proved) can also produce a DIFFERENT message every frame —
//      Ogre renames the shader on each retry — so both the per-message window
//      and the global flood budget are asserted.
//
// Needs a real engine (offscreen view, reachable DISPLAY) for half 1; half 2 is
// pure logic.
#include <QGuiApplication>
#include <QFile>
#include <QVariantList>
#include <QVariantMap>

#include <cstdio>
#include <string>

#include "jahshaka/engine/Engine.h"
#include "services/engineerrorpump.h"

using namespace jahshaka::engine;
static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

/// A file that EXISTS and decodes as nothing: a valid PNG signature and IHDR
/// followed by garbage. This is the shape of a CAS object torn by a kill -9
/// mid-store — a name that claims a sha256 over bytes that are not all there.
static bool writeTornPng(const QString &path)
{
    static const unsigned char kHead[] = {
        0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A,          // signature
        0x00, 0x00, 0x00, 0x0D, 'I', 'H', 'D', 'R',           // IHDR length + type
        0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x40,       // 64 x 64
        0x08, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00  // truncated here
    };
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(reinterpret_cast<const char *>(kHead), sizeof(kHead));
    return true;
}

static int entryCount(const QVariantMap &report)
{
    return report.value("entries").toList().size();
}

static QVariantMap entryAt(const QVariantMap &report, int i)
{
    return report.value("entries").toList().value(i).toMap();
}

// ---- half 2: the pump, with no engine ---------------------------------------
static void testRateLimiter()
{
    EngineErrorPump &pump = EngineErrorPump::instance();

    // A short window keeps the suite fast; the app runs 5000 ms.
    pump.reset();
    pump.setWindowMs(100000);   // nothing may age out during this block

    pump.record(QStringLiteral("alpha"));
    CHECK(entryCount(pump.report()) == 1, "the first occurrence is recorded");
    CHECK(entryAt(pump.report(), 0).value("count").toULongLong() == 1, "and counted once");

    // 60 repeats — one second of a per-frame failure.
    for (int i = 0; i < 60; ++i) pump.record(QStringLiteral("alpha"));
    QVariantMap r = pump.report();
    CHECK(entryCount(r) == 1, "60 identical drains stay ONE entry");
    CHECK(entryAt(r, 0).value("count").toULongLong() == 61, "with the full count kept (61)");
    CHECK(entryAt(r, 0).value("suppressed").toULongLong() == 60,
          "and 60 of them suppressed rather than logged");
    CHECK(r.value("recorded").toULongLong() == 61, "recorded counts every occurrence");

    // A different message is never suppressed by a pending one.
    pump.record(QStringLiteral("beta"));
    r = pump.report();
    CHECK(entryCount(r) == 2, "a DIFFERENT message gets its own entry");
    CHECK(entryAt(r, 0).value("message").toString() == QLatin1String("beta"),
          "newest first");
    CHECK(entryAt(r, 0).value("suppressed").toULongLong() == 0,
          "and is not suppressed by the pending one");

    // The key table is capped: a message built from a per-frame varying value
    // (a shader name, a node id) must not grow it without bound.
    pump.reset();
    for (int i = 0; i < EngineErrorPump::kMaxKeys * 3; ++i)
        pump.record(QStringLiteral("shader %1 failed").arg(i));
    r = pump.report();
    CHECK(entryCount(r) == EngineErrorPump::kMaxKeys,
          "the key table is capped at kMaxKeys distinct messages");
    CHECK(r.value("recorded").toULongLong() == quint64(EngineErrorPump::kMaxKeys * 3),
          "while every occurrence is still counted");
    CHECK(entryAt(r, 0).value("message").toString().contains(
              QString::number(EngineErrorPump::kMaxKeys * 3 - 1)),
          "and the newest message survives the eviction");
    // The global flood budget caught the rest: distinct messages cannot be
    // collapsed by keying, so they have to be capped by volume.
    CHECK(r.value("suppressed").toULongLong() >=
              quint64(EngineErrorPump::kMaxKeys * 3 - EngineErrorPump::kMaxLogsPerWindow),
          "the flood budget swallowed the lines the key limiter could not");

    pump.reset();
    r = pump.report();
    CHECK(entryCount(r) == 0 && r.value("recorded").toULongLong() == 0 &&
              r.value("drains").toULongLong() == 0,
          "reset() forgets everything, including the drain counter");
    pump.setWindowMs(5000);
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    testRateLimiter();

    // ---- half 1: the boundary ------------------------------------------------
    EngineConfig cfg;
    cfg.pluginDir    = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile      = "test_engine_error_pump-ogre.log";
    std::string err;
    auto engine = Engine::create(cfg, err);
    CHECK(engine != nullptr, "engine created");
    if (!engine) { std::printf("    %s\n", err.c_str()); return 1; }

    View *view   = engine->createOffscreenView("pump", 64, 64, Colour(0, 0, 0));
    Scene *scene = engine->createScene("pump");
    CHECK(view && scene, "offscreen view + scene");
    if (!view || !scene) return 1;
    view->setScene(scene);

    // Whatever startup left behind is not what this suite is about.
    (void)engine->takeLastError();
    CHECK(engine->takeLastError().empty(), "the sink starts (and stays) empty on success");

    const QString torn = QStringLiteral("torn-object.png");
    CHECK(writeTornPng(torn), "wrote a file that exists and decodes as nothing");

    const TextureId id = scene->loadTexture(torn.toStdString(), true);
    CHECK(id == 0, "loadTexture refuses it — this is the SILENT failure, it returns 0");
    CHECK(!engine->lastError().empty(), "lastError() explains why");
    const std::string peeked = engine->lastError();
    CHECK(peeked == engine->lastError(), "lastError() is a peek: it does not clear");

    const std::string taken = engine->takeLastError();
    CHECK(taken == peeked, "takeLastError() returns the same reason");
    CHECK(engine->takeLastError().empty(),
          "and CLEARS it — a poller cannot mistake it for a fresh failure");

    // ---- the two halves joined: drain() ---------------------------------------
    EngineErrorPump &pump = EngineErrorPump::instance();
    pump.reset();

    CHECK(scene->loadTexture(torn.toStdString() + "2", true) == 0 ||
              scene->loadTexture(torn.toStdString(), true) == 0,
          "fail it again, with the pump watching");
    pump.drain(engine.get());
    QVariantMap r = pump.report();
    CHECK(r.value("drains").toULongLong() == 1, "one drain");
    CHECK(entryCount(r) == 1, "which recorded exactly one message");
    CHECK(entryAt(r, 0).value("message").toString().contains(QStringLiteral("torn-object")),
          "and the message NAMES THE FILE the renderer could not use");

    // Draining a clean sink must not manufacture entries — the property that
    // makes it safe to call every frame.
    for (int i = 0; i < 120; ++i) pump.drain(engine.get());
    r = pump.report();
    CHECK(r.value("drains").toULongLong() == 121, "121 drains");
    CHECK(entryCount(r) == 1, "and still one message: a cleared sink adds nothing");

    pump.drain(nullptr);
    CHECK(pump.report().value("drains").toULongLong() == 122,
          "draining a null engine is a counted no-op, not a crash");

    QFile::remove(torn);
    engine->destroyView(view);
    engine->destroyScene(scene);
    std::printf(failures ? "FAILED: %d check(s)\n" : "engine.error_pump: PASS\n", failures);
    return failures ? 1 : 0;
}
