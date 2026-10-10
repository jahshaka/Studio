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
//                       and NOT the five rows underneath it; each mode runs
//                       Photon at the same name, Photon is never pinned, and a
//                       Photon off its column reads Custom (WORLD-MODE-1);
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
#include "services/testtier.h"
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
// What the scene RENDERS for the field: Auto (-1, what a tier leaves since
// DDGI-AUTO-1) resolved through the engine's fact, the row's own get.
static int giDdgi(const iris::ScenePtr &s)
{
    return s->giDdgi < 0 ? (worldmodes::photonFieldAuto(s) ? 1 : 0) : (s->giDdgi > 0 ? 1 : 0);
}
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
    // (The sixth column, the cascade switch, is deleted with the single
    // scene-fitted volume it selected — D4-PHOTON-TIERS: the chain always runs.)
    // The TECHNIQUE ordinals moved with Instant Radiosity's deletion (E2 (4)):
    // GiMode is Off 0 / VCT 1 / the hybrid 2, and Low is a voxel tier now.
    struct Want { PhotonTier tier; int mode, quality, ddgi, bounces, probeSize; const char *name; };
    const Want wants[] = {
        { PhotonTier::Low,    1, 0, 1, 1, 0, "Low = VCT, two cascades at 64^3, FIELD ON, 1 bounce, no probes" },
        { PhotonTier::Medium, 1, 1, 1, 1, 0, "Medium = VCT 64^3, FIELD ON (DDGI-fed), 1 bounce" },
        { PhotonTier::High,   2, 2, 1, 1, 0, "High = VCT + probes 128^3, FIELD ON, 1 bounce" },
        { PhotonTier::Epic,   2, 3, 1, 3, 0, "Epic = its own engine row (High + 4x the gather probes), FIELD ON, THREE bounces" },
    };
    for (const Want &w : wants) {
        auto s = freshScene();
        worldmodes::setPhoton(s, true, w.tier);
        CHECK(giMode(s) == w.mode && giQuality(s) == w.quality && giDdgi(s) == w.ddgi &&
                  giBounces(s) == w.bounces && s->giProbeCaptureSize == w.probeSize, w.name);
        // The accessors ARE the table (one owner): what they say per column
        // must be what the tier wrote.
        CHECK(worldmodes::photonTechnique(w.tier) == w.mode &&
                  worldmodes::photonQuality(w.tier) == w.quality &&
                  worldmodes::photonDdgi(w.tier) == w.ddgi &&
                  worldmodes::photonBounces(w.tier) == w.bounces &&
                  worldmodes::photonProbeSize(w.tier) == w.probeSize,
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
    CHECK(worldmodes::photonRowIds().size() == 5, "the tier writes exactly five rows through");
    // 5 x 4: EVERY cell of every Photon-tiered row is the table's cell. The rows
    // derive their tier[] from kPhotonTable (worldmodes.cpp photonColumns) and
    // the public readers read the same table one column at a time, so a cell
    // that disagrees here is a second copy of the table — which is exactly what
    // the rayontiers review found (13 of 20 cells untested while hand-copied).
    {
        using Reader = int (*)(PhotonTier);
        const Reader readers[5] = { worldmodes::photonTechnique, worldmodes::photonQuality,
                                    worldmodes::photonDdgi, worldmodes::photonBounces,
                                    worldmodes::photonProbeSize };
        const QStringList ids = worldmodes::photonRowIds();
        for (int c = 0; c < 5 && c < ids.size(); ++c) {
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
        for (int q = 0; q < 4; ++q) {
            const auto facts = giQualityFacts(GiQuality(q));
            CHECK(facts.cascadeCount > 0 && facts.cascadeCount <= 4,
                  qPrintable(QStringLiteral("quality %1: the tier table has 1-4 cascades").arg(q)));
            bool grows = true;
            for (int i = 1; i < facts.cascadeCount; ++i)
                grows = grows && facts.cascades[i].halfSize > facts.cascades[i - 1].halfSize &&
                        giCascadeCell(facts.cascades[i]) > giCascadeCell(facts.cascades[i - 1]);
            CHECK(grows, qPrintable(QStringLiteral("quality %1: every cascade is bigger AND "
                                                   "coarser than the one inside it").arg(q)));
            CHECK(facts.probeFaceSize >= 64u,
                  qPrintable(QStringLiteral("quality %1: the probe face is a sane size").arg(q)));
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
            const auto facts = giQualityFacts(GiQuality(qBound(0, worldmodes::photonQuality(tier), 3)));
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
        // room's own walls).
        CHECK(giQualityFacts(GiQuality::Low).cascades[0].resolution == 64,
              "Low: the CHAIN is 64 per axis");
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
        CHECK(giMode(h) == giMode(e) && giDdgi(h) == giDdgi(e),
              "High and Epic share technique and the field");
        CHECK(giQuality(e) == 3 && giQuality(h) == 2 && giBounces(e) > giBounces(h),
              "and Epic carries its own quality row (four times the gather's probes) and the "
              "extra bounces"); 
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
    CHECK(giQuality(s) == 3 && giDdgi(s) == 1, "Epic to start with");

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
    CHECK(giQuality(s) == 3, "clearOverride puts the tier's quality back");
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
    CHECK(worldmodes::photonRowIds().size() == 5 &&
          !worldmodes::photonRowIds().contains(QStringLiteral("giDynamicProbes")),
          "Photon is a FIVE-column table (the cascade switch went with the single volume)");
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
    // THE IDENTITY (owner 2026-10-09, WORLD-MODE-1): each World Mode runs Photon
    // at the same name; the machinery rows follow the PHOTON column of that name.
    struct Want { worldmodes::Mode mode; int giMode, giQuality, ddgi, bounces; const char *name; };
    const Want wants[] = {
        { worldmodes::Mode::Low,    1, 0, 1, 1, "World Low is Photon Low: VCT, two cascades at 64^3, the field on" },
        { worldmodes::Mode::Medium, 1, 1, 1, 1, "World Medium is Photon Medium: VCT 64^3, the field on" },
        { worldmodes::Mode::High,   2, 2, 1, 1, "World High is Photon High: the hybrid at 128^3, the field on" },
        { worldmodes::Mode::Epic,   2, 3, 1, 3, "World Epic is Photon Epic: the hybrid, the epic row, the field, 3 bounces" },
    };
    for (const Want &w : wants) {
        auto s = freshScene();
        worldmodes::setMode(s, w.mode);
        CHECK(giMode(s) == w.giMode && giDdgi(s) == w.ddgi && giQuality(s) == w.giQuality &&
                  giBounces(s) == w.bounces, w.name);
    }

    // THE PHOTON ROW IS NEVER PINNED: a set of it writes through and records
    // nothing, so the next World Mode pick snaps it back to that mode's column.
    auto s = freshScene();
    worldmodes::setMode(s, worldmodes::Mode::Epic);
    CHECK(worldmodes::setRowValue(s, worldmodes::photonRowId(), 2), "set the Photon dial to Medium");
    CHECK(giMode(s) == 1 && giQuality(s) == 1 && giDdgi(s) == 1 && giBounces(s) == 1,
          "the set resolved Medium through (VCT, medium, DDGI-fed, 1 bounce)");
    CHECK(!s->worldOverrides.contains(worldmodes::photonRowId()), "and recorded NO pin");
    worldmodes::setMode(s, worldmodes::Mode::Low);
    CHECK(worldmodes::photonEnabled(s) && worldmodes::photonTier(s) == PhotonTier::Low,
          "World Low then snapped Photon to Low — the World Mode owns the dial");
    CHECK(s->antiAliasing == 2 && s->shadowResolution == 512,
          "while the unpinned world rows followed Low");
    worldmodes::pinRowValue(s, worldmodes::photonRowId(), 4);
    CHECK(!s->worldOverrides.contains(worldmodes::photonRowId()), "pinRowValue refuses the photon row");
    CHECK(!worldmodes::clearOverride(s, worldmodes::photonRowId()),
          "and clearOverride(photon) has nothing to drop");
}

// ---------------------------------------------------------------------------
// 4b. THE IDENTITY AND THE HONEST CUSTOM (WORLD-MODE-1, the brief's named checks)
// ---------------------------------------------------------------------------
static void testWorldModePhotonIdentity()
{
    std::printf("\n-- 4b. world_mode_photon_identity --\n");
    for (int i = 0; i < 4; ++i) {
        const worldmodes::Mode m = worldmodes::Mode(i);
        auto s = freshScene();
        worldmodes::setMode(s, m);
        CHECK(worldmodes::photonEnabled(s) && worldmodes::photonTier(s) == PhotonTier(i),
              qPrintable(QStringLiteral("world_mode_photon_identity: World %1 runs Photon %1")
                             .arg(worldmodes::modeName(m))));
        CHECK(worldmodes::mode(s) == m && worldmodes::pickedMode(s) == m &&
                  !worldmodes::photonCustom(s),
              qPrintable(QStringLiteral("world_mode_photon_identity: %1 reads %1, not Custom")
                             .arg(worldmodes::modeName(m))));
        // Every World-tier row the mode wrote holds its column, so none says custom
        // (the `*`-free, honest source after a plain pick).
        QStringList off;
        for (const worldmodes::Row &r : worldmodes::rows())
            if (r.tierSpace != worldmodes::TierSpace::None &&
                worldmodes::source(s, r) != QLatin1String("mode"))
                off << r.id;
        CHECK(off.isEmpty(), qPrintable(QStringLiteral("world_mode_photon_identity: every tiered row "
                                                       "reads source 'mode' at %1 (off: %2)")
                                            .arg(worldmodes::modeName(m), off.join(QLatin1Char(',')))));
    }
}

static void testPhotonOverrideReadsCustom()
{
    std::printf("\n-- 4c. photon_override_reads_custom --\n");
    const worldmodes::Row *photon = worldmodes::row(worldmodes::photonRowId());
    auto s = freshScene();
    worldmodes::setMode(s, worldmodes::Mode::Epic);
    worldmodes::setPhoton(s, true, PhotonTier::High);
    CHECK(worldmodes::mode(s) == worldmodes::Mode::Custom,
          "photon_override_reads_custom: Epic with Photon High reads Custom");
    CHECK(worldmodes::pickedMode(s) == worldmodes::Mode::Epic && s->worldMode == 3,
          "photon_override_reads_custom: the PICK is still Epic (worldMode is not rewritten)");
    CHECK(photon && worldmodes::source(s, *photon) == QLatin1String("custom"),
          "photon_override_reads_custom: the photon row's source is 'custom'");
    CHECK(photon && worldmodes::tierValue(*photon, worldmodes::pickedMode(s), s) == 4,
          "photon_override_reads_custom: its tierValue is still Epic's column");
    worldmodes::setPhoton(s, false, worldmodes::photonTier(s));
    CHECK(worldmodes::mode(s) == worldmodes::Mode::Custom,
          "photon_override_reads_custom: Photon OFF under Epic reads Custom too");
    worldmodes::setMode(s, worldmodes::Mode::Epic);
    CHECK(worldmodes::photonEnabled(s) && worldmodes::photonTier(s) == PhotonTier::Epic &&
              worldmodes::mode(s) == worldmodes::Mode::Epic,
          "photon_override_reads_custom: re-picking Epic resets Photon to Epic and reads Epic");
    CHECK(worldmodes::setRowValue(s, worldmodes::photonRowId(), 2, true) &&
              !s->worldOverrides.contains(worldmodes::photonRowId()),
          "photon_override_reads_custom: setRowValue(photon, 2, record) inserts no pin");
    CHECK(worldmodes::mode(s) == worldmodes::Mode::Custom && worldmodes::photonTier(s) == PhotonTier::Medium,
          "photon_override_reads_custom: ... and that set reads Custom at Medium");
    // A PIN on another row is not Custom: it is marked `override`, the mode stays.
    worldmodes::setMode(s, worldmodes::Mode::High);
    worldmodes::setRowValue(s, QStringLiteral("msaa"), 8);
    const worldmodes::Row *msaa = worldmodes::row(QStringLiteral("msaa"));
    CHECK(worldmodes::mode(s) == worldmodes::Mode::High && msaa &&
              worldmodes::source(s, *msaa) == QLatin1String("override"),
          "photon_override_reads_custom: a pinned msaa leaves the mode High (source 'override')");
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
    CHECK(giMode(s) == 2 && giQuality(s) == 3 && giDdgi(s) == 1,
          "which is the hybrid, the epic quality row, irradiance field on");
    CHECK(giBounces(s) == 3, "with three bounces (Epic's column)");
}

// ---------------------------------------------------------------------------
// 6. RE-APPLYING A TIER HONOURS PINS (the Photon derivation for documents
//    without `giTier` is DELETED — FORWARD-ONLY-1)
// ---------------------------------------------------------------------------
static void testTierReapply()
{
    std::printf("\n-- 6. tier re-application --\n");
    // A giTier-medium document with the field at 0, the dial pinned against a
    // World Epic and nothing else pinned: re-applying the tier turns the field
    // on; a PINNED field stays as pinned.
    {
        auto s = freshScene();
        s->giMode = iris::GiMode::VCT;
        s->giQuality = iris::GiQuality::MEDIUM;
        s->giDdgi = 0;
        s->giTier = int(PhotonTier::Medium);
        s->worldMode = int(worldmodes::Mode::Epic);
        worldmodes::setPhoton(s, worldmodes::photonEnabled(s), worldmodes::photonTier(s));
        CHECK(giMode(s) == 1 && giQuality(s) == 1 && giDdgi(s) == 1 && giBounces(s) == 1,
              "Skeletal/World Background: the re-applied Medium tier turns the field on, nothing else moves");
        CHECK(!worldmodes::photonCustom(s) && worldmodes::mode(s) == worldmodes::Mode::Custom,
              "the Photon tier reads Medium (not Custom), and the World Mode reads Custom (Medium under Epic)");
        // The same shape with the field PINNED off keeps it off — a pin is a pin.
        s->giDdgi = 0;
        s->worldOverrides.insert(QStringLiteral("giDdgi"), 0);
        worldmodes::setPhoton(s, worldmodes::photonEnabled(s), worldmodes::photonTier(s));
        CHECK(giDdgi(s) == 0 && worldmodes::photonCustom(s), "a pinned field survives the re-application");
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

// THE TEST-TIER SWITCH NEVER WRITES A PINNED ROW (TEST-NEEDS-1 fix rounds F3 + round 2). RED (by
// construction, the lead's round-2 read) at
// fd22dbc66 (the restore went through the Rows' get/set and counted any machinery pin): (a) an
// auto field (giDdgi -1) under a giMode pin came back concrete; (b) a giBounces pin alone kept
// Photon ON in a process whose list does not name it.
static void testTestTierPins()
{
    testtier::set(QStringLiteral("low"));
    qputenv(testtier::kNeedsEnvVar, "none");
    {   // (a) giMode pinned to vct, the field auto: both survive the switch, raw
        iris::ScenePtr s = freshScene();
        worldmodes::setMode(s, worldmodes::Mode::Epic);
        worldmodes::setRowValue(s, QStringLiteral("giMode"), 1, true);
        s->giDdgi = -1;
        const int bounces = s->giNumBounces;
        worldmodes::applyTestTier(s);
        CHECK(giMode(s) == 1 && s->worldOverrides.contains(QStringLiteral("giMode")),
              "test tier: a giMode pin stays (the field and the pin) under NEEDS none");
        CHECK(s->giDdgi == -1, "test tier: the auto field (giDdgi -1) is restored RAW, never concrete");
        CHECK(s->giNumBounces == bounces, "test tier: the machinery fields come back as the document had them");
    }
    {   // (b) a machinery pin alone does not keep Photon on
        iris::ScenePtr s = freshScene();
        worldmodes::setMode(s, worldmodes::Mode::Epic);
        worldmodes::setRowValue(s, QStringLiteral("giBounces"), 2, true);
        worldmodes::applyTestTier(s);
        CHECK(!worldmodes::photonEnabled(s), "test tier: a giBounces pin does not keep Photon ON (NEEDS none)");
        CHECK(s->worldOverrides.contains(QStringLiteral("giBounces")) && giBounces(s) == 2,
              "test tier: ...and the giBounces pin itself stays");
    }
    {   // NEEDS photon: Photon on at the mode's own tier; an unpinned bloom off, a pinned one stays
        qputenv(testtier::kNeedsEnvVar, "photon");
        iris::ScenePtr s = freshScene();
        worldmodes::setMode(s, worldmodes::Mode::Epic);
        worldmodes::setRowValue(s, QStringLiteral("bloom"), 1, true);
        worldmodes::applyTestTier(s);
        CHECK(worldmodes::photonEnabled(s) && worldmodes::photonTier(s) == worldmodes::PhotonTier::Low,
              "test tier: NEEDS photon runs Photon at the mode's own tier (Low)");
        CHECK(s->bloomEnabled, "test tier: a pinned bloom stays ON though not named");
    }
    qunsetenv(testtier::kNeedsEnvVar);
    testtier::set(QString());
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
    testWorldModePhotonIdentity();
    testPhotonOverrideReadsCustom();
    testNewSceneDefault();
    testTierReapply();
    testTierTexts();
    testTestTierPins();

    std::printf(failures ? "\nFAILED: %d check(s)\n" : "\nALL CHECKS PASSED\n", failures);
    return failures ? 1 : 0;
}
