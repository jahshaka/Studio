// gi.changed_regions (PHOTON-7 stage 3, AB S1; law L2) — ONE CHANGED-REGION LIST A FRAME SAYS WHERE THE WORLD CHANGED.
//
// The producers append at their points of the frame (the drain's lamps, the GI step's slabs and edits, the cards' plan,
// the GPU scene's commit and the skin pass) and the list closes before the submit (Scene::giChangedRegions: the list of
// the last frame that drew the scene; GiStatus::region*: its counts). The laws, each on the frame it is about:
//   STILL:   a settled scene lists nothing - no region, no overflow, no restart - for 600 frames (the product's own
//            defaults: rays and cards on).
//   LAMP:    a lamp step lists EXACTLY its old range sphere joined with its new one (one Lamp region, nothing else); an
//            intensity step lists exactly its one sphere.
//   MOVER:   a movable cube's step lists its SWEPT box (this frame's bounds joined with last frame's) as a Mover, the same
//            box as its sun shadow (MoverShadow) with the sun's direction in the header (toSun = -the light's direction),
//            and the range sphere of the lamp whose range meets it (LampShadow) - and no Edit (movers are not GI geometry).
//   EDIT:    a still cube moved by script lists its old box joined with its new as an Edit; a MATERIAL edit (A19 deleted)
//            lists the wearer's box and re-voxelises only the cascades that hold it (the inner cascade it lies outside of
//            keeps its rebuild count); the mover's material edited lists the mover's box (a ray-only wearer, F6).
//   SUN/SKY: a sun turn and a 2x sun step are the sun's overflow without a restart, a 5x sun step restarts; a 2x sky step
//            (the ambient's constant band) is the sky's overflow without a restart, a 5x one restarts (Lumen's 4x).
//   CAP:     129 movers stepping in one frame merge into at most 128 regions with no overflow, and every mover's swept box
//            lies inside a listed region of its kind (Mover and MoverShadow).
//   SLAB:    a dolly's cascade-0 scroll lists, on its commit frame, EXACTLY the commit's injection boxes as Slab regions of
//            cascade 0, at its NEW corner (its slab's side and its trailing face, each its march's reach deep, 4-aligned),
//            and its LEAVING slab (the old box's cells the new one dropped: PHOTON-7 stage 5's ruling); its chunk frames list
//            no Slab.
//   GONE:    a lamp NODE deleted (removeNode, the mirror's path) lists exactly its sphere; a movable cube removed lists its
//            last box as a Mover with its shadows; a still cube removed while another object holds the tail slot lists
//            the removed cube's box (its REMOVE) and NOTHING for the tail object, renumbered into the freed slot unchanged
//            in the world (PHOTON-7 stage 4's read F2: a compaction move is the index's, not the world's).
//   ATTACH:  a lit attach lists its item's box as an Edit, never an overflow (stage 3's read F3).
//   BRICKS:  the light passes' brick mask (Scene::takeGiBrickMask): a still scene's forced re-injection (readings landed,
//            updates counted) changes no brick's direct term; the lamp step's re-injection marks bricks, every one in the
//            cover of the old sphere joined with the new.
//   Every count the status carries is read NON-ZERO somewhere (the lamp step, the sun, the cap) - bricksDirectLast,
//   bricksActiveLast and bricksBounceLast on the lamp step, regionClosesListing on a listing frame - except
//   brickReadingsSkipped, which only a GPU that falls four readings behind writes (asserted 0: no door forces it).
// A fixture (the engine alone, one small offscreen view; the gather and the field off). Counted in frames.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                                                              \
    do {                                                                                                              \
        if (cond) std::printf("  ok   %s\n", msg);                                                                    \
        else { std::printf("  FAIL %s\n", msg); ++failures; }                                                         \
    } while (0)

static const char *kindName(GiRegionKind k)
{
    switch (k) {
    case GiRegionKind::Slab: return "Slab";
    case GiRegionKind::Card: return "Card";
    case GiRegionKind::Edit: return "Edit";
    case GiRegionKind::Mover: return "Mover";
    case GiRegionKind::MoverShadow: return "MoverShadow";
    case GiRegionKind::LampShadow: return "LampShadow";
    case GiRegionKind::Lamp: return "Lamp";
    }
    return "?";
}

static void dump(const GiChangedRegions &r, const char *what)
{
    std::printf("   %s: frame %llu, %zu regions, overflow %u, restart %d, merges %u, sun %d toSun (%.4f %.4f %.4f)\n", what,
                r.frame, r.regions.size(), r.overflow, int(r.restart), r.merges, int(r.sun), double(r.toSun.x),
                double(r.toSun.y), double(r.toSun.z));
    for (size_t i = 0; i < r.regions.size() && i < 12; ++i) {
        const GiChangedRegion &g = r.regions[i];
        std::printf("     %-11s [%.4f %.4f %.4f] .. [%.4f %.4f %.4f] cascades 0x%x\n", kindName(g.kind), double(g.lo.x),
                    double(g.lo.y), double(g.lo.z), double(g.hi.x), double(g.hi.y), double(g.hi.z), g.cascadeMask);
    }
}

struct Box { Vec3 lo, hi; };
static Box join(const Box &a, const Box &b)
{
    return Box{ Vec3(std::min(a.lo.x, b.lo.x), std::min(a.lo.y, b.lo.y), std::min(a.lo.z, b.lo.z)),
                Vec3(std::max(a.hi.x, b.hi.x), std::max(a.hi.y, b.hi.y), std::max(a.hi.z, b.hi.z)) };
}
static Box sphere(const Vec3 &c, float r) { return Box{ Vec3(c.x - r, c.y - r, c.z - r), Vec3(c.x + r, c.y + r, c.z + r) }; }
/// A unit cube's world box at a position and a scale.
static Box cubeAt(const Vec3 &p, const Vec3 &s)
{
    return Box{ Vec3(p.x - 0.5f * s.x, p.y - 0.5f * s.y, p.z - 0.5f * s.z), Vec3(p.x + 0.5f * s.x, p.y + 0.5f * s.y, p.z + 0.5f * s.z) };
}
/// THE TOLERANCE: the float rounding of a world box (the transform and the bounds in float, a few ulps at 10-60 m).
static bool same(const GiChangedRegion &g, const Box &b, float tol = 1e-4f)
{
    return std::fabs(g.lo.x - b.lo.x) <= tol && std::fabs(g.lo.y - b.lo.y) <= tol && std::fabs(g.lo.z - b.lo.z) <= tol &&
           std::fabs(g.hi.x - b.hi.x) <= tol && std::fabs(g.hi.y - b.hi.y) <= tol && std::fabs(g.hi.z - b.hi.z) <= tol;
}
static bool inside(const Box &b, const GiChangedRegion &g, float tol = 1e-4f)
{
    return b.lo.x >= g.lo.x - tol && b.lo.y >= g.lo.y - tol && b.lo.z >= g.lo.z - tol && b.hi.x <= g.hi.x + tol &&
           b.hi.y <= g.hi.y + tol && b.hi.z <= g.hi.z + tol;
}
static unsigned countKind(const GiChangedRegions &r, GiRegionKind k)
{
    unsigned n = 0;
    for (const GiChangedRegion &g : r.regions) n += g.kind == k ? 1u : 0u;
    return n;
}
static const GiChangedRegion *firstKind(const GiChangedRegions &r, GiRegionKind k)
{
    for (const GiChangedRegion &g : r.regions)
        if (g.kind == k) return &g;
    return nullptr;
}

/// A UNIT CUBE WITH ITS SIX CARDS (the bake's box cards, enginetest::boxCards): the surface cache makes it resident.
static NodeId addCardedCube(Scene *s, const Colour &albedo, float roughness)
{
    const NodeId node = s->createNode();
    if (!node) return 0;
    MeshData d = enginetest::unitCubeMesh();
    d.cards = enginetest::boxCards(0.5f);
    PbrParams p;
    p.albedo = albedo;
    p.roughness = roughness;
    const MeshId mesh = s->createMesh(d);
    const MaterialId mat = s->createPbrMaterial(p);
    if (!mesh || !mat || !s->attachMesh(node, mesh, mat)) return 0;
    enginetest::poseRegistry()[s][node] = enginetest::NodePose{};
    return node;
}

int main()
{
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-gi-changed-regions-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    e->setRayTracing(true);   // the product's ray tier: the cards (the Card producer) and the skin pass
    int frames = 1;
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));
    const auto run = [&](int n) { enginetest::frames(e, n); frames += n; };
    View *view = e->createOffscreenView("regions", 64, 64, Colour(0, 0, 0), jahshaka::engine::OffscreenContract::Live);
    Scene *scene = e->createScene("regions");
    if (!view || !scene) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(scene);
    const auto regions = [&]() {
        GiChangedRegions r;
        scene->giChangedRegions(r);
        return r;
    };
    // A 40 m floor and a few props; the sun; a lamp of range 4 m over the floor.
    const NodeId floor = addCardedCube(scene, Colour(0.7f, 0.7f, 0.7f), 0.9f);
    enginetest::setNodePosition(scene, floor, Vec3(0.0f, -0.05f, 0.0f));
    enginetest::setNodeScale(scene, floor, Vec3(40.0f, 0.1f, 40.0f));
    for (int k = 0; k < 4; ++k) {
        const NodeId p = addCardedCube(scene, Colour(0.3f + 0.1f * k, 0.5f, 0.8f - 0.1f * k), 0.6f);
        enginetest::setNodePosition(scene, p, Vec3(-3.0f + 2.0f * float(k), 0.5f, -2.5f));
    }
    const Vec3 sunDir(0.3f, -1.0f, -0.4f);
    const NodeId sunNode = enginetest::addDirectionalLight(scene, sunDir, 3.0f);
    const NodeId lamp = scene->createNode();
    LightDesc lampDesc;
    lampDesc.type = LightType::Point;
    lampDesc.colour = Colour(1.0f, 0.9f, 0.8f);
    lampDesc.intensity = 4.0f;
    lampDesc.range = 4.0f;
    Vec3 lampPos(1.5f, 1.8f, 1.0f);
    scene->setNodeTransform(lamp, lampPos, Quat(), Vec3(1, 1, 1));
    scene->setLight(lamp, lampDesc);
    // THE MOVER: a movable cube (pushed movable before its geometry: no reclassification rebuild).
    const NodeId mover = scene->createNode();
    scene->setNodeMovable(mover, true);
    PbrParams moverP;
    moverP.albedo = Colour(0.8f, 0.3f, 0.2f);
    moverP.roughness = 0.5f;
    const MaterialId moverMat = scene->createPbrMaterial(moverP);
    {
        const MeshId mesh = scene->createMesh(enginetest::unitCubeMesh());
        if (!mesh || !moverMat || !scene->attachMesh(mover, mesh, moverMat)) { std::printf("FAIL: the mover\n"); return 1; }
    }
    Vec3 moverPos(0.8f, 0.6f, 0.4f);
    const Vec3 moverScale(0.5f, 0.5f, 0.5f);
    scene->setNodeTransform(mover, moverPos, Quat(), moverScale);
    // THE STILL PROP an edit moves.
    const NodeId prop = addCardedCube(scene, Colour(0.4f, 0.6f, 0.4f), 0.7f);
    Vec3 propPos(-1.0f, 0.5f, 1.5f);
    enginetest::setNodePosition(scene, prop, propPos);

    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.ddgi = GiToggle::Off;
    gi.bounces = true;
    enginetest::testCameraLookAt(view, Vec3(0.0f, 1.6f, 4.0f), Vec3(0.0f, 0.6f, 0.0f));
    if (!scene->setGlobalIllumination(gi)) { std::printf("FAIL: the chain\n"); return 1; }
    e->waitForAsyncShaders();
    // SETTLED: the chain at rest and the cards with nothing queued or relit, for 30 frames in a row (cap 900).
    const auto settle = [&](int cap) {
        int quiet = 0, n = 0;
        for (; n < cap && quiet < 30; ++n) {
            run(1);
            const GiStatus g = scene->published().gi;
            const bool still = g.giAtRest && !g.chainOwed && g.cards.queueLength == 0u && g.cards.relitLastFrame == 0u;
            quiet = still ? quiet + 1 : 0;
        }
        return quiet >= 30 ? n : -1;
    };
    {
        const int n = settle(900);
        std::printf("== settled in %d frames (cards built %d, %u resident) ==\n", n, int(scene->published().gi.cards.built),
                    scene->published().gi.cards.cardsResident);
        CHECK(n >= 0, "the scene settles (giAtRest, no card queued or relit, 30 frames in a row)");
    }

    // ---- STILL ---------------------------------------------------------------------------------------------------
    std::printf("\n== STILL: 600 frames ==\n");
    {
        const GiStatus g0 = scene->published().gi;
        const unsigned long long closes0 = g0.regionCloses;
        int listing = 0;
        unsigned activeDirect = 0;
        std::vector<unsigned long long> seenReadings;
        for (const GiStatus::CascadeStatus &cs : g0.cascades) seenReadings.push_back(cs.brickReadings);
        GiChangedRegions first;
        for (int f = 0; f < 600; ++f) {
            run(1);
            const GiStatus g = scene->published().gi;
            if (g.regionCount || g.regionOverflow || g.regionRestart) {
                if (!listing) first = regions();
                ++listing;
            }
            // (a reading that landed THIS frame: the last reading's count stands until the next lands)
            for (size_t i = 0; i < g.cascades.size(); ++i)
                if (i < seenReadings.size() && g.cascades[i].brickReadings != seenReadings[i]) {
                    activeDirect += g.cascades[i].bricksDirectLast;
                    seenReadings[i] = g.cascades[i].brickReadings;
                }
        }
        const GiStatus g1 = scene->published().gi;
        const unsigned long long closes = g1.regionCloses - closes0;
        unsigned long long updates = 0, readings = 0;
        for (size_t i = 0; i < g1.cascades.size() && i < g0.cascades.size(); ++i) {
            updates += g1.cascades[i].brickUpdates - g0.cascades[i].brickUpdates;
            readings += g1.cascades[i].brickReadings - g0.cascades[i].brickReadings;
        }
        std::printf("   closes %llu, frames listing %d; the brick mask: %llu light updates, %llu readings, %u direct-active "
                    "bricks summed over the readings\n", closes, listing, updates, readings, activeDirect);
        CHECK(activeDirect == 0u, "...and no reading of the brick mask landing in it holds a changed direct term");
        if (listing) dump(first, "the first listing frame");
        CHECK(closes == 600u, "the list closed once a frame");
        CHECK(listing == 0, "A STILL SCENE LISTS NOTHING FOR 600 FRAMES (no region, no overflow, no restart)");
        // ...AND THE BRICK CHECK IS NOT VACUOUS (stage 3's read F7): the still scene's lights re-injected with nothing
        // changed - the host's no-change relight - must land readings with updates, and no brick's direct term moved.
        {
            const GiStatus a = scene->published().gi;
            scene->refreshGiLighting();
            unsigned directSum = 0;
            std::vector<unsigned long long> seen;
            for (const GiStatus::CascadeStatus &cs : a.cascades) seen.push_back(cs.brickReadings);
            for (int f = 0; f < 12; ++f) {
                run(1);
                const GiStatus g = scene->published().gi;
                for (size_t i = 0; i < g.cascades.size() && i < seen.size(); ++i)
                    if (g.cascades[i].brickReadings != seen[i]) {
                        directSum += g.cascades[i].bricksDirectLast;
                        seen[i] = g.cascades[i].brickReadings;
                    }
            }
            const GiStatus b = scene->published().gi;
            unsigned long long ups = 0, reads = 0, skipped = 0;
            for (size_t i = 0; i < b.cascades.size() && i < a.cascades.size(); ++i) {
                ups += b.cascades[i].brickUpdates - a.cascades[i].brickUpdates;
                reads += b.cascades[i].brickReadings - a.cascades[i].brickReadings;
                skipped += b.cascades[i].brickReadingsSkipped;
            }
            std::printf("   a no-change re-injection: %llu light updates, %llu readings, %u direct-changed bricks, %llu "
                        "readings skipped\n", ups, reads, directSum, skipped);
            CHECK(ups > 0u && reads > 0u, "the forced re-injection updated the light and its readings landed");
            CHECK(directSum == 0u, "...AND NO BRICK'S DIRECT TERM MOVED (nothing changed: the store's own bits compared)");
            CHECK(skipped == 0u, "...and no reading was skipped (the GPU keeps up)");
            settle(600);
        }
    }

    // ---- LAMP ----------------------------------------------------------------------------------------------------
    std::printf("\n== LAMP: a pose step and an intensity step ==\n");
    {
        const Vec3 from = lampPos;
        // THE BRICK MASKS EMPTIED before the step (the settle let every reading in flight land).
        const size_t nCascades = scene->published().gi.cascades.size();
        for (size_t i = 0; i < nCascades; ++i) {
            GiBrickMask junk;
            scene->takeGiBrickMask(int(i), junk);
        }
        const unsigned long long listing0 = scene->published().gi.regionClosesListing;
        lampPos = Vec3(2.5f, 1.8f, 0.2f);
        scene->setNodeTransform(lamp, lampPos, Quat(), Vec3(1, 1, 1));
        run(1);
        const GiChangedRegions r = regions();
        dump(r, "the step's frame");
        CHECK(scene->published().gi.regionClosesListing == listing0 + 1u,
              "the step's frame is one close that listed (regionClosesListing read non-zero, stage 3's read F7)");
        {
            // THE STATUS'S COUNTS READ THIS LIST (the negative control of the still arm's zeros)
            const GiStatus g = scene->published().gi;

            CHECK(g.regionFrame == r.frame && g.regionCount == unsigned(r.regions.size()) && g.regionCount > 0u &&
                      g.regionsByKind[unsigned(GiRegionKind::Lamp)] == 1u,
                  "the status's counts are this list's (a frame, a count, one Lamp)");
        }
        const GiChangedRegion *l = firstKind(r, GiRegionKind::Lamp);
        const Box want = join(sphere(from, lampDesc.range), sphere(lampPos, lampDesc.range));
        // (the cards' own answer to the light write - today every resident card relit, the radiance signature C4 that
        // stage 5 deletes - is the Card producer's, listed beside it)
        const unsigned others = unsigned(r.regions.size()) - countKind(r, GiRegionKind::Lamp) - countKind(r, GiRegionKind::Card);
        std::printf("   Lamp %u, Card %u (the cache's relights), other %u\n", countKind(r, GiRegionKind::Lamp),
                    countKind(r, GiRegionKind::Card), others);
        CHECK(countKind(r, GiRegionKind::Lamp) == 1u && l && same(*l, want) && others == 0u && !r.overflow && !r.restart,
              "A LAMP STEP LISTS EXACTLY ITS OLD SPHERE JOINED WITH ITS NEW (one Lamp region; no other producer but the cards')");
        CHECK(l && l->cascadeMask != 0u, "...and names the cascades it reaches");
        // THE BRICK MASK OF THE STEP (PHOTON-7 stage 3): the chain re-injected (the host's refresh) - every brick whose
        // direct term changed by a code or more lies in the cover of the old sphere joined with the new (past its range a
        // lamp lights nothing), and some brick did change (the count is read non-zero).
        {
            scene->refreshGlobalIllumination();
            unsigned directLastMax = 0, activeLastMax = 0, bounceLastMax = 0;
            for (int f = 0; f < 8; ++f) {
                run(1);
                for (const GiStatus::CascadeStatus &cs : scene->published().gi.cascades) {
                    directLastMax = std::max(directLastMax, cs.bricksDirectLast);
                    activeLastMax = std::max(activeLastMax, cs.bricksActiveLast);
                    bounceLastMax = std::max(bounceLastMax, cs.bricksBounceLast);
                }
            }
            CHECK(activeLastMax > 0u && bounceLastMax > 0u,
                  "...the status's active and bounce counts read it non-zero too (stage 3's read F7)");
            unsigned marked = 0, outside = 0, bounceMarked = 0;
            for (size_t i = 0; i < nCascades; ++i) {
                GiBrickMask m;
                if (!scene->takeGiBrickMask(int(i), m)) continue;
                unsigned mi = 0, oi = 0, bi = 0;
                for (int z = 0; z < m.dims[2]; ++z)
                    for (int y = 0; y < m.dims[1]; ++y)
                        for (int x = 0; x < m.dims[0]; ++x) {
                            const size_t k = (size_t(z) * m.dims[1] + y) * m.dims[0] + x;
                            bi += m.bounce[k] >= 1.0f ? 1u : 0u;
                            if (!(m.direct[k] >= 1.0f)) continue;
                            ++mi;
                            const Box b{ Vec3(m.origin.x + m.brickMetres * x, m.origin.y + m.brickMetres * y,
                                              m.origin.z + m.brickMetres * z),
                                         Vec3(m.origin.x + m.brickMetres * (x + 1), m.origin.y + m.brickMetres * (y + 1),
                                              m.origin.z + m.brickMetres * (z + 1)) };
                            const bool meets = b.lo.x <= want.hi.x && b.hi.x >= want.lo.x && b.lo.y <= want.hi.y &&
                                               b.hi.y >= want.lo.y && b.lo.z <= want.hi.z && b.hi.z >= want.lo.z;
                            oi += meets ? 0u : 1u;
                        }
                std::printf("   cascade %zu: %d^3 bricks of %.3f m, %u readings: %u direct-changed (%u outside the "
                            "spheres' cover), %u moved by the last pass\n", i, m.dims[0], double(m.brickMetres), m.readings,
                            mi, oi, bi);
                marked += mi;
                outside += oi;
                bounceMarked += bi;
            }
            CHECK(marked > 0u && directLastMax > 0u,
                  "THE LAMP STEP'S DIRECT CHANGE IS IN THE BRICK MASK (the door and the status's count read it non-zero)");
            CHECK(outside == 0u, "...AND EVERY BRICK IT MARKS LIES IN THE COVER OF THE OLD SPHERE JOINED WITH THE NEW");
        }
        settle(600);
        lampDesc.intensity = 8.0f;
        scene->setLight(lamp, lampDesc);
        run(1);
        const GiChangedRegions r2 = regions();
        dump(r2, "the intensity step's frame");
        const GiChangedRegion *l2 = firstKind(r2, GiRegionKind::Lamp);
        const unsigned others2 = unsigned(r2.regions.size()) - countKind(r2, GiRegionKind::Lamp) - countKind(r2, GiRegionKind::Card);
        CHECK(countKind(r2, GiRegionKind::Lamp) == 1u && l2 && same(*l2, sphere(lampPos, lampDesc.range)) && others2 == 0u &&
                  !r2.overflow,
              "an intensity step lists exactly its one sphere");
        settle(600);
    }

    // ---- MOVER ---------------------------------------------------------------------------------------------------
    std::printf("\n== MOVER: a movable cube's step ==\n");
    {
        const Box before = cubeAt(moverPos, moverScale);
        moverPos = Vec3(moverPos.x + 0.3f, moverPos.y, moverPos.z - 0.2f);
        scene->setNodeTransform(mover, moverPos, Quat(), moverScale);
        run(1);
        const GiChangedRegions r = regions();
        dump(r, "the step's frame");
        const Box swept = join(before, cubeAt(moverPos, moverScale));
        const GiChangedRegion *m = firstKind(r, GiRegionKind::Mover);
        const GiChangedRegion *s = firstKind(r, GiRegionKind::MoverShadow);
        const GiChangedRegion *ls = firstKind(r, GiRegionKind::LampShadow);
        const float len = std::sqrt(sunDir.x * sunDir.x + sunDir.y * sunDir.y + sunDir.z * sunDir.z);
        const bool dirOk = r.sun && std::fabs(r.toSun.x + sunDir.x / len) < 1e-4f &&
                           std::fabs(r.toSun.y + sunDir.y / len) < 1e-4f && std::fabs(r.toSun.z + sunDir.z / len) < 1e-4f;
        CHECK(countKind(r, GiRegionKind::Mover) == 1u && m && same(*m, swept), "A MOVER LISTS ITS SWEPT BOX (Mover)");
        CHECK(countKind(r, GiRegionKind::MoverShadow) == 1u && s && same(*s, swept) && dirOk,
              "...AND ITS SUN SHADOW: the same box with the sun's direction in the header (MoverShadow, toSun)");
        CHECK(countKind(r, GiRegionKind::LampShadow) == 1u && ls && same(*ls, sphere(lampPos, lampDesc.range)),
              "...and the range sphere of the lamp whose range meets it (LampShadow)");
        CHECK(countKind(r, GiRegionKind::Edit) == 0u && !r.overflow, "...and no Edit (a mover is no GI geometry), no overflow");
        run(1);
        const GiChangedRegions r2 = regions();
        CHECK(!countKind(r2, GiRegionKind::Mover) && !countKind(r2, GiRegionKind::MoverShadow) &&
                  !countKind(r2, GiRegionKind::LampShadow),
              "the frame after the step lists no mover");
        settle(600);
        // ITS MATERIAL EDITED (stage 3's read F6): a ray-only wearer (no GI geometry, no dirty path) is listed at its box.
        moverP.albedo = Colour(0.3f, 0.8f, 0.2f);
        scene->setPbrMaterial(moverMat, moverP);
        run(1);
        const GiChangedRegions r3 = regions();
        dump(r3, "the mover's material edit");
        const GiChangedRegion *e3 = firstKind(r3, GiRegionKind::Edit);
        CHECK(countKind(r3, GiRegionKind::Edit) == 1u && e3 && same(*e3, cubeAt(moverPos, moverScale), 1e-3f) && !r3.overflow,
              "A MATERIAL EDIT ON A RAY-ONLY WEARER (a mover) LISTS ITS BOX (Edit), no overflow");
        settle(600);
    }

    // ---- EDIT ----------------------------------------------------------------------------------------------------
    std::printf("\n== EDIT: a still prop moved by script, and a material edit far out ==\n");
    {
        const Box before = cubeAt(propPos, Vec3(1, 1, 1));
        propPos = Vec3(propPos.x - 0.4f, propPos.y, propPos.z + 0.3f);
        enginetest::setNodePosition(scene, prop, propPos);
        run(1);
        const GiChangedRegions r = regions();
        dump(r, "the move's frame");
        const GiChangedRegion *ed = firstKind(r, GiRegionKind::Edit);
        CHECK(countKind(r, GiRegionKind::Edit) == 1u && ed && same(*ed, join(before, cubeAt(propPos, Vec3(1, 1, 1))), 1e-3f),
              "A STILL PROP MOVED LISTS ITS OLD BOX JOINED WITH ITS NEW (Edit)");
        CHECK(!r.overflow, "...no overflow");
        settle(900);
        // THE MATERIAL EDIT (A19 deleted): a cube outside cascade 0 and inside cascade 1, arrived and settled first.
        const GiStatus g0 = scene->published().gi;
        if (g0.cascades.size() < 2u) {
            CHECK(false, "the chain has two cascades");
        } else {
            const GiStatus::CascadeStatus &c0 = g0.cascades[0];
            const Vec3 farPos(c0.centre.x + c0.halfSize + 1.5f, 0.5f, c0.centre.z);
            const NodeId farCube = scene->createNode();
            PbrParams p;
            p.albedo = Colour(0.2f, 0.4f, 0.9f);
            p.roughness = 0.6f;
            const MeshId mesh = scene->createMesh(enginetest::unitCubeMesh());
            const MaterialId mat = scene->createPbrMaterial(p);
            scene->setNodeTransform(farCube, farPos, Quat(), Vec3(1, 1, 1));
            const bool attached = mesh && mat && scene->attachMesh(farCube, mesh, mat);
            CHECK(attached, "the far cube attached");
            run(1);
            {
                // ATTACH (stage 3's read F3): a lit attach is told to the voxeliser as "somewhere", to the list as its box
                const GiChangedRegions ra = regions();
                dump(ra, "the attach's frame");
                bool listed = false;
                for (const GiChangedRegion &g : ra.regions)
                    if (g.kind == GiRegionKind::Edit && same(g, cubeAt(farPos, Vec3(1, 1, 1)), 1e-3f)) listed = true;
                CHECK(listed && !ra.overflow, "A LIT ATTACH LISTS ITS ITEM'S BOX (Edit), NEVER AN OVERFLOW");
            }
            settle(900);
            const GiStatus before = scene->published().gi;
            p.albedo = Colour(0.9f, 0.4f, 0.2f);
            scene->setPbrMaterial(mat, p);
            scene->refreshGlobalIllumination();   // the host's refresh (a fixture drains no scheduler record)
            GiChangedRegions edit;
            for (int f = 0; f < 120 && edit.regions.empty(); ++f) {
                run(1);
                const GiChangedRegions r2 = regions();
                if (firstKind(r2, GiRegionKind::Edit)) edit = r2;
            }
            dump(edit, "the material edit's frame");
            settle(900);
            const GiStatus after = scene->published().gi;
            const GiChangedRegion *me = firstKind(edit, GiRegionKind::Edit);
            CHECK(me && same(*me, cubeAt(farPos, Vec3(1, 1, 1)), 1e-3f) && !edit.overflow,
                  "A MATERIAL EDIT LISTS ITS WEARER'S BOX (Edit), no overflow");
            std::printf("   cascade rebuilds over the edit: c0 %llu -> %llu, c1 %llu -> %llu\n", before.cascades[0].rebuilds,
                        after.cascades[0].rebuilds, before.cascades[1].rebuilds, after.cascades[1].rebuilds);
            CHECK(after.cascades[0].rebuilds == before.cascades[0].rebuilds && after.cascades[1].rebuilds > before.cascades[1].rebuilds,
                  "...AND RE-VOXELISES ONLY THE CASCADES THAT HOLD IT (cascade 0, which it lies outside, keeps its count)");
        }
    }

    // ---- SUN AND SKY ---------------------------------------------------------------------------------------------
    std::printf("\n== SUN AND SKY ==\n");
    {
        const auto step = [&](const char *what) {
            run(1);
            const GiChangedRegions r = regions();
            dump(r, what);
            return r;
        };
        // a sun turn: its direction, no change of its peak
        enginetest::poseRegistry()[scene][sunNode].rot = Quat(0.2f, 0.0f, 0.1f, 0.97467943f);
        {
            const enginetest::NodePose &p = enginetest::poseRegistry()[scene][sunNode];
            scene->setNodeTransform(sunNode, p.pos, p.rot, p.scale);
        }
        const GiChangedRegions turn = step("the sun's turn");
        CHECK((turn.overflow & kGiRegionOverflowSun) && !turn.restart, "A SUN TURN IS THE SUN'S OVERFLOW, NO RESTART");
        LightDesc sun;
        sun.type = LightType::Directional;
        sun.colour = Colour(1.f, 1.f, 1.f);
        sun.intensity = 2.0f * 3.0f / 3.14159265358979323846f;
        scene->setLight(sunNode, sun);
        const GiChangedRegions twice = step("the sun x2");
        CHECK((twice.overflow & kGiRegionOverflowSun) && !twice.restart, "a 2x sun step: the sun's overflow, no restart");
        sun.intensity *= 5.0f;
        scene->setLight(sunNode, sun);
        const GiChangedRegions five = step("the sun x5");
        CHECK((five.overflow & kGiRegionOverflowSun) && five.restart, "A 5x SUN STEP RESTARTS");
        CHECK((scene->published().gi.regionOverflow & kGiRegionOverflowSun) && scene->published().gi.regionRestart,
              "...and the status reads the overflow and the restart (non-zero)");
        float sh[27] = {};
        sh[0] = sh[1] = sh[2] = 0.1f;
        scene->setAmbientSh(sh);
        step("the sky set");
        sh[0] = sh[1] = sh[2] = 0.2f;
        scene->setAmbientSh(sh);
        const GiChangedRegions sky2 = step("the sky x2");
        CHECK((sky2.overflow & kGiRegionOverflowSky) && !sky2.restart, "A 2x SKY STEP IS THE SKY'S OVERFLOW, NO RESTART");
        sh[0] = sh[1] = sh[2] = 1.0f;
        scene->setAmbientSh(sh);
        const GiChangedRegions sky5 = step("the sky x5");
        CHECK((sky5.overflow & kGiRegionOverflowSky) && sky5.restart, "A 5x SKY STEP SETS RESTART");
        settle(900);
    }

    // ---- THE CAP -------------------------------------------------------------------------------------------------
    std::printf("\n== THE CAP: 129 movers in one frame ==\n");
    {
        std::vector<NodeId> movers;
        std::vector<Vec3> pos;
        const MeshId mesh = scene->createMesh(enginetest::unitCubeMesh());
        PbrParams p;
        p.albedo = Colour(0.6f, 0.6f, 0.6f);
        const MaterialId mat = scene->createPbrMaterial(p);
        const Vec3 s(0.2f, 0.2f, 0.2f);
        for (int k = 0; k < 129; ++k) {
            const NodeId n = scene->createNode();
            scene->setNodeMovable(n, true);
            const Vec3 at(-6.0f + 0.9f * float(k % 13), 0.3f + 0.5f * float(k / 13 % 3), -4.0f + 0.8f * float(k / 39));
            scene->setNodeTransform(n, at, Quat(), s);
            if (!scene->attachMesh(n, mesh, mat)) { CHECK(false, "a cap mover attached"); break; }
            movers.push_back(n);
            pos.push_back(at);
        }
        settle(900);
        std::vector<Box> swept;
        for (size_t k = 0; k < movers.size(); ++k) {
            const Box b0 = cubeAt(pos[k], s);
            pos[k] = Vec3(pos[k].x + 0.05f, pos[k].y, pos[k].z + 0.03f);
            scene->setNodeTransform(movers[k], pos[k], Quat(), s);
            swept.push_back(join(b0, cubeAt(pos[k], s)));
        }
        run(1);
        const GiChangedRegions r = regions();
        dump(r, "the frame");
        unsigned inMover = 0, inShadow = 0;
        for (const Box &b : swept) {
            bool m = false, sh = false;
            for (const GiChangedRegion &g : r.regions) {
                if (g.kind == GiRegionKind::Mover && inside(b, g)) m = true;
                if (g.kind == GiRegionKind::MoverShadow && inside(b, g)) sh = true;
            }
            inMover += m ? 1u : 0u;
            inShadow += sh ? 1u : 0u;
        }
        std::printf("   %zu regions (Mover %u, MoverShadow %u, LampShadow %u), merges %u; the swept boxes inside a Mover %u, a "
                    "MoverShadow %u of %zu\n", r.regions.size(), countKind(r, GiRegionKind::Mover),
                    countKind(r, GiRegionKind::MoverShadow), countKind(r, GiRegionKind::LampShadow), r.merges, inMover,
                    inShadow, swept.size());
        CHECK(r.regions.size() <= kGiRegionCap && r.merges > 0u && !r.overflow,
              "129 MOVERS MERGE AT THE CAP AND NEVER OVERFLOW");
        CHECK(scene->published().gi.regionMerges == r.merges && scene->published().gi.regionCount == unsigned(r.regions.size()),
              "...and the status reads the merges and the count");
        CHECK(inMover == swept.size() && inShadow == swept.size(),
              "...every mover's swept box inside a region of its kind (Mover, MoverShadow)");
        for (NodeId n : movers) scene->removeNode(n);
        settle(900);
    }

    // ---- SLAB ----------------------------------------------------------------------------------------------------
    std::printf("\n== SLAB: a dolly's cascade-0 scroll ==\n");
    {
        const GiStatus pre = scene->published().gi;
        Vec3 cam(0.0f, 1.6f, 4.0f);
        const float v = 0.25f;
        int commitFrames = 0, slabsOk = 0, chunkSlabFrames = 0, scrollFrames = 0;
        unsigned long long scrolls = pre.cascades.empty() ? 0ull : pre.cascades[0].scrolls;
        Vec3 prevCentre = pre.cascades.empty() ? Vec3() : pre.cascades[0].centre;
        for (int f = 0; f < 120 && commitFrames < 2; ++f) {
            cam = Vec3(cam.x + v, cam.y, cam.z);
            enginetest::testCameraLookAt(view, cam, Vec3(cam.x, 0.6f, 0.0f));
            run(1);
            const GiStatus g = scene->published().gi;
            if (g.cascades.size() < 2u) continue;
            const GiChangedRegions r = regions();
            const GiStatus::CascadeStatus &c0 = g.cascades[0];
            std::vector<const GiChangedRegion *> slab0;
            for (const GiChangedRegion &q : r.regions)
                if (q.kind == GiRegionKind::Slab && q.cascadeMask == 1u) slab0.push_back(&q);
            if (c0.scrolls != scrolls) {
                scrolls = c0.scrolls;
                ++commitFrames;
                if (commitFrames == 1) dump(r, "cascade 0's first commit frame");
                // THE COMMIT's LIST, BY ITS LAW (PHOTON-7's walk hitch, 2: the light-blind rule this read reproduced - both
                // faces of every moved axis, each the march's reach deep, across the whole cascade - is deleted; a commit
                // computes its entering slab and the kept voxels whose march toward a light meets a moved face, within the
                // reach). So: the LEAVING slab listed exactly (per moved axis, the old box's cells the new one dropped,
                // across the union of the old and new boxes on the other axes); the ENTERING slab covered by the listed
                // injection boxes; every injection box's cell within the reach (the shell's end D_0 = 8 cells plus cascade
                // 1's first cell, in cascade 0's cells, + 1) plus the sweep's 2-cell pad and the 4-cell groups (+ 6) of a
                // moved face (the entering slab or the leaving one); at most the injection's 16 boxes.
                const int res = c0.resolution;
                const float cell = c0.cell;
                const int reach = int(std::ceil((8.0 * double(cell) + double(g.cascades[1].cell)) / double(cell))) + 1;
                const Vec3 corner(c0.centre.x - c0.halfSize, c0.centre.y - c0.halfSize, c0.centre.z - c0.halfSize);
                const float dc[3] = { c0.centre.x - prevCentre.x, c0.centre.y - prevCentre.y, c0.centre.z - prevCentre.z };
                int sh[3];
                for (int a = 0; a < 3; ++a) sh[a] = int(std::lround(dc[a] / cell));
                const float cc[3] = { corner.x, corner.y, corner.z };
                std::vector<Box> leaving, entering;
                for (int a = 0; a < 3; ++a) {
                    if (sh[a] == 0) continue;
                    float l[3], h[3], el[3], eh[3];
                    for (int k = 0; k < 3; ++k) {
                        const float nLo = cc[k], nHi = cc[k] + float(res) * cell;
                        const float oLo = nLo - float(sh[k]) * cell, oHi = nHi - float(sh[k]) * cell;
                        if (k == a) {
                            l[k] = sh[a] > 0 ? oLo : nHi;
                            h[k] = sh[a] > 0 ? nLo : oHi;
                            el[k] = sh[a] > 0 ? oHi : nLo;   // the new box's cells the old did not hold
                            eh[k] = sh[a] > 0 ? nHi : oLo;
                        } else {
                            l[k] = std::min(oLo, nLo);
                            h[k] = std::max(oHi, nHi);
                            el[k] = nLo;
                            eh[k] = nHi;
                        }
                    }
                    leaving.push_back(Box{ Vec3(l[0], l[1], l[2]), Vec3(h[0], h[1], h[2]) });
                    entering.push_back(Box{ Vec3(el[0], el[1], el[2]), Vec3(eh[0], eh[1], eh[2]) });
                }
                std::vector<const GiChangedRegion *> inject;
                unsigned leavingMatched = 0;
                for (const GiChangedRegion *q : slab0) {
                    bool isLeaving = false;
                    for (const Box &w : leaving) isLeaving = isLeaving || same(*q, w, 1e-3f);
                    if (isLeaving) ++leavingMatched;
                    else inject.push_back(q);
                }
                const float far = float(reach + 6) * cell;
                const auto nearFace = [&](float x, float y, float z) {
                    const float p[3] = { x, y, z };
                    for (const std::vector<Box> *set : { &entering, &leaving })
                        for (const Box &f : *set) {
                            const float lo[3] = { f.lo.x, f.lo.y, f.lo.z }, hi[3] = { f.hi.x, f.hi.y, f.hi.z };
                            bool in = true;
                            for (int k = 0; k < 3; ++k) in = in && p[k] >= lo[k] - far && p[k] <= hi[k] + far;
                            if (in) return true;
                        }
                    return false;
                };
                unsigned farCells = 0, slabMissed = 0;
                for (const GiChangedRegion *q : inject)
                    for (float z = q->lo.z + 0.5f * cell; z < q->hi.z; z += 4.0f * cell)
                        for (float y = q->lo.y + 0.5f * cell; y < q->hi.y; y += 4.0f * cell)
                            for (float x = q->lo.x + 0.5f * cell; x < q->hi.x; x += 4.0f * cell)
                                farCells += nearFace(x, y, z) ? 0u : 1u;
                for (const Box &e : entering)
                    for (float z = e.lo.z + 0.5f * cell; z < e.hi.z; z += 4.0f * cell)
                        for (float y = e.lo.y + 0.5f * cell; y < e.hi.y; y += 4.0f * cell)
                            for (float x = e.lo.x + 0.5f * cell; x < e.hi.x; x += 4.0f * cell) {
                                bool held = false;
                                for (const GiChangedRegion *q : inject)
                                    held = held || (x >= q->lo.x && x <= q->hi.x && y >= q->lo.y && y <= q->hi.y &&
                                                    z >= q->lo.z && z <= q->hi.z);
                                slabMissed += held ? 0u : 1u;
                            }
                std::printf("   commit %d: shift (%.0f, %.0f, %.0f) cells, %zu listed: %u leaving slab(s) of %zu, %zu injection "
                            "box(es); entering-slab samples missed %u, injection samples beyond the reach %u\n",
                            commitFrames, double(dc[0] / cell), double(dc[1] / cell), double(dc[2] / cell), slab0.size(),
                            leavingMatched, leaving.size(), inject.size(), slabMissed, farCells);
                if (!leaving.empty() && leavingMatched == leaving.size() && !inject.empty() && inject.size() <= 16u &&
                    slabMissed == 0u && farCells == 0u)
                    ++slabsOk;
            } else if (c0.scrolling) {
                ++scrollFrames;
                if (!slab0.empty()) ++chunkSlabFrames;
            }
            prevCentre = c0.centre;
        }
        std::printf("   commits %d (Slab regions of cascade 0 inside its box on %d), chunk frames %d (listing a Slab %d)\n",
                    commitFrames, slabsOk, scrollFrames, chunkSlabFrames);
        CHECK(commitFrames >= 1 && slabsOk == commitFrames,
              "A COMMIT LISTS ITS LEAVING SLAB EXACTLY AND ITS INJECTION BOXES (THE ENTERING SLAB COVERED, NOTHING BEYOND A "
              "MOVED FACE'S REACH) AS SLAB REGIONS OF CASCADE 0, AT ITS NEW CORNER");
        CHECK(chunkSlabFrames == 0, "...and its chunk frames list none");
    }

    // ---- GONE ----------------------------------------------------------------------------------------------------
    std::printf("\n== GONE: a lamp node, a mover and a still cube deleted ==\n");
    {
        settle(900);
        // A LAMP NODE DELETED (stage 3's read F1: the mirror deletes with removeNode, not removeLight).
        scene->removeNode(lamp);
        run(1);
        const GiChangedRegions rl = regions();
        dump(rl, "the lamp node's deletion");
        const GiChangedRegion *gl = firstKind(rl, GiRegionKind::Lamp);
        CHECK(countKind(rl, GiRegionKind::Lamp) == 1u && gl && same(*gl, sphere(lampPos, lampDesc.range)),
              "A LAMP NODE DELETED LISTS EXACTLY ITS SPHERE (Lamp)");
        settle(900);
        // A MOVER REMOVED (stage 3's read F2): its last box is a Mover, with its sun shadow - the tail slot: no ADD.
        const MeshId mesh = scene->createMesh(enginetest::unitCubeMesh());
        PbrParams p;
        p.albedo = Colour(0.5f, 0.5f, 0.5f);
        const MaterialId mat = scene->createPbrMaterial(p);
        const Vec3 ms(0.4f, 0.4f, 0.4f);
        const Vec3 mp(-1.5f, 0.4f, 0.5f);
        const NodeId gone = scene->createNode();
        scene->setNodeMovable(gone, true);
        scene->setNodeTransform(gone, mp, Quat(), ms);
        CHECK(scene->attachMesh(gone, mesh, mat), "the mover to remove attached");
        settle(900);
        scene->removeNode(gone);
        run(1);
        const GiChangedRegions rm = regions();
        dump(rm, "the mover's removal");
        bool moverGone = false, shadowGone = false;
        for (const GiChangedRegion &g : rm.regions) {
            if (g.kind == GiRegionKind::Mover && same(g, cubeAt(mp, ms), 1e-3f)) moverGone = true;
            if (g.kind == GiRegionKind::MoverShadow && same(g, cubeAt(mp, ms), 1e-3f)) shadowGone = true;
        }
        CHECK(moverGone && shadowGone, "A MOVER REMOVED LISTS ITS LAST BOX (Mover) AND ITS SUN SHADOW (MoverShadow)");
        settle(900);
        // A SLOT REUSED (stage 3's read F2; stage 4's read F2): a still cube removed while another object holds the tail
        // slot - the removed cube's box is its REMOVE; the tail object, renumbered into the freed slot unchanged in the
        // world, lists NOTHING.
        const Vec3 cs(0.5f, 0.5f, 0.5f), ap(-2.0f, 0.25f, 2.0f), bp(2.0f, 0.25f, 2.0f);
        const NodeId ca = scene->createNode(), cb = scene->createNode();
        scene->setNodeTransform(ca, ap, Quat(), cs);
        scene->setNodeTransform(cb, bp, Quat(), cs);
        CHECK(scene->attachMesh(ca, mesh, mat) && scene->attachMesh(cb, mesh, mat), "the two cubes attached (b the tail)");
        settle(900);
        scene->removeNode(ca);
        run(1);
        const GiChangedRegions rs = regions();
        dump(rs, "the slot's reuse");
        bool removed = false, added = false;
        for (const GiChangedRegion &g : rs.regions) {
            if (g.kind != GiRegionKind::Mover) continue;
            if (same(g, cubeAt(ap, cs), 1e-3f)) removed = true;
            if (same(g, cubeAt(bp, cs), 1e-3f)) added = true;
        }
        CHECK(removed && !added,
              "A SLOT REUSED IS A REMOVE (the removed cube's box); THE TAIL'S UNCHANGED OBJECT, RENUMBERED, LISTS NOTHING");
    }

    GiParams off;
    off.mode = GiMode::Off;
    scene->setGlobalIllumination(off);
    run(2);
    std::printf("\nframes %d\n%s (%d failure%s)\n", frames, failures ? "FAILED" : "PASSED", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
