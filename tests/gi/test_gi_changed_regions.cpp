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
//            keeps its rebuild count).
//   SUN/SKY: a sun turn and a 2x sun step are the sun's overflow without a restart, a 5x sun step restarts; a 2x sky step
//            (the ambient's constant band) is the sky's overflow without a restart, a 5x one restarts (Lumen's 4x).
//   CAP:     129 movers stepping in one frame merge into at most 128 regions with no overflow, and every mover's swept box
//            lies inside a listed region of its kind (Mover and MoverShadow).
//   SLAB:    a dolly's cascade-0 scroll lists, on its commit frame, the commit's injection boxes as Slab regions of
//            cascade 0, each inside cascade 0's new box; its chunk frames list no Slab.
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
    View *view = e->createOffscreenView("regions", 64, 64, Colour(0, 0, 0));
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
    {
        PbrParams p;
        p.albedo = Colour(0.8f, 0.3f, 0.2f);
        p.roughness = 0.5f;
        const MeshId mesh = scene->createMesh(enginetest::unitCubeMesh());
        const MaterialId mat = scene->createPbrMaterial(p);
        if (!mesh || !mat || !scene->attachMesh(mover, mesh, mat)) { std::printf("FAIL: the mover\n"); return 1; }
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
    gi.gather = GiToggle::Off;
    gi.numBounces = 1;
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
        const unsigned long long closes0 = scene->published().gi.regionCloses;
        int listing = 0;
        GiChangedRegions first;
        for (int f = 0; f < 600; ++f) {
            run(1);
            const GiStatus g = scene->published().gi;
            if (g.regionCount || g.regionOverflow || g.regionRestart) {
                if (!listing) first = regions();
                ++listing;
            }
        }
        const unsigned long long closes = scene->published().gi.regionCloses - closes0;
        std::printf("   closes %llu, frames listing %d\n", closes, listing);
        if (listing) dump(first, "the first listing frame");
        CHECK(closes == 600u, "the list closed once a frame");
        CHECK(listing == 0, "A STILL SCENE LISTS NOTHING FOR 600 FRAMES (no region, no overflow, no restart)");
    }

    // ---- LAMP ----------------------------------------------------------------------------------------------------
    std::printf("\n== LAMP: a pose step and an intensity step ==\n");
    {
        const Vec3 from = lampPos;
        lampPos = Vec3(2.5f, 1.8f, 0.2f);
        scene->setNodeTransform(lamp, lampPos, Quat(), Vec3(1, 1, 1));
        run(1);
        const GiChangedRegions r = regions();
        dump(r, "the step's frame");
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
        for (int f = 0; f < 120 && commitFrames < 2; ++f) {
            cam = Vec3(cam.x + v, cam.y, cam.z);
            enginetest::testCameraLookAt(view, cam, Vec3(cam.x, 0.6f, 0.0f));
            run(1);
            const GiStatus g = scene->published().gi;
            if (g.cascades.empty()) continue;
            const GiChangedRegions r = regions();
            unsigned slab0 = 0, slab0Inside = 0;
            const GiStatus::CascadeStatus &c0 = g.cascades[0];
            const GiChangedRegion box0{ Vec3(c0.centre.x - c0.halfSize, c0.centre.y - c0.halfSize, c0.centre.z - c0.halfSize),
                                        Vec3(c0.centre.x + c0.halfSize, c0.centre.y + c0.halfSize, c0.centre.z + c0.halfSize),
                                        GiRegionKind::Slab, 1u };
            for (const GiChangedRegion &q : r.regions)
                if (q.kind == GiRegionKind::Slab && q.cascadeMask == 1u) {
                    ++slab0;
                    if (inside(Box{ q.lo, q.hi }, box0, 1e-3f)) ++slab0Inside;
                }
            if (c0.scrolls != scrolls) {
                scrolls = c0.scrolls;
                ++commitFrames;
                if (commitFrames == 1) dump(r, "cascade 0's first commit frame");
                if (slab0 >= 1u && slab0 == slab0Inside) ++slabsOk;
            } else if (c0.scrolling) {
                ++scrollFrames;
                if (slab0) ++chunkSlabFrames;
            }
        }
        std::printf("   commits %d (Slab regions of cascade 0 inside its box on %d), chunk frames %d (listing a Slab %d)\n",
                    commitFrames, slabsOk, scrollFrames, chunkSlabFrames);
        CHECK(commitFrames >= 1 && slabsOk == commitFrames,
              "A COMMIT LISTS ITS INJECTION BOXES AS SLAB REGIONS OF CASCADE 0, INSIDE ITS NEW BOX");
        CHECK(chunkSlabFrames == 0, "...and its chunk frames list none");
    }

    GiParams off;
    off.mode = GiMode::Off;
    scene->setGlobalIllumination(off);
    run(2);
    std::printf("\nframes %d\n%s (%d failure%s)\n", frames, failures ? "FAILED" : "PASSED", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
