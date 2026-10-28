// gi.scroll_slab (ENGINE-UPGRADE-U5 stage 1; RESTORED AND RE-POINTED by PHOTON-7 stage 2, AB S4/A4/A7) — A SCROLLED
// CASCADE IS, BIT FOR BIT, THE CASCADE BUILT WHERE IT IS NOW.
//
// A camera scroll keeps the voxels (and, since PHOTON-7 stage 2, the light) the new box shares with the old one: the
// entering slab is voxelised in chunks into the cascade's SLAB-SIZED SCRATCH while the readers keep the unshifted
// store; at the commit the live store is shifted in place, the scratch's slabs copied into its far ends, and the
// placement moves. A scroll carries the shifts its scratch holds - the largest first, cut to the scratch's layers;
// another rides along while its slab fits (PhotonComponent::planScroll) - so the reference whole build is made AT THE
// SCROLLED CASCADE'S OWN PLACEMENT (the placement pin restores each cascade's builtCam).
//   A. THREE SLABS IN ONE SCROLL: a shift on all three axes, none a multiple of the dispatch's 4 (the bounds guard),
//      all within the scratch's layers - cascade 0's voxeliser store (Scene::giVoxelVolume) EQUAL, float for float, to
//      a whole build at the same placement.
//   B. THE SPREAD: the slab cut into many chunks over many frames; the placement - the readers' box - unchanged until
//      the commit, GI not at rest meanwhile, the committed store a whole build's. A step past the scratch's layers is
//      cut there and its remainder taken by the next scroll(s).
//   D. THE DIRECTIONAL LEVELS (PHOTON-7 stage 2's read D1): after G's one-axis commit the directional volumes, every
//      level, are a whole regeneration from the level-0 light, float for float (gi.regenerateDirectional regenerates
//      nothing else). (A's three-axis commit cannot see a stale interior: its pass region's box is the whole cube.)
//   C. AN EDIT that reaches only the box being built drops the scroll, and the next one builds the edited scene.
//   E. TWO SCROLLS WITH NO REBUILD BETWEEN: a whole build's store; a scroll whose LIVE box is edited mid-flight is
//      dropped, the edit served by its REGION rebuild (PHOTON-7 8A: an edit rebuilds its region, never the whole
//      cascade) and the scroll issued again after it, the committed store a whole build's.
//   F. A SHIFT THAT THROWS HALF WAY (gi.cascadeFaultCommit): the live store is torn, so the cascade scrolls no more
//      until a WHOLE rebuild has made it one box's again. F2. ...and when that rebuild fails twice the store is
//      cleared and the cascade stands down.
//   G. WHAT IS LIT AT A COMMIT: the light was shifted with the store, so the direct term is re-injected AS A DELTA and
//      the sky and the bounces re-solved over the slab and its bounce reach only. A KEPT VOXEL WHOSE DIRECT (both sides)
//      DID NOT CHANGE KEEPS EVERY BIT of its total and back outside that region; THE WORK LAW - the voxels the commit
//      lit, counted by the door (CascadeStatus::commitLit*), are exactly the slab's lit voxels plus the changed direct
//      set the store shows; and WALK = TELEPORT on the direct: the scrolled cascade's direct term is a whole build's,
//      bit for bit. (A placement pin holds cascade 1, so the step scrolls cascade 0 alone.)
//   H. A ONE-FRAME COMMIT (PHOTON-7 stage 2's read D2): the slab's chunks voxelised in the commit's own command buffer
//      before the shift - the store a whole build's (red while the shift's first transition carried no dependency).
//   B2. A CUT SCROLL'S REMAINDER THE CAMERA TAKES BACK (the read's W2): no zero-shift scroll, no rebuild, nothing owed.
//   I. AN OUTER COMMIT AND CASCADE 0 (PHOTON-7 stage 4's read R1): a low sun toward +x and a wall standing just outside
//      cascade 1's +x face, then a step along +x by cascade 1's step, so cascade 1's entering slab brings the wall into
//      its box and cascade 0's sun marches - which read cascade 1's cells past its own shell, out through that face - see
//      it. The two commits NEVER land in one frame (the slots' one-writer rule: a frame whose reserved slot commits or
//      rebuilds cascade 0 holds an outer commit a frame - so an outer commit's re-march of cascade 0 never meets the
//      latch from the slots; the read's case, and the engine's answer to it - the latched re-march OWED to the relight
//      path, never dropped - stands for any other writer), and once still, cascade 0's direct term is a whole build's at
//      the same placements, bit for bit.
//   V. THE VRAM: every cascade's scratch at Epic, measured by the texture-memory door, against the per-size box-sized
//      targets the scratch replaces (the same eleven volumes at res^3).
//   EVERY LEVEL-0 VOLUME is compared - albedo, normal, emissive, its factor per half, the coverage and the position per
//   half, the area weight - and nothing else needs to be: the commit regenerates every mip from level 0.
// A, B, C, E, F run a two-cascade table at 64 cells (a dyadic cell, 10 m / 64: the arithmetic every shipped table has),
// so a store read is 0.26 M voxels. A fixture (the engine alone, one offscreen view).
// ON THE PLACEMENT LATTICE (PHOTON-7 stage 10, 10-2e): a cascade's centre is the camera rounded to its QUANTUM (its step,
// a power of two of cells) and it re-centres once the camera is (0.5 + h) of a step off that centre - so a step is a
// move FROM THE CENTRE (offCentre below), where it was a move from the last re-centre's camera; every shift is a whole
// quantum (or a cut of one). A's bounds guard (no slab a multiple of the dispatch's four) needs the lattice at Q = 2:
// A runs its own chain with cascade 0's step pinned at two cells.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                     \
    } while (0)

static const unsigned kSize = 128;

static GiParams chainGi()
{
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.bounces = true;
    gi.ddgi = GiToggle::Off;
    gi.updateBudget = 0;
    return gi;
}

static bool sameStore(const GiVoxelVolume &a, const GiVoxelVolume &b, const char *what)
{
    if (!a.available || !b.available || a.width != b.width || a.height != b.height || a.depth != b.depth) {
        std::printf("   %s: a store is missing or the sizes differ\n", what);
        return false;
    }
    for (int i = 0; i < 3; ++i)
        if (a.origin[i] != b.origin[i] || a.cell[i] != b.cell[i]) {
            std::printf("   %s: the placement differs (axis %d: origin %.6f vs %.6f)\n", what, i,
                        double(a.origin[i]), double(b.origin[i]));
            return false;
        }
    struct Arr { const char *name; const std::vector<float> *x, *y; };
    const Arr arrs[] = { { "albedo", &a.albedo, &b.albedo }, { "normal", &a.normal, &b.normal },
                         { "coverageP", &a.coverageP, &b.coverageP }, { "coverageN", &a.coverageN, &b.coverageN },
                         { "positionP", &a.positionP, &b.positionP }, { "positionN", &a.positionN, &b.positionN },
                         { "emissive", &a.emissive, &b.emissive },
                         { "emitP", &a.emitP, &b.emitP }, { "emitN", &a.emitN, &b.emitN } };
    bool same = true;
    {
        size_t diff = 0;
        if (a.accumWeight.size() != b.accumWeight.size() || a.accumWeight.empty()) {
            std::printf("   %s: accumWeight sizes %zu vs %zu\n", what, a.accumWeight.size(), b.accumWeight.size());
            same = false;
        } else {
            for (size_t i = 0; i < a.accumWeight.size(); ++i) diff += a.accumWeight[i] != b.accumWeight[i] ? 1u : 0u;
            if (diff) {
                std::printf("   %s: accumWeight differs in %zu of %zu\n", what, diff, a.accumWeight.size());
                same = false;
            }
        }
    }
    for (const Arr &r : arrs) {
        if (r.x->size() != r.y->size() || r.x->empty()) {
            std::printf("   %s: %s sizes %zu vs %zu\n", what, r.name, r.x->size(), r.y->size());
            same = false;
            continue;
        }
        size_t diff = 0, first = size_t(-1);
        for (size_t i = 0; i < r.x->size(); ++i)
            if (std::memcmp(&(*r.x)[i], &(*r.y)[i], sizeof(float)) != 0) {
                if (first == size_t(-1)) first = i;
                ++diff;
            }
        if (diff) {
            const size_t v = first / 4u;
            std::printf("   %s: %s differs in %zu of %zu floats (first at voxel %zu,%zu,%zu: %.7f vs %.7f)\n",
                        what, r.name, diff, r.x->size(), v % size_t(a.width), (v / size_t(a.width)) % size_t(a.height),
                        v / size_t(a.width * a.height), double((*r.x)[first]), double((*r.y)[first]));
            same = false;
        }
    }
    size_t lit = 0;
    for (size_t i = 3; i < a.albedo.size(); i += 4) lit += a.albedo[i] > 0.0f ? 1u : 0u;
    std::printf("   %s: %s (%zu occupied voxels)\n", what, same ? "the stores are equal" : "THE STORES DIFFER", lit);
    return same;
}

/// THE SPREAD ARMS' COUNT (gi.scrollFrames): the slab's units split over this many chunk frames before the commit's
/// (B asserts two or more chunk frames).
static const double kSpreadFrames = 6.0;

int main()
{
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-gi-scroll-slab-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->submitFrame(jahshaka::engine::FrameInput::delta(1.0f / 60.0f));
    Engine *e = engine.get();
    int frames = 1;
    const auto run = [&](int n) { enginetest::frames(e, n); frames += n; };

    View *view = e->createOffscreenView("scroll", kSize, kSize, Colour(0, 0, 0), jahshaka::engine::OffscreenContract::Live);
    Scene *scene = e->createScene("scroll");
    if (!view || !scene) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(scene);
    scene->setAmbient(Colour(0.05f, 0.05f, 0.05f), Colour(0.05f, 0.05f, 0.05f));
    // A room, plus objects in and around the box the walk enters: a junction (two walls on a floor), two-sided plates,
    // and props on both sides of the step.
    const NodeId ground = enginetest::addTestCube(scene, Colour(0.85f, 0.85f, 0.85f), 0.0f, 0.9f);
    enginetest::setNodePosition(scene, ground, Vec3(0.0f, -0.05f, 0.0f));
    enginetest::setNodeScale(scene, ground, Vec3(60.0f, 0.1f, 60.0f));
    const NodeId wallB = enginetest::addTestCube(scene, Colour(0.1f, 0.9f, 0.2f), 0.0f, 0.8f);
    enginetest::setNodePosition(scene, wallB, Vec3(-4.0f, 2.5f, 0.0f));
    enginetest::setNodeScale(scene, wallB, Vec3(0.3f, 5.0f, 12.0f));
    struct Prop { Vec3 pos, scale; Colour albedo; };
    const Prop props[] = {
        { Vec3(1.2f, 0.7f, 1.0f),  Vec3(1.0f, 1.0f, 1.0f),   Colour(0.2f, 0.3f, 0.95f) },
        { Vec3(5.6f, 0.5f, 0.3f),  Vec3(0.9f, 1.0f, 0.7f),   Colour(0.9f, 0.6f, 0.1f) },
        { Vec3(7.3f, 1.4f, -1.7f), Vec3(0.4f, 2.8f, 0.4f),   Colour(0.7f, 0.2f, 0.7f) },
        { Vec3(6.1f, 2.0f, 2.6f),  Vec3(1.7f, 0.05f, 1.3f),  Colour(0.3f, 0.8f, 0.8f) },   // a plate
        { Vec3(6.1f, 2.3f, 2.6f),  Vec3(1.7f, 0.05f, 1.3f),  Colour(0.8f, 0.8f, 0.3f) },   // its twin
        { Vec3(3.4f, 4.6f, -0.9f), Vec3(2.2f, 0.3f, 0.6f),   Colour(0.6f, 0.6f, 0.6f) },
        { Vec3(-1.8f, 1.1f, -2.4f),Vec3(0.6f, 2.2f, 0.6f),   Colour(0.9f, 0.1f, 0.1f) },
        { Vec3(-3.1f, 0.4f, 4.2f), Vec3(0.8f, 0.8f, 0.8f),   Colour(0.4f, 0.9f, 0.4f) },
        { Vec3(1.0f, 0.9f, -4.6f), Vec3(1.1f, 1.8f, 0.5f),   Colour(0.95f, 0.95f, 0.2f) },
    };
    std::vector<NodeId> propNodes;
    for (const Prop &p : props) {
        const NodeId n = enginetest::addTestCube(scene, p.albedo, 0.0f, 0.6f);
        enginetest::setNodePosition(scene, n, p.pos);
        enginetest::setNodeScale(scene, n, p.scale);
        propNodes.push_back(n);
    }
    // ONE EMISSIVE SURFACE: the emissive store is a half float.
    NodeId glow = 0;
    {
        glow = scene->createNode();
        PbrParams gp;
        gp.albedo = Colour(0.8f, 0.8f, 0.8f);
        gp.roughness = 0.35f;
        gp.emissive = Colour(2.5f, 1.2f, 0.4f);
        const MeshId gm = scene->createMesh(enginetest::unitCubeMesh());
        const MaterialId gmat = scene->createPbrMaterial(gp);
        CHECK(glow && gm && gmat && scene->attachMesh(glow, gm, gmat), "an emissive panel");
        enginetest::setNodePosition(scene, glow, Vec3(5.2f, 1.6f, -1.0f));
        enginetest::setNodeScale(scene, glow, Vec3(0.1f, 1.2f, 1.6f));
    }
    NodeId sunNode = enginetest::addDirectionalLight(scene, Vec3(0.3f, -1.0f, -0.5f), 5.0f);

    GiParams small = chainGi();
    small.cascadeCount = 2;
    small.cascadeSet[0] = GiParams::GiCascadeDesc{ 5.0f, 64, 0.0f };
    small.cascadeSet[1] = GiParams::GiCascadeDesc{ 20.0f, 64, 0.0f };
    const Vec3 home(0.0f, 2.0f, 2.0f);
    Vec3 cam = home;
    const auto look = [&](const Vec3 &p) {
        cam = p;
        enginetest::testCameraLookAt(view, p, Vec3(p.x + 1.0f, 1.0f, p.z - 4.0f));
    };
    look(home);
    CHECK(scene->setGlobalIllumination(small), "a two-cascade chain at 64 cells builds");
    run(8);
    // THE COMMIT'S SHADER BUILT BEFORE ANY ARM COUNTS FRAMES: the chain's first build hands the delta injection's
    // permutation to the background compiler, and a commit waits while it builds - a wall-clock wait; the arms count
    // frames, so they start with nothing pending.
    e->waitForAsyncShaders();
    GiStatus st = scene->published().gi;
    if (st.cascades.size() != 2u) { std::printf("FAIL: the chain has %zu cascades\n", st.cascades.size()); return 1; }
    const float cell = st.cascades[0].cell, step = st.cascades[0].step;
    std::printf("   cascade 0: %d^3, cell %.5f m, step %.3f m (%.0f cells)\n", st.cascades[0].resolution, double(cell),
                double(step), double(step / cell));

    // THE REFERENCE: a whole build of the chain AT THE CASCADES' CURRENT PLACEMENTS (the pin restores each builtCam).
    const GiParams *chain = &small;   // the chain the arms run (A pins its own) - the reference is built from the same
    const auto rebuildWhole = [&]() {
        const GiStatus now = scene->published().gi;
        GiPlacementPin pin;
        pin.on = true;
        for (const GiStatus::CascadeStatus &cs : now.cascades) pin.cascadeBuiltCam.push_back(cs.builtCam);
        scene->setGiPlacementPin(pin);
        GiParams off; off.mode = GiMode::Off;
        scene->setGlobalIllumination(off);
        run(2);
        scene->setGlobalIllumination(*chain);
        run(8);
        scene->setGiPlacementPin(GiPlacementPin());
    };
    // A STEP ON THE LATTICE: the camera at cascade `i`'s CENTRE plus d steps on each axis whose d is not 0 (the others
    // where the camera is) - 1.05 steps off the centre crosses its (0.5 + h) band and rounds to the next lattice point.
    const auto offCentre = [&](size_t i, float dx, float dy, float dz) {
        const GiStatus::CascadeStatus &c = scene->published().gi.cascades[i];
        return Vec3(dx != 0.0f ? c.centre.x + dx * c.step : cam.x, dy != 0.0f ? c.centre.y + dy * c.step : cam.y,
                    dz != 0.0f ? c.centre.z + dz * c.step : cam.z);
    };
    // Frames until cascade 0 has committed `want` scrolls (or `cap` frames).
    const auto untilScrolls = [&](unsigned long long want, int cap) {
        int n = 0;
        for (; n < cap && scene->published().gi.cascades[0].scrolls < want; ++n) run(1);
        return n;
    };
    // THE COMMIT'S FRAME: from a step, frames until cascade 0's scroll has LANDED - committed, or thrown at its commit -
    // (a scroll spreads over its count: giCascadeScrollFrames, at most a frame per 4-cell layer of its slab).
    const auto untilLanded = [&](int cap) {
        int n = 1;
        run(1);
        for (; n < cap && scene->published().gi.cascades[0].scrolling; ++n) run(1);
        return n;
    };
    // Frames until cascade 0 owes nothing and has nothing in flight (or `cap` frames).
    const auto untilSettled = [&](int cap) {
        int n = 0;
        for (; n < cap && (scene->published().gi.cascades[0].pending || scene->published().gi.cascades[0].scrolling); ++n)
            run(1);
        return n;
    };
    GiVoxelVolume scrolled, built;
    // THE DIRECTIONAL CHECK, a lambda run on G's one-axis commit (A's three-axis pass region is the whole cube).
    const auto directionalIsRegenerated = [&]() {
        // ---- D. THE DIRECTIONAL LEVELS ARE THE LEVEL-0 LIGHT'S (PHOTON-7 stage 2's read D1) ----------------------
        // Every cone past level 0 reads the directional volumes, and step 0 / step 1 make them from the level-0 light
        // alone - so after a commit they must equal, float for float and at every level, a WHOLE regeneration from the
        // same level 0 (the door gi.regenerateDirectional: nothing else is written). A commit that regenerated them over
        // its boxes only left the kept interior's texels where the shift moved the light away from them (red at
        // 52064e7a3: a step's displacement, growing a step a commit).
        // THE FEEDBACK AT REST FIRST (PHOTON-7 stage 4): its passes move level 0 and regenerate the levels over the bricks
        // they ran, so the comparison is made where nothing moves - every level must still be a whole regeneration of
        // level 0 (a kept interior holding a displaced texel is never a brick the feedback runs, so it still shows).
        for (int f = 0; f < 600 && scene->published().gi.cascades[0].feedbackOwed; ++f) run(1);
        CHECK(!scene->published().gi.cascades[0].feedbackOwed, "D: cascade 0's feedback comes to rest after the commit");
        GiVoxelVolume v;
        CHECK(scene->giVoxelVolume(0, v), "D: the store at rest reads back");
        {
            GiVoxelVolume regen;
            e->setArm("gi.regenerateDirectional", 0.0);
            run(1);
            e->setArm("gi.regenerateDirectional", -1.0);
            CHECK(scene->giVoxelVolume(0, regen), "the regenerated store reads back");
            size_t lightDiff = 0;
            for (size_t i = 0; i < v.light.size() && i < regen.light.size(); ++i)
                lightDiff += std::memcmp(&v.light[i], &regen.light[i], sizeof(float)) != 0 ? 1u : 0u;
            CHECK(v.light.size() == regen.light.size() && lightDiff == 0u,
                  "D: the level-0 light did not move under the door (the comparison's premise)");
            bool same = true;
            size_t levels = 0, texels = 0;
            for (int a = 0; a < 3; ++a) {
                if (v.directional[a].empty() || v.directional[a].size() != regen.directional[a].size()) {
                    std::printf("   D: axis %d holds %zu vs %zu levels\n", a, v.directional[a].size(),
                                regen.directional[a].size());
                    same = false;
                    continue;
                }
                for (size_t L = 0; L < v.directional[a].size(); ++L) {
                    const std::vector<float> &x = v.directional[a][L], &y = regen.directional[a][L];
                    ++levels;
                    texels += x.size() / 4u;
                    size_t diff = 0, first = size_t(-1);
                    for (size_t i = 0; i < x.size() && i < y.size(); ++i)
                        if (std::memcmp(&x[i], &y[i], sizeof(float)) != 0) {
                            if (first == size_t(-1)) first = i;
                            ++diff;
                        }
                    if (diff || x.size() != y.size()) {
                        std::printf("   D: axis %d level %zu differs in %zu of %zu floats (first at float %zu: %.7f vs %.7f)\n",
                                    a, L, diff, x.size(), first, first < x.size() ? double(x[first]) : 0.0,
                                    first < y.size() ? double(y[first]) : 0.0);
                        same = false;
                    }
                }
            }
            std::printf("   D: %zu directional levels, %zu texels compared\n", levels, texels);
            CHECK(same && levels > 3u,
                  "D: A COMMIT'S DIRECTIONAL VOLUMES, EVERY LEVEL, ARE A WHOLE REGENERATION FROM ITS LEVEL 0, FLOAT FOR FLOAT");
        }
    };

    // ---- H. one-frame commits (PHOTON-7 stage 2's read D2) ----------------------------------------
    // THE ROW THAT SEES THE COMMIT'S BARRIER (PhotonVoxelScroll::commit): the slab spread over ONE frame, so the slab's
    // chunks are voxelised in the commit's own command buffer before the shift - a 16-cell step on x (a two-frame commit
    // here), then a step on y, which commits in one frame. (On the lattice the step is y alone: the step back on x the
    // old H1 also made is a whole quantum there and goes first - it was LEFT OUT by the scratch on the old placement,
    // whose y shift was the larger, so the old H1's measured scroll was y alone too.) Measured 2026-10-23: when the shift's first transition
    // carried no dependency (the solver's per-frame reset: oldAccess Undefined, no source stage) the one-frame store lost
    // 186 INTERIOR cells against a whole build (a mid-frame submit used to hide it); with the store's prior use given to
    // the solver, a whole build's. Run first, from home, then home restored.
    std::printf("\n== H: one-frame commits are the store a whole build writes ==\n");
    {
        const Vec3 steps[2] = { Vec3(16.2f * cell, 0.0f, 0.0f), Vec3(0.0f, 16.2f * cell, 0.0f) };
        for (int k = 0; k < 2; ++k) {
            e->setArm("gi.scrollFrames", 1.0);
            run(1);
            const GiStatus pre = scene->published().gi;
            look(Vec3(cam.x + steps[k].x, cam.y + steps[k].y, cam.z + steps[k].z));
            const int n = untilScrolls(pre.cascades[0].scrolls + 1u, 32);
            const GiStatus post = scene->published().gi;
            const Vec3 &c1 = post.cascades[0].centre, &c0 = pre.cascades[0].centre;
            const int sh[3] = { int(std::lround((c1.x - c0.x) / cell)), int(std::lround((c1.y - c0.y) / cell)),
                                int(std::lround((c1.z - c0.z) / cell)) };
            std::printf("   H%d: committed after %d frame(s); shift (%d, %d, %d) cells\n", k, n, sh[0], sh[1], sh[2]);
            CHECK(scene->giVoxelVolume(0, scrolled), "H: the scrolled store reads back");
            e->setArm("gi.scrollFrames", 0.0);
            rebuildWhole();
            CHECK(scene->giVoxelVolume(0, built), "H: the whole build's store reads back");
            CHECK(sameStore(scrolled, built, k ? "H1" : "H0"),
                  k ? "H: A ONE-FRAME COMMIT (THE CHUNKS AND THE SHIFT IN ONE COMMAND BUFFER) IS THE WHOLE BUILD'S, FLOAT FOR FLOAT"
                    : "H: the step's commit is the whole build's");
            if (k) CHECK(n == 1, "H: ...the commit took ONE frame (its chunks voxelised in it)");
        }
        // HOME RESTORED: the chain rebuilt where the arms below start.
        look(home);
        GiParams off; off.mode = GiMode::Off;
        scene->setGlobalIllumination(off);
        run(2);
        scene->setGlobalIllumination(small);
        run(8);
        e->waitForAsyncShaders();
    }

    // ---- A. one scroll, three slabs ----------------------------------------------------------
    std::printf("\n== A: a scroll by three slabs is the store a whole build writes ==\n");
    {
        // ITS OWN CHAIN: cascade 0's step pinned at TWO cells, the lattice at Q = 2 (on a step of 16 every shift is a
        // multiple of four, and the bounds guard is never met). Built at home.
        GiParams q2 = small;
        q2.cascadeSet[0].stepCells = 2.0f;
        chain = &q2;
        {
            GiParams off; off.mode = GiMode::Off;
            scene->setGlobalIllumination(off);
            run(2);
            scene->setGlobalIllumination(q2);
            run(8);
            e->waitForAsyncShaders();
        }
        const GiStatus pre = scene->published().gi;
        std::printf("   A's chain: cascade 0 step %.0f cells, centre %.4f %.4f %.4f\n", double(pre.cascades[0].step / cell),
                    double(pre.cascades[0].centre.x), double(pre.cascades[0].centre.y), double(pre.cascades[0].centre.z));
        // x past its band (1.05 steps off the centre: a shift of 2 cells), y and z RIDING ALONG (0.55 of a step off the
        // centre each, under the band but past the rounding: 2 cells each): three slabs, 6 layers - the Q = 2 scratch's
        // (one step, the even placement's cell and a frame of the fastest camera's travel) - none a multiple of 4.
        // (Was x 1.9 cells past the camera with y and z riding 6 cells each on the 15-cell step's 20-layer scratch.)
        const Vec3 to = offCentre(0, -1.05f, 0.55f, -0.55f);
        look(to);
        const int n = untilScrolls(pre.cascades[0].scrolls + 1u, 32);
        const GiStatus post = scene->published().gi;
        const int sx = int(std::lround((post.cascades[0].centre.x - pre.cascades[0].centre.x) / cell));
        const int sy = int(std::lround((post.cascades[0].centre.y - pre.cascades[0].centre.y) / cell));
        const int sz = int(std::lround((post.cascades[0].centre.z - pre.cascades[0].centre.z) / cell));
        std::printf("   committed after %d frame(s); shift (%d, %d, %d) cells; scroll frames %llu; rebuilds %llu -> %llu\n",
                    n, sx, sy, sz, post.cascades[0].scrollFrames, pre.cascades[0].rebuilds, post.cascades[0].rebuilds);
        CHECK(post.cascades[0].scrolls == pre.cascades[0].scrolls + 1u, "THE CAMERA'S STEP TOOK THE SLAB PATH");
        CHECK(post.cascades[0].rebuilds == pre.cascades[0].rebuilds + 1u, "...as one rebuild of cascade 0");
        CHECK(sx != 0 && sy != 0 && sz != 0, "...on all three axes (three slabs in one scroll)");
        CHECK(sx % 4 != 0 && sy % 4 != 0 && sz % 4 != 0, "...none a multiple of the dispatch's four (the bounds guard)");
        CHECK(scene->giVoxelVolume(0, scrolled), "the scrolled store reads back");
        rebuildWhole();
        CHECK(scene->giVoxelVolume(0, built), "the whole build's store reads back");
        CHECK(sameStore(scrolled, built, "A"), "A SCROLLED STORE IS THE WHOLE BUILD'S, FLOAT FOR FLOAT");
        // THE FIXTURE's CHAIN RESTORED, at home (the arms below start there).
        chain = &small;
        look(home);
        GiParams off; off.mode = GiMode::Off;
        scene->setGlobalIllumination(off);
        run(2);
        scene->setGlobalIllumination(small);
        run(8);
        e->waitForAsyncShaders();
    }

    // ---- B. the spread, and a step past the scratch's layers ---------------------------------
    std::printf("\n== B: the slab spread over frames commits the same store ==\n");
    {
        e->setArm("gi.scrollFrames", kSpreadFrames);
        run(1);
        const GiStatus pre = scene->published().gi;
        const Vec3 from = cam;
        // x 1.6 steps off the CENTRE (the lattice's anchor): the target two quanta away, past the scratch's layers, so
        // the scroll is cut (was 1.5 steps off the camera: from the centre 1.5 rounds to one quantum and nothing is cut).
        const Vec3 to(offCentre(0, -1.6f, 0.0f, 0.0f).x, from.y - 5.2f * cell, from.z + 6.6f * cell);
        look(to);
        int flight = 0;
        bool placedStill = true, notAtRest = true, sawFlight = false;
        for (; flight < 400 && scene->published().gi.cascades[0].scrolls == pre.cascades[0].scrolls; ++flight) {
            run(1);
            const GiStatus mid = scene->published().gi;
            if (mid.cascades[0].scrolls != pre.cascades[0].scrolls) break;
            sawFlight = sawFlight || mid.cascades[0].scrolling;
            placedStill = placedStill && mid.cascades[0].centre.x == pre.cascades[0].centre.x &&
                          mid.cascades[0].centre.y == pre.cascades[0].centre.y &&
                          mid.cascades[0].centre.z == pre.cascades[0].centre.z;
            notAtRest = notAtRest && !mid.giAtRest;
        }
        const GiStatus post = scene->published().gi;
        const unsigned long long spent = post.cascades[0].scrollFrames - pre.cascades[0].scrollFrames;
        const int sx = int(std::lround((post.cascades[0].centre.x - pre.cascades[0].centre.x) / cell));
        std::printf("   the first scroll committed after %d frame(s), %llu of them building chunks; its x shift %d cells "
                    "(the step asked %.1f)\n", flight + 1, spent, sx, double((to.x - pre.cascades[0].centre.x) / cell));
        CHECK(post.cascades[0].scrolls == pre.cascades[0].scrolls + 1u, "the spread scroll commits");
        CHECK(sawFlight && spent >= 2u, "...SPREAD over several frames (the slab's 4-cell layers, eight a frame)");
        CHECK(placedStill, "...the cascade's placement - its readers' box - unchanged until the commit");
        CHECK(notAtRest, "...and GI not at rest while the scroll is owed");
        // (the scratch's layers: one step, the even placement's cell and a frame of the fastest camera's travel)
        const int layers = int(std::ceil((step / cell + 1.0f + std::ceil(kGiFlyMetresPerFrame / cell)) * 0.5f)) * 2;
        CHECK(std::abs(sx) <= layers && std::abs(sx) < int(std::ceil(1.5f * step / cell)),
              "...its shift cut to the scratch's layers (one step's slab and a frame's travel)");
        const int rest = untilSettled(600);
        const GiStatus done = scene->published().gi;
        std::printf("   the remainder: %llu more scroll(s), settled after %d frames\n",
                    done.cascades[0].scrolls - post.cascades[0].scrolls, rest);
        CHECK(rest < 600 && done.cascades[0].scrolls > post.cascades[0].scrolls,
              "...and THE REMAINDER IS SCROLLED (a cut scroll leaves its cascade owed), then it owes nothing");
        CHECK(done.cascades[0].builtCam.x == cam.x && done.cascades[0].builtCam.z == cam.z,
              "...the cascade placed for the camera on every axis");
        e->setArm("gi.scrollFrames", 0.0);
        CHECK(scene->giVoxelVolume(0, scrolled), "the spread store reads back");
        rebuildWhole();
        CHECK(scene->giVoxelVolume(0, built), "the whole build's store reads back");
        CHECK(sameStore(scrolled, built, "B"), "A SPREAD SCROLL'S STORE IS THE WHOLE BUILD'S, FLOAT FOR FLOAT");
    }

    // ---- C. an edit in the box being built -------------------------------------------------------
    std::printf("\n== C: an edit reaching only the box being built drops the scroll ==\n");
    {
        e->setArm("gi.scrollFrames", kSpreadFrames);
        run(1);
        const GiStatus pre = scene->published().gi;
        const Vec3 from = cam;
        const Vec3 to = offCentre(0, 1.05f, 0.0f, 0.0f);   // (was 1.05 steps from the camera)
        look(to);
        run(3);   // in flight: a few chunks built
        const bool flying = scene->published().gi.cascades[0].scrolling;
        // The prop beyond the live box's +x face, inside the target's: the dirty path's box test (the live box) does
        // not reach it.
        const float liveMaxX = pre.cascades[0].centre.x + pre.cascades[0].halfSize;
        enginetest::setNodePosition(scene, propNodes[2], Vec3(liveMaxX + 0.4f * step, 1.0f, from.z - 1.0f));
        const int n = untilScrolls(pre.cascades[0].scrolls + 1u, 400);
        const GiStatus post = scene->published().gi;
        std::printf("   in flight when edited: %d; committed %d frame(s) later; aborts %llu -> %llu\n",
                    int(flying), n, pre.cascadeScrollAborts, post.cascadeScrollAborts);
        CHECK(flying, "the scroll was in flight when the edit landed");
        CHECK(post.cascadeScrollAborts > pre.cascadeScrollAborts, "THE EDIT DROPPED THE SCROLL");
        CHECK(post.cascades[0].scrolls == pre.cascades[0].scrolls + 1u, "...and a scroll committed after it");
        e->setArm("gi.scrollFrames", 0.0);
        untilSettled(400);
        run(2);
        CHECK(scene->giVoxelVolume(0, scrolled), "the store reads back");
        rebuildWhole();
        CHECK(scene->giVoxelVolume(0, built), "the whole build's store reads back");
        CHECK(sameStore(scrolled, built, "C"), "THE COMMITTED STORE HOLDS THE EDITED SCENE, AS A WHOLE BUILD DOES");
    }

    // ---- E. two scrolls chained; a live-box edit mid-flight -----------------------------------
    std::printf("\n== E: two scrolls with no rebuild between; a live-box edit drops a scroll ==\n");
    {
        const GiStatus pre = scene->published().gi;
        look(Vec3(cam.x + 1.05f * step, cam.y, cam.z));
        untilScrolls(pre.cascades[0].scrolls + 1u, 32);
        look(Vec3(cam.x, cam.y + 0.2f * step, cam.z - 1.05f * step));
        untilScrolls(pre.cascades[0].scrolls + 2u, 32);
        const GiStatus two = scene->published().gi;
        CHECK(two.cascades[0].scrolls == pre.cascades[0].scrolls + 2u &&
              two.cascades[0].rebuilds == pre.cascades[0].rebuilds + 2u,
              "two scrolls, no rebuild between them");
        CHECK(scene->giVoxelVolume(0, scrolled), "the twice-scrolled store reads back");
        rebuildWhole();
        CHECK(scene->giVoxelVolume(0, built), "the whole build's store reads back");
        CHECK(sameStore(scrolled, built, "E1"), "TWO CHAINED SCROLLS LEAVE A WHOLE BUILD'S STORE, FLOAT FOR FLOAT");

        e->setArm("gi.scrollFrames", kSpreadFrames);
        run(1);
        const GiStatus pre2 = scene->published().gi;
        look(offCentre(0, -1.05f, 0.0f, 0.0f));   // (was 1.05 steps from the camera: the lattice's step is from the centre)
        run(3);
        const bool flying = scene->published().gi.cascades[0].scrolling;
        // A prop INSIDE the live box (and the target's): the dirty path marks the cascade itself.
        enginetest::setNodePosition(scene, propNodes[0], Vec3(pre2.cascades[0].centre.x - 0.4f, 0.7f, 1.0f));
        const int n = untilSettled(400);
        const GiStatus post = scene->published().gi;
        std::printf("   in flight when edited: %d; aborts %llu -> %llu; scrolls %llu -> %llu; region rebuilds %llu -> %llu;"
                    " settled after %d frames\n",
                    int(flying), pre2.cascadeScrollAborts, post.cascadeScrollAborts, pre2.cascades[0].scrolls,
                    post.cascades[0].scrolls, (unsigned long long)pre2.cascades[0].regionRebuilds,
                    (unsigned long long)post.cascades[0].regionRebuilds, n);
        CHECK(flying, "the scroll was in flight when its live box was edited");
        CHECK(post.cascadeScrollAborts > pre2.cascadeScrollAborts, "THE LIVE-BOX EDIT DROPPED THE SCROLL");
        CHECK(post.cascades[0].regionRebuilds > pre2.cascades[0].regionRebuilds,
              "...the edit served by its REGION rebuild (PHOTON-7 8A: never the whole cascade)");
        CHECK(post.cascades[0].scrolls == pre2.cascades[0].scrolls + 1u, "...and the scroll issued again after it, committed");
        e->setArm("gi.scrollFrames", 0.0);
        run(2);
        CHECK(scene->giVoxelVolume(0, scrolled), "the store reads back");
        rebuildWhole();
        CHECK(scene->giVoxelVolume(0, built), "the whole build's store reads back");
        CHECK(sameStore(scrolled, built, "E2"), "THE EDITED SCENE'S STORE IS A WHOLE BUILD'S");
    }

    // ---- F. a shift that throws half way -------------------------------------------------------
    std::printf("\n== F: a shift that throws half way leaves a torn store that never scrolls ==\n");
    {
        const GiStatus pre = scene->published().gi;
        e->setArm("gi.cascadeFaultCommit", 0.0);
        look(Vec3(cam.x + 1.05f * step, cam.y, cam.z));
        untilLanded(32);   // the scroll's chunk frames, then its commit: the shift throws half way - torn, the first failure
        // ...AND THE WHOLE REBUILD THAT REPAIRS IT FAILS TOO (gi.cascadeFault): the second failure, which stands an
        // ordinary cascade down - never a torn one.
        e->setArm("gi.cascadeFaultCommit", -1.0);
        e->setArm("gi.cascadeFault", 0.0);
        run(1);
        e->setArm("gi.cascadeFault", -1.0);
        const GiStatus torn = scene->published().gi;
        CHECK(torn.cascades[0].pending != 0 && torn.cascades[0].scrolls == pre.cascades[0].scrolls,
              "A TORN STORE IS NEVER STOOD DOWN: two failures, still owed its whole rebuild");
        run(1);
        int n = 0;
        for (; n < 8 && scene->published().gi.cascades[0].pending; ++n) run(1);
        const GiStatus post = scene->published().gi;
        std::printf("   scrolls %llu -> %llu, rebuilds %llu -> %llu, owed after %d frames: %d\n",
                    pre.cascades[0].scrolls, post.cascades[0].scrolls, pre.cascades[0].rebuilds,
                    post.cascades[0].rebuilds, n, post.cascades[0].pending);
        CHECK(post.cascades[0].scrolls == pre.cascades[0].scrolls, "THE TORN STORE IS NEVER SCROLLED FROM");
        CHECK(post.cascades[0].rebuilds == pre.cascades[0].rebuilds + 1u && !post.cascades[0].pending,
              "...the next frame rebuilds it whole, and it owes nothing");
        CHECK(scene->giVoxelVolume(0, scrolled), "the store reads back");
        rebuildWhole();
        CHECK(scene->giVoxelVolume(0, built), "the whole build's store reads back");
        CHECK(sameStore(scrolled, built, "F"), "THE REBUILT STORE IS A WHOLE BUILD'S");
    }

    // ---- F2. a torn store whose whole rebuild fails TWICE ----------------------------------------
    std::printf("\n== F2: a torn store whose whole rebuild fails twice is cleared and stood down ==\n");
    {
        const GiStatus pre = scene->published().gi;
        e->setArm("gi.cascadeFaultCommit", 0.0);
        look(Vec3(cam.x + 1.05f * step, cam.y, cam.z));
        untilLanded(32);   // torn
        e->setArm("gi.cascadeFaultCommit", -1.0);
        e->setArm("gi.cascadeFault", 0.0);
        run(1);   // the first whole rebuild fails: still owed
        const GiStatus once = scene->published().gi;
        run(1);   // the second: cleared and stood down
        const GiStatus twice = scene->published().gi;
        const unsigned long long rebuildsBefore = scene->published().gi.cascades[0].rebuilds;
        int owed = 0;
        for (int f = 0; f < 6; ++f) { run(1); owed += scene->published().gi.cascades[0].pending ? 1 : 0; }
        const GiStatus after = scene->published().gi;
        std::printf("   pending %d / %d / %d, torn-cleared %d / %d (cleared %llu -> %llu)\n", once.cascades[0].pending,
                    twice.cascades[0].pending, after.cascades[0].pending, int(twice.cascades[0].tornCleared),
                    int(after.cascades[0].tornCleared), pre.cascadeTornCleared, after.cascadeTornCleared);
        CHECK(once.cascades[0].pending != 0 && !once.cascades[0].tornCleared,
              "one failed whole rebuild of a torn store: still owed it");
        CHECK(twice.cascades[0].pending == 0 && after.cascades[0].pending == 0 && owed == 0 &&
                  after.cascades[0].tornCleared && after.cascadeTornCleared == pre.cascadeTornCleared + 1u &&
                  after.cascades[0].rebuilds == rebuildsBefore,
              "TWO: ITS VOLUMES CLEARED AND IT STANDS DOWN (bounded, the status saying torn-cleared)");
        GiVoxelVolume v;
        CHECK(scene->giVoxelVolume(0, v), "the cleared store reads back");
        e->setArm("gi.cascadeFault", -1.0);
        bool empty = v.available && !v.albedo.empty();
        for (const std::vector<float> *a : { &v.albedo, &v.emissive, &v.coverageP, &v.coverageN, &v.light })
            for (float x : *a) empty = empty && x == 0.0f;
        for (unsigned x : v.accumWeight) empty = empty && x == 0u;
        CHECK(empty, "...EMPTY: albedo, emissive, coverage, the area weight and the light all zero");
        rebuildWhole();
    }

    // ---- G. what is lit at a commit (PHOTON-7 stage 2) --------------------------------------------
    std::printf("\n== G: the commit's light - the direct as a delta, the feedback over the bricks it changed ==\n");
    {
        // NOTHING OF THE CHAIN IN FLIGHT (an outer commit re-injects cascade 0 too), THE FEEDBACK AT REST (PHOTON-7 stage
        // 4: no brick still converging) AND THE LAST COMMIT'S COUNT LANDED (read back a few frames late): the counts and
        // the store's changes below are this step's commit's alone.
        const auto chainBusy = [&]() {
            for (const GiStatus::CascadeStatus &cs : scene->published().gi.cascades)
                if (cs.pending || cs.scrolling || cs.feedbackOwed) return true;
            return false;
        };
        for (int round = 0; round < 4; ++round) {
            for (int f = 0; f < 128 && chainBusy(); ++f) run(1);
            run(8);
            if (!chainBusy()) break;
        }
        GiVoxelVolume before, after;
        CHECK(scene->giVoxelVolume(0, before), "the store before the step reads back");
        const GiStatus pre = scene->published().gi;
        // THE STEP SCROLLS CASCADE 0 ALONE, so the counts are its commit's: a placement pin holds cascade 1 at its own
        // camera and places cascade 0 for the stepped one (an outer commit re-injects cascade 0 as a delta too).
        const Vec3 target = offCentre(0, 1.05f, 0.0f, 0.0f);   // (was 1.05 steps from the camera)
        {
            GiPlacementPin pin;
            pin.on = true;
            pin.cascadeBuiltCam.push_back(target);
            for (size_t k = 1; k < pre.cascades.size(); ++k) pin.cascadeBuiltCam.push_back(pre.cascades[k].builtCam);
            scene->setGiPlacementPin(pin);
        }
        look(target);
        const int n = untilScrolls(pre.cascades[0].scrolls + 1u, 32);
        CHECK(scene->giVoxelVolume(0, after), "the store after the commit reads back");
        run(4);   // the commit's count lands (read back a few frames late)
        const GiStatus post = scene->published().gi;
        const int res = after.width;
        const int sx = int(std::lround((after.origin[0] - before.origin[0]) / after.cell[0]));
        const int sy = int(std::lround((after.origin[1] - before.origin[1]) / after.cell[1]));
        const int sz = int(std::lround((after.origin[2] - before.origin[2]) / after.cell[2]));
        // THE BRICKS THE COMMIT ACTIVATED (PHOTON-7 stage 4): the feedback's pass of the commit's frame runs over the 8^3
        // bricks the shift brought in (the slab's) and those holding a voxel whose direct term changed (the mask's word
        // 0) - every other brick is stopped, and its voxels keep their bits (a resume takes effect the next frame).
        const int bres = (res + 7) / 8;
        std::vector<char> activeBrick(size_t(bres) * bres * bres, 0);
        const auto brickOf = [&](int x, int y, int z) { return (size_t(z >> 3) * bres + (y >> 3)) * bres + (x >> 3); };
        for (int z = 0; z < res; ++z)
            for (int y = 0; y < res; ++y)
                for (int x = 0; x < res; ++x) {
                    const size_t ia = (size_t(z) * res + y) * res + x;
                    const int ox = x + sx, oy = y + sy, oz = z + sz;
                    if (!(ox >= 0 && ox < res && oy >= 0 && oy < res && oz >= 0 && oz < res)) { activeBrick[brickOf(x, y, z)] = 1; continue; }
                    const size_t ib = (size_t(oz) * res + oy) * res + ox;
                    if (std::memcmp(&after.direct[ia * 4], &before.direct[ib * 4], 16) != 0 ||
                        std::memcmp(&after.directBack[ia * 4], &before.directBack[ib * 4], 16) != 0)
                        activeBrick[brickOf(x, y, z)] = 1;
                }
        size_t activeBricks = 0;
        for (char a : activeBrick) activeBricks += a ? 1u : 0u;
        size_t kept = 0, unchangedKept = 0, unchangedOutside = 0, violations = 0, bandAll = 0, slabLit = 0;
        int bandXLo = res, bandXHi = 0;   // the band's extent on the moved axis
        for (int z = 0; z < res; ++z)
            for (int y = 0; y < res; ++y)
                for (int x = 0; x < res; ++x) {
                    const size_t ia = (size_t(z) * res + y) * res + x;
                    const int ox = x + sx, oy = y + sy, oz = z + sz;
                    const bool inOld = ox >= 0 && ox < res && oy >= 0 && oy < res && oz >= 0 && oz < res;
                    if (!inOld) {   // the slab
                        bool lit = false;
                        for (int k = 0; k < 4; ++k)
                            lit = lit || after.direct[ia * 4 + k] != 0.0f || after.directBack[ia * 4 + k] != 0.0f;
                        slabLit += lit ? 1u : 0u;
                        continue;
                    }
                    ++kept;
                    const size_t ib = (size_t(oz) * res + oy) * res + ox;
                    // the direct terms of BOTH sides (a delta replaces each in its own total)
                    const bool sameDirect = std::memcmp(&after.direct[ia * 4], &before.direct[ib * 4], 16) == 0 &&
                                            std::memcmp(&after.directBack[ia * 4], &before.directBack[ib * 4], 16) == 0;
                    if (!sameDirect) {
                        ++bandAll;
                        bandXLo = std::min(bandXLo, x);
                        bandXHi = std::max(bandXHi, x + 1);
                        continue;
                    }
                    ++unchangedKept;
                    if (activeBrick[brickOf(x, y, z)]) continue;   // a brick the feedback ran: re-solved by design
                    ++unchangedOutside;
                    if (std::memcmp(&after.light[ia * 4], &before.light[ib * 4], 16) != 0 ||
                        std::memcmp(&after.lightBack[ia * 4], &before.lightBack[ib * 4], 16) != 0)
                        ++violations;
                }
        const unsigned long long litSlab = post.cascades[0].commitLitSlab - pre.cascades[0].commitLitSlab;
        const unsigned long long litBand = post.cascades[0].commitLitBand - pre.cascades[0].commitLitBand;
        const unsigned long long counted = post.cascades[0].commitCounted - pre.cascades[0].commitCounted;
        const unsigned long long computed = post.cascades[0].commitInjectCells - pre.cascades[0].commitInjectCells;
        const unsigned long long marched = post.cascades[0].deltaMarched - pre.cascades[0].deltaMarched;
        const unsigned long long whole = (unsigned long long)res * res * res;
        std::printf("   committed after %d frame(s), shift (%d, %d, %d); the bricks the commit activated %zu of %d; the "
                    "outer cascades' scrolls", n, sx, sy, sz, activeBricks, bres * bres * bres);
        bool outerScrolled = false;
        for (size_t k = 1; k < post.cascades.size(); ++k) {
            std::printf(" +%llu", post.cascades[k].scrolls - pre.cascades[k].scrolls);
            outerScrolled = outerScrolled || post.cascades[k].scrolls != pre.cascades[k].scrolls;
        }
        std::printf("\n");
        std::printf("   kept %zu voxels: %zu with both sides' direct unchanged (%zu outside the activated bricks: %zu "
                    "changed their total or back), %zu with a changed one (the band, x [%d, %d))\n", kept, unchangedKept,
                    unchangedOutside, violations, bandAll, bandXLo, bandXHi);
        std::printf("   THE WORK: lit %llu in the slab (the store's own: %zu lit slab voxels) + %llu outside it (the store's "
                    "band %zu) = %llu of %llu voxels (%.2f %%); the direct computed over %llu (%.1f %%), MARCHED %llu (the "
                    "door's read-back); counted %llu commit(s)\n",
                    litSlab, slabLit, litBand, bandAll, litSlab + litBand, whole,
                    100.0 * double(litSlab + litBand) / double(whole), computed, 100.0 * double(computed) / double(whole),
                    marched, counted);
        CHECK(post.cascades[0].scrolls == pre.cascades[0].scrolls + 1u && sx != 0, "the step committed as a scroll");
        CHECK(unchangedOutside > 0 && violations == 0,
              "A VOXEL WHOSE DIRECT DID NOT CHANGE KEEPS EVERY BIT of its total and back (outside the bricks the commit "
              "activated: the feedback ran nowhere else)");
        CHECK(!outerScrolled, "only cascade 0 scrolled (its counts are its own commit's)");
        CHECK(counted == 1u && litBand == bandAll && litSlab == slabLit,
              "THE WORK LAW, counted by the door: the voxels lit are the slab's lit voxels plus the changed direct set");
        CHECK(litSlab + litBand < whole / 2u && computed < whole && marched == computed,
              "...a fraction of the cascade, computed over its boxes - and the door's MARCHES are exactly those cells "
              "(the old commit lit it whole)");
        directionalIsRegenerated();   // D, on this one-axis commit (at the feedback's rest)
        scene->setGiPlacementPin(GiPlacementPin());
        rebuildWhole();
        GiVoxelVolume built;
        CHECK(scene->giVoxelVolume(0, built), "the whole build's store reads back");
        size_t directDiff = 0;
        for (size_t i = 0; i < built.direct.size() && i < after.direct.size(); ++i)
            directDiff += std::memcmp(&built.direct[i], &after.direct[i], 4) != 0 ? 1u : 0u;
        std::printf("   the direct after the walk against a whole build at its placement: %zu of %zu floats differ\n",
                    directDiff, built.direct.size());
        CHECK(built.direct.size() == after.direct.size() && directDiff == 0,
              "WALK = TELEPORT ON THE DIRECT: the scrolled cascade's direct term is the whole build's, bit for bit");
    }

    // ---- B2. a cut scroll's remainder the camera takes back (PHOTON-7 stage 2's read W2) ----------
    // A cut scroll leaves its cascade owed the rest; if the camera comes back to where the cut placed the cascade
    // before the rest is planned, nothing is owed - no zero-shift scroll (it would re-inject the whole cascade as a delta).
    std::printf("\n== B2: a cut scroll's remainder taken back by the camera owes nothing ==\n");
    {
        e->setArm("gi.scrollFrames", kSpreadFrames);
        run(1);
        const GiStatus pre = scene->published().gi;
        look(Vec3(cam.x - 1.5f * step, cam.y, cam.z));
        untilScrolls(pre.cascades[0].scrolls + 1u, 400);
        const GiStatus post = scene->published().gi;
        look(Vec3(post.cascades[0].centre.x, cam.y, cam.z));   // back to the cut's own placement
        run(30);
        const GiStatus done = scene->published().gi;
        std::printf("   the cut: x %.3f m; after the camera came back: scrolls %llu -> %llu, rebuilds %llu -> %llu, pending %d\n",
                    double(post.cascades[0].centre.x - pre.cascades[0].centre.x), post.cascades[0].scrolls,
                    done.cascades[0].scrolls, post.cascades[0].rebuilds, done.cascades[0].rebuilds, done.cascades[0].pending);
        CHECK(post.cascades[0].scrolls == pre.cascades[0].scrolls + 1u && post.cascades[0].centre.x != pre.cascades[0].centre.x,
              "B2: the cut scroll committed");
        CHECK(done.cascades[0].scrolls == post.cascades[0].scrolls && done.cascades[0].rebuilds == post.cascades[0].rebuilds &&
                  !done.cascades[0].pending && !done.cascades[0].scrolling,
              "B2: THE REMAINDER THE CAMERA TOOK BACK IS NO SCROLL AND NO REBUILD - nothing is owed");
        e->setArm("gi.scrollFrames", 0.0);
        run(1);
    }

    // ---- I. an outer commit and cascade 0 (PHOTON-7 stage 4's read R1) ----------------------------
    std::printf("\n== I: an outer commit and cascade 0: never one frame; still, cascade 0's direct is a whole build's ==\n");
    {
        const auto chainBusy = [&]() {
            for (const GiStatus::CascadeStatus &cs : scene->published().gi.cascades)
                if (cs.pending || cs.scrolling || cs.feedbackOwed) return true;
            return scene->published().gi.chainOwed;
        };
        const auto settle = [&]() {
            for (int round = 0; round < 4; ++round) {
                for (int f = 0; f < 200 && chainBusy(); ++f) run(1);
                run(8);
                if (!chainBusy()) break;
            }
        };
        // THE SUN LOW TOWARD +x, THE WALL just past cascade 1's +x face (the camera's x + its 20 m half): 15 m tall, 40 m wide
        scene->removeNode(sunNode);
        sunNode = enginetest::addDirectionalLight(scene, Vec3(-1.0f, -0.35f, -0.1f), 5.0f);
        const NodeId wall = enginetest::addTestCube(scene, Colour(0.5f, 0.5f, 0.5f), 0.0f, 0.9f);
        enginetest::setNodePosition(scene, wall, Vec3(cam.x + 22.5f, 7.5f, cam.z));
        enginetest::setNodeScale(scene, wall, Vec3(0.5f, 15.0f, 40.0f));
        settle();
        // THE STEP: both cascades scroll; every frame either commits in is recorded (the scheduler's one-writer rule, PG
        // planCascades: a frame whose reserved slot commits or rebuilds cascade 0 holds an outer commit a frame - so the
        // two never land in one frame, and the latch case cannot arise from the slots).
        e->setArm("gi.scrollFrames", 3.0);
        run(1);
        const GiStatus pre = scene->published().gi;
        // (1.05 of cascade 1's steps off ITS centre: was off the camera - the lattice's step is from the centre)
        const Vec3 toI = offCentre(1, 1.05f, 0.0f, 0.0f);
        const float up = toI.x - cam.x;
        look(toI);
        bool together = false;
        unsigned long long s0 = pre.cascades[0].scrolls, s1 = pre.cascades[1].scrolls;
        std::string when;
        for (int f = 0; f < 40; ++f) {
            run(1);
            const GiStatus g = scene->published().gi;
            const bool m0 = g.cascades[0].scrolls != s0, m1 = g.cascades[1].scrolls != s1;
            together = together || (m0 && m1);
            if (m0) when += " c0@" + std::to_string(f);
            if (m1) when += " c1@" + std::to_string(f);
            s0 = g.cascades[0].scrolls;
            s1 = g.cascades[1].scrolls;
        }
        e->setArm("gi.scrollFrames", 0.0);
        settle();
        const GiStatus post = scene->published().gi;
        std::printf("   the step +x %.3f m: cascade 0 scrolls %llu -> %llu, cascade 1 %llu -> %llu; the commits' frames:%s\n",
                    double(up), pre.cascades[0].scrolls, post.cascades[0].scrolls, pre.cascades[1].scrolls,
                    post.cascades[1].scrolls, when.c_str());
        CHECK(post.cascades[0].scrolls > pre.cascades[0].scrolls && post.cascades[1].scrolls > pre.cascades[1].scrolls,
              "I: both cascades scrolled");
        CHECK(!together, "I: CASCADE 0 AND AN OUTER CASCADE NEVER COMMIT IN ONE FRAME (the slots' one-writer rule)");
        // BOTH CASCADES' DIRECT against a whole build: cascade 0's (the outer commit's re-march of it, by the region its moved
        // faces swept against the sun reach - PHOTON-7 stage 4's read R4) and cascade 1's own (the outermost commit's own
        // delta, by the same region and its slabs)
        GiVoxelVolume walked[2], whole[2];
        for (int k = 0; k < 2; ++k) CHECK(scene->giVoxelVolume(k, walked[k]), "I: the walked store reads back");
        rebuildWhole();   // (at every cascade's builtCam)
        for (int k = 0; k < 2; ++k) CHECK(scene->giVoxelVolume(k, whole[k]), "I: the whole build's store reads back");
        size_t diff[2] = { 0, 0 };
        for (int k = 0; k < 2; ++k)
            for (size_t i = 0; i < whole[k].direct.size() && i < walked[k].direct.size(); ++i)
                diff[k] += std::memcmp(&whole[k].direct[i], &walked[k].direct[i], 4) != 0 ? 1u : 0u;
        std::printf("   the direct against a whole build at the same placements: cascade 0 %zu of %zu floats differ, cascade 1 "
                    "%zu of %zu\n", diff[0], whole[0].direct.size(), diff[1], whole[1].direct.size());
        CHECK(whole[0].direct.size() == walked[0].direct.size() && diff[0] == 0,
              "I: ONCE STILL, CASCADE 0'S DIRECT IS THE WHOLE BUILD'S, bit for bit (the outer commit's re-march reached it)");
        CHECK(whole[1].direct.size() == walked[1].direct.size() && diff[1] == 0,
              "I: ...AND THE OUTERMOST'S OWN, bit for bit (its commit's region: the faces swept against the light, the slabs)");
        scene->removeNode(wall);
        scene->removeNode(sunNode);
        sunNode = enginetest::addDirectionalLight(scene, Vec3(0.3f, -1.0f, -0.5f), 5.0f);
        look(Vec3(cam.x - up, cam.y, cam.z));
        settle();
    }

    // ---- V. the VRAM at Epic ---------------------------------------------------------------------
    std::printf("\n== V: the scroll scratches' VRAM at Epic (the texture-memory door) ==\n");
    {
        GiParams epic = chainGi();
        epic.quality = GiQuality::Epic;
        CHECK(scene->setGlobalIllumination(epic), "the Epic chain builds");
        run(4);
        std::vector<TextureMemoryEntry> tex;
        CHECK(e->textureMemory(tex), "the texture door answers");
        const GiStatus ep = scene->published().gi;
        unsigned long long scratch = 0, perVoxelBytes = 0, scratchVoxels = 0;
        std::vector<int> sizes;
        for (const TextureMemoryEntry &t : tex)
            if (t.name.rfind("PhotonScrollScratch/", 0) == 0) {
                scratch += t.bytes;
                scratchVoxels += (unsigned long long)t.width * t.height * t.depth;
            }
        // the deleted box-sized targets: one per SIZE, the same eleven volumes at res^3 (per-voxel bytes from the scratch)
        for (const GiStatus::CascadeStatus &cs : ep.cascades)
            if (std::find(sizes.begin(), sizes.end(), cs.resolution) == sizes.end()) sizes.push_back(cs.resolution);
        // the scratch's per-voxel bytes: its bytes over its voxels across the eleven volumes (they share dims per cascade)
        perVoxelBytes = scratchVoxels ? scratch * 11ull / scratchVoxels : 0ull;
        unsigned long long targets = 0;
        for (int r : sizes) targets += perVoxelBytes * (unsigned long long)r * r * r;
        std::printf("   the scratches: %.1f MB (%zu cascades; %llu B a voxel over the eleven volumes); the box-sized "
                    "targets they replace: %.1f MB (%zu sizes); net %.1f MB\n",
                    double(scratch) / 1048576.0, ep.cascades.size(), perVoxelBytes, double(targets) / 1048576.0,
                    sizes.size(), (double(scratch) - double(targets)) / 1048576.0);
        CHECK(scratch > 0 && scratch < targets, "THE SCRATCHES HOLD LESS THAN THE TARGETS THEY REPLACE");
        // THE COMMIT'S LIGHT AT EPIC, MEASURED (PHOTON-7 stage 2's read D1: the directional levels are regenerated WHOLE
        // at every commit): the GPU ms of cascade 0's commit's injection phases - direct, mips (step 0 and step 1 whole,
        // D's chain), sky, bounce - from the monitor, the clocks unlocked. Printed, not a bar.
        run(8);
        e->waitForAsyncShaders();
        const GiStatus pre = scene->published().gi;
        e->setFrameMonitor(MonitorLevel::Review);
        look(Vec3(cam.x + 1.05f * pre.cascades[0].step, cam.y, cam.z));
        untilScrolls(pre.cascades[0].scrolls + 1u, 64);
        run(6);   // the GPU samples come back a few frames late
        e->setFrameMonitor(MonitorLevel::Off);
        std::vector<MonitorRecord> records;
        while (e->popMonitorRecords(records) > 0) {}
        unsigned commits = 0;
        for (const MonitorRecord &r : records) {
            bool commit = false;
            float ms[4] = { -1.0f, -1.0f, -1.0f, -1.0f };
            const char *const names[4] = { "vct.inject.direct0", "vct.inject.mips0", "vct.inject.sky0", "vct.inject.bounce0" };
            for (const CacheWork &w : r.cacheWork) {
                if (w.detail == "vct.cascade0") commit = true;
                for (int k = 0; k < 4; ++k)
                    if (w.detail == names[k] && w.gpuMs >= 0.0f) ms[k] = std::max(ms[k], 0.0f) + w.gpuMs;
            }
            if (!commit) continue;
            ++commits;
            std::printf("   Epic cascade 0's commit (frame %llu): GPU ms direct %.3f, mips %.3f (the directional levels whole), "
                        "sky %.3f, bounce %.3f\n", r.frame, double(ms[0]), double(ms[1]), double(ms[2]), double(ms[3]));
        }
        CHECK(commits >= 1u, "the Epic commit's light was measured");
    }

    scene->removeNode(ground);
    scene->removeNode(wallB);
    scene->removeNode(glow);
    for (NodeId n : propNodes) scene->removeNode(n);
    run(2);
    std::printf("\nframes %d\n%s (%d failure%s)\n", frames, failures ? "FAILED" : "PASSED", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
