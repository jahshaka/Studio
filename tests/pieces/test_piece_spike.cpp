// HLMS_ADOPTION P5 stage 5a — THE SPIKE.
//
// Before an emitter is written, prove the mechanism it would emit into. This
// suite hand-writes piece files (no generator involved) and asserts the five
// properties the whole phase rests on:
//
//   1. a per-datablock custom piece COMPILES through the normal render path
//      and through the warm-up path;
//   2. it RENDERS — the surface the graph describes replaces the material's
//      own, and a per-frame constant (the animation clock) changes the picture
//      with no recompile;
//   3. editing the graph (a NEW content-addressed file) re-renders, while
//      re-registering the SAME name with different content is refused loudly
//      rather than silently rendering the stale shader;
//   4. a material WITHOUT a piece renders BYTE-IDENTICAL pixels while another
//      material in the same scene carries one (the isolation contract every
//      existing pixel suite depends on);
//   5. a broken piece fails at BIND time, not as a black frame later —
//      the emit-then-validate story the risk table asks for.
//
// Links JahshakaEngine only, like every other engine suite.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace jahshaka::engine;

namespace {

int gFailures = 0;
int gChecks = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        ++gChecks;                                                               \
        if (!(cond)) {                                                           \
            ++gFailures;                                                         \
            std::printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
        }                                                                        \
    } while (0)
#define CHECK_MSG(cond, ...)                                                     \
    do {                                                                         \
        ++gChecks;                                                               \
        if (!(cond)) {                                                           \
            ++gFailures;                                                         \
            std::printf("    FAIL %s:%d: %s — ", __FILE__, __LINE__, #cond);     \
            std::printf(__VA_ARGS__);                                            \
            std::printf("\n");                                                   \
        }                                                                        \
    } while (0)
#define REQUIRE(cond) do { CHECK(cond); if (!(cond)) return; } while (0)

std::unique_ptr<Engine> gEngine;
std::string gDir;   // scratch directory for piece files

EngineConfig testConfig() {
    EngineConfig cfg;
    cfg.backend = Backend::Vulkan;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test_piece_spike-ogre.log";
    return cfg;
}

struct Fixture {
    Engine *e;
    std::vector<View *> views;
    std::vector<Scene *> scenes;
    Fixture() : e(gEngine.get()) {}
    ~Fixture() {
        for (View *v : views) e->destroyView(v);
        for (Scene *s : scenes) e->destroyScene(s);
    }
    View *view(const std::string &name, unsigned w, unsigned h, const Colour &bg) {
        View *v = e->createOffscreenView(name, w, h, bg);
        if (v) views.push_back(v);
        else std::printf("    createOffscreenView('%s'): %s\n", name.c_str(), e->lastError().c_str());
        return v;
    }
    Scene *scene(const std::string &name) {
        Scene *s = e->createScene(name);
        if (s) scenes.push_back(s);
        else std::printf("    createScene('%s'): %s\n", name.c_str(), e->lastError().c_str());
        return s;
    }
};

struct Px { int r, g, b, a; };
Px px(const Image &img, unsigned x, unsigned y) {
    const Colour c = img.at(x, y);
    return { int(std::lround(c.r * 255)), int(std::lround(c.g * 255)),
             int(std::lround(c.b * 255)), int(std::lround(c.a * 255)) };
}
Px centre(const Image &img) { return px(img, img.width / 2, img.height / 2); }

unsigned long long pixelHash(const Image &img) {
    unsigned long long h = 1469598103934665603ull;
    for (unsigned char v : img.rgba) { h ^= v; h *= 1099511628211ull; }
    return h;
}

const Colour kBlack(0.0f, 0.0f, 0.0f);

void render(Engine *e, int frames = 3) { for (int i = 0; i < frames; ++i) e->renderOneFrame(); }

/// Writes `source` to <scratch>/<name> and hands back the absolute path.
std::string writePiece(const std::string &name, const std::string &source) {
    const std::string path = gDir + "/" + name;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << source;
    out.close();
    return path;
}

// The spike's piece: replace the surface's diffuse with a colour driven by the
// per-material animation clock. Deliberately writes pixelData members ONLY —
// the semantics the master sockets have — and touches nothing else.
std::string pulseSource(const char *green) {
    return std::string(
        "@piece( custom_ps_preLights )\n"
        "\tpixelData.diffuse.xyz = midf3_c( passBuf.jahClock.x, ") + green + ", 0.0 );\n"
        "@end\n";
}

/// A lit scene whose only object is a cube facing the camera, with the material
/// exposed so the test can bind a piece to it.
struct CubeScene {
    Scene *s = nullptr;
    View *v = nullptr;
    NodeId node = 0;
    MaterialId material = 0;
};

CubeScene makeCubeScene(Fixture &f, const std::string &name, const Colour &albedo) {
    CubeScene cs;
    cs.v = f.view(name, 64, 64, kBlack);
    cs.s = f.scene(name);
    if (!cs.v || !cs.s) return cs;
    cs.s->setAmbient(Colour(0.3f, 0.3f, 0.3f), Colour(0.2f, 0.2f, 0.2f));
    enginetest::addDirectionalLight(cs.s, Vec3(-0.5f, -0.7f, -0.5f), 3.14159f);
    cs.node = cs.s->createNode();
    const MeshId mesh = cs.s->createMesh(enginetest::unitCubeMesh());
    PbrParams p;
    p.albedo = albedo;
    p.metalness = 0.0f;
    p.roughness = 0.6f;
    cs.material = cs.s->createPbrMaterial(p);
    cs.s->attachMesh(cs.node, mesh, cs.material);
    enginetest::setNodeScale(cs.s, cs.node, Vec3(1.4f, 1.4f, 1.4f));
    cs.v->setScene(cs.s);
    enginetest::testCameraLookAt(cs.v, Vec3(2.2f, 1.8f, 2.6f), Vec3(0.0f, 0.0f, 0.0f));
    return cs;
}

// ---------------------------------------------------------------------------
// 1 + 2: it compiles, it renders, and the clock moves it.
void piece_compiles_and_renders() {
    Fixture f; Engine *e = f.e;
    CubeScene cs = makeCubeScene(f, "spike-render", Colour(0.0f, 0.0f, 1.0f));
    REQUIRE(cs.material);

    render(e);
    Image before;
    REQUIRE(cs.v->readPixels(before));
    const Px blue = centre(before);
    std::printf("    no piece:      centre = %d %d %d\n", blue.r, blue.g, blue.b);
    CHECK_MSG(blue.b > blue.r + 20, "baseline must be the BLUE material");

    const std::string path = writePiece("spike_pulse.piece_ps.glsl", pulseSource("0.0"));
    CHECK_MSG(cs.s->setMaterialCustomPiece(cs.material, path, CustomPieceStage::PixelPreLights),
              "bind: %s", e->lastError().c_str());
    cs.s->setShaderTime(0.9f);
    render(e);
    Image lit;
    REQUIRE(cs.v->readPixels(lit));
    const Px hot = centre(lit);
    std::printf("    piece, t=0.9:  centre = %d %d %d\n", hot.r, hot.g, hot.b);
    CHECK_MSG(hot.r > hot.b + 20, "the piece must replace the surface: red, not blue");

    // The clock: a constant-buffer write, no recompile, a different picture.
    cs.s->setShaderTime(0.05f);
    render(e);
    Image cold;
    REQUIRE(cs.v->readPixels(cold));
    const Px dim = centre(cold);
    std::printf("    piece, t=0.05: centre = %d %d %d\n", dim.r, dim.g, dim.b);
    CHECK_MSG(dim.r + 20 < hot.r, "the animation clock must change the rendered colour");

    // Clearing it puts the material's own surface back.
    CHECK(cs.s->setMaterialCustomPiece(cs.material, "", CustomPieceStage::PixelPreLights));
    render(e);
    Image cleared;
    REQUIRE(cs.v->readPixels(cleared));
    const Px back = centre(cleared);
    std::printf("    cleared:       centre = %d %d %d\n", back.r, back.g, back.b);
    CHECK_MSG(back.b > back.r + 20, "clearing the piece restores the material's own surface");
}

// ---------------------------------------------------------------------------
// 3: an EDIT is a new content-addressed file; the same name with new content
// is refused rather than silently stale.
void edit_is_a_new_file_and_reuse_is_refused() {
    Fixture f; Engine *e = f.e;
    CubeScene cs = makeCubeScene(f, "spike-edit", Colour(0.0f, 0.0f, 1.0f));
    REQUIRE(cs.material);

    const std::string v1 = writePiece("spike_edit_a.piece_ps.glsl", pulseSource("0.0"));
    CHECK(cs.s->setMaterialCustomPiece(cs.material, v1, CustomPieceStage::PixelPreLights));
    cs.s->setShaderTime(0.8f);
    render(e);
    Image a;
    REQUIRE(cs.v->readPixels(a));
    const Px pa = centre(a);

    // "The user edited the graph": different source, therefore a DIFFERENT
    // file name. This is what content addressing buys.
    const std::string v2 = writePiece("spike_edit_b.piece_ps.glsl", pulseSource("0.9"));
    CHECK(cs.s->setMaterialCustomPiece(cs.material, v2, CustomPieceStage::PixelPreLights));
    render(e);
    Image b;
    REQUIRE(cs.v->readPixels(b));
    const Px pb = centre(b);
    std::printf("    edit: A centre = %d %d %d   B centre = %d %d %d\n",
                pa.r, pa.g, pa.b, pb.r, pb.g, pb.b);
    CHECK_MSG(pb.g > pa.g + 20, "the edited piece must render");

    // Now the trap the naming scheme exists to make unreachable: SAME name,
    // different content. The backend throws; the verb must report it and the
    // engine must survive.
    std::ofstream(gDir + "/spike_edit_a.piece_ps.glsl", std::ios::binary | std::ios::trunc)
        << pulseSource("0.5");
    const bool reused = cs.s->setMaterialCustomPiece(cs.material, v1, CustomPieceStage::PixelPreLights);
    std::printf("    same name, new content -> %s (%s)\n",
                reused ? "ACCEPTED" : "refused", e->lastError().c_str());
    CHECK_MSG(!reused, "same filename with different content must be refused, not rendered stale");
    render(e);
    Image after;
    CHECK_MSG(cs.v->readPixels(after), "the engine survives the refusal and keeps rendering");
}

// ---------------------------------------------------------------------------
// 4: THE ISOLATION CONTRACT. A material with no piece is byte-identical while
// its neighbour carries one.
void no_piece_is_byte_identical() {
    Fixture f; Engine *e = f.e;
    CubeScene plain = makeCubeScene(f, "spike-plain", Colour(0.9f, 0.3f, 0.1f));
    CubeScene fancy = makeCubeScene(f, "spike-fancy", Colour(0.0f, 0.0f, 1.0f));
    REQUIRE(plain.material && fancy.material);

    render(e);
    Image before;
    REQUIRE(plain.v->readPixels(before));
    const unsigned long long hashBefore = pixelHash(before);

    const std::string path = writePiece("spike_isolation.piece_ps.glsl", pulseSource("0.0"));
    CHECK(fancy.s->setMaterialCustomPiece(fancy.material, path, CustomPieceStage::PixelPreLights));
    fancy.s->setShaderTime(1.0f);
    render(e);

    Image after;
    REQUIRE(plain.v->readPixels(after));
    const unsigned long long hashAfter = pixelHash(after);
    std::printf("    plain view hash %llu -> %llu\n", hashBefore, hashAfter);
    CHECK_MSG(hashBefore == hashAfter,
              "a scene with no piece must render byte-identically while another carries one");

    Image fancyImg;
    REQUIRE(fancy.v->readPixels(fancyImg));
    const Px pf = centre(fancyImg);
    CHECK_MSG(pf.r > pf.b + 20, "...and the piece-carrying view really did change");
}

// ---------------------------------------------------------------------------
// 1b: the WARM-UP path compiles a piece-carrying material.
void warm_up_compiles_a_piece_material() {
    Fixture f; Engine *e = f.e;
    CubeScene cs = makeCubeScene(f, "spike-warmup", Colour(0.0f, 0.0f, 1.0f));
    REQUIRE(cs.material);
    const std::string path = writePiece("spike_warmup.piece_ps.glsl", pulseSource("0.25"));
    CHECK(cs.s->setMaterialCustomPiece(cs.material, path, CustomPieceStage::PixelPreLights));
    cs.s->setShaderTime(0.7f);
    const bool warmed = cs.v->warmUpShaders();
    std::printf("    warmUpShaders -> %s (%s)\n", warmed ? "ok" : "failed", e->lastError().c_str());
    CHECK_MSG(warmed, "the warm-up pass must compile a piece-carrying material");
    render(e);
    Image img;
    REQUIRE(cs.v->readPixels(img));
    const Px c = centre(img);
    std::printf("    after warm-up: centre = %d %d %d\n", c.r, c.g, c.b);
    CHECK_MSG(c.r > c.b + 20, "and the material still renders its piece afterwards");
}

// ---------------------------------------------------------------------------
// 5: a BROKEN piece must fail where the caller can see it.
void broken_piece_is_reported() {
    Fixture f; Engine *e = f.e;
    CubeScene cs = makeCubeScene(f, "spike-broken", Colour(0.0f, 0.0f, 1.0f));
    REQUIRE(cs.material);
    render(e);
    Image good;
    REQUIRE(cs.v->readPixels(good));

    // A duplicate of a piece the Hlms library already defines: the parser fails
    // the WHOLE shader on a duplicate @piece name (Hlms::collectPieces). This
    // is the collision rule the emitter's allowed-piece list exists to prevent.
    const std::string path = writePiece(
        "spike_broken.piece_ps.glsl",
        "@piece( custom_ps_uv_modifier_macros )\n#define UV_DIFFUSE(x) (x)\n@end\n");
    const bool bound = cs.s->setMaterialCustomPiece(cs.material, path,
                                                    CustomPieceStage::PixelPreLights);
    std::printf("    duplicate-piece bind -> %s (%s)\n",
                bound ? "accepted" : "refused", e->lastError().c_str());
    // Whether the throw happens at bind or at first draw is exactly what this
    // spike is here to FIND OUT; both outcomes are reported, and the engine
    // must survive either way.
    render(e);
    Image after;
    const bool readable = cs.v->readPixels(after);
    std::printf("    after a duplicate-piece bind the view %s\n",
                readable ? "still reads back" : "FAILED to read back");
    CHECK_MSG(readable, "a bad piece must not take the process down");
    // Put the material back into a good state so the fixture tears down clean.
    cs.s->setMaterialCustomPiece(cs.material, "", CustomPieceStage::PixelPreLights);
    render(e);
}

// ---------------------------------------------------------------------------
// 5b: the VERTEX stage exists and moves geometry (the sockets the baker has
// always classified `unsupported`).
void vertex_piece_moves_geometry() {
    Fixture f; Engine *e = f.e;
    CubeScene cs = makeCubeScene(f, "spike-vertex", Colour(0.9f, 0.3f, 0.1f));
    REQUIRE(cs.material);
    render(e);
    Image before;
    REQUIRE(cs.v->readPixels(before));
    const unsigned long long hashBefore = pixelHash(before);

    // custom_vs_preTransform runs AFTER `worldPos` is computed and BEFORE it is
    // multiplied by the view-projection — so a world-space displacement written
    // here is exactly the Vertex Offset socket. Note what is NOT in scope: the
    // MATERIAL buffer is a pixel-shader declaration in this template, so a
    // vertex piece cannot read a per-material value at all. That is why the
    // clock lives in the PASS buffer — this case proves a vertex piece reads it.
    const std::string path = writePiece(
        "spike_vertex.piece_vs.glsl",
        "@piece( custom_vs_preTransform )\n"
        "\tworldPos.xyz += float3( 0.0, passBuf.jahClock.x, 0.0 );\n"
        "@end\n");
    const bool bound = cs.s->setMaterialCustomPiece(cs.material, path,
                                                    CustomPieceStage::VertexPreTransform);
    std::printf("    vertex bind -> %s (%s)\n", bound ? "ok" : "failed", e->lastError().c_str());
    CHECK(bound);
    cs.s->setShaderTime(0.6f);
    render(e);
    Image after;
    REQUIRE(cs.v->readPixels(after));
    std::printf("    vertex piece hash %llu -> %llu\n", hashBefore, pixelHash(after));
    CHECK_MSG(hashBefore != pixelHash(after), "a vertex piece must move the geometry");
    cs.s->setMaterialCustomPiece(cs.material, "", CustomPieceStage::VertexPreTransform);
    render(e);
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
    struct Case { const char *name; void (*fn)(); };
    const Case cases[] = {
        { "piece_compiles_and_renders", piece_compiles_and_renders },
        { "warm_up_compiles_a_piece_material", warm_up_compiles_a_piece_material },
        { "no_piece_is_byte_identical", no_piece_is_byte_identical },
        { "edit_is_a_new_file_and_reuse_is_refused", edit_is_a_new_file_and_reuse_is_refused },
        { "vertex_piece_moves_geometry", vertex_piece_moves_geometry },
        { "broken_piece_is_reported", broken_piece_is_reported },
    };
    for (const Case &c : cases) {
        std::printf("== %s\n", c.name);
        c.fn();
    }
    gEngine.reset();
    std::printf("%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
