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
//                       quality, irradiance-field state, bounce count and
//                       dynamic-probe reservation (owner option (b), 2026-09-09:
//                       Medium/High DDGI-fed, Epic = its own column);
//   2. THE SWITCH     — off is giMode OFF and nothing else, the tier is
//                       remembered across an off/on trip, and turning off does
//                       not silently keep a technique pin alive;
//   3. PINS           — an Advanced edit deviates, survives a tier switch, is
//                       reported as a deviation, and can be handed back;
//   4. THE WORLD MODE — one owner: applying a World Mode drives the Rayon row
//                       and NOT the five rows underneath it, and Low/Medium/
//                       High resolve to what they always did;
//   5. NEW SCENES     — born Realtime-Epic (owner decision D2), through the
//                       same path MainWindow::createDefaultScene uses;
//   6. MIGRATION      — the five pre-tier shipped samples' REAL serialized GI
//                       blocks (read out of their databases for this suite)
//                       derive a tier that preserves technique, quality and
//                       bounces exactly and moves ONE value on purpose: the
//                       untouched field tri-state follows the tier, which is
//                       the owner's re-pin of the vct+medium samples to the
//                       DDGI-fed Medium row (the pixel evidence is in the
//                       lane report; the numbers moved WITH the decision, not
//                       with a tolerance).
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
static int giBounces(const iris::ScenePtr &s) { return s->giNumBounces; }

// ---------------------------------------------------------------------------
// 1. THE TABLE
// ---------------------------------------------------------------------------
static void testTierTable()
{
    std::printf("\n-- 1. the tier table --\n");
    // THE TABLE AS SHIPPED (worldmodes.h carries the readable form). Every
    // column that is a registry row is pinned here; the derived columns
    // (voxel resolution, probe faces/HDR/shadows, the 8192-probe field grid,
    // ddgiSource auto = voxel) follow giQuality / the engine and are pinned
    // by gi.modes, gi.pcc_mirror and gi.ddgi.
    // The fifth column is the PROBE CAPTURE SIZE (owner 2026-09-13 Q4): 0 at
    // every tier = "follow the engine's quality dial", because the halving that
    // decision asked for is the engine's default now (High 512 -> 256). The
    // column exists so a scene can PIN a size, which case 3 gates.
    struct Want { RayonTier tier; int mode, quality, ddgi, bounces, probeSize; const char *name; };
    const Want wants[] = {
        { RayonTier::Low,    1, 0, 0, 1, 0, "Low = Instant Radiosity, low quality, no field, 1 bounce, automatic probe size" },
        { RayonTier::Medium, 2, 1, 1, 1, 0, "Medium = VCT 64^3, FIELD ON (DDGI-fed), 1 bounce" },
        { RayonTier::High,   3, 2, 1, 1, 0, "High = VCT + probes 128^3, FIELD ON, 1 bounce" },
        { RayonTier::Epic,   3, 2, 1, 3, 0, "Epic = VCT + probes 128^3, FIELD ON, THREE bounces (the FOUR-column table: movers are reflected by SSR + planar, never by a probe re-capture)" },
    };
    for (const Want &w : wants) {
        auto s = freshScene();
        worldmodes::setRayon(s, true, w.tier);
        CHECK(giMode(s) == w.mode && giQuality(s) == w.quality && giDdgi(s) == w.ddgi &&
                  giBounces(s) == w.bounces && s->giProbeCaptureSize == w.probeSize, w.name);
        // The accessors ARE the table (one owner): what they say per column
        // must be what the tier wrote.
        CHECK(worldmodes::rayonTechnique(w.tier) == w.mode &&
                  worldmodes::rayonQuality(w.tier) == w.quality &&
                  worldmodes::rayonDdgi(w.tier) == w.ddgi &&
                  worldmodes::rayonBounces(w.tier) == w.bounces &&
                  worldmodes::rayonProbeSize(w.tier) == w.probeSize,
              "the column accessors agree with the write-through");
        CHECK(s->giTier == int(w.tier), "the tier is recorded on the document");
        CHECK(worldmodes::rayonEnabled(s), "and Rayon reads enabled");
        CHECK(!worldmodes::rayonCustom(s), "a freshly applied tier is not Custom");
        // The write-through invariant, for each of the five rows: the backing
        // field IS the resolved value, so every existing reader (the mirror,
        // the serializer, world.gi) sees the tier without knowing it exists.
        for (const QString &id : worldmodes::rayonRowIds()) {
            const worldmodes::Row *r = worldmodes::row(id);
            CHECK(r && r->rayonTiered &&
                      worldmodes::resolved(s, *r) == worldmodes::tierValue(*r, worldmodes::mode(s), s),
                  qPrintable(QStringLiteral("write-through holds for %1").arg(id)));
        }
    }
    CHECK(worldmodes::rayonRowIds().size() == 5, "the tier writes exactly five rows through");
    // 5 x 4: EVERY cell of every rayonTiered row is the table's cell. The rows
    // derive their tier[] from kRayonTable (worldmodes.cpp rayonColumns) and
    // the public readers read the same table one column at a time, so a cell
    // that disagrees here is a second copy of the table — which is exactly what
    // the rayontiers review found (13 of 20 cells untested while hand-copied).
    {
        using Reader = int (*)(RayonTier);
        const Reader readers[5] = { worldmodes::rayonTechnique, worldmodes::rayonQuality,
                                    worldmodes::rayonDdgi, worldmodes::rayonBounces,
                                    worldmodes::rayonProbeSize };
        const QStringList ids = worldmodes::rayonRowIds();
        for (int c = 0; c < 5 && c < ids.size(); ++c) {
            const worldmodes::Row *r = worldmodes::row(ids[c]);
            for (int t = 0; t < 4; ++t) {
                const int want = readers[c](RayonTier(t));
                CHECK(r && r->tier[t] == want,
                      qPrintable(QStringLiteral("%1.tier[%2] == kRayonTable[%2] column %3 (%4)")
                                     .arg(ids[c]).arg(t).arg(c).arg(want)));
            }
        }
    }
    // High and Epic differ in TWO rows and nowhere else — the whole point of
    // Epic's column (before option (b) they differed only in the field, and
    // once High is DDGI-fed that would have collapsed them onto one row).
    {
        auto h = freshScene(), e = freshScene();
        worldmodes::setRayon(h, true, RayonTier::High);
        worldmodes::setRayon(e, true, RayonTier::Epic);
        CHECK(giMode(h) == giMode(e) && giQuality(h) == giQuality(e) && giDdgi(h) == giDdgi(e),
              "High and Epic share technique, quality and the field");
        CHECK(giBounces(e) > giBounces(h),
              "and Epic alone carries the extra bounces — the ONLY column between "
              "them since R2 deleted the dynamic-probe reservation");
        // The engine-side column costs something real and is gated where it
        // renders: gi.ddgi (bounces 1 -> 3 on the DDGI-fed floor).
    }
    // A tier switch DOWN from Epic hands the column back: a Medium scene has
    // one bounce, whatever it was before.
    {
        auto s = freshScene();
        worldmodes::setRayon(s, true, RayonTier::Epic);
        worldmodes::setRayon(s, true, RayonTier::Medium);
        CHECK(giBounces(s) == 1, "Medium after Epic is back to 1 bounce");
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

    // And so is Epic's column: the Advanced "Light Bounces" row goes through
    // the registry, so an edit there is a pin.
    CHECK(worldmodes::setRowValue(s, QStringLiteral("giBounces"), 2), "pin the bounces to 2 at Epic");
    CHECK(giBounces(s) == 2 && worldmodes::rayonCustom(s), "Epic at 2 bounces is Custom");
    CHECK(worldmodes::rayonDeviations(s) == QStringList{ QStringLiteral("Rayon Light Bounces") },
          "and the deviation is named");
    worldmodes::setRayon(s, true, RayonTier::High);
    CHECK(giBounces(s) == 2, "a tier switch keeps the pinned bounces");
    CHECK(!worldmodes::setRowValue(s, QStringLiteral("giBounces"), 9), "an out-of-range bounce count is refused");
    // THE DELETED ROW (lane R2): `giDynamicProbes` is not a registry row any
    // more, so the registry refuses it by name — which is also the guard that
    // nothing re-introduces it quietly.
    CHECK(!worldmodes::setRowValue(s, QStringLiteral("giDynamicProbes"), 4),
          "the retired dynamic-probe row is gone from the registry");
    CHECK(worldmodes::rayonRowIds().size() == 5 &&
          !worldmodes::rayonRowIds().contains(QStringLiteral("giDynamicProbes")),
          "Rayon is a FIVE-column table (the probe capture size joined it)");
    // THE PROBE CAPTURE SIZE ROW (owner 2026-09-13 Q4) — a tier row with a pin
    // like every other: an explicit size survives a tier switch, is named as a
    // deviation, and Reset hands it back to Automatic.
    CHECK(worldmodes::setRowValue(s, QStringLiteral("giProbeSize"), 512),
          "pin the probe capture size to 512 px");
    CHECK(s->giProbeCaptureSize == 512 && worldmodes::rayonCustom(s),
          "a pinned probe size is Custom");
    CHECK(worldmodes::rayonDeviations(s).contains(QStringLiteral("Rayon Probe Capture Size")),
          "and the deviation is named");
    worldmodes::setRayon(s, true, RayonTier::Epic);
    CHECK(s->giProbeCaptureSize == 512, "a tier switch keeps the pinned probe size");
    CHECK(!worldmodes::setRowValue(s, QStringLiteral("giProbeSize"), 333),
          "a size that is not on the dial is refused by the registry");
    worldmodes::setRayon(s, true, RayonTier::High);
    worldmodes::clearRayonOverrides(s);
    CHECK(giBounces(s) == 1 && s->giProbeCaptureSize == 0 && !worldmodes::rayonCustom(s),
          "and Reset hands both columns back to High");
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
    struct Want { worldmodes::Mode mode; int giMode, giQuality, ddgi, bounces, dynamic; const char *name; };
    const Want wants[] = {
        { worldmodes::Mode::Low,    0, 0, 0, 1, 0, "World Low leaves GI off, as it always did" },
        { worldmodes::Mode::Medium, 0, 1, 0, 1, 0, "World Medium leaves GI off, as it always did" },
        { worldmodes::Mode::High,   1, 0, 0, 1, 0, "World High is Instant Radiosity at low quality, as it always did" },
        { worldmodes::Mode::Epic,   3, 2, 1, 3, 0, "World Epic is Rayon Epic: the hybrid, high, the field, 3 bounces" },
    };
    for (const Want &w : wants) {
        auto s = freshScene();
        worldmodes::setMode(s, w.mode);
        CHECK(giMode(s) == w.giMode && giDdgi(s) == w.ddgi, w.name);
        if (w.giMode != 0) CHECK(giQuality(s) == w.giQuality && giBounces(s) == w.bounces,
                                 "  ... and its quality and bounces");
    }

    // A pinned Rayon dial survives a World Mode switch like any other pin —
    // this is what keeps a migrated sample rendering as it was serialized.
    auto s = freshScene();
    worldmodes::setMode(s, worldmodes::Mode::Epic);
    CHECK(worldmodes::setRowValue(s, worldmodes::rayonRowId(), 2), "pin the Rayon dial to Medium");
    CHECK(giMode(s) == 2 && giQuality(s) == 1 && giDdgi(s) == 1 && giBounces(s) == 1,
          "the pin resolved Medium through (VCT, medium, DDGI-fed, 1 bounce)");
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
    CHECK(giBounces(s) == 3, "with three bounces (Epic's column)");
    CHECK(s->giDdgiIntensity == 1.0f, "at the calibrated intensity 1.0");
    CHECK(s->giDdgiSource == -1, "and the field's source left at auto (voxel at every tier)");
}

// ---------------------------------------------------------------------------
// 6. MIGRATION — the five pre-tier shipped samples, from their real databases
// ---------------------------------------------------------------------------
static void testMigration()
{
    std::printf("\n-- 6. migration --\n");
    // THE REAL SERIALIZED VALUES, read out of scenes/*.zip's databases on
    // 2026-09-09. Three (Matcaps, Particles, Physics) carry vct+medium, giDdgi
    // ABSENT (-1), giNumBounces 1, no GI pins and NO giTier — the
    // pre-unification shape, so they derive here. Skeletal Animation and World
    // Background were re-staged under the one-day P2 table and carry
    // giTier=medium with giDdgi normalised to 0 and no field pin: those take
    // the reader's option-(b) bump instead (scenereader.cpp: a tier-carrying
    // document that never carried the retired `giDynamicProbes` key has its
    // tier re-applied, pins
    // honoured), which is the tier application section 1 pins — modelled
    // below as the same setRayon call. Mirror Room and Showroom carry
    // giTier=epic and were re-staged by this lane to Epic's columns without
    // the generator pins. All seven are on worldMode "epic".
    struct Sample {
        const char *name;
        int giMode, giQuality, giDdgi, giBounces;   // as serialized (-1 = the absent tri-state)
        RayonTier wantTier;
        const char *why;
    };
    const Sample samples[] = {
        { "Matcaps",           2, 1, -1, 1, RayonTier::Medium, "vct + medium -> Medium" },
        { "Particles",         2, 1, -1, 1, RayonTier::Medium, "vct + medium -> Medium" },
        { "Physics",           2, 1, -1, 1, RayonTier::Medium, "vct + medium -> Medium" },
    };
    for (const Sample &sm : samples) {
        auto s = freshScene();
        s->giMode = iris::GiMode(sm.giMode);
        s->giQuality = iris::GiQuality(sm.giQuality);
        s->giDdgi = sm.giDdgi;
        s->giNumBounces = sm.giBounces;
        s->worldMode = int(worldmodes::Mode::Epic);
        // What the renderer reads, BEFORE.
        const int mode0 = giMode(s), quality0 = giQuality(s), bounces0 = giBounces(s);

        worldmodes::deriveRayonFromDocument(s);

        // THE ACCEPTANCE CRITERION, option (b): technique, quality and bounces
        // did not move — and the untouched field FOLLOWED THE
        // TIER, which for a vct+medium document is ON. That is the owner's
        // re-pin of these five samples (Rayon-2 S1-S3: the DDGI-fed arm is the
        // one that is right in open AND sealed scenes), taken here on purpose.
        CHECK(giMode(s) == mode0 && giQuality(s) == quality0 && giBounces(s) == bounces0,
              qPrintable(QStringLiteral("%1: technique, quality, bounces preserved").arg(sm.name)));
        CHECK(giDdgi(s) == 1,
              qPrintable(QStringLiteral("%1: the untouched field follows the tier -> DDGI-fed (the re-pin)").arg(sm.name)));
        CHECK(worldmodes::rayonTier(s) == sm.wantTier, sm.why);
        CHECK(!worldmodes::rayonCustom(s),
              qPrintable(QStringLiteral("%1: reads as its tier, not as Custom").arg(sm.name)));
        // The dial itself is pinned, because this scene's World Mode (Epic)
        // would otherwise resolve it to Epic and change how it renders.
        CHECK(s->worldOverrides.contains(worldmodes::rayonRowId()),
              qPrintable(QStringLiteral("%1: the Rayon dial is pinned against the World Mode").arg(sm.name)));
        worldmodes::setMode(s, worldmodes::Mode::Epic);
        CHECK(giMode(s) == mode0 && giQuality(s) == quality0 && giDdgi(s) == 1 && giBounces(s) == bounces0,
              qPrintable(QStringLiteral("%1: and re-applying its World Mode still changes nothing").arg(sm.name)));
    }

    // The P2-table shape of Skeletal Animation and World Background (giTier
    // medium, field normalised to 0, dial pinned against world Epic, nothing
    // else pinned): the reader re-applies the tier, and the field comes up.
    {
        auto s = freshScene();
        s->giMode = iris::GiMode::VCT;
        s->giQuality = iris::GiQuality::MEDIUM;
        s->giDdgi = 0;
        s->giTier = int(RayonTier::Medium);
        s->worldMode = int(worldmodes::Mode::Epic);
        s->worldOverrides.insert(worldmodes::rayonRowId(), 2);
        worldmodes::setRayon(s, worldmodes::rayonEnabled(s), worldmodes::rayonTier(s));
        CHECK(giMode(s) == 2 && giQuality(s) == 1 && giDdgi(s) == 1 && giBounces(s) == 1,
              "Skeletal/World Background: the re-applied Medium tier turns the field on, nothing else moves");
        CHECK(!worldmodes::rayonCustom(s) && s->worldOverrides.value(worldmodes::rayonRowId()).toInt() == 2,
              "reads as Medium (not Custom), dial pin kept");
        // The same shape with the field PINNED off keeps it off — a pin is a pin.
        s->giDdgi = 0;
        s->worldOverrides.insert(QStringLiteral("giDdgi"), 0);
        worldmodes::setRayon(s, worldmodes::rayonEnabled(s), worldmodes::rayonTier(s));
        CHECK(giDdgi(s) == 0 && worldmodes::rayonCustom(s), "a pinned field survives the re-application");
    }

    // The pre-re-stage shape of the two hybrid samples (hybrid + high, field
    // absent, giMode/giQuality pins from their World-Mode-row days): derives
    // High — DDGI-fed now — with the redundant pins dropped.
    {
        auto s = freshScene();
        s->giMode = iris::GiMode::VCT_PCC_HYBRID;
        s->giQuality = iris::GiQuality::HIGH;
        s->giDdgi = -1;
        s->worldMode = int(worldmodes::Mode::Epic);
        s->worldOverrides.insert(QStringLiteral("giMode"), 3);
        s->worldOverrides.insert(QStringLiteral("giQuality"), 2);
        worldmodes::deriveRayonFromDocument(s);
        CHECK(worldmodes::rayonTier(s) == RayonTier::High, "hybrid + high, field untouched -> High");
        CHECK(giDdgi(s) == 1 && giBounces(s) == 1, "DDGI-fed, one bounce");
        CHECK(!worldmodes::rayonCustom(s), "reads as High, not Custom");
        CHECK(!s->worldOverrides.contains(QStringLiteral("giMode")) &&
                  !s->worldOverrides.contains(QStringLiteral("giQuality")),
              "redundant row pins dropped");
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
    // An EXPLICIT field value is preserved, both ways: a document that opted
    // out renders without the field (pinned, Custom); one that opted in at
    // Medium IS the Medium row now and needs no pin.
    {
        auto s = freshScene();
        s->giMode = iris::GiMode::VCT;
        s->giQuality = iris::GiQuality::MEDIUM;
        s->giDdgi = 0;
        s->worldMode = int(worldmodes::Mode::Epic);
        worldmodes::deriveRayonFromDocument(s);
        CHECK(giDdgi(s) == 0 && s->worldOverrides.value(QStringLiteral("giDdgi")).toInt() == 0,
              "an explicit field OFF survives as a pin");
        CHECK(worldmodes::rayonCustom(s), "and reads Custom (Medium without its field)");
    }
    {
        auto s = freshScene();
        s->giMode = iris::GiMode::VCT;
        s->giQuality = iris::GiQuality::MEDIUM;
        s->giDdgi = 1;
        s->worldMode = int(worldmodes::Mode::Epic);
        worldmodes::deriveRayonFromDocument(s);
        CHECK(giDdgi(s) == 1 && !s->worldOverrides.contains(QStringLiteral("giDdgi")) &&
                  !worldmodes::rayonCustom(s),
              "a P1-era explicit field ON at Medium is the Medium row: no pin, not Custom");
    }
    // A hand-set bounce count deviates and is kept as a pin (Epic's column is
    // a row like any other; nothing is owed to old data, but what the document
    // rendered is preserved where preserving costs nothing).
    {
        auto s = freshScene();
        s->giMode = iris::GiMode::VCT;
        s->giQuality = iris::GiQuality::MEDIUM;
        s->giNumBounces = 3;
        s->worldMode = int(worldmodes::Mode::Epic);
        worldmodes::deriveRayonFromDocument(s);
        CHECK(worldmodes::rayonTier(s) == RayonTier::Medium && giBounces(s) == 3 &&
                  s->worldOverrides.value(QStringLiteral("giBounces")).toInt() == 3,
              "vct + medium at 3 bounces derives Medium with the bounces pinned");
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
    // A P1-era scene that opted into the field explicitly on the hybrid is
    // the new HIGH row, not Epic: no pre-tier document could have rendered
    // Epic's bounces, so Epic is only ever chosen deliberately.
    {
        auto s = freshScene();
        s->giMode = iris::GiMode::VCT_PCC_HYBRID;
        s->giQuality = iris::GiQuality::HIGH;
        s->giDdgi = 1;
        s->worldMode = int(worldmodes::Mode::Custom);
        worldmodes::deriveRayonFromDocument(s);
        CHECK(worldmodes::rayonTier(s) == RayonTier::High, "hybrid + high + field derives High (never Epic)");
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
