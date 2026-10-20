// engine.texture_residency — A TEXTURE'S METADATA CACHE CHANGES NOTHING ON SCREEN (BUGS-1 item 13; preflight-photon-3
// §G; ENGINE-POLISH-PHOTON-2's carrier test, its REPORT.md stage 2).
//
// THE PREMISE UNDER TEST. Ogre's texture metadata cache (texture-meta.json beside the shader cache; OgreTextureCache.cpp)
// makes a file texture RESIDENT before its data is decoded — the metadata names its size, format and mips, so the
// texture transitions at once and the worker fills it later. Without the cache a texture becomes Resident only when its
// data is in. A consumer that takes the texture at its first RESIDENT frame (the datablock's bind, the cut-out's 1-bit
// mask, the voxel row, a capture) would then see EMPTY data on the metadata path and real data on the other — and a
// consumer that keeps what it made never corrects itself. PHOTON-2 suspected exactly that carrier for a near difference
// on Bistro that survived every rebuild.
//
// THE FIXTURE: an ALPHA-TESTED quad (fine foliage alpha: one- and two-texel holes, a file texture loaded by path, so
// the metadata cache keys it) tilted over a floor under a sun with shadows (its cut-out caster) - the routes a cut-out
// takes: the Atom split's masked id pass and caster cut, the ray tier's alpha mask, the stock shadow pass.
// TWO PROCESSES ON ONE CACHE DIRECTORY: the first starts with NO texture-meta.json (the engine writes it at teardown,
// asserted), the second starts WITH it (its texture Resident from metadata, before its data). Each renders the same
// counted frames and digests the settled presented picture (two frames, equal: settled). THE LAW: the two digests are
// EQUAL - the metadata path changes nothing anyone sees.
// Frames, never time: kFrames each.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <tuple>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK_MSG(cond, ...)                                                    \
    do {                                                                        \
        std::printf((cond) ? "ok: " : "FAIL: ");                                \
        std::printf(__VA_ARGS__);                                               \
        std::printf("\n");                                                      \
        if (!(cond)) ++failures;                                                \
    } while (0)

static const char *kCacheDir = "residency-cache";
static const char *kTexture = "residency-foliage.tga";
static const unsigned kW = 320, kH = 180;
static const int kFrames = 90;

static bool exists(const std::string &p)
{
    struct stat st {};
    return ::stat(p.c_str(), &st) == 0;
}

/// FINE FOLIAGE ALPHA, 256 x 256, written as an uncompressed 32-bit TGA (top-left origin): green-brown colour, and
/// an alpha of leaves and holes at one to three texels - the cut-out at its finest.
static bool writeFoliage(const char *path)
{
    const unsigned n = 256u;
    std::FILE *f = std::fopen(path, "wb");
    if (!f) return false;
    const unsigned char header[18] = { 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                       (unsigned char)(n & 255u), (unsigned char)(n >> 8),
                                       (unsigned char)(n & 255u), (unsigned char)(n >> 8), 32, 0x28 };
    std::fwrite(header, 1, sizeof(header), f);
    std::vector<unsigned char> row(size_t(n) * 4u);
    for (unsigned y = 0; y < n; ++y) {
        for (unsigned x = 0; x < n; ++x) {
            unsigned h = x * 73856093u ^ y * 19349663u;
            h ^= h >> 13;
            h *= 0x5bd1e995u;
            h ^= h >> 15;
            const bool frond = ((x / 3u + y / 2u) % 5u) < 3u;
            const bool leaf = frond && (h & 7u) != 0u;
            unsigned char *q = &row[size_t(x) * 4u];
            q[0] = 40;                                   // B
            q[1] = (unsigned char)(120 + (h & 63u));     // G
            q[2] = 70;                                   // R
            q[3] = leaf ? 255 : 0;                       // A
        }
        std::fwrite(row.data(), 1, row.size(), f);
    }
    std::fclose(f);
    return true;
}

static unsigned long long digestOf(const Image &img)
{
    unsigned long long h = 1469598103934665603ull;
    for (unsigned char b : img.rgba) { h ^= b; h *= 1099511628211ull; }
    return h;
}

/// ONE PROCESS: the scene, kFrames frames, the settled digest written to `out` ("<digest> <settled 0|1>").
static int runMain(const char *out)
{
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-texture-residency-ogre.log",
                                                  enginetest::EngineOverrides().shaderCacheDir(kCacheDir));
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->submitFrame(FrameInput::delta(1.0f / 60.0f));
    Engine *e = engine.get();
    View *view = e->createOffscreenView("residency", kW, kH, Colour(0, 0, 0));
    Scene *s = e->createScene("residency");
    if (!view || !s) { std::printf("FAIL: view/scene\n"); return 1; }
    // the cache is LOADED with the Hlms (ensureHlms, the first view), not at create: counted here, before the load
    const unsigned metaAtBoot = e->textureMetadataCacheEntries();
    std::printf("   the texture metadata cache holds %u entries before the texture loads\n", metaAtBoot);
    view->setOffscreenContract(OffscreenContract::StillPicture);
    view->setScene(s);
    view->setShadows(true);
    {
        PostFxDesc fx;
        fx.allowOffscreen = true;
        fx.ssr = 0;
        view->setPostFx(fx);
    }
    s->setAmbient(Colour(0.25f, 0.28f, 0.32f), Colour(0.10f, 0.10f, 0.10f));
    const MeshId cube = s->createMesh(enginetest::unitCubeMesh());
    {   // THE FLOOR, the receiver
        PbrParams gp;
        gp.albedo = Colour(0.55f, 0.55f, 0.52f);
        gp.roughness = 0.8f;
        const NodeId n = s->createNode();
        s->setNodeTransform(n, Vec3(0.0f, -0.05f, 0.0f), Quat(), Vec3(12.0f, 0.1f, 12.0f));
        s->attachMesh(n, cube, s->createPbrMaterial(gp));
    }
    {   // THE CUT-OUT: a 2 m quad tilted 30 degrees about x, 1 m up
        MeshData q;
        q.positions = { -1.0f, 0.0f, -1.0f, 1.0f, 0.0f, -1.0f, 1.0f, 0.0f, 1.0f, -1.0f, 0.0f, 1.0f };
        q.normals = { 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0 };
        q.uvs = { 0, 0, 1, 0, 1, 1, 0, 1 };
        q.indices = { 0, 2, 1, 0, 3, 2 };
        PbrParams cp;
        cp.alphaMode = PbrAlphaMode::Cutout;
        cp.alphaCutoff = 0.5f;
        cp.roughness = 0.7f;
        const MaterialId m = s->createPbrMaterial(cp);
        const TextureId tex = s->loadTexture(kTexture, true);
        CHECK_MSG(tex != 0, "the foliage texture loads by path (%s)", e->lastError().c_str());
        CHECK_MSG(s->setPbrTexture(m, PbrTextureSlot::Albedo, tex), "the foliage texture is the cut-out's albedo");
        const NodeId n = s->createNode();
        const Quat tilt(std::sin(0.2618f), 0.0f, 0.0f, std::cos(0.2618f));
        s->setNodeTransform(n, Vec3(0.0f, 1.0f, 0.0f), tilt, Vec3(1, 1, 1));
        s->attachMesh(n, s->createMesh(q), m);
    }
    enginetest::addDirectionalLight(s, Vec3(-0.3f, -0.85f, -0.4f), 2.5f);
    enginetest::testCameraLookAt(view, Vec3(0.0f, 2.6f, 3.6f), Vec3(0.0f, 0.6f, 0.0f));
    enginetest::frames(e, kFrames - 1);
    Image a, b;
    const bool readA = view->readPixels(a);
    enginetest::frames(e, 1);
    const bool readB = view->readPixels(b);
    CHECK_MSG(readA && readB && a.width == kW && b.width == kW, "the picture reads back");
    const unsigned long long da = digestOf(a), db = digestOf(b);
    const bool settled = readA && readB && da == db;
    std::printf("   frame %d digest %016llx, frame %d %016llx (%s)\n", kFrames - 1, da, kFrames, db,
                settled ? "settled" : "STILL MOVING");
    if (std::FILE *f = std::fopen(out, "w")) {
        std::fprintf(f, "%016llx %d %u\n", db, settled ? 1 : 0, metaAtBoot);
        std::fclose(f);
    }
    if (const char *dump = std::getenv("JAH_RESIDENCY_DUMP")) {
        if (std::FILE *f = std::fopen((std::string(dump) + "-" + out + ".ppm").c_str(), "wb")) {
            std::fprintf(f, "P6 %u %u 255\n", b.width, b.height);
            for (size_t i = 0; i + 3 < b.rgba.size(); i += 4) std::fwrite(&b.rgba[i], 1, 3, f);
            std::fclose(f);
        }
    }
    e->destroyView(view);
    e->destroyScene(s);
    engine.reset();   // the engine's destructor writes the texture cache (OgreTextureCache.cpp save)
    return failures ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (argc > 2 && std::strcmp(argv[1], "--run") == 0) return runMain(argv[2]);
    std::printf("== engine.texture_residency: the texture metadata cache changes nothing on screen\n");
    if (const char *off = std::getenv("JAH_TEXTURE_CACHE"); off && off[0] == '0') {
        std::printf("FAIL: JAH_TEXTURE_CACHE=0 - the texture cache is this row's subject\n");
        return 1;
    }
    CHECK_MSG(writeFoliage(kTexture), "the foliage texture is written (%s)", kTexture);
    // THE CACHE DIRECTORY, WIPED: the first process starts with no metadata
    (void)std::system((std::string("rm -rf ") + kCacheDir).c_str());
    ::mkdir(kCacheDir, 0755);
    const auto child = [&](const char *tag) {
        const std::string out = std::string("residency-") + tag + ".txt", log = std::string("residency-") + tag + ".log";
        ::unlink(out.c_str());
        const int rc = std::system((std::string("\"") + argv[0] + "\" --run " + out + " > " + log + " 2>&1").c_str());
        std::ifstream lf(log);
        std::string line;
        while (std::getline(lf, line))
            if (line.rfind("ok:", 0) == 0 || line.rfind("FAIL", 0) == 0 || line.rfind("   ", 0) == 0)
                std::printf("   [%s] %s\n", tag, line.c_str());
        CHECK_MSG(rc == 0, "the %s process exited %d", tag, rc);
        std::ifstream f(out);
        std::string digest;
        int settled = 0;
        unsigned meta = 0;
        f >> digest >> settled >> meta;
        return std::make_tuple(digest, settled, meta);
    };
    const auto cold = child("cold");
    const std::string meta = std::string(kCacheDir) + "/texture-meta.json";
    CHECK_MSG(exists(meta), "the first process wrote the texture metadata cache (%s)", meta.c_str());
    const auto warm = child("meta");
    CHECK_MSG(std::get<2>(cold) == 0u && std::get<2>(warm) > 0u,
              "the first process started with no metadata (%u entries), the second with it (%u)", std::get<2>(cold),
              std::get<2>(warm));
    CHECK_MSG(std::get<1>(cold) == 1 && std::get<1>(warm) == 1, "both pictures are settled (%d, %d)", std::get<1>(cold),
              std::get<1>(warm));
    CHECK_MSG(!std::get<0>(cold).empty() && std::get<0>(cold) == std::get<0>(warm),
              "THE METADATA PATH CHANGES NOTHING ON SCREEN: the settled digest without the cache %s, with it %s",
              std::get<0>(cold).c_str(), std::get<0>(warm).c_str());
    std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
