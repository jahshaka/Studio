// gi.voxel_two_sided — A TWO-SIDED SHEET COVERS BOTH HALVES OF ITS VOXELS
// (PHOTON-5 stage 2; SPECS/audits/PHOTON_AUDIT_2026-10-21.md F2).
//
// The voxel store splits a voxel's coverage by the side a piece faces, and a
// march meets only the faces looking against it (jah_voxel_sample.glsl: "a ray
// travelling +a meets the faces looking -a"). A single-winding sheet drawn with
// CULL_NONE (a leaf card, an awning) covered only the half its winding faces,
// so it was TRANSPARENT FROM ITS BACK on the voxel routes: the ground under a
// canopy saw the sky through it and read too bright, and the gather (which
// traces rays that hit both sides) disagreed with the field at the hand-off.
// Since PHOTON-5 the voxel gather carries kGpuTwoSided in the record's flags
// lane and the voxeliser writes both halves for such an instance.
//
// THE FIXTURE: a 20 m ground under a sun and a uniform sky; a 4 m sheet 1 m
// above its centre. Two arms, one scene each, the same tier (Medium: the voxel
// route, no rays):
//   A  the sheet as ONE winding (normal up) worn two-sided (FaceCull::TwoSided)
//   B  the reference: the same sheet authored as TWO OPPOSED WINDINGS (one up,
//      one down), one-sided — what the store always handled
// The ground's radiance at the shade's centre (a 9x9 HDR patch seen from under
// the sheet; the sun is shadowed there in both arms, so it is the indirect) must
// agree within 2 %. MEASURED at the base: A 0.146, B 0.0089 (16.4x - the sky
// through the sheet's back); with only the coverage halves written, A 0.060
// (6.8x - the voxel's normal groups still said one-sided, so its back was
// lit as nothing); with the second winding's sums too, A = B bit for bit.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)

/// A horizontal square sheet of half-size h at height y: the up-facing winding,
/// and (`both`) the down-facing one beside it as a second pair of triangles.
static MeshData sheetMesh(float h, bool both)
{
    MeshData d;
    auto quad = [&](float ny) {
        const unsigned b = unsigned(d.positions.size() / 3u);
        const float p[4][3] = { { -h, 0, -h }, { h, 0, -h }, { h, 0, h }, { -h, 0, h } };
        for (int v = 0; v < 4; ++v) {
            d.positions.insert(d.positions.end(), { p[v][0], p[v][1], p[v][2] });
            d.normals.insert(d.normals.end(), { 0.0f, ny, 0.0f });
            d.uvs.insert(d.uvs.end(), { p[v][0] > 0 ? 1.0f : 0.0f, p[v][2] > 0 ? 1.0f : 0.0f });
        }
        // Counter-clockwise seen from the side the normal points to.
        if (ny > 0) d.indices.insert(d.indices.end(), { b, b + 2, b + 1, b, b + 3, b + 2 });
        else        d.indices.insert(d.indices.end(), { b, b + 1, b + 2, b, b + 2, b + 3 });
    };
    quad(+1.0f);
    if (both) quad(-1.0f);
    return d;
}

static double shadeOf(Engine *engine, View *view, bool twoSidedSingle)
{
    Scene *s = engine->createScene(twoSidedSingle ? "single" : "pair");
    view->setScene(s);
    s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
    SkyDesc sky;
    const unsigned char px[4] = { 150, 170, 200, 255 };
    sky.mode = SkyMode::Equirectangular;
    sky.equirect = s->createTexture(1, 1, px, true);
    s->setSky(sky);

    const NodeId ground = enginetest::addTestCube(s, Colour(0.5f, 0.5f, 0.5f), 0.0f, 0.9f);
    enginetest::setNodePosition(s, ground, Vec3(0.0f, -0.1f, 0.0f));
    enginetest::setNodeScale(s, ground, Vec3(20.0f, 0.2f, 20.0f));

    PbrParams sp;
    sp.albedo = Colour(0.5f, 0.5f, 0.5f);
    sp.roughness = 0.9f;
    const NodeId sheet = s->createNode();
    s->attachMesh(sheet, s->createMesh(sheetMesh(2.0f, !twoSidedSingle)), s->createPbrMaterial(sp));
    s->setNodeTransform(sheet, Vec3(0.0f, 1.0f, 0.0f), Quat(), Vec3(1.0f, 1.0f, 1.0f));
    if (twoSidedSingle) s->setNodeFaceCull(sheet, FaceCull::TwoSided);

    // THE SUN, SHADOWING (the view's shadow maps on): the shade's centre gets no
    // direct light in either arm, so what the patch reads is the indirect the
    // voxel route answers.
    enginetest::addDirectionalLight(s, Vec3(-0.15f, -1.0f, -0.1f), 5.0f);
    // From UNDER the sheet, looking down at the ground in its shade.
    enginetest::testCameraLookAt(view, Vec3(0.0f, 0.6f, 0.0f), Vec3(0.0f, 0.0f, -0.3f));

    GiParams gi;
    gi.mode = GiMode::VctPccHybrid;
    gi.quality = GiQuality::Medium;      // the voxel route: no rays
    gi.numBounces = 1;
    if (!s->setGlobalIllumination(gi))
        std::printf("   engine error: %s\n", engine->lastError().c_str());
    enginetest::frames(engine, 48);      // the chain built, the field integrated (8 frames) many times over

    ImageF img;
    double sum = 0.0;
    int n = 0;
    if (view->readPixelsHdr(img)) {
        const int cx = int(img.width / 2), cy = int(img.height / 2);
        for (int y = cy - 4; y <= cy + 4; ++y)
            for (int x = cx - 4; x <= cx + 4; ++x) {
                const Colour c = img.at(unsigned(x), unsigned(y));
                sum += 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b;
                ++n;
            }
    }
    const GiStatus st = s->published().gi;
    std::printf("   %-46s shade luminance %.5f (cascades %zu)\n",
                twoSidedSingle ? "A one winding, worn two-sided:" : "B two opposed windings (the reference):",
                n ? sum / n : -1.0, st.cascades.size());
    engine->destroyScene(s);
    return n ? sum / n : -1.0;
}

int main()
{
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-gi-voxel-two-sided-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    View *view = engine->createOffscreenView("two-sided", 128, 128, Colour(0, 0, 0));
    PostFxDesc fx;
    fx.hdrReadback = true;               // the scene's linear radiance (HDR-READBACK-1)
    view->setPostFx(fx);
    view->setShadows(true);

    const double a = shadeOf(engine.get(), view, true);
    const double b = shadeOf(engine.get(), view, false);
    std::printf("   A / B = %.4f\n", b > 0 ? a / b : -1.0);
    CHECK(a > 0.0 && b > 0.0, "both arms read a shade");
    CHECK(b > 0 && std::fabs(a / b - 1.0) <= 0.02,
          "the shade under a single-winding two-sided sheet equals the two-winding reference within 2 %");

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
