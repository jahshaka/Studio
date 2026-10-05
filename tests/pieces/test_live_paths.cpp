// pieces.live_paths — the engine half of the LIVE GRAPH PATHS (TORNADO-1).
//
// Two backend mechanisms the shader-graph emitter now targets, each proven
// against the ordinary material route it must equal — the same scene, the same
// camera, the whole image compared byte for byte:
//
//   G1, THE UV SCROLL. A material whose maps scroll at velocity v, drawn at
//       shader time t, IS the same material with its maps offset by v*t
//       (wrapped to [0,1)): the backend writes that offset into userValue[2].zw
//       at every setShaderTime and the UV macro (JahFog_piece_vs_piece_ps.any,
//       under our `jah_uv_scroll` property) adds it after the transform.
//       A control proves the scroll is visible (t = 0 against the same offset
//       must differ), and a zero velocity is inert (no property, no clock).
//
//   G2, THE EMISSIVE HOOK. A custom piece that adds a constant through the
//       fork's custom_ps_emissive hook (the end of HlmsPbs' DoEmissiveLight)
//       draws exactly what the same constant does as the material's emissive.
//
// Both scenes run the stock PBR shader (setAtomDrawEnabled(false)): a live
// material leaves the Atom route by design, and the comparison must be route
// against route, not decode against stock.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace jahshaka::engine;

namespace {

int gFailures = 0;
int gChecks = 0;

#define CHECK_MSG(cond, ...)                                                     \
    do {                                                                         \
        ++gChecks;                                                               \
        if (!(cond)) {                                                           \
            ++gFailures;                                                         \
            std::printf("    FAIL %s:%d: %s — ", __FILE__, __LINE__, #cond);     \
            std::printf(__VA_ARGS__);                                            \
            std::printf("\n");                                                   \
        } else {                                                                 \
            std::printf("    ok   ");                                            \
            std::printf(__VA_ARGS__);                                            \
            std::printf("\n");                                                   \
        }                                                                        \
    } while (0)

std::unique_ptr<Engine> gEngine;
std::string gDir;

EngineConfig testConfig() {
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test_live_paths-ogre.log";
    return cfg;
}

/// A unit quad in the XY plane facing +Z, UVs 0..1 — the subject every row
/// textures.
MeshData quadMesh() {
    MeshData d;
    d.positions = { -0.5f, -0.5f, 0.0f,  0.5f, -0.5f, 0.0f,  0.5f, 0.5f, 0.0f,  -0.5f, 0.5f, 0.0f };
    d.normals = { 0, 0, 1,  0, 0, 1,  0, 0, 1,  0, 0, 1 };
    d.uvs = { 0, 1,  1, 1,  1, 0,  0, 0 };
    d.indices = { 0, 1, 2, 0, 2, 3 };
    return d;
}

/// A deterministic, high-frequency test pattern: any shift of the lookup shows.
std::vector<unsigned char> pattern(unsigned n) {
    std::vector<unsigned char> px(n * n * 4);
    for (unsigned y = 0; y < n; ++y)
        for (unsigned x = 0; x < n; ++x) {
            unsigned char *p = &px[(y * n + x) * 4];
            p[0] = (unsigned char)((x * 37 + y * 11) & 255);
            p[1] = (unsigned char)((x * 5 + y * 71) & 255);
            p[2] = (unsigned char)(((x ^ y) * 29) & 255);
            p[3] = 255;
        }
    return px;
}

struct Probe {
    Scene *scene = nullptr;
    View *view = nullptr;
    MaterialId material = 0;
    TextureId texture = 0;
};

Probe makeProbe(const std::string &name) {
    Probe p;
    p.view = gEngine->createOffscreenView(name, 64, 64, Colour(0.0f, 0.0f, 0.0f));
    p.scene = gEngine->createScene(name);
    if (!p.view || !p.scene) return p;
    p.scene->setAtomDrawEnabled(false);
    p.scene->setAmbient(Colour(0.3f, 0.3f, 0.3f), Colour(0.2f, 0.2f, 0.2f));
    enginetest::addDirectionalLight(p.scene, Vec3(-0.3f, -0.4f, -1.0f), 2.0f);
    const NodeId node = p.scene->createNode();
    const MeshId mesh = p.scene->createMesh(quadMesh());
    PbrParams params;
    params.albedo = Colour(1.0f, 1.0f, 1.0f);
    params.metalness = 0.0f;
    params.roughness = 0.8f;
    p.material = p.scene->createPbrMaterial(params);
    p.scene->attachMesh(node, mesh, p.material);
    const std::vector<unsigned char> px = pattern(32);
    p.texture = p.scene->createTexture(32, 32, px.data(), false);
    p.scene->setPbrTexture(p.material, PbrTextureSlot::Albedo, p.texture);
    p.view->setScene(p.scene);
    enginetest::testCameraLookAt(p.view, Vec3(0.0f, 0.0f, 1.2f), Vec3(0.0f, 0.0f, 0.0f));
    return p;
}

PbrParams baseParams() {
    PbrParams params;
    params.albedo = Colour(1.0f, 1.0f, 1.0f);
    params.metalness = 0.0f;
    params.roughness = 0.8f;
    return params;
}

Probe gA, gB;

void render(int frames = 3) { for (int i = 0; i < frames; ++i) gEngine->renderOneFrame(); }

/// Pixels that differ, and by how much at worst.
struct Diff { int pixels = 0; int worst = 0; };
Diff compare(const Image &a, const Image &b) {
    Diff d;
    if (a.width != b.width || a.height != b.height || a.rgba.size() != b.rgba.size()) {
        d.pixels = -1;
        return d;
    }
    for (size_t i = 0; i < a.rgba.size(); i += 4) {
        int w = 0;
        for (int c = 0; c < 3; ++c) w = std::max(w, std::abs(int(a.rgba[i + c]) - int(b.rgba[i + c])));
        if (w) ++d.pixels;
        d.worst = std::max(d.worst, w);
    }
    return d;
}

bool readBoth(Image &a, Image &b) {
    return gA.view->readPixels(a) && gB.view->readPixels(b);
}

// ------------------------------------------------------------------- G1

void g1_scroll_is_an_offset() {
    std::printf("== G1: a scroll at velocity v, time t, IS an offset of v*t\n");
    PbrParams scroll = baseParams();
    scroll.uvVelocity[0] = 0.25f;
    scroll.uvVelocity[1] = -0.5f;
    PbrParams offset = baseParams();
    // v * t at t = 1.0, WRAPPED to [0,1) as the backend wraps it (the maps
    // repeat, so a whole-unit shift is no shift): (0.25, -0.5) -> (0.25, 0.5).
    offset.uvOffset[0] = 0.25f;
    offset.uvOffset[1] = 0.5f;
    gA.scene->setPbrMaterial(gA.material, scroll);
    gB.scene->setPbrMaterial(gB.material, offset);
    gA.scene->setShaderTime(1.0f);
    gB.scene->setShaderTime(1.0f);
    render();
    Image a, b;
    CHECK_MSG(readBoth(a, b), "readback");
    const Diff same = compare(a, b);
    CHECK_MSG(same.pixels == 0, "scroll (0.25,-0.5) at t=1 == offset (0.25,0.5) [wrapped]: %d pixels differ "
              "(worst %d/255)", same.pixels, same.worst);

    // THE CONTROL: the same scroll at t = 0 is NOT that offset.
    gA.scene->setShaderTime(0.0f);
    render();
    CHECK_MSG(readBoth(a, b), "readback");
    const Diff moved = compare(a, b);
    CHECK_MSG(moved.pixels > 200, "control: at t=0 the scroll is NOT the offset (%d pixels differ, "
              "worst %d/255) — the comparison can see a shift", moved.pixels, moved.worst);

    // And at t = 2 it is the offset doubled — the motion is linear in the clock.
    PbrParams offset2 = baseParams();
    offset2.uvOffset[0] = 0.5f;     // (0.5, -1.0) wrapped
    offset2.uvOffset[1] = 0.0f;
    gB.scene->setPbrMaterial(gB.material, offset2);
    gA.scene->setShaderTime(2.0f);
    render();
    CHECK_MSG(readBoth(a, b), "readback");
    const Diff twice = compare(a, b);
    CHECK_MSG(twice.pixels == 0, "scroll at t=2 == offset (0.5,0.0) [wrapped]: %d pixels differ (worst %d/255)",
              twice.pixels, twice.worst);

    // INERT: a zero velocity is the plain material, whatever the clock says.
    gA.scene->setPbrMaterial(gA.material, baseParams());
    gB.scene->setPbrMaterial(gB.material, baseParams());
    gA.scene->setShaderTime(5.0f);
    render();
    CHECK_MSG(readBoth(a, b), "readback");
    const Diff inert = compare(a, b);
    CHECK_MSG(inert.pixels == 0, "a zero velocity is inert: %d pixels differ", inert.pixels);
}

// ------------------------------------------------------------------- G2

void g2_emissive_hook_equals_material_emissive() {
    std::printf("== G2: an emissive added through custom_ps_emissive == the material's emissive\n");
    const std::string path = gDir + "/live_paths_emissive.piece_ps.glsl";
    {
        std::ofstream f(path, std::ios::binary);
        // What the emitter writes for a constant Emissive socket, by hand: the
        // engine suite must not depend on the graph slice. No at-sign in any
        // comment here (the Hlms parser reads them).
        f << "@piece( custom_ps_emissive )\n"
             "{\n"
             "\tfinalColour += midf3_c( float3( 0.3, 0.6, 0.9 ) );\n"
             "}\n"
             "@end\n";
    }
    PbrParams dark = baseParams();
    PbrParams lit = baseParams();
    lit.emissive = Colour(0.3f, 0.6f, 0.9f);
    gA.scene->setPbrMaterial(gA.material, dark);
    gB.scene->setPbrMaterial(gB.material, lit);
    CHECK_MSG(gA.scene->setMaterialCustomPiece(gA.material, path, CustomPieceStage::PixelPreLights),
              "the emissive piece binds (%s)", gEngine->lastError().c_str());
    render();
    Image a, b;
    CHECK_MSG(readBoth(a, b), "readback");
    const Diff d = compare(a, b);
    CHECK_MSG(d.pixels == 0, "piece emissive (0.3,0.6,0.9) == material emissive (0.3,0.6,0.9): "
              "%d pixels differ (worst %d/255)", d.pixels, d.worst);

    // THE CONTROL: without the piece the two differ — the emissive is visible.
    gA.scene->setMaterialCustomPiece(gA.material, "", CustomPieceStage::PixelPreLights);
    render();
    CHECK_MSG(readBoth(a, b), "readback");
    const Diff off = compare(a, b);
    CHECK_MSG(off.pixels > 200, "control: without the piece the emissive is missing (%d pixels "
              "differ, worst %d/255)", off.pixels, off.worst);
}

} // namespace

int main(int argc, char **argv) {
    gDir = argc > 1 ? argv[1] : ".";
    std::string error;
    gEngine = Engine::create(testConfig(), error);
    if (!gEngine) {
        std::printf("Engine::create failed: %s\n", error.c_str());
        return 1;
    }
    gA = makeProbe("live_a");
    gB = makeProbe("live_b");
    if (!gA.material || !gB.material || !gA.texture || !gB.texture) {
        std::printf("could not build the probes\n");
        return 1;
    }
    g1_scroll_is_an_offset();
    g2_emissive_hook_equals_material_emissive();

    gEngine->destroyView(gA.view);
    gEngine->destroyView(gB.view);
    gEngine->destroyScene(gA.scene);
    gEngine->destroyScene(gB.scene);
    gEngine.reset();
    std::printf("%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
