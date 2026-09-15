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
    {
        iris::SkyRealistic r = iris::SkyRealistic::defaults();
        r.density   = 50.0f;     // the panel row's range is 0.01 .. 1
        r.diffusion = -3.0f;     //                          0 .. 4
        r.horizon   = 9.0f;      //                          0 .. 0.5
        r.power     = 99.0f;     //                          0 .. 4
        r.sunHaze   = 0.1f;      //                          1 .. 10
        scene->setSkyRealistic(r);
        CHECK(near_(scene->skyRealistic.density, 1.0f) &&
              near_(scene->skyRealistic.diffusion, 0.0f) &&
              near_(scene->skyRealistic.horizon, 0.5f) &&
              near_(scene->skyRealistic.power, 4.0f) &&
              near_(scene->skyRealistic.sunHaze, 1.0f),
              "B: EVERY dial is clamped by the document, not just the one a verb "
              "remembered (%.3f %.3f %.3f %.3f %.3f)",
              double(scene->skyRealistic.density), double(scene->skyRealistic.diffusion),
              double(scene->skyRealistic.horizon), double(scene->skyRealistic.power),
              double(scene->skyRealistic.sunHaze));
        CHECK(scene->skyRealisticInSync(), "B: ...and both halves still agree after the write");
        // E: the STORED block carries the clamped values, not the asked-for ones.
        const QJsonObject block = scene->skyData.value(QStringLiteral("Realistic"));
        CHECK(std::fabs(block.value("density").toDouble() - 1.0) < 1e-5 &&
              std::fabs(block.value("sunHaze").toDouble() - 1.0) < 1e-5,
              "E: the stored block cannot keep a value the renderer refused "
              "(density %.3f, sunHaze %.3f)",
              block.value("density").toDouble(), block.value("sunHaze").toDouble());
    }

    // ---- C: the predicate has teeth --------------------------------------
    {
        scene->setSkyRealistic(iris::SkyRealistic::defaults());
        CHECK(scene->skyRealisticInSync(), "C: in sync before the bypass");
        scene->skyRealistic.power = 0.02f;     // the bypass this suite exists to catch
        CHECK(!scene->skyRealisticInSync(),
              "C: a write straight to the typed field is REPORTED — a future writer that "
              "bypasses setSkyRealistic fails this suite instead of a user's scene");
        scene->setSkyRealistic(scene->skyRealistic);
        CHECK(scene->skyRealisticInSync(), "C: ...and the one path repairs it");
    }

    // ---- D: the round trip, and what an absent key means ------------------
    {
        iris::SkyRealistic r = iris::SkyRealistic::defaults();
        r.density = 0.31f; r.diffusion = 1.75f; r.horizon = 0.04f;
        r.power = 2.25f; r.sunHaze = 6.0f; r.skyColour = QColor(17, 34, 51);
        const iris::SkyRealistic back =
            iris::Scene::skyRealisticFromJson(iris::Scene::skyRealisticJson(r));
        CHECK(near_(back.density, r.density) && near_(back.diffusion, r.diffusion) &&
              near_(back.horizon, r.horizon) && near_(back.power, r.power) &&
              near_(back.sunHaze, r.sunHaze) && back.skyColour == r.skyColour,
              "D: every dial survives the JSON round trip exactly, the colour included");

        const iris::SkyRealistic d = iris::SkyRealistic::defaults();
        const iris::SkyRealistic empty = iris::Scene::skyRealisticFromJson(QJsonObject());
        CHECK(near_(empty.density, d.density) && near_(empty.sunHaze, d.sunHaze) &&
              empty.skyColour == d.skyColour,
              "D: an ABSENT key reads as what a NEW scene means, never as zero — the "
              "reader-defaults trap (sunHaze %.3f against the default %.3f)",
              double(empty.sunHaze), double(d.sunHaze));

        // A block that carries ONE key leaves the others at the new-scene value:
        // this is exactly the old-document case the four hand-written readers
        // each had to get right on their own.
        QJsonObject partial;
        partial.insert("density", 0.42);
        const iris::SkyRealistic one = iris::Scene::skyRealisticFromJson(partial);
        CHECK(near_(one.density, 0.42f) && near_(one.power, d.power) &&
              near_(one.sunHaze, d.sunHaze),
              "D: a document that knows one dial opens at the defaults for the rest");
    }

    std::printf(failures ? "\nFAILURES: %d\n" : "\nall checks passed\n", failures);
    return failures ? 1 : 0;
}
