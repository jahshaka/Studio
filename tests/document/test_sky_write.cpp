// document.sky_write — THE REALISTIC SKY HAS TWO REPRESENTATIONS AND EXACTLY
// ONE WRITER (lane SKY-SMALL, item SKY-WRITE-1).
//
// THE HAZARD. The realistic sky's dials live in the document twice: as the
// typed `Scene::skyRealistic`, which SceneMirror reads and the renderer draws,
// and as `skyData["Realistic"]`, which SceneWriter serialises and every panel
// binds from. A writer that sets one half and forgets the other is SILENTLY
// REVERTED at the next bind or the next save, and nothing anywhere says so.
//
// Four writers kept both halves by hand — the `world.sky` verb, the sky panel's
// six dials, the undo command's capture/apply pair and two file readers — each
// with its own copy of the clamps and its own per-key defaults. Two of those
// copies disagreed: the verb clamped only `sunHaze` where the panel clamped all
// five, so a scripted `density: 50` reached the renderer and survived until
// somebody opened the panel, and the undo blob carried the six dials as six
// MORE keys beside the JSON block, so a dial added to SkyRealistic and
// forgotten there would be reverted by any undo of any sky edit.
//
// WHAT IS ASSERTED — the contract that makes the class impossible rather than
// merely absent today:
//   A. a NEW scene is in sync (the constructor writes both halves too);
//   B. `setSkyRealistic` writes both, and CLAMPS every dial, not one;
//   C. writing the typed field DIRECTLY breaks `skyRealisticInSync()` — the
//      predicate has teeth, so a future writer that bypasses the setter is
//      caught by this suite rather than by a user;
//   D. the JSON round-trip is exact, and an ABSENT key reads as what a NEW
//      scene means (the reader-defaults trap), never as zero;
//   E. a clamped write leaves BOTH halves clamped — the stored block cannot
//      keep a value the renderer refused.
//
// (The dials are the planet's atmosphere's since SKY-ATMOSPHERE-1.)
//
// Document only: the headless NULL render system, no display, no pixels.
#include <QGuiApplication>
#include <QJsonObject>
#include <cstdio>
#include <cmath>

#include "irisgl/irisglfwd.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"

#include "../support/documentgraph.h"

static int failures = 0;
#define CHECK(cond, ...) do { if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); \
                              std::printf("\n"); } \
                         else { std::printf("FAIL: "); std::printf(__VA_ARGS__); \
                                std::printf("\n"); ++failures; } } while (0)

static bool near_(float a, float b) { return std::fabs(a - b) < 1e-5f; }

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    enginetest::DocumentGraph graph("document-sky-write-ogre.log");

    // ---- A: a new scene is in sync ---------------------------------------
    auto scene = iris::Scene::create();
    CHECK(scene->skyRealisticInSync(),
          "A: a new scene's typed sky dials and its stored block agree");
    CHECK(scene->skyData.contains(QStringLiteral("Realistic")),
          "A: ...and the block exists (the constructor writes it through the setter)");

    // ---- B: the setter writes both halves, and clamps every dial ----------
    // (the planet's atmosphere's dials, SKY-ATMOSPHERE-1: the haze 0..100, the Rayleigh scale 0..10, the
    // aerial scale and the albedo 0..1)
    {
        iris::SkyRealistic r = iris::SkyRealistic::defaults();
        r.sunHaze       = 500.0f;    // 0 .. 100
        r.aerialScale   = -3.0f;     // 0 .. 1
        r.groundAlbedo  = 9.0f;      // 0 .. 1
        r.rayleighScale = -1.0f;     // 0 .. 10
        scene->setSkyRealistic(r);
        CHECK(near_(scene->skyRealistic.sunHaze, 100.0f) &&
              near_(scene->skyRealistic.aerialScale, 0.0f) &&
              near_(scene->skyRealistic.groundAlbedo, 1.0f) &&
              near_(scene->skyRealistic.rayleighScale, 0.0f),
              "B: EVERY dial is clamped by the document, not just the one a verb "
              "remembered (%.3f %.3f %.3f %.3f)",
              double(scene->skyRealistic.sunHaze), double(scene->skyRealistic.aerialScale),
              double(scene->skyRealistic.groundAlbedo), double(scene->skyRealistic.rayleighScale));
        CHECK(scene->skyRealisticInSync(), "B: ...and both halves still agree after the write");
        // E: the STORED block carries the clamped values, not the asked-for ones.
        const QJsonObject block = scene->skyData.value(QStringLiteral("Realistic"));
        CHECK(std::fabs(block.value("sunHaze").toDouble() - 100.0) < 1e-5 &&
              std::fabs(block.value("groundAlbedo").toDouble() - 1.0) < 1e-5,
              "E: the stored block cannot keep a value the renderer refused "
              "(sunHaze %.3f, groundAlbedo %.3f)",
              block.value("sunHaze").toDouble(), block.value("groundAlbedo").toDouble());
    }

    // ---- C: the predicate has teeth --------------------------------------
    {
        scene->setSkyRealistic(iris::SkyRealistic::defaults());
        CHECK(scene->skyRealisticInSync(), "C: in sync before the bypass");
        scene->skyRealistic.groundAlbedo = 0.02f;     // the bypass this suite exists to catch
        CHECK(!scene->skyRealisticInSync(),
              "C: a write straight to the typed field is REPORTED — a future writer that "
              "bypasses setSkyRealistic fails this suite instead of a user's scene");
        scene->setSkyRealistic(scene->skyRealistic);
        CHECK(scene->skyRealisticInSync(), "C: ...and the one path repairs it");
    }

    // ---- D: the round trip, and what an absent key means ------------------
    {
        iris::SkyRealistic r = iris::SkyRealistic::defaults();
        r.sunHaze = 3.5f; r.aerialScale = 0.25f; r.groundAlbedo = 0.15f;
        r.rayleighScale = 2.5f; r.ozone = false;
        const iris::SkyRealistic back =
            iris::Scene::skyRealisticFromJson(iris::Scene::skyRealisticJson(r));
        CHECK(near_(back.sunHaze, r.sunHaze) && near_(back.aerialScale, r.aerialScale) &&
              near_(back.groundAlbedo, r.groundAlbedo) && near_(back.rayleighScale, r.rayleighScale) &&
              back.ozone == r.ozone,
              "D: every dial survives the JSON round trip exactly, the ozone switch included");

        const iris::SkyRealistic d = iris::SkyRealistic::defaults();
        const iris::SkyRealistic empty = iris::Scene::skyRealisticFromJson(QJsonObject());
        CHECK(near_(empty.sunHaze, d.sunHaze) && near_(empty.aerialScale, d.aerialScale) &&
              empty.ozone == d.ozone,
              "D: an ABSENT key reads as what a NEW scene means, never as zero — the "
              "reader-defaults trap (aerialScale %.3f against the default %.3f)",
              double(empty.aerialScale), double(d.aerialScale));

        // A block that carries ONE key leaves the others at the new-scene value.
        QJsonObject partial;
        partial.insert("sunHaze", 4.0);
        const iris::SkyRealistic one = iris::Scene::skyRealisticFromJson(partial);
        CHECK(near_(one.sunHaze, 4.0f) && near_(one.groundAlbedo, d.groundAlbedo) &&
              near_(one.aerialScale, d.aerialScale),
              "D: a document that knows one dial opens at the defaults for the rest");

        // FORWARD ONLY (SKY-ATMOSPHERE-1): the retired model's keys are not read.
        QJsonObject old;
        old.insert("density", 0.9);
        old.insert("power", 3.0);
        const iris::SkyRealistic ignored = iris::Scene::skyRealisticFromJson(old);
        CHECK(iris::Scene::skyRealisticJson(ignored) == iris::Scene::skyRealisticJson(d),
              "D: an old block's non-physical dials are not read — it opens at the defaults");
    }

    std::printf(failures ? "\nFAILURES: %d\n" : "\nall checks passed\n", failures);
    return failures ? 1 : 0;
}
