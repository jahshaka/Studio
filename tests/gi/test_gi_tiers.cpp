// gi.tiers — RAYON's tier table, its pins, and its migration
// (SPECS/GI_UNIFIED_SPEC.md §2 / P2).
//
// Document-only and display-free: this is the RESOLUTION half of the Rayon
// unification, and none of it needs a renderer. What the renderer does with the
// resolved values is gated by gi.modes, gi.pcc_mirror and gi.ddgi, which are
// unchanged by this phase — that is the point of the design. A tier writes the
// SAME three backing fields those suites already drive; it just decides them
// from one dial instead of four.
//
// WHAT IT GATES, in order:
//   1. THE TABLE      — each tier resolves to exactly the documented technique,
//                       quality and irradiance-field state;
//   2. THE SWITCH     — off is giMode OFF and nothing else, the tier is
//                       remembered across an off/on trip, and turning off does
//                       not silently keep a technique pin alive;
//   3. PINS           — an Advanced edit deviates, survives a tier switch, is
//                       reported as a deviation, and can be handed back;
//   4. THE WORLD MODE — one owner: applying a World Mode drives the Rayon row
//                       and NOT the three rows underneath it, and Low/Medium/
//                       High resolve to what they always did;
//   5. NEW SCENES     — born Realtime-Epic (owner decision D2), through the
//                       same path MainWindow::createDefaultScene uses;
//   6. MIGRATION      — the seven shipped samples' REAL serialized GI blocks
//                       (read out of their databases for this suite) derive a
//                       tier without changing one rendered value. That is the
//                       whole acceptance criterion for the unification: an
//                       existing scene must open pixel-identical, and it does
//                       so BY CONSTRUCTION if no field the renderer reads moves.
#include <QGuiApplication>
#include <cstdio>

#include "irisgl/irisglfwd.h"
#include "irisgl/document/scenegraph/scene.h"

#include "services/worldmodes.h"

#include "../support/documentgraph.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

using worldmodes::RayonTier;

static iris::ScenePtr freshScene()
{
    return iris::Scene::create();
}

static int giMode(const iris::ScenePtr &s)    { return int(s->giMode); }
static int giQuality(const iris::ScenePtr &s) { return int(s->giQuality); }
static int giDdgi(const iris::ScenePtr &s)    { return s->giDdgi > 0 ? 1 : 0; }

// ---------------------------------------------------------------------------
// 1. THE TABLE
// ---------------------------------------------------------------------------
static void testTierTable()
{
    std::printf("\n-- 1. the tier table --\n");
    struct Want { RayonTier tier; int mode, quality, ddgi; const char *name; };
    const Want wants[] = {
        { RayonTier::Low,    1, 0, 0, "Low = Instant Radiosity, low quality, no field" },
        { RayonTier::Medium, 2, 1, 0, "Medium = VCT, medium voxels, no field" },
        { RayonTier::High,   3, 2, 0, "High = VCT + probes, high quality, no field" },
        { RayonTier::Epic,   3, 2, 1, "Epic = VCT + probes, high quality, FIELD ON" },
    };
    for (const Want &w : wants) {
        auto s = freshScene();
        worldmodes::setRayon(s, true, w.tier);
        CHECK(giMode(s) == w.mode && giQuality(s) == w.quality && giDdgi(s) == w.ddgi, w.name);
        CHECK(s->giTier == int(w.tier), "the tier is recorded on the document");
        CHECK(worldmodes::rayonEnabled(s), "and Rayon reads enabled");
        CHECK(!worldmodes::rayonCustom(s), "a freshly applied tier is not Custom");
        // The write-through invariant, for each of the three rows: the backing
        // field IS the resolved value, so every existing reader (the mirror,
        // the serializer, world.gi) sees the tier without knowing it exists.
        for (const QString &id : worldmodes::rayonRowIds()) {
            const worldmodes::Row *r = worldmodes::row(id);
            CHECK(r && r->rayonTiered &&
                      worldmodes::resolved(s, *r) == worldmodes::tierValue(*r, worldmodes::mode(s), s),
                  qPrintable(QStringLiteral("write-through holds for %1").arg(id)));
        }
    }
    // The intensity is NOT tiered: 1.0 is the calibrated default and a scene
    // that trimmed it must keep the trim across a tier switch.
    auto s = freshScene();
    s->giDdgiIntensity = 2.5f;
    worldmodes::setRayon(s, true, RayonTier::Epic);
    CHECK(s->giDdgiIntensity == 2.5f, "a tier switch never regrades the field's intensity");
    // Nor is the update budget (owner decision D5: its own visible row).
    s->giUpdateBudget = 0;
    worldmodes::setRayon(s, true, RayonTier::Low);
    CHECK(s->giUpdateBudget == 0, "a tier switch never touches the GI update budget");
}

// ---------------------------------------------------------------------------
// 2. THE SWITCH
// ---------------------------------------------------------------------------
static void testSwitch()
{
    std::printf("\n-- 2. the switch --\n");
    auto s = freshScene();
    worldmodes::setRayon(s, true, RayonTier::High);
    CHECK(giMode(s) == 3, "Rayon on at High = the hybrid");

    worldmodes::setRayon(s, false, worldmodes::rayonTier(s));
    CHECK(giMode(s) == 0, "off is giMode OFF — the renderer's own switch, not a second flag");
    CHECK(!worldmodes::rayonEnabled(s), "and Rayon reads disabled");
    CHECK(s->giTier == int(RayonTier::High), "the tier is REMEMBERED while off");
    CHECK(!worldmodes::rayonCustom(s), "an off scene is never 'Custom' (nothing to deviate from)");

    worldmodes::setRayon(s, true, worldmodes::rayonTier(s));
    CHECK(giMode(s) == 3 && giQuality(s) == 2,
          "turning it back on restores the remembered quality");

    // A pinned technique cannot outlive an off: the pin and the enable share
    // one field, and "off, but pinned to VCT" is a state nothing can render.
    worldmodes::setRowValue(s, QStringLiteral("giMode"), 2);
    CHECK(s->worldOverrides.contains(QStringLiteral("giMode")), "the technique is pinned to VCT");
    worldmodes::setRayon(s, false, worldmodes::rayonTier(s));
    CHECK(!s->worldOverrides.contains(QStringLiteral("giMode")),
          "turning Rayon off drops the technique pin");
    worldmodes::setRayon(s, true, worldmodes::rayonTier(s));
    CHECK(giMode(s) == 3, "and turning it on gives the tier's technique back");
}

// ---------------------------------------------------------------------------
// 3. PINS — the Advanced disclosure's whole contract
// ---------------------------------------------------------------------------
static void testPins()
{
    std::printf("\n-- 3. pins --\n");
    auto s = freshScene();
    worldmodes::setRayon(s, true, RayonTier::Epic);
    CHECK(giQuality(s) == 2 && giDdgi(s) == 1, "Epic to start with");

    // An Advanced edit: quality down to medium, everything else left alone.
    CHECK(worldmodes::setRowValue(s, QStringLiteral("giQuality"), 1), "pin the quality to Medium");
    CHECK(giQuality(s) == 1, "the backing field followed the pin");
    CHECK(worldmodes::rayonCustom(s), "the tier row now reads Custom");
    CHECK(worldmodes::rayonDeviations(s).size() == 1, "exactly one deviation is reported");

    // THE CONTRACT: a tier switch moves everything EXCEPT the pin.
    worldmodes::setRayon(s, true, RayonTier::Medium);
    CHECK(giMode(s) == 2, "the tier switch moved the technique");
    CHECK(giQuality(s) == 1, "and left the pinned quality alone");
    worldmodes::setRayon(s, true, RayonTier::Epic);
    CHECK(giMode(s) == 3 && giDdgi(s) == 1, "back at Epic, the unpinned rows follow again");
    CHECK(giQuality(s) == 1, "the pin SURVIVED both switches");

    // And it can be handed back, one row or all of them.
    worldmodes::clearOverride(s, QStringLiteral("giQuality"));
    CHECK(giQuality(s) == 2, "clearOverride puts the tier's quality back");
    CHECK(!worldmodes::rayonCustom(s), "and the tier row stops saying Custom");

    // The field is pinnable the same way, including AGAINST Epic.
    CHECK(worldmodes::setRowValue(s, QStringLiteral("giDdgi"), 0), "pin the field off at Epic");
    CHECK(giDdgi(s) == 0 && worldmodes::rayonCustom(s), "Epic without its field is Custom");
    worldmodes::clearRayonOverrides(s);
    CHECK(giDdgi(s) == 1 && !worldmodes::rayonCustom(s),
          "Reset Advanced Settings hands every Rayon row back to the tier");
}

// ---------------------------------------------------------------------------
// 4. ONE OWNER — the World Mode drives the dial, never the machinery
// ---------------------------------------------------------------------------
static void testWorldModeOwnership()
{
    std::printf("\n-- 4. one owner --\n");
    // The three lower World Modes must resolve to exactly what they resolved to
    // before the unification, or every scene on them would change when this
    // landed: Low and Medium had GI off, High had Instant Radiosity at low
    // quality. Epic is the one column that moves (owner decision D2).
    struct Want { worldmodes::Mode mode; int giMode, giQuality, ddgi; const char *name; };
    const Want wants[] = {
        { worldmodes::Mode::Low,    0, 0, 0, "World Low leaves GI off, as it always did" },
        { worldmodes::Mode::Medium, 0, 1, 0, "World Medium leaves GI off, as it always did" },
        { worldmodes::Mode::High,   1, 0, 0, "World High is Instant Radiosity at low quality, as it always did" },
        { worldmodes::Mode::Epic,   3, 2, 1, "World Epic is the hybrid at high quality WITH the field (D2)" },
    };
    for (const Want &w : wants) {
        auto s = freshScene();
        worldmodes::setMode(s, w.mode);
        CHECK(giMode(s) == w.giMode && giDdgi(s) == w.ddgi, w.name);
        if (w.giMode != 0) CHECK(giQuality(s) == w.giQuality, "  ... and its quality");
    }

    // A pinned Rayon dial survives a World Mode switch like any other pin —
    // this is what keeps a migrated sample rendering as it was serialized.
    auto s = freshScene();
    worldmodes::setMode(s, worldmodes::Mode::Epic);
    CHECK(worldmodes::setRowValue(s, worldmodes::rayonRowId(), 2), "pin the Rayon dial to Medium");
    CHECK(giMode(s) == 2 && giQuality(s) == 1 && giDdgi(s) == 0, "the pin resolved Medium through");
    worldmodes::setMode(s, worldmodes::Mode::Low);
    CHECK(giMode(s) == 2 && giQuality(s) == 1,
          "and World Low did NOT switch it off — the pin won");
    CHECK(s->antiAliasing == 1 && s->shadowResolution == 512,
          "while the unpinned world rows followed Low");
}

// ---------------------------------------------------------------------------
// 5. NEW SCENES ARE BORN EPIC (owner decision D2)
// ---------------------------------------------------------------------------
static void testNewSceneDefault()
{
    std::printf("\n-- 5. the new-scene default --\n");
    auto s = freshScene();
    CHECK(s->giMode == iris::GiMode::OFF,
          "a BARE document still renders no GI (nothing is applied by the ctor)");
    CHECK(s->giTier == int(RayonTier::Epic), "but it carries Epic as the tier it would come up at");
    // Exactly what MainWindow::createDefaultScene does.
    worldmodes::setMode(s, worldmodes::Mode::Epic);
    CHECK(worldmodes::rayonEnabled(s), "a NEW scene is born with Rayon ON");
    CHECK(worldmodes::rayonTier(s) == RayonTier::Epic, "at Epic");
    CHECK(giMode(s) == 3 && giQuality(s) == 2 && giDdgi(s) == 1,
          "which is the hybrid, high quality, irradiance field on");
    CHECK(s->giDdgiIntensity == 1.0f, "at the calibrated intensity 1.0");
}

// ---------------------------------------------------------------------------
// 6. MIGRATION — the seven shipped samples, from their real databases
// ---------------------------------------------------------------------------
static void testMigration()
{
    std::printf("\n-- 6. migration --\n");
    // THE REAL SERIALIZED VALUES, read out of scenes/*.zip's databases on
    // 2026-09-08 (five carry vct+medium with no GI pins; Mirror Room and
    // Showroom carry the hybrid at high quality WITH giMode/giQuality pins from
    // when those were World Mode rows). All seven are on worldMode "epic".
    struct Sample {
        const char *name;
        int giMode, giQuality, giDdgi;   // as serialized (-1 = the absent tri-state)
        bool hasGiPins;
        RayonTier wantTier;
        const char *why;
    };
    const Sample samples[] = {
        { "Matcaps",           2, 1, -1, false, RayonTier::Medium, "vct + medium -> Medium" },
        { "Particles",         2, 1, -1, false, RayonTier::Medium, "vct + medium -> Medium" },
        { "Physics",           2, 1, -1, false, RayonTier::Medium, "vct + medium -> Medium" },
        { "Skeletal Animation",2, 1, -1, false, RayonTier::Medium, "vct + medium -> Medium" },
        { "World Background",  2, 1, -1, false, RayonTier::Medium, "vct + medium -> Medium" },
        { "Mirror Room",       3, 2, -1, true,  RayonTier::High,   "hybrid + high -> High" },
        { "Showroom",          3, 2, -1, true,  RayonTier::High,   "hybrid + high -> High" },
    };
    for (const Sample &sm : samples) {
        auto s = freshScene();
        s->giMode = iris::GiMode(sm.giMode);
        s->giQuality = iris::GiQuality(sm.giQuality);
        s->giDdgi = sm.giDdgi;
        s->worldMode = int(worldmodes::Mode::Epic);
        if (sm.hasGiPins) {
            s->worldOverrides.insert(QStringLiteral("giMode"), sm.giMode);
            s->worldOverrides.insert(QStringLiteral("giQuality"), sm.giQuality);
        }
        // What the renderer reads, BEFORE.
        const int mode0 = giMode(s), quality0 = giQuality(s), ddgi0 = giDdgi(s);

        worldmodes::deriveRayonFromDocument(s);

        // THE ACCEPTANCE CRITERION: not one rendered value moved.
        CHECK(giMode(s) == mode0 && giQuality(s) == quality0 && giDdgi(s) == ddgi0,
              qPrintable(QStringLiteral("%1: renders IDENTICALLY after migration").arg(sm.name)));
        CHECK(worldmodes::rayonTier(s) == sm.wantTier, sm.why);
        CHECK(!worldmodes::rayonCustom(s),
              qPrintable(QStringLiteral("%1: reads as its tier, not as Custom").arg(sm.name)));
        // The dial itself is pinned, because this scene's World Mode (Epic)
        // would otherwise resolve it to Epic and change how it renders.
        CHECK(s->worldOverrides.contains(worldmodes::rayonRowId()),
              qPrintable(QStringLiteral("%1: the Rayon dial is pinned against the World Mode").arg(sm.name)));
        worldmodes::setMode(s, worldmodes::Mode::Epic);
        CHECK(giMode(s) == mode0 && giQuality(s) == quality0 && giDdgi(s) == ddgi0,
              qPrintable(QStringLiteral("%1: and re-applying its World Mode still changes nothing").arg(sm.name)));
        // The redundant pins the two hand-tuned samples carried are dropped —
        // they were the same values the derived tier gives, and leaving them
        // would freeze those rows through every future tier switch.
        if (sm.hasGiPins)
            CHECK(!s->worldOverrides.contains(QStringLiteral("giMode")) &&
                      !s->worldOverrides.contains(QStringLiteral("giQuality")),
                  qPrintable(QStringLiteral("%1: redundant row pins dropped").arg(sm.name)));
    }

    // A scene that DEVIATES from every tier keeps its deviation as a pin.
    {
        auto s = freshScene();
        s->giMode = iris::GiMode::VCT;        // vct ...
        s->giQuality = iris::GiQuality::HIGH; // ... at high quality: Medium + a pin
        s->worldMode = int(worldmodes::Mode::Epic);
        worldmodes::deriveRayonFromDocument(s);
        CHECK(worldmodes::rayonTier(s) == RayonTier::Medium, "vct + high derives Medium");
        CHECK(giQuality(s) == 2, "and keeps rendering at high quality");
        CHECK(s->worldOverrides.value(QStringLiteral("giQuality")).toInt() == 2,
              "because the deviation became a pin");
        CHECK(worldmodes::rayonCustom(s), "which is exactly what 'Custom' means");
    }
    // A GI-off document stays off, derives a plausible tier from its quality,
    // and pins nothing about the technique (OFF is the enable, not a deviation).
    {
        auto s = freshScene();
        s->giMode = iris::GiMode::OFF;
        s->giQuality = iris::GiQuality::MEDIUM;
        s->worldMode = int(worldmodes::Mode::Epic);
        worldmodes::deriveRayonFromDocument(s);
        CHECK(giMode(s) == 0, "an off scene stays off");
        CHECK(worldmodes::rayonTier(s) == RayonTier::Medium, "its quality picks the tier");
        CHECK(!s->worldOverrides.contains(QStringLiteral("giMode")),
              "and OFF is not recorded as a technique pin");
        CHECK(s->worldOverrides.value(worldmodes::rayonRowId()).toInt() == 0,
              "the dial is pinned OFF against its World Mode");
        worldmodes::setMode(s, worldmodes::Mode::Epic);
        CHECK(giMode(s) == 0, "so re-applying Epic does NOT switch GI on behind the user");
    }
    // A P1-era scene that opted into the field explicitly derives Epic and
    // keeps it — the one legacy shape that lands on the top tier.
    {
        auto s = freshScene();
        s->giMode = iris::GiMode::VCT_PCC_HYBRID;
        s->giQuality = iris::GiQuality::HIGH;
        s->giDdgi = 1;
        s->worldMode = int(worldmodes::Mode::Custom);
        worldmodes::deriveRayonFromDocument(s);
        CHECK(worldmodes::rayonTier(s) == RayonTier::Epic, "hybrid + high + field derives Epic");
        CHECK(giDdgi(s) == 1 && !worldmodes::rayonCustom(s), "with nothing pinned");
        CHECK(!s->worldOverrides.contains(worldmodes::rayonRowId()),
              "a Custom-mode scene needs no dial pin: no tier can clobber it");
    }
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    // v1 INTERIM (SPECS/SCENEGRAPH_SPEC.md §3): a document node IS an engine
    // node now, so even a document-only suite needs the headless graph.
    enginetest::DocumentGraph graph("gi-tiers-ogre.log");
    if (!graph.require()) return 1;

    testTierTable();
    testSwitch();
    testPins();
    testWorldModeOwnership();
    testNewSceneDefault();
    testMigration();

    std::printf(failures ? "\nFAILED: %d check(s)\n" : "\nALL CHECKS PASSED\n", failures);
    return failures ? 1 : 0;
}
