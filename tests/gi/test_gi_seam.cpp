// gi.directional_seam (PHOTON-7 stage 1, AB A11; PREFLIGHT_1_2 W8; stage 1's read F2) — THE PACKED HALVES DO NOT BLEED
// INTO EACH OTHER, AT ANY LEVEL.
//
// A cascade's directional volumes pack both signs of an axis side by side along x: the faces a reader travelling +b
// meets in [0, 1/2) of u, the other sign's in [1/2, 1) (PhotonVoxelLighting.h). The read folds x into a half and
// CLAMPS IT PER HALF (jah_voxel_sample.glsl jahVoxelReadAxis), so a bilinear footprint never straddles the seam into
// the OTHER SIGN's texel at the opposite end of the box - at EACH LEVEL a trilinear read blends its own half texel
// (stage 1's read F2: one clamp at the coarser level's pinned the finer level up to half a coarse texel inside the
// face, and past the chain's last level the clamp's min passed its max: undefined).
//
// THE LAWS, over a uniformly sun-lit floor wider than every cascade, at every cascade of the High chain:
//   (1) THE HIT READ of the floor's lit face (travelling -y: the faces looking +y, the second half) a quarter texel
//       from the box's -x face and from its +x face equals the read at the box's middle (red on stage 1's base at the
//       -x face of every cascade);
//   (2) THE +X FACE OF THE FIRST HALF (travelling +y: the floor's unlit underside, whose +x end borders the second
//       half's lit -x end) equals the first half's middle at levels 0, 0.5, the chain's last and one past it - a
//       bleed there CAN fail it;
//   (3) PAST THE LAST LEVEL: the second half's read at the chain's last level + 1 equals the read at the last level
//       (the sampler clamps the level there; so must the clamp - red on stage 1's tip: the seam's two texels mixed);
// and, with a dark-roofed strip shading cascade 0's first two cells at its -x face (the level-0 texels differ there):
//   (4) A FRACTIONAL LEVEL BLENDS ITS TWO LEVELS' OWN READS: the read at level 0.5 a quarter texel from the -x face
//       equals the mean of the reads at levels 0 and 1 there (red on stage 1's tip: the finer level clamped to the
//       coarser half texel took the next texel half and half).
// THE TOLERANCES ARE THE QUANTITIES' OWN (R3): (1)-(3) compare reads of one fp16 store, so a texel's quantum, 2^-10
// of the lit value; (4) compares a float blend of the same two fetches, eight float ulps (2^-20) of the larger read.
// The reads are the reader's own through the parity door (Engine::voxelReaderParity; VoxelReaderCone::kAxisRead:
// jahVoxelReadAxis at the cone's level).
//
// A fixture (the engine alone, one offscreen view, the High chain), ~5 s.
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

int main()
{
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-gi-seam-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));
    View *view = e->createOffscreenView("seam", 128, 128, Colour(0, 0, 0));
    Scene *scene = e->createScene("seam");
    view->setScene(scene);
    // A floor wider than the outermost cascade (High: 120 m), its top at y = 0.1, and a sun straight down.
    const NodeId floor = enginetest::addTestCube(scene, Colour(0.6f, 0.6f, 0.6f), 0.0f, 1.0f);
    enginetest::setNodePosition(scene, floor, Vec3(0.0f, 0.05f, 0.0f));
    enginetest::setNodeScale(scene, floor, Vec3(400.0f, 0.1f, 400.0f));
    enginetest::addDirectionalLight(scene, Vec3(0.0f, -1.0f, 0.0f), 1.0f);
    enginetest::testCameraLookAt(view, Vec3(0.3f, 2.0f, 0.4f), Vec3(0.3f, 0.0f, -3.0f));
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.ddgi = GiToggle::Off;
    gi.gather = GiToggle::Off;
    gi.updateBudget = 0;
    gi.numBounces = 1;
    CHECK(scene->setGlobalIllumination(gi), "the chain is up");
    enginetest::frames(e, 24);
    const auto axisRead = [&](int c, float x, float y, float z, bool positive, float level, float out[4]) {
        VoxelReaderCone cone;
        cone.posLS = Vec3(x, y, z);
        cone.dirLS = Vec3(0.0f, positive ? 1.0f : -1.0f, 0.0f);
        cone.biasDirLS = Vec3(0.0f, 0.0f, 0.0f);
        cone.tanHalfAngle = 0.0f;
        cone.cascade = unsigned(c);
        cone.lod = level;
        cone.flags = VoxelReaderCone::kAxisRead;
        std::vector<VoxelReaderAnswer> fr, co;
        if (!e->voxelReaderParity(scene, { cone }, fr, co) || co.empty()) {
            std::printf("  the parity harness refused (%s)\n", e->lastError().c_str());
            for (int k = 0; k < 4; ++k) out[k] = -1.0f;
            return false;
        }
        for (int k = 0; k < 4; ++k) out[k] = co[0].hitRead[k];
        return true;
    };
    const auto maxDiff = [](const float a[4], const float b[4]) {
        float d = 0.0f;
        for (int k = 0; k < 4; ++k) d = std::max(d, std::fabs(a[k] - b[k]));
        return d;
    };
    const float kFp16Quantum = 1.0f / 1024.0f;   // a store texel's relative step
    const float kBlendUlps = 1.0f / 1048576.0f;  // eight float ulps
    bool allOk = true, faceOk = true, lastOk = true;
    int cascades = 0;
    struct Placed { double origin[3], cell; float yLS, quarterX; int last; };
    std::vector<Placed> placed;
    for (int c = 0; c < 4; ++c) {
        GiVoxelVolume v;
        if (!scene->giVoxelVolume(c, v) || !v.available) break;
        ++cascades;
        const double size[3] = { double(v.cell[0]) * v.width, double(v.cell[1]) * v.height,
                                 double(v.cell[2]) * v.depth };
        // the directional level-0 texel is two cells; its plane holding the floor's top (y = 0.1)
        const double texel = 2.0 * double(v.cell[1]);
        const double yTex = (std::floor((0.1 - double(v.origin[1])) / texel) + 0.5) * texel;
        const float yLS = float(yTex / size[1]);
        const float quarterX = float(0.25 * 2.0 * double(v.cell[0]) / size[0]);   // a quarter of a directional texel
        const int shortest = std::min(v.width, std::min(v.height, v.depth));
        const int last = int(std::lround(std::log2(double(shortest)))) - 1;      // the directional chain's last level
        placed.push_back({ { double(v.origin[0]), double(v.origin[1]), double(v.origin[2]) }, double(v.cell[0]), yLS,
                           quarterX, last });
        // (1) THE HIT READ at the faces and the middle
        const float xs[3] = { quarterX, 0.5f, 1.0f - quarterX };
        float rad[3] = {};
        for (int k = 0; k < 3; ++k) {
            VoxelReaderCone cone;
            cone.posLS = Vec3(xs[k], yLS, 0.5f);
            cone.dirLS = Vec3(0.0f, -1.0f, 0.0f);   // travelling -y: the floor's lit +y faces (the second half)
            cone.biasDirLS = Vec3(0.0f, 0.0f, 0.0f);
            cone.tanHalfAngle = 0.0f;
            cone.cascade = unsigned(c);
            cone.lod = 1.0f;                        // a footprint past a cell: the directional volumes, level 0
            std::vector<VoxelReaderAnswer> fr, co;
            if (!e->voxelReaderParity(scene, { cone }, fr, co) || co.empty()) {
                std::printf("  the parity harness refused (%s)\n", e->lastError().c_str());
                allOk = false;
                continue;
            }
            const float a = co[0].hitRead[3];
            rad[k] = a > 0.0f ? co[0].hitRead[1] / a : 0.0f;
        }
        const float tol = std::max(kFp16Quantum * rad[1], 1e-6f);
        const bool ok = rad[1] > 0.0f && std::fabs(rad[0] - rad[1]) <= tol && std::fabs(rad[2] - rad[1]) <= tol;
        std::printf("   cascade %d (cell %.4f m, last level %d): the floor's hit read at x %.5f / 0.5 / %.5f of the box: "
                    "%.6f / %.6f / %.6f%s\n", c, double(v.cell[0]), last, double(xs[0]), double(xs[2]), double(rad[0]),
                    double(rad[1]), double(rad[2]), ok ? "" : "  <- the seam");
        allOk = allOk && ok;
        // the second half's lit middle: the scale every tolerance below is a quantum of
        float lit[4];
        axisRead(c, 0.5f, yLS, 0.5f, false, 0.0f, lit);
        const float litScale = std::max(lit[0], std::max(lit[1], lit[2]));   // the lit radiance (premultiplied)
        // (2) THE FIRST HALF'S +X FACE at levels 0, 0.5, the last and past it
        const float levels[4] = { 0.0f, 0.5f, float(last), float(last + 1) };
        for (float lv : levels) {
            float face[4], mid[4];
            axisRead(c, 1.0f - quarterX, yLS, 0.5f, true, lv, face);
            axisRead(c, 0.5f, yLS, 0.5f, true, lv, mid);
            const float d = maxDiff(face, mid);
            const bool okF = d <= kFp16Quantum * litScale;
            std::printf("     first half at level %.1f: its +x face %.6f vs its middle %.6f (alpha %.4f / %.4f) - "
                        "|diff| %.2e of the lit %.4f%s\n", double(lv), double(face[1]), double(mid[1]), double(face[3]),
                        double(mid[3]), double(d), double(litScale), okF ? "" : "  <- the seam");
            faceOk = faceOk && okF;
        }
        // (3) PAST THE LAST LEVEL
        {
            float atLast[4], past[4];
            axisRead(c, 0.5f, yLS, 0.5f, false, float(last), atLast);
            axisRead(c, 0.5f, yLS, 0.5f, false, float(last + 1), past);
            const float d = maxDiff(atLast, past);
            const bool okL = d <= kFp16Quantum * litScale && atLast[3] > 0.0f;
            std::printf("     second half at its last level %d: %.6f, one past it %.6f (alpha %.4f / %.4f)%s\n", last,
                        double(atLast[1]), double(past[1]), double(atLast[3]), double(past[3]),
                        okL ? "" : "  <- past the chain's last level");
            lastOk = lastOk && okL;
        }
    }
    CHECK(cascades >= 2, "the chain holds cascades to read");
    CHECK(allOk, "(1) a hit read a quarter texel from either x face of every cascade equals the box's middle (one fp16 "
                 "quantum): the packed halves do not bleed into each other");
    CHECK(faceOk, "(2) the first half's +x face reads its own texels at levels 0, 0.5, the last and past it");
    CHECK(lastOk, "(3) past the chain's last level the read is the last level's (the clamp is defined there)");

    // (4) A FRACTIONAL LEVEL: a dark roof over cascade 0's first two cells at its -x face (the sun straight down shades
    // them; its edge 5 mm inside the cell plane, PREFLIGHT_1_2 W4), so level 0 differs between its first two texels.
    if (!placed.empty()) {
        const Placed &p0 = placed[0];
        const double face = p0.origin[0];
        const double edge = face + 2.0 * p0.cell - 0.005;
        const unsigned long long before = scene->published().gi.cascades[0].rebuilds;
        const NodeId roof = enginetest::addTestCube(scene, Colour(0.05f, 0.05f, 0.05f), 0.0f, 1.0f);
        enginetest::setNodePosition(scene, roof, Vec3(float(0.5 * (face - 1.0 + edge)), 1.05f, 0.4f));
        enginetest::setNodeScale(scene, roof, Vec3(float(edge - (face - 1.0)), 0.1f, 6.0f));
        int n = 0;
        for (; n < 60 && scene->published().gi.cascades[0].rebuilds == before; ++n) enginetest::frames(e, 1);
        enginetest::frames(e, 12);
        float r0[4], r1[4], rh[4], next0[4];
        const float x = p0.quarterX;
        axisRead(0, x, p0.yLS, 0.5f, false, 0.0f, r0);
        axisRead(0, x, p0.yLS, 0.5f, false, 1.0f, r1);
        axisRead(0, x, p0.yLS, 0.5f, false, 0.5f, rh);
        axisRead(0, x + 4.0f * p0.quarterX, p0.yLS, 0.5f, false, 0.0f, next0);   // the next level-0 texel
        float want[4], scale = 0.0f;
        for (int k = 0; k < 4; ++k) {
            want[k] = 0.5f * (r0[k] + r1[k]);
            scale = std::max(scale, std::max(std::fabs(r0[k]), std::fabs(r1[k])));
        }
        const float d = maxDiff(rh, want);
        const float step = maxDiff(r0, next0);
        std::printf("   cascade 0 under the roof strip (re-voxelised after %d frames): level 0 %.6f (the next texel "
                    "%.6f), level 1 %.6f, level 0.5 %.6f against their mean %.6f - |diff| %.2e (bar %.2e)\n", n,
                    double(r0[1]), double(next0[1]), double(r1[1]), double(rh[1]), double(want[1]), double(d),
                    double(kBlendUlps * scale));
        CHECK(n < 60 && step > 100.0f * kBlendUlps * scale,
              "(4) the roof strip made cascade 0's first two level-0 texels differ (the arm can fail)");
        CHECK(d <= kBlendUlps * scale, "(4) a fractional level is the blend of its two levels' own reads at the face");
        scene->removeNode(roof);
    }
    GiParams off;
    off.mode = GiMode::Off;
    scene->setGlobalIllumination(off);
    enginetest::frames(e, 2);
    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
