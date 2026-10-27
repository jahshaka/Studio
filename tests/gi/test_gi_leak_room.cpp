// gi.leak_room — THE FIELD'S LEAK FIX, UNDER A CASCADE CHAIN TOO
// (SPECS/PHOTON_SPEC.md E1's bar; promoted from the PHOTON-S2 spike harness,
// spikes/photon-s2/FINDINGS.md §3, which measured the numbers this pins).
//
// A sealed 10 m room built four times at four wall thicknesses, with one lamp
// INSIDE it (pure green, so the red channel is 100 % the other lamp) and one
// OUTSIDE it (red, a metre off the -Z wall). Shadows are on, and with them the
// direct term through the wall is exactly zero at every thickness — so every
// red pixel inside the room arrived through global illumination, and that
// number IS the leak.
//
// WHY IT IS A GATE SUITE NOW. The irradiance field's reason for existing in
// this engine is that its Chebyshev visibility test stops that leak: the spike
// measured 0.973 without the field and 0.041 with it at a 0.5 m wall — the
// field removes 96 % of it. E1 moves the field onto the innermost cascade of a
// camera-centred chain: a different volume, re-placed as the camera walks. The
// one thing that must survive that move is exactly this, so the suite runs the
// chain over the rooms with the camera standing close enough to the measured
// wall for it to be inside cascade 0's box. (A scene-fitted single-volume arm
// ran beside it as a reference until D4-PHOTON-TIERS deleted that arm; the
// absolute bars below are the spike's measured numbers.)
//
// THE CARDS' ARM (PHOTON-7 stage 5, 5-3 - restored on carded walls): the room at 0.2 m, its slabs CARDED, the rays
// and the gather on (the gather's hits make the walls resident). On the +Z wall's inner face - it FACES the outside
// lamp, 11.3 m off through the -Z wall, inside its 12 m range - THE OUTSIDE LAMP'S DIRECT IS EXACTLY ZERO: at every
// point (Scene::readCardAt) the red lamp is in the card's lamp table and its traced bit is 0, so the relight
// multiplies its light by 0 (the inside lamp's bit 1: it lights the face); one weighted byte for every lamp let the red
// pass at 98 % (measured: 2.03 % of the green). THE LOBE TOO: a glossy carded panel before that wall, asked through
// the ray job's own card read (Engine::cardReadParity, head-on: the red lamp's mirror direction, where its highlight
// would peak), gains no red-lamp highlight - the read's lobe takes each lamp's own bit by its world id (the red's
// change of the read minus its change of the stored radiance under one code of the read's green). Printed beside them:
// the card's whole leak against the VOXEL STORE's at the same points (the surface voxel's light over its coverage,
// Scene::giVoxelLight) - what is left is the bounce, and the cones' own leak through a thin wall at coarse levels
// (VOXEL-LEAK-1, out of stage 5), which the -Z wall's inner face (the lamp behind it) shows whole.
//
// Its own binary like every GI suite.
#include "jahshaka/engine/Engine.h"
#include "jahshaka/engine/EngineInspect.h"   // the engine\'s record of what the fixture pushed (U3 stage 2)
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <tuple>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)

// THE FRAMES THE ROW RENDERS (every frame goes through here): printed at the end, the row's FRAMES (source.testing_rules R7)
static int gFrames = 0;
static void render(Engine *e, int frames = 8) { enginetest::frames(e, frames); gFrames += frames; }

// THE ROOM ITSELF LIVES IN tests/support/enginetesthelpers.h SINCE GATHER-0
// (its fix round, B1): gi.gather measures the same room with a third arm
// and the two suites compare their numbers against each other, which is only
// worth anything while the rooms are the same room. `enginetest::leakroom`
// carries the shell, the two lamps, the pose and the measurement block.

/// ONE FLAT QUAD WITH A CHOSEN NORMAL — the smallest thing that can be put in a
/// voxel facing a direction of our choosing.
static MeshData tiltedQuadMesh(float nx, float ny, float nz, float size)
{
    const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
    const float w[3] = { nx / len, ny / len, nz / len };
    const float up[3] = { std::fabs(w[1]) < 0.9f ? 0.0f : 1.0f,
                          std::fabs(w[1]) < 0.9f ? 1.0f : 0.0f, 0.0f };
    float u[3] = { up[1] * w[2] - up[2] * w[1], up[2] * w[0] - up[0] * w[2],
                   up[0] * w[1] - up[1] * w[0] };
    const float ul = std::sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
    u[0] /= ul; u[1] /= ul; u[2] /= ul;
    // v = w x u, so u x v = w and the winding below is counter-clockwise from +w.
    const float v[3] = { w[1] * u[2] - w[2] * u[1], w[2] * u[0] - w[0] * u[2],
                         w[0] * u[1] - w[1] * u[0] };
    const float h = size * 0.5f;
    const float sgn[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
    MeshData d;
    for (int i = 0; i < 4; ++i) {
        for (int c = 0; c < 3; ++c)
            d.positions.push_back((u[c] * sgn[i][0] + v[c] * sgn[i][1]) * h);
        d.normals.insert(d.normals.end(), { w[0], w[1], w[2] });
    }
    d.indices.insert(d.indices.end(), { 0u, 1u, 2u, 0u, 2u, 3u });
    return d;
}

static NodeId addMesh(Scene *s, const MeshData &md, const Colour &albedo, const Vec3 &pos,
                      const Vec3 &scale)
{
    const NodeId node = s->createNode();
    const MeshId mesh = s->createMesh(md);
    PbrParams p;
    p.albedo = albedo;
    p.metalness = 0.0f;
    p.roughness = 0.9f;
    const MaterialId mat = s->createPbrMaterial(p);
    if (!node || !mesh || !mat || !s->attachMesh(node, mesh, mat)) return 0;
    s->setNodeTransform(node, pos, Quat(), scale);
    return node;
}

/// Mean red and green of the centred block, so one texel of noise cannot move
/// a number — `enginetest::leakroom::meanRG`, which is the SAME block (it was
/// 40..88 of 128 here and is 5/16..11/16 there, which is those pixels exactly).
using enginetest::leakroom::meanRG;

int main()
{
    const float thicknesses[4] = { 0.5f, 0.2f, 0.1f, 0.05f };
    // THE BARS, DERIVED FROM THE STORE (PHOTON-VOXEL-4, SUITES-REANCHOR-1): an opaque wall leaks
    // nothing in the physics, so the bar is what the store may still let through - its quantum
    // (the 10-bit coverage over ~3 crossings: 0.003 of the escape) plus the oblique spread (a
    // ray crossing a wall's texel at a slant reads its coverage through the kernel's tent: at
    // worst 0.17 of the wall's own share of the probe's sky, <= 0.1 here) = 0.02 at EVERY
    // thickness. The split store counts a surface on the half it faces and by where it lies,
    // so the 0.05 m wall - sub-voxel at every cascade - is held as a surface, not a 6 %
    // opacity: measured 0.000 / 0.000 / 0.000 / 0.010 (chain) where the leaky store read
    // 0.053 / 0.055 / 0.096 / 0.262 against bars of 0.068 / 0.071 / 0.100 / 0.325 fitted to
    // it. The comparative bar below still carries the cascade claim: the field on a cascade
    // leaks no more than the field on the scene-fitted box.
    const float bars[4] = { 0.02f, 0.02f, 0.02f, 0.02f };

    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-gi-leak-room-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->submitFrame(jahshaka::engine::FrameInput::delta(1.0f / 60.0f));
    Engine *e = engine.get();

    View *view = e->createOffscreenView("leak", 128, 128, Colour(0, 0, 0), jahshaka::engine::OffscreenContract::Live);
    // Offscreen views ship with shadows OFF; without them the outside lamp
    // lights the inside straight through the wall and there is no GI
    // measurement left to make (the spike established this the hard way).
    view->setShadows(true);

    struct Row { float leakChain = 0.0f, greenChain = 0.0f;
                 bool fieldChain = false;
                 float fieldSpan = 0.0f; };
    Row rows[4];

    for (int a = 0; a < 4; ++a) {
        const float T = thicknesses[a];
        Scene *s = e->createScene("leak" + std::to_string(a));
        view->setScene(s);
        // The shell, the two lamps and the pose — one shared fixture, so
        // gi.gather's third arm measures THIS room and not a copy of it.
        enginetest::leakroom::Room room = enginetest::leakroom::build(s, view, T);

        // ONE measurement path, run once per arm: set the arm, light the
        // outside lamp, read; darken it, read again. The difference in the red
        // channel is the leak, and the green channel beside it says the room is
        // lit at all (a black room leaks nothing and proves nothing).
        const auto leakOf = [&](const char *what) {
            GiParams gi;
            gi.mode = GiMode::Vct;
            gi.quality = GiQuality::High;
            gi.ddgi = GiToggle::On;
            gi.updateBudget = 1;
            gi.bounces = true;
            if (!s->setGlobalIllumination(gi))
                std::printf("   engine error: %s\n", e->lastError().c_str());
            enginetest::leakroom::setOutsideIntensity(s, room, 25.0f);
            s->refreshGlobalIllumination();
            render(e, 10);
            Image img; view->readPixels(img);
            float onR = 0.0f, onG = 0.0f; meanRG(img, onR, onG);
            enginetest::leakroom::setOutsideIntensity(s, room, 0.0f);
            s->refreshGlobalIllumination();
            render(e, 10);
            view->readPixels(img);
            float offR = 0.0f, offG = 0.0f; meanRG(img, offR, offG);
            const GiStatus st = s->published().gi;
            std::printf("   %-6s wall %.2f m: LEAK %.4f (lamp on r %.4f, off r %.4f), green %.4f, "
                        "field %s over %.1f m, cascades %zu\n",
                        what, double(T), double(onR - offR), double(onR), double(offR),
                        double(onG), st.ifdBound ? "bound" : "ABSENT",
                        double(st.ifdMax.x - st.ifdMin.x), st.cascades.size());
            return std::make_tuple(onR - offR, onG, st.ifdBound);
        };

        const auto chain = leakOf("chain");
        const GiStatus stChain = s->published().gi;
        // The arm must carry a bound field, or the bar below grades the cone
        // leak (~0.97) and would fail for the wrong reason.
        rows[a].leakChain = std::get<0>(chain);
        rows[a].greenChain = std::get<1>(chain);
        rows[a].fieldChain = std::get<2>(chain) && stChain.ifdBound;
        rows[a].fieldSpan = stChain.ifdMax.x - stChain.ifdMin.x;
        CHECK(stChain.ifdBound && !stChain.cascades.empty(),
              "the chain arm really has a chain AND a field bound");
        e->destroyScene(s);
    }

    std::printf("\n wall(m)   leak CHAIN   bar      green(chain)\n");
    for (int a = 0; a < 4; ++a)
        std::printf("  %5.2f    %10.4f   %6.4f   %10.4f\n", double(thicknesses[a]),
                    double(rows[a].leakChain), double(bars[a]), double(rows[a].greenChain));

    // THE BARS, asserted on the chain (E1's claim).
    for (int a = 0; a < 4; ++a) {
        char msg[192];
        std::snprintf(msg, sizeof(msg),
                      "the field on cascade 0 holds the %.2f m wall's leak at or under %.4f",
                      double(thicknesses[a]), double(bars[a]));
        CHECK(rows[a].leakChain <= bars[a], msg);
    }
    CHECK(rows[0].greenChain > 0.02f,
          "the room is lit by its own lamp (the leak is measured on a lit wall, not a black one)");
    // =====================================================================
    // THE FOLD'S SEAM — a curved SINGLE-SIDED surface is one surface
    // (fork ad452604a+155a56bf8+0338ca7f2+c4c80b5f7 (was 0065); the review's F1)
    // =====================================================================
    //
    // The voxelisation decides "this voxel holds surfaces facing opposite ways"
    // from two sums: the raw one and a FOLDED one, folded into a half-space by
    // the sign of the normal's largest component. The fold's seam runs along the
    // six arcs where that largest component changes, and a smooth surface
    // crosses those arcs everywhere — so "the two groups are both non-empty"
    // calls a sphere's voxels two-sided, the injection takes abs(NdotL), and the
    // sphere's FAR side is lit by a lamp that is behind it. That is the leak
    // class, and no other suite sees it: this file's rooms and
    // gi.cascade_determinism's plates are all axis-aligned, where the seam is
    // never crossed.
    //
    // THE INSTRUMENT is the far side of the subject itself. One lamp, nothing
    // else in the scene, ambient black, shadows on: the DIRECT term on the far
    // side is zero whatever the voxels say (the pixel shader uses the mesh
    // normal), so every photon the camera sees there came out of the voxel
    // volume. A sphere's far side must stay dark; a plate thinner than a voxel
    // IS two-sided and its far side must NOT — that is the pin's behaviour and
    // the half of the test that stops the fix from being "never flag anything".
    std::printf("\n== the fold's seam: a curved single-sided surface is ONE surface ==\n");
    {
        // A FIXED GRADE, or this measures the auto-exposure instead of the light:
        // the scene is one lamp over a black void, so the 1x1 luminance history
        // opens all the way up on the dark side and reads a black frame as white
        // (measured: the sphere's unlit side came back at 1.0000 and its LIT side
        // at 0.2609 before this was pinned). tonemapFixed is the same filmic
        // curve with the metering taken out.
        PostFxDesc fx = jahshaka::engine::inspect::view(view)->postFx();
        fx.allowOffscreen = true;
        fx.hdr = true;
        fx.tonemapFixed = true;
        fx.exposure = 0.0f;
        view->setPostFx(fx);

        // THE TWO SUBJECTS ARE THE SAME TWO QUADS, 2 mm APART — far less than a
        // voxel, so both live in the same voxels — and they differ in ONE thing:
        // the second quad's normal.
        //
        //   SEAM: normals (0.72, 0, -0.69) and (0.69, 0, -0.72). The first has
        //     its largest component in x and is kept; the second has it in z,
        //     negative, and is FOLDED. Two groups, six and a half degrees apart:
        //     one surface, which the pin's 120-degree rule never flagged.
        //   TWO-SIDED: normals (0.707, 0, -0.707) and its exact opposite. Two
        //     groups, 180 degrees apart: two surfaces, which the pin DID flag,
        //     and which must still be flagged or "fix the seam" could be
        //     satisfied by never flagging anything.
        //
        // THE INSTRUMENT is the camera on the side both subjects' first quad
        // faces, with the lamp BEHIND. The direct term there is zero for both
        // (the pixel shader uses the mesh normal), so the difference between the
        // bounce on and the bounce off is light that was injected into a voxel
        // facing away from the lamp — which is the flag, and nothing else.
        const float k = 0.70710678f;
        const Vec3 frontEye(k * 3.4f, 0.0f, -k * 3.4f);
        const Vec3 behind(-k * 3.0f, 0.0f, k * 3.0f), inFront(k * 3.0f, 0.0f, -k * 3.0f);
        const auto measure = [&](bool seam, float &frontGi, float &frontLit, float &storeFront, float &storeBack) {
            Scene *s = e->createScene(seam ? "seam-pair" : "twosided-pair");
            view->setScene(s);
            s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
            const float d = 0.002f;
            addMesh(s, tiltedQuadMesh(0.72180f, 0.0f, -0.69207f, 3.0f), Colour(0.9f, 0.9f, 0.9f),
                    Vec3(k * d, 0.0f, -k * d), Vec3(1, 1, 1));
            if (seam)
                addMesh(s, tiltedQuadMesh(0.69207f, 0.0f, -0.72180f, 3.0f),
                        Colour(0.9f, 0.9f, 0.9f), Vec3(-k * d, 0.0f, k * d), Vec3(1, 1, 1));
            else
                addMesh(s, tiltedQuadMesh(-k, 0.0f, k, 3.0f), Colour(0.9f, 0.9f, 0.9f),
                        Vec3(-k * d, 0.0f, k * d), Vec3(1, 1, 1));

            const NodeId lamp = s->createNode();
            LightDesc ld;
            ld.type = LightType::Point;
            ld.colour = Colour(1, 1, 1);
            ld.intensity = 40.0f;
            ld.range = 20.0f;
            ld.castShadows = true;
            const auto placeLamp = [&](const Vec3 &at) {
                s->setNodeTransform(lamp, at, Quat(), Vec3(1, 1, 1));
                s->setLight(lamp, ld);
            };
            const auto readFront = [&]() {
                enginetest::testCameraLookAt(view, frontEye, Vec3(0, 0, 0));
                render(e, 10);
                Image img; view->readPixels(img);
                float r = 0.0f, g = 0.0f; meanRG(img, r, g);
                return g;
            };
            const auto setGi = [&](bool on) {
                GiParams gi;
                gi.mode = on ? GiMode::Vct : GiMode::Off;
                gi.quality = GiQuality::High;
                gi.ddgi = GiToggle::Off;     // the cone bounce, read directly
                gi.updateBudget = 0;
                gi.bounces = true;
                if (!s->setGlobalIllumination(gi))
                    std::printf("   engine error: %s\n", e->lastError().c_str());
                render(e, 6);
            };

            placeLamp(behind);
            setGi(false);
            const float off = readFront();
            setGi(true);
            const float on = readFront();
            frontGi = on - off;
            // THE STORE, READ FROM THE FRONT: the plate's voxel along a ray arriving from the
            // camera's side (Engine::voxelReaderParity's point read, the ray hit's read) - the
            // light the voxel sends TOWARDS the front, which is the flag's whole effect.
            storeFront = 0.0f;
            storeBack = 0.0f;
            {
                GiVoxelVolume v;
                if (s->giVoxelVolume(0, v) && v.available) {
                    const double size[3] = { double(v.cell[0]) * v.width, double(v.cell[1]) * v.height,
                                             double(v.cell[2]) * v.depth };
                    const double dw[3] = { -k, 0.0, k };   // from the front eye towards the plate
                    double dl[3], l = 0.0;
                    for (int a = 0; a < 3; ++a) { dl[a] = dw[a] / size[a]; l += dl[a] * dl[a]; }
                    l = std::sqrt(l);
                    VoxelReaderCone c;
                    c.posLS = Vec3(float((0.0 - v.origin[0]) / size[0]), float((0.0 - v.origin[1]) / size[1]),
                                   float((0.0 - v.origin[2]) / size[2]));
                    c.dirLS = Vec3(float(dl[0] / l), float(dl[1] / l), float(dl[2] / l));
                    c.biasDirLS = Vec3(0, 0, 0);
                    c.tanHalfAngle = 0.0f;
                    c.lod = 0.0f;
                    // ...and the same voxel read from BEHIND (the lamp's side): the travel reversed
                    VoxelReaderCone cb = c;
                    cb.dirLS = Vec3(-c.dirLS.x, -c.dirLS.y, -c.dirLS.z);
                    std::vector<VoxelReaderAnswer> fr, co;
                    if (e->voxelReaderParity(s, { c, cb }, fr, co) && co.size() == 2u) {
                        if (co[0].hitRead[3] > 0.0f) storeFront = co[0].hitRead[1] / co[0].hitRead[3];
                        if (co[1].hitRead[3] > 0.0f) storeBack = co[1].hitRead[1] / co[1].hitRead[3];
                    }
                }
            }
            // ...and the same camera with the lamp IN FRONT, so a subject that
            // simply is not there cannot pass by reading zero twice.
            placeLamp(inFront);
            setGi(false);
            frontLit = readFront();
            std::printf("   %-9s lamp behind: GI off %.4f, GI on %.4f -> bounce %.4f | "
                        "lamp in front: %.4f\n", seam ? "seam" : "two-sided", double(off),
                        double(on), double(frontGi), double(frontLit));
            e->destroyScene(s);
        };

        float seamFront = 0.0f, seamLit = 0.0f, twoFront = 0.0f, twoLit = 0.0f;
        float seamStore = 0.0f, twoStore = 0.0f, seamBack = 0.0f, twoBack = 0.0f;
        measure(true, seamFront, seamLit, seamStore, seamBack);
        measure(false, twoFront, twoLit, twoStore, twoBack);
        std::printf("   the store, lamp behind: read from the front seam %.4f, two-sided %.4f; from behind seam %.4f, "
                    "two-sided %.4f\n", double(seamStore), double(twoStore), double(seamBack), double(twoBack));
        CHECK(seamLit > 0.05f && twoLit > 0.05f,
              "both subjects are there and face the camera (the lamp in front lights them)");
        char msg[256];
        std::snprintf(msg, sizeof(msg),
                      "A SINGLE-SIDED SURFACE THAT CROSSES THE FOLD'S SEAM IS NOT LIT FROM "
                      "BEHIND: the seam pair's unlit side gains %.4f from the bounce against "
                      "the genuinely two-sided pair's %.4f", double(seamFront), double(twoFront));
        CHECK(seamFront < twoFront * 0.25f + 0.004f, msg);
        // THE OTHER HALF, READ IN THE STORE (PHOTON-VOXEL-3 round 9; PHOTON-7 stages 7 and 10): the flag's
        // effect is a voxel that keeps TWO SIDES - the side facing the lamp lit, the side facing the camera its own.
        // The store holds the two (stage 7) and a read takes the side its travel meets, so the instrument is the
        // voxel read FROM BEHIND: the two-sided pair's back holds the lamp's light (the flag: its back quad is a
        // surface of its own), the seam pair's - one surface, its voxel one-sided, its back the inside of a solid
        // (stage 10's one-sided write) - none. READ FROM THE FRONT both are dark: an opaque plate lit from behind
        // sends no light forward (L9). (This law read "the two-sided pair's voxel, read from the front, holds
        // light": that light was the bounce's mean of a voxel's two sides - the back's lamp light gathered into the
        // front by its neighbours - which stage 10 deleted as a leak carrier.)
        std::snprintf(msg, sizeof(msg),
                      "...and a genuinely two-sided pair still IS, which is the half that stops the fix being "
                      "\"never flag anything\": its voxel, read from behind, holds the lamp's light (%.4f) and the "
                      "seam pair's does not (%.4f); read from the front, neither does (%.4f, %.4f)",
                      double(twoBack), double(seamBack), double(twoStore), double(seamStore));
        CHECK(twoBack > 0.02f && seamBack < twoBack * 0.25f + 0.004f && twoStore < 0.004f && seamStore < 0.004f, msg);
    }

    view->setScene(nullptr);
    e->destroyView(view);
    // ---- THE CARDS' ARM ----------------------------------------------------------------------------------------------
    std::printf("\n== the cards: the leak on a carded wall, card against the store ==\n");
    {
        e->setRayTracing(true);
        View *cv = e->createOffscreenView("leak_cards", 256, 256, Colour(0, 0, 0), OffscreenContract::StillPicture);
        Scene *s = e->createScene("leak_cards");
        cv->setScene(s);
        cv->setShadows(true);
        {
            PostFxDesc fx;   // the chain, so the gather runs (its hits read the walls' cards)
            fx.allowOffscreen = true;
            fx.ssr = 1;
            cv->setPostFx(fx);
        }
        // A STILL PICTURE GATHERS (View::setOffscreenContract; PHOTON-7 7-5: the gather is recorded only for a view whose
        // chain gathers, and a Live view takes the field's answer)
        const float T = 0.2f;
        enginetest::leakroom::Room room = enginetest::leakroom::build(s, cv, T, 0.0f, true);
        // THE GLOSSY PANEL before the +Z wall (its card's lobe is the lobe arm's subject)
        NodeId panel = 0;
        {
            panel = s->createNode();
            MeshData md = enginetest::unitCubeMesh();
            md.cards = enginetest::boxCards(0.5f);
            PbrParams pp;
            pp.albedo = Colour(0.5f, 0.5f, 0.5f);
            pp.roughness = 0.15f;
            const MeshId mesh = s->createMesh(md);
            const MaterialId mat = s->createPbrMaterial(pp);
            if (panel && mesh && mat) s->attachMesh(panel, mesh, mat);
            s->setNodeTransform(panel, Vec3(0.0f, 2.0f, 4.85f), Quat(), Vec3(2.0f, 2.0f, 0.1f));
        }
        const unsigned redId = unsigned(room.outside & 0x7FFFFFFFull), greenId = unsigned(room.inside & 0x7FFFFFFFull);
        GiParams gi;
        gi.mode = GiMode::Vct;
        gi.quality = GiQuality::High;
        gi.ddgi = GiToggle::Off;
        gi.cards = true;
        gi.bounces = true;
        gi.updateBudget = 0;
        const auto settle = [&]() {
            unsigned long long relights = ~0ull, admitted = ~0ull;
            int quiet = 0, f = 0;
            for (; f < 1500 && quiet < 60; ++f) {
                render(e, 1);
                const GiStatus g = s->published().gi;
                const bool still = !g.chainOwed && !g.feedbackOwed && g.cards.built && g.cards.relights == relights &&
                                   g.cards.admitted == admitted && g.cards.queueLength == 0u;
                relights = g.cards.relights;
                admitted = g.cards.admitted;
                quiet = still ? quiet + 1 : 0;
            }
            return quiet >= 60 ? f - quiet : -1;
        };
        // a face's points (its inner plane z = face, facing `nz`), and their surface voxel in cascade 0
        struct Reading { double cardR = 0.0, cardG = 0.0, directR = 0.0, voxR = 0.0, voxG = 0.0; size_t cards = 0, voxels = 0;
                         size_t redTabled = 0, redLit = 0, greenLit = 0, greenSeen = 0; };
        // does the segment a-b cross the panel's box? (slabs) - the inside lamp's shadow of the panel on the wall
        const auto behindPanel = [](const Vec3 &a, const Vec3 &b) {
            const double lo[3] = { -1.0, 1.0, 4.8 }, hi[3] = { 1.0, 3.0, 4.9 };
            const double A[3] = { a.x, a.y, a.z }, B[3] = { b.x, b.y, b.z };
            double t0 = 0.0, t1 = 1.0;
            for (int k = 0; k < 3; ++k) {
                const double d = B[k] - A[k];
                if (std::fabs(d) < 1e-12) { if (A[k] < lo[k] || A[k] > hi[k]) return false; continue; }
                double u = (lo[k] - A[k]) / d, v = (hi[k] - A[k]) / d;
                if (u > v) std::swap(u, v);
                t0 = std::max(t0, u);
                t1 = std::min(t1, v);
                if (t0 > t1) return false;
            }
            return true;
        };
        const Vec3 greenAt(0.0f, 3.0f, 2.5f);   // the leak room's inside lamp
        size_t pointCount = 0;
        const auto readAll = [&](float face, float nz) {
            std::vector<Vec3> pts;
            for (int i = -4; i <= 4; ++i)
                for (int j = 0; j < 4; ++j) pts.push_back(Vec3(0.6f * float(i), 1.1f + 0.6f * float(j), face));
            pointCount = pts.size();
            Reading r;
            for (const Vec3 &p : pts) {
                CardSample c;
                if (s->readCardAt(p, Vec3(0, 0, nz), c) && c.ok && c.lit) {
                    const bool shaded = behindPanel(p, greenAt);   // the panel's own shadow of the inside lamp
                    for (unsigned k = 0; k < c.lampCount && k < 48u; ++k) {   // the two lamps' bits, by their world ids
                        if (c.lampIds[k] == redId) { ++r.redTabled; r.redLit += unsigned((c.lampMask >> k) & 1ull); }
                        if (c.lampIds[k] == greenId && !shaded) {
                            ++r.greenSeen;
                            r.greenLit += unsigned((c.lampMask >> k) & 1ull);
                        }
                    }
                    r.directR += c.radiance[0] - c.indirect[0] - c.flat[0];   // the direct half (the lamps')
                    r.cardR += c.radiance[0];
                    r.cardG += c.radiance[1];
                    ++r.cards;
                }
            }
            // the finest cascade whose box holds the face's points (cascade 0 rides the camera: the far wall is cascade 1's)
            GiVoxelVolume v;
            bool have = false;
            for (int k = 0; k < 4 && !have; ++k) {
                GiVoxelVolume t;
                if (!s->giVoxelLight(k, t) || !t.available) continue;
                const float zLo = t.origin[2], zHi = t.origin[2] + t.cell[2] * float(t.depth);
                if (face - 2.0f * t.cell[2] > zLo && face + 2.0f * t.cell[2] < zHi) { v = std::move(t); have = true; }
            }
            if (!have) std::printf("   (no cascade's store holds the face)\n");
            if (have) {
                for (const Vec3 &p : pts) {
                    // THE SURFACE VOXEL: of the texels within two cells of the face, along its normal, the most covered
                    const float cell = v.cell[0];
                    const int x = int(std::floor((p.x - v.origin[0]) / cell));
                    const int y = int(std::floor((p.y - v.origin[1]) / cell));
                    size_t best = size_t(-1);
                    float bestCov = 0.5f;
                    for (int dz = -2; dz <= 2; ++dz) {
                        if (float(dz) * nz > 0.0f) continue;   // into the wall, behind its inner face
                        const int z = int(std::floor((p.z - v.origin[2]) / cell)) + dz;
                        if (x < 0 || y < 0 || z < 0 || x >= v.width || y >= v.height || z >= v.depth) continue;
                        const size_t i = ((size_t(z) * v.height + y) * v.width + x) * 4u;
                        if (v.light[i + 3] > bestCov) { bestCov = v.light[i + 3]; best = i; }
                    }
                    if (best == size_t(-1)) continue;
                    r.voxR += v.light[best] / bestCov;
                    r.voxG += v.light[best + 1] / bestCov;
                    ++r.voxels;
                }
            }
            return r;
        };
        s->setGlobalIllumination(gi);
        e->waitForAsyncShaders();
        // THE LOBE ARM's queries: the panel's face toward the room, head-on (the read's viewer along its facing)
        std::vector<CardReadQuery> lobeQ;
        for (int i = -2; i <= 2; ++i)
            for (int j = -2; j <= 2; ++j) {
                CardReadQuery q;
                q.position = Vec3(0.3f * float(i), 2.0f + 0.3f * float(j), 4.8f);
                q.facing = Vec3(0.0f, 0.0f, -1.0f);
                q.node = panel;
                lobeQ.push_back(q);
            }
        const auto lobeRead = [&](std::vector<CardReadPick> &out) { return e->cardReadParity(s, lobeQ, out); };
        enginetest::leakroom::setOutsideIntensity(s, room, 25.0f);
        const int restOn = settle();
        const Reading onFar = readAll(5.0f, -1.0f), onNear = readAll(-5.0f, 1.0f);
        std::vector<CardReadPick> lobeOn, lobeOff;
        const bool lobeOkOn = lobeRead(lobeOn);
        enginetest::leakroom::setOutsideIntensity(s, room, 0.0f);
        const int restOff = settle();
        const Reading offFar = readAll(5.0f, -1.0f), offNear = readAll(-5.0f, 1.0f);
        const bool lobeOkOff = lobeRead(lobeOff);
        const auto leak = [](const Reading &on, const Reading &off, bool card) {
            const double g = card ? on.cardG : on.voxG;
            if (!(g > 0.0) || !(card ? on.cards : on.voxels)) return -1.0;
            return card ? (on.cardR - off.cardR) / g : (on.voxR - off.voxR) / g;
        };
        const double cardFar = leak(onFar, offFar, true), voxFar = leak(onFar, offFar, false);
        const double directFar = onFar.cards && onFar.cardG > 0.0 ? (onFar.directR - offFar.directR) / onFar.cardG : -1.0;
        std::printf("   rest %d / %d frames | THE +Z FACE (facing the outside lamp through the -Z wall): %zu of %zu points "
                    "carded, %zu with a surface voxel - the leak (the outside red over the inside green): card %.4f, "
                    "store %.4f, the card's DIRECT half's %.4f | the -Z face (the lamp behind it: the cones' leak, "
                    "VOXEL-LEAK-1): card %.4f, store %.4f\n",
                    restOn, restOff, onFar.cards, pointCount, onFar.voxels, cardFar, voxFar, directFar,
                    leak(onNear, offNear, true), leak(onNear, offNear, false));
        {
            const GiStatus gd = s->published().gi;
            // THE CARD-TO-CARD RE-LISTING (10-2c, the lead's ruling): the read lists' overflow share and the geometric
            // bound's re-listings, printed
            const CardCacheStatus::GatherCounts &gc = gd.cards.gather;
            std::printf("   the cards: %llu relights, %llu of the indirect (%llu reported: %llu card to card, %llu with the "
                        "geometric bound; %llu by region, %llu overflows); the TLAS gather %llu dispatches, %llu texels, "
                        "%llu over their read list (%.2f %%)\n",
                        gd.cards.relights, gd.cards.indirectRelights, gd.cards.indirectReported, gd.cards.indirectByCards,
                        gd.cards.indirectByBound, gd.cards.indirectByRegion, gd.cards.indirectOverflows, gc.dispatches,
                        gc.texels, gc.readsOverflowed,
                        gc.texels ? 100.0 * double(gc.readsOverflowed) / double(gc.texels) : 0.0);
        }
        CHECK(restOn >= 0 && restOff >= 0 && onFar.cards == pointCount && offFar.cards == pointCount && onFar.voxels > 0,
              "the carded room comes to rest and its +Z wall reads in the cards and the store");
        std::printf("   the +Z face's lamp tables: the red lamp tabled at %zu of %zu points, its bit set at %zu; the green "
                    "lamp's bit set at %zu of the %zu points the panel does not shade from it\n", onFar.redTabled,
                    pointCount, onFar.redLit, onFar.greenLit, onFar.greenSeen);
        CHECK(onFar.redTabled == pointCount && onFar.redLit == 0u && onFar.greenSeen > 0u &&
                  onFar.greenLit == onFar.greenSeen,
              "THE OUTSIDE LAMP'S DIRECT IS EXACTLY ZERO on the face that faces it through the wall: its traced bit 0 "
              "at every point (the inside lamp's 1)");
        // THE LOBE ARM
        size_t lobeN = 0, lobeOver = 0;
        double lobeWorst = 0.0;
        if (lobeOkOn && lobeOkOff && lobeOn.size() == lobeQ.size() && lobeOff.size() == lobeQ.size())
            for (size_t i = 0; i < lobeQ.size(); ++i) {
                if (!lobeOn[i].ok || !lobeOff[i].ok) continue;
                ++lobeN;
                const double highlight = (double(lobeOn[i].viewed[0]) - double(lobeOff[i].viewed[0])) -
                                         (double(lobeOn[i].radiance[0]) - double(lobeOff[i].radiance[0]));
                const double codes = std::fabs(highlight) / std::max(0.01 * double(lobeOn[i].viewed[1]), 1e-9);
                lobeWorst = std::max(lobeWorst, codes);
                lobeOver += codes > 1.0 ? 1u : 0u;
            }
        std::printf("   the lobe arm (the glossy panel, head-on): %zu of %zu card reads answered; the red lamp's highlight "
                    "(its change of the read less its change of the stored radiance) worst %.3f codes of the read's green, "
                    "%zu over one\n", lobeN, lobeQ.size(), lobeWorst, lobeOver);
        CHECK(lobeN == lobeQ.size() && lobeOver == 0u,
              "THE LOBE TOO: no red-lamp highlight at the card hits on the panel before the shadowed wall");
        GiParams goff;
        goff.mode = GiMode::Off;
        s->setGlobalIllumination(goff);
        render(e, 2);
        e->destroyScene(s);
        e->destroyView(cv);
        render(e, 2);
    }

    std::printf("\n%d frames rendered\n", gFrames);
    std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
