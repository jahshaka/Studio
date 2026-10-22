// gi.directional_seam (PHOTON-7 stage 1, AB A11; PREFLIGHT_1_2 W8) — THE PACKED HALVES DO NOT BLEED INTO EACH OTHER.
//
// A cascade's directional volumes pack both signs of an axis side by side along x: the faces a reader travelling +b
// meets in [0, 1/2) of u, the other sign's in [1/2, 1) (PhotonVoxelLighting.h). The read folded x into a half by
// clamping it to the box and halving it (jah_voxel_sample.glsl jahVoxelAnisoUvw), so a read within half a texel of
// a half's edge - the box's x faces, where every cone and ray hands over to the next cascade - had its bilinear
// footprint straddle the seam and took part of the OTHER SIGN's texel at the OPPOSITE end of the box. THE FIX is a
// per-half clamp (each half clamped like CLAMP_TO_EDGE on its own texels, at the read's coarser level): no binding
// change, every read off the seam bit-identical.
//
// THE LAW: over a uniformly sun-lit floor (wider than every cascade), the ray hit's directional read of the floor's
// lit face (travelling -y: the faces looking +y, the second half) a quarter texel from the box's -x face equals the
// same read at the box's middle, and so does the read a quarter texel from the +x face; at every cascade of the
// chain. Red on the base at the -x face of every cascade (the second half's first texel read beside the first half's
// last: the +y read mixed with the floor's unlit -y faces at the box's far end).
//
// A fixture (the engine alone, one offscreen view, the High chain), ~5 s.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

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
    bool allOk = true;
    int cascades = 0;
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
        const float tol = std::max(1e-3f * rad[1], 1e-6f);
        const bool ok = rad[1] > 0.0f && std::fabs(rad[0] - rad[1]) <= tol && std::fabs(rad[2] - rad[1]) <= tol;
        std::printf("   cascade %d (cell %.4f m): the floor's directional read at x %.5f / 0.5 / %.5f of the box: "
                    "%.6f / %.6f / %.6f%s\n", c, double(v.cell[0]), double(xs[0]), double(xs[2]), double(rad[0]),
                    double(rad[1]), double(rad[2]), ok ? "" : "  <- the seam");
        allOk = allOk && ok;
    }
    CHECK(cascades >= 2, "the chain holds cascades to read");
    CHECK(allOk, "a read a quarter texel from either x face of every cascade equals the box's middle (within 0.1 %): "
                 "the packed halves do not bleed into each other");
    GiParams off;
    off.mode = GiMode::Off;
    scene->setGlobalIllumination(off);
    enginetest::frames(e, 2);
    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
