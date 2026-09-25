// gi.tiers — PHOTON's tier table, its pins, and its migration
// (SPECS/GI_UNIFIED_SPEC.md §2 / P2).
//
// Document-only and display-free: this is the RESOLUTION half of the Photon
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
//   4. THE WORLD MODE — one owner: applying a World Mode drives the Photon row
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
//                       with a tolerance);
//   7. THE TEXTS      — the rows that name a reflection source at High/Epic and
//                       the technique's name follow the engine's rayReflections
//                       in both machine states (STUDIO-CRUD-1 item 6).
#include <QGuiApplication>
#include <cstdio>

#include "irisgl/irisglfwd.h"
#include "irisgl/document/scenegraph/scene.h"

#include "services/worldmodes.h"
#include "jahshaka/engine/Types.h"

#include "../support/documentgraph.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

using worldmodes::PhotonTier;

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
    // (voxel resolution, probe faces/HDR/shadows, the 8192-probe field grid)
    // follow giQuality / the engine and are pinned
    // by gi.modes, gi.pcc_mirror and gi.ddgi.
    // The fifth column is the PROBE CAPTURE SIZE (owner 2026-09-13 Q4): 0 at
    // every tier = "follow the engine's quality dial", because the halving that
    // decision asked for is the engine's default now (High 512 -> 256). The
    // column exists so a scene can PIN a size, which case 3 gates.
    // The SIXTH is PHOTON'S CAMERA CASCADES (PHOTON_SPEC §7 E2 (6)): ON in
    // every tier, which is what "the boundary is gone for users" means.
    // The TECHNIQUE ordinals moved with Instant Radiosity's deletion (E2 (4)):
    // GiMode is Off 0 / VCT 1 / the hybrid 2, and Low is a voxel tier now.
    struct Want { PhotonTier tier; int mode, quality, ddgi, bounces, probeSize, cascades; const char *name; };
    const Want wants[] = {
        { PhotonTier::Low,    1, 0, 1, 1, 0, 1, "Low = VCT, two cascades at 64^3, FIELD ON, 1 bounce, no probes" },
        { PhotonTier::Medium, 1, 1, 1, 1, 0, 1, "Medium = VCT 64^3, FIELD ON (DDGI-fed), 1 bounce" },
        { PhotonTier::High,   2, 2, 1, 1, 0, 1, "High = VCT + probes 128^3, FIELD ON, 1 bounce" },
        { PhotonTier::Epic,   2, 2, 1, 3, 0, 1, "Epic = VCT + probes 128^3, FIELD ON, THREE bounces" },
    };
    for (const Want &w : wants) {
        auto s = freshScene();
        worldmodes::setPhoton(s, true, w.tier);
        CHECK(giMode(s) == w.mode && giQuality(s) == w.quality && giDdgi(s) == w.ddgi &&
                  giBounces(s) == w.bounces && s->giProbeCaptureSize == w.probeSize &&
                  s->giCascades == (w.cascades != 0), w.name);
        // The accessors ARE the table (one owner): what they say per column
        // must be what the tier wrote.
        CHECK(worldmodes::photonTechnique(w.tier) == w.mode &&
                  worldmodes::photonQuality(w.tier) == w.quality &&
                  worldmodes::photonDdgi(w.tier) == w.ddgi &&
                  worldmodes::photonBounces(w.tier) == w.bounces &&
                  worldmodes::photonProbeSize(w.tier) == w.probeSize &&
                  worldmodes::photonCascades(w.tier) == w.cascades,
              "the column accessors agree with the write-through");
        CHECK(s->giTier == int(w.tier), "the tier is recorded on the document");
        CHECK(worldmodes::photonEnabled(s), "and Photon reads enabled");
        CHECK(!worldmodes::photonCustom(s), "a freshly applied tier is not Custom");
        // The write-through invariant, for each of the five rows: the backing
        // field IS the resolved value, so every existing reader (the mirror,
        // the serializer, world.gi) sees the tier without knowing it exists.
        for (const QString &id : worldmodes::photonRowIds()) {
            const worldmodes::Row *r = worldmodes::row(id);
            CHECK(r && r->tierSpace == worldmodes::TierSpace::Photon &&
                      worldmodes::resolved(s, *r) == worldmodes::tierValue(*r, worldmodes::mode(s), s),
                  qPrintable(QStringLiteral("write-through holds for %1").arg(id)));
        }
    }
    CHECK(worldmodes::photonRowIds().size() == 6, "the tier writes exactly six rows through");
    // 5 x 4: EVERY cell of every Photon-tiered row is the table's cell. The rows
    // derive their tier[] from kPhotonTable (worldmodes.cpp photonColumns) and
    // the public readers read the same table one column at a time, so a cell
    // that disagrees here is a second copy of the table — which is exactly what
    // the rayontiers review found (13 of 20 cells untested while hand-copied).
    {
        using Reader = int (*)(PhotonTier);
        const Reader readers[6] = { worldmodes::photonTechnique, worldmodes::photonQuality,
                                    worldmodes::photonDdgi, worldmodes::photonBounces,
                                    worldmodes::photonProbeSize, worldmodes::photonCascades };
        const QStringList ids = worldmodes::photonRowIds();
        for (int c = 0; c < 6 && c < ids.size(); ++c) {
            const worldmodes::Row *r = worldmodes::row(ids[c]);
            for (int t = 0; t < 4; ++t) {
                const int want = readers[c](PhotonTier(t));
                CHECK(r && r->tier[t] == want,
                      qPrintable(QStringLiteral("%1.tier[%2] == kPhotonTable[%2] column %3 (%4)")
                                     .arg(ids[c]).arg(t).arg(c).arg(want)));
            }
        }
    }
    // ---- THE TIER AGAINST THE ENGINE'S OWN TABLE (render audit A5) --------
    //
    // Everything above asserts the registry against ITSELF: the rows against
    // kPhotonTable, the write-through against the rows. That is exactly the
    // hole the audit named — five tier tooltips described a renderer that did
    // not exist, for MONTHS, because nothing compared a tier's description
    // with what the engine does with it. These assertions read
    // `jahshaka::engine::giQualityFacts` — the engine's OWN tier table, the one
    // OgreGi.cpp builds the cascade chain, the voxel volume and the probe faces
    // from — and require the generated text to contain its numbers.
    {
        using jahshaka::engine::GiQuality;
        using jahshaka::engine::giQualityFacts;
        using jahshaka::engine::giCascadeCell;

        // (i) the facts themselves, as physics: a chain must grow OUTWARD in
        // both reach and cell or the cone march cannot hand over (the rule
        // resolveCascadeTable enforces on a PINNED table; the tier's own table
        // has to satisfy it too, and nothing checked that it did).
        for (int q = 0; q < 3; ++q) {
            const auto facts = giQualityFacts(GiQuality(q));
            CHECK(facts.cascadeCount > 0 && facts.cascadeCount <= 4,
                  qPrintable(QStringLiteral("quality %1: the tier table has 1-4 cascades").arg(q)));
            bool grows = true;
            for (int i = 1; i < facts.cascadeCount; ++i)
                grows = grows && facts.cascades[i].halfSize > facts.cascades[i - 1].halfSize &&
                        giCascadeCell(facts.cascades[i]) > giCascadeCell(facts.cascades[i - 1]);
            CHECK(grows, qPrintable(QStringLiteral("quality %1: every cascade is bigger AND "
                                                   "coarser than the one inside it").arg(q)));
            CHECK(facts.voxelResolution >= 16u && facts.probeFaceSize >= 64u,
                  qPrintable(QStringLiteral("quality %1: the single volume and the probe face "
                                            "are sane sizes").arg(q)));
        }
        // (ii) the two expensive probe options resolve ON at High and Epic and
        // nowhere else — the pair GiToggle::Auto reads.
        CHECK(giQualityFacts(GiQuality::High).probeHdrDefault &&
                  giQualityFacts(GiQuality::High).probeShadowsDefault &&
                  !giQualityFacts(GiQuality::Medium).probeHdrDefault &&
                  !giQualityFacts(GiQuality::Low).probeHdrDefault &&
                  !giQualityFacts(GiQuality::Medium).probeShadowsDefault &&
                  !giQualityFacts(GiQuality::Low).probeShadowsDefault,
              "HDR and shadowed probe captures are the High quality column, and only it");

        // (iii) THE DESCRIPTION IS THE TABLE. Each tier's generated sentence
        // must carry its own cascade count and the innermost cascade's
        // resolution — the two numbers the old hand-written tooltips got wrong
        // ("Medium voxelizes at twice the resolution"; "32/64/128 voxels per
        // axis"). A sentence that stops naming them has stopped being generated.
        for (int t = 0; t < 4; ++t) {
            const PhotonTier tier = PhotonTier(t);
            const auto facts = giQualityFacts(GiQuality(qBound(0, worldmodes::photonQuality(tier), 2)));
            const QString text = worldmodes::photonTierSentence(tier);
            CHECK(text.contains(QString::number(facts.cascadeCount)) &&
                      text.contains(QString::number(facts.cascades[0].resolution)),
                  qPrintable(QStringLiteral("tier %1's description names its own chain (%2 "
                                            "cascades, innermost %3 cubed): %4")
                                 .arg(worldmodes::photonTierName(tier))
                                 .arg(facts.cascadeCount)
                                 .arg(facts.cascades[0].resolution)
                                 .arg(text)));
            CHECK(text.contains(QStringLiteral("%1 light bounce").arg(worldmodes::photonBounces(tier))),
                  qPrintable(QStringLiteral("tier %1's description names its bounce count")
                                 .arg(worldmodes::photonTierName(tier))));
            // Probes are the TECHNIQUE column, not the quality one.
            const bool probes = worldmodes::photonTechnique(tier) == 2;
            CHECK(text.contains(QStringLiteral("no reflection probes")) != probes,
                  qPrintable(QStringLiteral("tier %1's description tells the truth about probes")
                                 .arg(worldmodes::photonTierName(tier))));
            // ...and at a RAY tier it says the grid is not built where rays run
            // (PHOTON-F12-PCC; the engine's own rayReflections row).
            if (probes && facts.rayReflections)
                CHECK(text.contains(QStringLiteral("no reflection-probe grid is built")),
                      qPrintable(QStringLiteral("tier %1 says it builds no grid where rays run: %2")
                                     .arg(worldmodes::photonTierName(tier), text)));
            if (probes)
                CHECK(text.contains(QString::number(worldmodes::photonTierProbeFaceSize(tier))),
                      qPrintable(QStringLiteral("tier %1 names its probe face size (%2 px)")
                                     .arg(worldmodes::photonTierName(tier))
                                     .arg(worldmodes::photonTierProbeFaceSize(tier))));
        }
        // (iv) Low's chain is 64 (PHOTON_SPEC §7 E2 (4): a 0.31 m cell smears a
        // room's own walls) and so is its scene-fitted volume since PHOTON-VOXEL-4
        // (at 32 the field's corner fell outside its derived bracket).
        CHECK(giQualityFacts(GiQuality::Low).cascades[0].resolution == 64 &&
                  giQualityFacts(GiQuality::Low).voxelResolution == 64u,
              "Low: the CHAIN and the single scene-fitted volume are both 64 per axis");
        CHECK(worldmodes::photonTierVoxelPhrase(PhotonTier::Low) == QStringLiteral("64") &&
                  worldmodes::photonTierVoxelPhrase(PhotonTier::Medium) == QStringLiteral("64"),
              "Low and Medium voxelise the chain at the SAME resolution — the "
              "\"Medium is twice Low\" tooltip was never true");
        CHECK(worldmodes::photonTierVoxelPhrase(PhotonTier::High).contains(QStringLiteral("64")) &&
                  worldmodes::photonTierVoxelPhrase(PhotonTier::High).contains(QStringLiteral("128")),
              "High's chain is BOTH 64 and 128 — two of its four cascades are 64");
        // (v) every tier feeds the irradiance field, Low included. The panel
        // said "Low cannot" for months; the column has always been 1.
        for (int t = 0; t < 4; ++t)
            CHECK(worldmodes::photonDdgi(PhotonTier(t)) == 1,
                  qPrintable(QStringLiteral("tier %1 turns the irradiance field ON")
                                 .arg(worldmodes::photonTierName(PhotonTier(t)))));
    }

    // High and Epic differ in TWO rows and nowhere else — the whole point of
    // Epic's column (before option (b) they differed only in the field, and
    // once High is DDGI-fed that would have collapsed them onto one row).
    {
        auto h = freshScene(), e = freshScene();
        worldmodes::setPhoton(h, true, PhotonTier::High);
        worldmodes::setPhoton(e, true, PhotonTier::Epic);
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
        worldmodes::setPhoton(s, true, PhotonTier::Epic);
        worldmodes::setPhoton(s, true, PhotonTier::Medium);
        CHECK(giBounces(s) == 1, "Medium after Epic is back to 1 bounce");
    }
    auto s = freshScene();
    // Nor is the update budget (owner decision D5: its own visible row).
    s->giUpdateBudget = 0;
    worldmodes::setPhoton(s, true, PhotonTier::Low);
    CHECK(s->giUpdateBudget == 0, "a tier switch never touches the GI update budget");
}

// ---------------------------------------------------------------------------
// 2. THE SWITCH
// ---------------------------------------------------------------------------
static void testSwitch()
{
    std::printf("\n-- 2. the switch --\n");
    auto s = freshScene();
    worldmodes::setPhoton(s, true, PhotonTier::High);
    CHECK(giMode(s) == 2, "Photon on at High = the hybrid");

    worldmodes::setPhoton(s, false, worldmodes::photonTier(s));
    CHECK(giMode(s) == 0, "off is giMode OFF — the renderer's own switch, not a second flag");
    CHECK(!worldmodes::photonEnabled(s), "and Photon reads disabled");
    CHECK(s->giTier == int(PhotonTier::High), "the tier is REMEMBERED while off");
    CHECK(!worldmodes::photonCustom(s), "an off scene is never 'Custom' (nothing to deviate from)");

    worldmodes::setPhoton(s, true, worldmodes::photonTier(s));
    CHECK(giMode(s) == 2 && giQuality(s) == 2,
          "turning it back on restores the remembered quality");

    // A pinned technique cannot outlive an off: the pin and the enable share
    // one field, and "off, but pinned to VCT" is a state nothing can render.
    worldmodes::setRowValue(s, QStringLiteral("giMode"), 2);
    CHECK(s->worldOverrides.contains(QStringLiteral("giMode")), "the technique is pinned to VCT");
    worldmodes::setPhoton(s, false, worldmodes::photonTier(s));
    CHECK(!s->worldOverrides.contains(QStringLiteral("giMode")),
          "turning Photon off drops the technique pin");
    worldmodes::setPhoton(s, true, worldmodes::photonTier(s));
    CHECK(giMode(s) == 2, "and turning it on gives the tier's technique back");
}

// ---------------------------------------------------------------------------
// 3. PINS — the Advanced disclosure's whole contract
// ---------------------------------------------------------------------------
static void testPins()
{
    std::printf("\n-- 3. pins --\n");
    auto s = freshScene();
    worldmodes::setPhoton(s, true, PhotonTier::Epic);
    CHECK(giQuality(s) == 2 && giDdgi(s) == 1, "Epic to start with");

    // An Advanced edit: quality down to medium, everything else left alone.
    CHECK(worldmodes::setRowValue(s, QStringLiteral("giQuality"), 1), "pin the quality to Medium");
    CHECK(giQuality(s) == 1, "the backing field followed the pin");
    CHECK(worldmodes::photonCustom(s), "the tier row now reads Custom");
    CHECK(worldmodes::photonDeviations(s).size() == 1, "exactly one deviation is reported");

    // THE CONTRACT: a tier switch moves everything EXCEPT the pin.
    worldmodes::setPhoton(s, true, PhotonTier::Medium);
    CHECK(giMode(s) == 1, "the tier switch moved the technique");
    CHECK(giQuality(s) == 1, "and left the pinned quality alone");
    worldmodes::setPhoton(s, true, PhotonTier::Epic);
    CHECK(giMode(s) == 2 && giDdgi(s) == 1, "back at Epic, the unpinned rows follow again");
    CHECK(giQuality(s) == 1, "the pin SURVIVED both switches");

    // And it can be handed back, one row or all of them.
    worldmodes::clearOverride(s, QStringLiteral("giQuality"));
    CHECK(giQuality(s) == 2, "clearOverride puts the tier's quality back");
    CHECK(!worldmodes::photonCustom(s), "and the tier row stops saying Custom");

    // The field is pinnable the same way, including AGAINST Epic.
    CHECK(worldmodes::setRowValue(s, QStringLiteral("giDdgi"), 0), "pin the field off at Epic");
    CHECK(giDdgi(s) == 0 && worldmodes::photonCustom(s), "Epic without its field is Custom");
    worldmodes::clearPhotonOverrides(s);
    CHECK(giDdgi(s) == 1 && !worldmodes::photonCustom(s),
          "Reset Advanced Settings hands every Photon row back to the tier");

    // And so is Epic's column: the Advanced "Light Bounces" row goes through
    // the registry, so an edit there is a pin.
    CHECK(worldmodes::setRowValue(s, QStringLiteral("giBounces"), 2), "pin the bounces to 2 at Epic");
    CHECK(giBounces(s) == 2 && worldmodes::photonCustom(s), "Epic at 2 bounces is Custom");
    CHECK(worldmodes::photonDeviations(s) == QStringList{ QStringLiteral("Photon Light Bounces") },
          "and the deviation is named");
    worldmodes::setPhoton(s, true, PhotonTier::High);
    CHECK(giBounces(s) == 2, "a tier switch keeps the pinned bounces");
    CHECK(!worldmodes::setRowValue(s, QStringLiteral("giBounces"), 9), "an out-of-range bounce count is refused");
    // THE DELETED ROW (lane R2): `giDynamicProbes` is not a registry row any
    // more, so the registry refuses it by name — which is also the guard that
    // nothing re-introduces it quietly.
    CHECK(!worldmodes::setRowValue(s, QStringLiteral("giDynamicProbes"), 4),
          "the retired dynamic-probe row is gone from the registry");
    CHECK(worldmodes::photonRowIds().size() == 6 &&
          !worldmodes::photonRowIds().contains(QStringLiteral("giDynamicProbes")),
          "Photon is a SIX-column table (the cascade chain joined it, E2 (6))");
    // THE PROBE CAPTURE SIZE ROW (owner 2026-09-13 Q4) — a tier row with a pin
    // like every other: an explicit size survives a tier switch, is named as a
    // deviation, and Reset hands it back to Automatic.
    CHECK(worldmodes::setRowValue(s, QStringLiteral("giProbeSize"), 512),
          "pin the probe capture size to 512 px");
    CHECK(s->giProbeCaptureSize == 512 && worldmodes::photonCustom(s),
          "a pinned probe size is Custom");
    CHECK(worldmodes::photonDeviations(s).contains(QStringLiteral("Photon Probe Capture Size")),
          "and the deviation is named");
    worldmodes::setPhoton(s, true, PhotonTier::Epic);
    CHECK(s->giProbeCaptureSize == 512, "a tier switch keeps the pinned probe size");
    CHECK(!worldmodes::setRowValue(s, QStringLiteral("giProbeSize"), 333),
          "a size that is not on the dial is refused by the registry");
    worldmodes::setPhoton(s, true, PhotonTier::High);
    worldmodes::clearPhotonOverrides(s);
    CHECK(giBounces(s) == 1 && s->giProbeCaptureSize == 0 && !worldmodes::photonCustom(s),
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
        { worldmodes::Mode::High,   1, 0, 1, 1, 0, "World High is Photon Low: VCT, two cascades at 64^3, the field on" },
        { worldmodes::Mode::Epic,   2, 2, 1, 3, 0, "World Epic is Photon Epic: the hybrid, high, the field, 3 bounces" },
    };
    for (const Want &w : wants) {
        auto s = freshScene();
        worldmodes::setMode(s, w.mode);
        CHECK(giMode(s) == w.giMode && giDdgi(s) == w.ddgi, w.name);
        if (w.giMode != 0) CHECK(giQuality(s) == w.giQuality && giBounces(s) == w.bounces,
                                 "  ... and its quality and bounces");
    }

    // A pinned Photon dial survives a World Mode switch like any other pin —
    // this is what keeps a migrated sample rendering as it was serialized.
    auto s = freshScene();
    worldmodes::setMode(s, worldmodes::Mode::Epic);
    CHECK(worldmodes::setRowValue(s, worldmodes::photonRowId(), 2), "pin the Photon dial to Medium");
    CHECK(giMode(s) == 1 && giQuality(s) == 1 && giDdgi(s) == 1 && giBounces(s) == 1,
          "the pin resolved Medium through (VCT, medium, DDGI-fed, 1 bounce)");
    worldmodes::setMode(s, worldmodes::Mode::Low);
    CHECK(giMode(s) == 1 && giQuality(s) == 1,
          "and World Low did NOT switch it off — the pin won");
    CHECK(s->antiAliasing == 2 && s->shadowResolution == 512,
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
    CHECK(s->giTier == int(PhotonTier::Epic), "but it carries Epic as the tier it would come up at");
    // Exactly what MainWindow::createDefaultScene does.
    worldmodes::setMode(s, worldmodes::Mode::Epic);
    CHECK(worldmodes::photonEnabled(s), "a NEW scene is born with Photon ON");
    CHECK(worldmodes::photonTier(s) == PhotonTier::Epic, "at Epic");
    CHECK(giMode(s) == 2 && giQuality(s) == 2 && giDdgi(s) == 1,
          "which is the hybrid, high quality, irradiance field on");
    CHECK(giBounces(s) == 3, "with three bounces (Epic's column)");
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
    // below as the same setPhoton call. Mirror Room and Showroom carry
    // giTier=epic and were re-staged by this lane to Epic's columns without
    // the generator pins. All seven are on worldMode "epic".
    struct Sample {
        const char *name;
        int giMode, giQuality, giDdgi, giBounces;   // as serialized (-1 = the absent tri-state)
        PhotonTier wantTier;
        const char *why;
    };
    const Sample samples[] = {
        { "Matcaps",           1, 1, -1, 1, PhotonTier::Medium, "vct + medium -> Medium" },
        { "Particles",         1, 1, -1, 1, PhotonTier::Medium, "vct + medium -> Medium" },
        { "Physics",           1, 1, -1, 1, PhotonTier::Medium, "vct + medium -> Medium" },
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

        worldmodes::derivePhotonFromDocument(s);

        // THE ACCEPTANCE CRITERION, option (b): technique, quality and bounces
        // did not move — and the untouched field FOLLOWED THE
        // TIER, which for a vct+medium document is ON. That is the owner's
        // re-pin of these five samples (Photon-2 S1-S3: the DDGI-fed arm is the
        // one that is right in open AND sealed scenes), taken here on purpose.
        CHECK(giMode(s) == mode0 && giQuality(s) == quality0 && giBounces(s) == bounces0,
              qPrintable(QStringLiteral("%1: technique, quality, bounces preserved").arg(sm.name)));
        CHECK(giDdgi(s) == 1,
              qPrintable(QStringLiteral("%1: the untouched field follows the tier -> DDGI-fed (the re-pin)").arg(sm.name)));
        CHECK(worldmodes::photonTier(s) == sm.wantTier, sm.why);
        CHECK(!worldmodes::photonCustom(s),
              qPrintable(QStringLiteral("%1: reads as its tier, not as Custom").arg(sm.name)));
        // The dial itself is pinned, because this scene's World Mode (Epic)
        // would otherwise resolve it to Epic and change how it renders.
        CHECK(s->worldOverrides.contains(worldmodes::photonRowId()),
              qPrintable(QStringLiteral("%1: the Photon dial is pinned against the World Mode").arg(sm.name)));
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
        s->giTier = int(PhotonTier::Medium);
        s->worldMode = int(worldmodes::Mode::Epic);
        s->worldOverrides.insert(worldmodes::photonRowId(), 2);
        worldmodes::setPhoton(s, worldmodes::photonEnabled(s), worldmodes::photonTier(s));
        CHECK(giMode(s) == 1 && giQuality(s) == 1 && giDdgi(s) == 1 && giBounces(s) == 1,
              "Skeletal/World Background: the re-applied Medium tier turns the field on, nothing else moves");
        CHECK(!worldmodes::photonCustom(s) && s->worldOverrides.value(worldmodes::photonRowId()).toInt() == 2,
              "reads as Medium (not Custom), dial pin kept");
        // The same shape with the field PINNED off keeps it off — a pin is a pin.
        s->giDdgi = 0;
        s->worldOverrides.insert(QStringLiteral("giDdgi"), 0);
        worldmodes::setPhoton(s, worldmodes::photonEnabled(s), worldmodes::photonTier(s));
        CHECK(giDdgi(s) == 0 && worldmodes::photonCustom(s), "a pinned field survives the re-application");
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
        worldmodes::derivePhotonFromDocument(s);
        CHECK(worldmodes::photonTier(s) == PhotonTier::High, "hybrid + high, field untouched -> High");
        CHECK(giDdgi(s) == 1 && giBounces(s) == 1, "DDGI-fed, one bounce");
        CHECK(!worldmodes::photonCustom(s), "reads as High, not Custom");
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
        worldmodes::derivePhotonFromDocument(s);
        CHECK(worldmodes::photonTier(s) == PhotonTier::Medium, "vct + high derives Medium");
        CHECK(giQuality(s) == 2, "and keeps rendering at high quality");
        CHECK(s->worldOverrides.value(QStringLiteral("giQuality")).toInt() == 2,
              "because the deviation became a pin");
        CHECK(worldmodes::photonCustom(s), "which is exactly what 'Custom' means");
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
        worldmodes::derivePhotonFromDocument(s);
        CHECK(giDdgi(s) == 0 && s->worldOverrides.value(QStringLiteral("giDdgi")).toInt() == 0,
              "an explicit field OFF survives as a pin");
        CHECK(worldmodes::photonCustom(s), "and reads Custom (Medium without its field)");
    }
    {
        auto s = freshScene();
        s->giMode = iris::GiMode::VCT;
        s->giQuality = iris::GiQuality::MEDIUM;
        s->giDdgi = 1;
        s->worldMode = int(worldmodes::Mode::Epic);
        worldmodes::derivePhotonFromDocument(s);
        CHECK(giDdgi(s) == 1 && !s->worldOverrides.contains(QStringLiteral("giDdgi")) &&
                  !worldmodes::photonCustom(s),
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
        worldmodes::derivePhotonFromDocument(s);
        CHECK(worldmodes::photonTier(s) == PhotonTier::Medium && giBounces(s) == 3 &&
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
        worldmodes::derivePhotonFromDocument(s);
        CHECK(giMode(s) == 0, "an off scene stays off");
        CHECK(worldmodes::photonTier(s) == PhotonTier::Medium, "its quality picks the tier");
        CHECK(!s->worldOverrides.contains(QStringLiteral("giMode")),
              "and OFF is not recorded as a technique pin");
        CHECK(s->worldOverrides.value(worldmodes::photonRowId()).toInt() == 0,
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
        worldmodes::derivePhotonFromDocument(s);
        CHECK(worldmodes::photonTier(s) == PhotonTier::High, "hybrid + high + field derives High (never Epic)");
        CHECK(giDdgi(s) == 1 && !worldmodes::photonCustom(s), "with nothing pinned");
        CHECK(!s->worldOverrides.contains(worldmodes::photonRowId()),
              "a Custom-mode scene needs no dial pin: no tier can clobber it");
    }
}

// ---------------------------------------------------------------------------
// 7. THE TIER TEXTS FOLLOW THE ENGINE'S RAY FACT (STUDIO-CRUD-1 item 6)
// ---------------------------------------------------------------------------
// After PHOTON-F12-PCC no shipped tier builds a reflection-probe grid wherever
// the scene traces rays: High and Epic's reflections are the screen march, the
// traced rays and the voxel cone. The texts that name a reflection source at
// High and Epic are therefore machine-dependent, and they are computed from ONE
// fact — the engine's giQualityFacts(High).rayReflections met with the
// machine's sceneTracesRays. This asserts them against that fact in BOTH
// machine states, so a text can neither promise probes where rays resolve nor
// rays where they do not.
static void testTierTexts()
{
    std::printf("\n-- 7. the tier texts follow rayReflections --\n");
    const bool rayTier = jahshaka::engine::giQualityFacts(jahshaka::engine::GiQuality::High)
                             .rayReflections;
    CHECK(rayTier, "the engine's High quality traces its reflections (the premise of the texts)");

    // Every tier whose technique is the hybrid resolves rays exactly when the
    // engine says its quality does.
    for (PhotonTier t : { PhotonTier::Low, PhotonTier::Medium, PhotonTier::High, PhotonTier::Epic }) {
        const bool facts = jahshaka::engine::giQualityFacts(
            jahshaka::engine::GiQuality(worldmodes::photonQuality(t))).rayReflections;
        CHECK(worldmodes::tierRaysResolve(t, true) == facts
                  && !worldmodes::tierRaysResolve(t, false),
              qPrintable(QStringLiteral("%1: rays resolve iff the machine traces AND the "
                                        "quality's rayReflections").arg(worldmodes::photonTierName(t))));
    }

    struct Promise { const char *row; const char *probes; const char *rays; };
    const Promise promises[] = {
        { "ssr",                       "fall back to the reflection probes",
                                       "fall back to the traced rays" },
        { "reflectionRoughnessCutoff", "the reflection probes' own blurred photograph",
                                       "a traced ray answers the rest" },
        { "giMode",                    "VCT + probes adds sharp reflections",
                                       "VCT + rays is where the scene traces" },
        { "giQuality",                 "Under VCT + probes High ALSO",
                                       "High traces its reflections here" },
        { "giProbeSize",               "build a probe grid at all",
                                       "NO shipped tier builds a probe grid" },
    };
    for (bool traces : { false, true }) {
        const bool resolves = traces && rayTier;
        const char *machine = traces ? "tracing machine" : "non-tracing machine";
        CHECK(worldmodes::techniqueLabel(2, resolves)
                  == (resolves ? QStringLiteral("VCT + rays") : QStringLiteral("VCT + probes")),
              qPrintable(QStringLiteral("%1: the hybrid is named %2").arg(machine,
                  worldmodes::techniqueLabel(2, resolves))));
        // THE NAME IS THE SCENE'S (fix round): the World Modes combo, the
        // Photon section's Technique combo and world.modeTable all call
        // optionLabel(row, option, scene, traces), which reads the SAME
        // predicate world.gi and the probe rows read — probeGridByRays.
        const worldmodes::Row *mode = worldmodes::row(QStringLiteral("giMode"));
        for (int quality : { 0, 1, 2 }) {
            auto scene = iris::Scene::create();
            scene->giQuality = iris::GiQuality(quality);
            const bool here = worldmodes::probeGridByRays(scene, traces);
            const bool expect = traces && jahshaka::engine::giQualityFacts(
                                              jahshaka::engine::GiQuality(quality)).rayReflections;
            bool named = mode != nullptr && here == expect;
            if (mode)
                for (const worldmodes::EnumOption &o : mode->options)
                    if (worldmodes::optionLabel(*mode, o, scene, traces)
                        != worldmodes::techniqueLabel(o.value, here))
                        named = false;
            const QString hybrid = mode ? worldmodes::optionLabel(*mode, mode->options[2], scene,
                                                                  traces)
                                        : QString();
            CHECK(named && hybrid == (expect ? QStringLiteral("VCT + rays")
                                             : QStringLiteral("VCT + probes")),
                  qPrintable(QStringLiteral("%1, scene quality %2: the hybrid is named '%3' "
                                            "(probeGridByRays %4)")
                                 .arg(machine).arg(quality).arg(hybrid)
                                 .arg(here ? "true" : "false")));
        }
        for (const Promise &p : promises) {
            const worldmodes::Row *r = worldmodes::row(QString::fromLatin1(p.row));
            const QString text = r ? worldmodes::rowCost(*r, traces) : QString();
            CHECK(r && text.contains(QLatin1String(p.probes)) == !resolves,
                  qPrintable(QStringLiteral("%1: %2 %3 the probes").arg(machine,
                      QLatin1String(p.row), resolves ? "does not promise" : "names")));
            CHECK(r && text.contains(QLatin1String(p.rays)) == resolves,
                  qPrintable(QStringLiteral("%1: %2 %3 the rays").arg(machine,
                      QLatin1String(p.row), resolves ? "names" : "does not promise")));
        }
        // No row's text is forgotten by the machine-dependent split.
        bool allSay = true;
        for (const worldmodes::Row &r : worldmodes::rows())
            if (worldmodes::rowCost(r, traces).isEmpty()) {
                std::printf("      (%s has no text)\n", qPrintable(r.id));
                allSay = false;
            }
        CHECK(allSay, qPrintable(QStringLiteral("%1: every row has a text").arg(machine)));
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
    testTierTexts();

    std::printf(failures ? "\nFAILED: %d check(s)\n" : "\nALL CHECKS PASSED\n", failures);
    return failures ? 1 : 0;
}
