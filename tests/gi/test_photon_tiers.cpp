// photon.tiers — THE SETTINGS TRUTH FOR PHOTON (D4-PHOTON-TIERS §2.1).
//
// ONE tier table: the engine's `giQualityFacts` (what each quality row physically
// is) and worldmodes' row table (which row each tier writes), where every
// Photon-facing number is READ from the engine's table rather than copied. This
// suite holds the claims that make that true:
//
//   1. EVERY ROW x EVERY TIER HAS A VALUE, and it is one the row can hold (an
//      Enum row's option, an Int row's range) — no cell is left to a default.
//   2. THE SENTENCE IS GENERATED FROM THE TABLE: each tier's description names its
//      own chain (count, innermost resolution) and its own gather row.
//   3. THE SSR ROW'S MEANING AT A RAY TIER: where the rays are the reflection the
//      row reads "Traced", and the trace resolution the renderer uses there is the
//      engine's (reflectTrace), which is the row's own High/Epic column — so the
//      shipped pictures do not move.
//   4. THE GATHER ROW exists, is AUTO (the tier's) at every tier, and its text is
//      the engine's gather row.
//   5. THE TRI-STATE, RESOLVED ONE WAY: the field's column is a projection of the
//      engine's `fieldDefault` (the answer GiParams::ddgi's Auto resolves to), every
//      tier application writes a concrete 0/1 through, and a document's untouched
//      -1 reads as the tier's answer — never as "off".
//   6. EPIC IS A QUALITY ROW OF ITS OWN (GiQuality::Epic): High's chain, four times
//      the gather's probes, its own bounce column.
//   7. THE PROBE GRID'S VRAM BUDGET (PCC-BUDGET-1): the arithmetic reproduces the
//      F12-PCC measurement to the byte, the count is derived from the budget, and
//      the budget is honoured by the count it derives.
//   8. THE OFFSCREEN PICTURE CONTRACT IS DECLARED: every capture kind names the
//      picture it renders, and only a declared kind renders a lower one.
//
// No display: the tables are pure functions (the engine's is header-only).
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
using jahshaka::engine::GiQuality;
using jahshaka::engine::giQualityFacts;

static const PhotonTier kTiers[4] = { PhotonTier::Low, PhotonTier::Medium, PhotonTier::High,
                                      PhotonTier::Epic };

static bool validCell(const worldmodes::Row &r, int v)
{
    if (r.type == worldmodes::RowType::Bool) return v == 0 || v == 1;
    if (r.type == worldmodes::RowType::Enum) {
        for (const worldmodes::EnumOption &o : r.options)
            if (o.value == v) return true;
        return false;
    }
    return v >= r.minValue && v <= r.maxValue;
}

static void testEveryCell()
{
    std::printf("\n-- 1. every row x every tier has a value it can hold --\n");
    int rows = 0, cells = 0, bad = 0;
    for (const worldmodes::Row &r : worldmodes::rows()) {
        if (!r.available) continue;
        ++rows;
        for (int t = 0; t < 4; ++t) {
            ++cells;
            if (!validCell(r, r.tier[t])) {
                ++bad;
                std::printf("   row %s tier %d holds %d, not one of its values\n",
                            qPrintable(r.id), t, r.tier[t]);
            }
        }
    }
    std::printf("   %d rows, %d cells\n", rows, cells);
    CHECK(bad == 0, "every cell of every row is a value the row can hold");
    // The Photon rows are the table's own columns, cell by cell.
    const QStringList ids = worldmodes::photonRowIds();
    CHECK(ids.size() == 5, "five Photon-tiered rows (technique, quality, field, bounces, probe size)");
    for (const QString &id : ids) {
        const worldmodes::Row *r = worldmodes::row(id);
        CHECK(r && r->tierSpace == worldmodes::TierSpace::Photon,
              qPrintable(QStringLiteral("%1 is a Photon row").arg(id)));
    }
    CHECK(worldmodes::row(QStringLiteral("giCascades")) == nullptr,
          "no cascade row: the chain always runs (the single volume is deleted)");
}

static void testSentences()
{
    std::printf("\n-- 2. the sentence is the table --\n");
    for (PhotonTier t : kTiers) {
        const auto facts = giQualityFacts(GiQuality(worldmodes::photonQuality(t)));
        const QString text = worldmodes::photonTierSentence(t);
        std::printf("   %s\n", qPrintable(text));
        CHECK(text.contains(QString::number(facts.cascadeCount)) &&
                  text.contains(QString::number(facts.cascades[0].resolution)),
              qPrintable(QStringLiteral("%1 names its own chain").arg(worldmodes::photonTierName(t))));
        CHECK(!text.contains(QStringLiteral("scene-fitted")),
              qPrintable(QStringLiteral("%1 describes no fixed volume").arg(worldmodes::photonTierName(t))));
    }
}

static void testSsrRow()
{
    std::printf("\n-- 3. the SSR row at a ray tier --\n");
    const worldmodes::Row *ssr = worldmodes::row(QStringLiteral("ssr"));
    CHECK(ssr != nullptr, "the SSR row exists");
    if (!ssr) return;
    for (PhotonTier t : kTiers) {
        const auto facts = giQualityFacts(GiQuality(worldmodes::photonQuality(t)));
        // The trace resolution the renderer uses at a ray tier is the row's own
        // column at that tier (World mode of the same name), so nothing moves.
        const int col = ssr->tier[int(t)];
        if (facts.rayReflections)
            CHECK(facts.reflectTrace == col && facts.reflectTrace > 0,
                  qPrintable(QStringLiteral("%1 traces its reflections at the SSR row's own column (%2)")
                                 .arg(worldmodes::photonTierName(t)).arg(col)));
        else
            CHECK(facts.reflectTrace == 0,
                  qPrintable(QStringLiteral("%1 traces no reflections (the row is the screen march)")
                                 .arg(worldmodes::photonTierName(t))));
    }
    for (PhotonTier t : kTiers) {
        auto s = iris::Scene::create();
        worldmodes::setPhoton(s, true, t);
        const bool raysTier = giQualityFacts(GiQuality(worldmodes::photonQuality(t))).rayReflections;
        // THE ENTRIES OFFERED (worldmodes::comboItems, what every panel and
        // world.modeTable list): where the scene traces, exactly "Off" (choosable)
        // and ONE disabled "Traced" standing for every non-zero value; where it
        // cannot, the three march entries under their own labels.
        const auto traced = worldmodes::comboItems(*ssr, s, true);
        const auto plain  = worldmodes::comboItems(*ssr, s, false);
        if (raysTier) {
            CHECK(traced.size() == 2 && traced[0].value == 0 && traced[0].label == QStringLiteral("Off") &&
                      traced[0].enabled && traced[1].label == QStringLiteral("Traced") && !traced[1].enabled,
                  qPrintable(QStringLiteral("%1: where the scene traces the SSR row offers 'Off' and one "
                                            "disabled 'Traced'").arg(worldmodes::photonTierName(t))));
            CHECK(worldmodes::comboIndexOf(traced, 0) == 0 && worldmodes::comboIndexOf(traced, 1) == 1 &&
                      worldmodes::comboIndexOf(traced, 2) == 1,
                  qPrintable(QStringLiteral("%1: off shows 'Off', either march quality shows 'Traced'")
                                 .arg(worldmodes::photonTierName(t))));
        }
        const auto &cannot = raysTier ? plain : traced;
        bool own = cannot.size() == ssr->options.size();
        for (int k = 0; own && k < cannot.size(); ++k)
            own = cannot[k].label == ssr->options[k].label && cannot[k].value == ssr->options[k].value &&
                  cannot[k].enabled;
        CHECK(own && (raysTier || plain.size() == ssr->options.size()),
              qPrintable(QStringLiteral("%1: where nothing is traced, the three march entries under their own labels")
                             .arg(worldmodes::photonTierName(t))));
        CHECK(worldmodes::reflectionsTraced(s, true) == raysTier,
              qPrintable(QStringLiteral("%1: reflectionsTraced agrees with the table")
                             .arg(worldmodes::photonTierName(t))));
        worldmodes::setPhoton(s, false, t);
        CHECK(!worldmodes::reflectionsTraced(s, true),
              "...and Photon OFF traces no reflections, whatever the quality");
    }
}

static void testGatherRow()
{
    std::printf("\n-- 4. the gather row --\n");
    const worldmodes::Row *g = worldmodes::row(QStringLiteral("giGather"));
    CHECK(g != nullptr, "the gather has a row");
    if (!g) return;
    CHECK(g->tier[0] == -1 && g->tier[1] == -1 && g->tier[2] == -1 && g->tier[3] == -1,
          "AUTO at every tier: the engine's table decides");
    const QString text = g->costAt ? g->costAt(true) : g->cost;
    std::printf("   %s\n", qPrintable(text));
    for (PhotonTier t : kTiers) {
        const auto gf = worldmodes::photonGather(t);
        QString n = worldmodes::photonTierName(t);
        n[0] = n[0].toUpper();
        if (gf.on)
            CHECK(text.contains(QStringLiteral("%1 %2 rays a probe").arg(n).arg(gf.octRes * gf.octRes)),
                  qPrintable(QStringLiteral("the text names %1's gather from the table").arg(n)));
    }
    CHECK(!worldmodes::photonGather(PhotonTier::Low).on &&
              worldmodes::photonGather(PhotonTier::Medium).on &&
              worldmodes::photonGather(PhotonTier::High).on &&
              worldmodes::photonGather(PhotonTier::Epic).on,
          "the gather row: off at Low, on at Medium, High and Epic");
    CHECK(worldmodes::photonVrGather(PhotonTier::Epic).on == worldmodes::photonGather(PhotonTier::Epic).on,
          "...and the VR column follows the desktop's (GA-VR)");
}

static void testTriState()
{
    std::printf("\n-- 5. the tri-state, one way --\n");
    for (int q = 0; q < 4; ++q)
        CHECK(giQualityFacts(GiQuality(q)).fieldDefault,
              qPrintable(QStringLiteral("quality %1: the field's Auto resolves ON").arg(q)));
    for (PhotonTier t : kTiers)
        CHECK(worldmodes::photonDdgi(t) ==
                  (giQualityFacts(GiQuality(worldmodes::photonQuality(t))).fieldDefault ? 1 : 0),
              qPrintable(QStringLiteral("%1's field column IS the engine's fieldDefault")
                             .arg(worldmodes::photonTierName(t))));
    for (PhotonTier t : kTiers) {
        auto s = iris::Scene::create();
        CHECK(s->giDdgi == -1, "a bare document holds -1 (the tier's)");
        const worldmodes::Row *r = worldmodes::row(QStringLiteral("giDdgi"));
        CHECK(r && r->get(s) == 1, "...and the row resolves it to the tier's answer, ON, never OFF");
        worldmodes::setPhoton(s, true, t);
        CHECK(s->giDdgi == 0 || s->giDdgi == 1,
              qPrintable(QStringLiteral("applying %1 writes a concrete 0/1 through: the renderer "
                                        "never sees -1 from a tier").arg(worldmodes::photonTierName(t))));
    }
    // A pre-tier document (the reader's derive path) resolves its -1 the same way.
    auto d = iris::Scene::create();
    d->giMode = iris::GiMode::VCT;
    d->giQuality = iris::GiQuality::MEDIUM;
    worldmodes::derivePhotonFromDocument(d);
    CHECK(d->giDdgi == 1, "a derived tier resolves the field's -1 to the table's ON");
}

static void testEpic()
{
    std::printf("\n-- 6. Epic is a quality row --\n");
    const auto hi = giQualityFacts(GiQuality::High), ep = giQualityFacts(GiQuality::Epic);
    CHECK(worldmodes::photonQuality(PhotonTier::Epic) == int(GiQuality::Epic),
          "the Epic tier writes GiQuality::Epic");
    bool sameChain = hi.cascadeCount == ep.cascadeCount;
    for (int i = 0; sameChain && i < hi.cascadeCount; ++i)
        sameChain = hi.cascades[i] == ep.cascades[i];
    CHECK(sameChain && hi.probeFaceSize == ep.probeFaceSize && hi.rayReflections == ep.rayReflections,
          "Epic has High's chain, probe face and reflection row");
    CHECK(ep.gather.stride * 2u == hi.gather.stride, "...and four times the gather's probes");
    CHECK(worldmodes::photonBounces(PhotonTier::Epic) == 3 &&
              worldmodes::photonBounces(PhotonTier::High) == 1,
          "...and its own bounce column");
}

static void testProbeBudget()
{
    std::printf("\n-- 7. the probe grid's VRAM budget --\n");
    using jahshaka::engine::giProbeGridBytes;
    using jahshaka::engine::giProbeGridBudgetCount;
    const unsigned long long measured = 838987760ull;   // PHOTON-F12-PCC, Showroom 2 at Epic
    CHECK(giProbeGridBytes(512u, true, true, 32u) == measured,
          "the WHOLE grid's arithmetic (array + shadow targets + cubes) is the F12-PCC measurement");
    for (int q = 0; q < 4; ++q) {
        const auto f = giQualityFacts(GiQuality(q));
        const bool hdr = f.probeHdrDefault, sh = f.probeShadowsDefault;
        const unsigned n = giProbeGridBudgetCount(f.probeGridBudgetBytes, f.probeFaceSize, hdr, sh);
        const unsigned long long at = giProbeGridBytes(f.probeFaceSize, hdr, sh, n);
        const unsigned long long over = giProbeGridBytes(f.probeFaceSize, hdr, sh, n + 1u);
        std::printf("   quality %d: budget %llu MiB, %u px%s%s -> %u probes (%.1f MiB)\n", q,
                    f.probeGridBudgetBytes >> 20, f.probeFaceSize, hdr ? " HDR" : "",
                    sh ? " shadowed" : "", n, double(at) / 1048576.0);
        CHECK(at <= f.probeGridBudgetBytes && over > f.probeGridBudgetBytes,
              qPrintable(QStringLiteral("quality %1: the derived count is the most that fits").arg(q)));
    }
    CHECK(giProbeGridBudgetCount(giQualityFacts(GiQuality::Epic).probeGridBudgetBytes, 512u, true, true) >= 32u,
          "Epic's budget holds the measured 32-probe Showroom grid");
}

static void testOffscreenContract()
{
    std::printf("\n-- 8. the offscreen picture contract --\n");
    const QVector<worldmodes::OffscreenPicture> &kinds = worldmodes::offscreenPictures();
    QStringList names;
    for (const worldmodes::OffscreenPicture &k : kinds) {
        names << k.kind;
        std::printf("   %-22s %s%s\n", qPrintable(k.kind), k.tierPicture ? "the tier's picture" : "DECLARED LOWER: ",
                    k.tierPicture ? "" : qPrintable(k.renders));
        CHECK(k.tierPicture || !k.renders.isEmpty(),
              qPrintable(QStringLiteral("%1: a lower picture says what it renders").arg(k.kind)));
    }
    for (const char *want : { "viewport", "screenshot.scene", "screenshot.viewport", "screenshot.plain",
                              "screenshot.tonemap", "projectTile", "liveOffscreen", "probeCapture", "cardCapture" })
        CHECK(names.contains(QLatin1String(want)),
              qPrintable(QStringLiteral("the table declares %1").arg(QLatin1String(want))));
    for (const worldmodes::OffscreenPicture &k : kinds)
        if (k.kind == QLatin1String("viewport") || k.kind == QLatin1String("screenshot.scene") ||
            k.kind == QLatin1String("screenshot.viewport"))
            CHECK(k.tierPicture, qPrintable(QStringLiteral("%1 renders the tier's picture").arg(k.kind)));
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    enginetest::DocumentGraph graph("photon-tiers-ogre.log");
    if (!graph.require()) return 1;
    testEveryCell();
    testSentences();
    testSsrRow();
    testGatherRow();
    testTriState();
    testEpic();
    testProbeBudget();
    testOffscreenContract();
    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "all ok", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
