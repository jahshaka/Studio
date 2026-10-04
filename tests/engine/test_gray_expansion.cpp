// engine.gray_expansion — A GRAYSCALE MAP IS THE SAME BYTES ON THE GPU, EXPANDED ON THE
// STREAMING WORKER (SPEED-CPU CS-3; fork TextureFilter::ExpandMonochrome).
//
// OgreScene::loadTexture used to expand a single-channel file on the calling thread (the
// UI thread, inside the mirror walk): Image2::load2, a getColourAt/setColourAt loop into
// RGBA8, Image2::generateMipmaps(srgb, FILTER_BILINEAR), a manual texture per call. The
// fork's filter does it on the worker and the texture takes the pooled path. This suite
// is the arm that says the bytes did not move: every shipped preset's 8-bit gray map, in
// both colour spaces, is loaded through loadTexture, streamed, read back mip by mip
// (Image2::convertFromTexture), and compared with the old path's bytes computed here by
// the old calls — every texel of every level, exactly. A colour map is loaded beside them
// and must keep its own format (the filter passes it through).
//
// White-box (EnginePrivate.h for the TextureGpu behind a TextureId); a real Vulkan device.

#include "jahshaka/engine/Engine.h"

#include "EnginePrivate.h"

#include <OgreImage2.h>
#include <OgrePixelFormatGpuUtils.h>
#include <OgreTextureBox.h>
#include <OgreTextureGpu.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace jahshaka::engine;
using jahshaka::engine::detail::OgreScene;

static int gFailures = 0;
#define CHECK(cond, msg)                                                                  \
    do {                                                                                  \
        if (cond) std::printf("ok: %s\n", msg);                                           \
        else { std::printf("FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__); ++gFailures; } \
    } while (0)

namespace {

bool loadFile(const std::string &path, Ogre::Image2 &out)
{
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::vector<unsigned char> bytes;
    unsigned char buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) bytes.insert(bytes.end(), buf, buf + n);
    std::fclose(f);
    Ogre::DataStreamPtr stream(OGRE_NEW Ogre::MemoryDataStream(bytes.data(), bytes.size(), false, true));
    out.load2(stream, path.substr(path.find_last_of('/') + 1));
    return true;
}

/// THE OLD PATH'S BYTES: the calls loadTexture made on the UI thread before CS-3.
bool oldExpansion(const std::string &path, bool srgb, Ogre::Image2 &rgba)
{
    Ogre::Image2 probe;
    if (!loadFile(path, probe)) return false;
    const Ogre::PixelFormatGpu pf = probe.getPixelFormat();
    if (Ogre::PixelFormatGpuUtils::getNumberOfComponents(pf) != 1u ||
        Ogre::PixelFormatGpuUtils::isCompressed(pf))
        return false;
    const Ogre::uint32 w = probe.getWidth(), h = probe.getHeight();
    rgba.createEmptyImage(w, h, 1u, Ogre::TextureTypes::Type2D,
                          srgb ? Ogre::PFG_RGBA8_UNORM_SRGB : Ogre::PFG_RGBA8_UNORM,
                          Ogre::PixelFormatGpuUtils::getMaxMipmapCount(w, h));
    for (Ogre::uint32 y = 0; y < h; ++y)
        for (Ogre::uint32 x = 0; x < w; ++x) {
            Ogre::ColourValue c = probe.getColourAt(x, y, 0);
            c.g = c.b = c.r; c.a = 1.0f;
            rgba.setColourAt(c, x, y, 0);
        }
    rgba.generateMipmaps(srgb, Ogre::Image2::FILTER_BILINEAR);
    return true;
}

/// Texels that differ between two RGBA8 boxes of one mip (row padding ignored).
size_t differingTexels(const Ogre::TextureBox &a, const Ogre::TextureBox &b, int &maxDelta)
{
    size_t n = 0;
    for (Ogre::uint32 y = 0; y < a.height; ++y) {
        const unsigned char *pa = static_cast<const unsigned char *>(a.at(0, y, 0));
        const unsigned char *pb = static_cast<const unsigned char *>(b.at(0, y, 0));
        for (Ogre::uint32 x = 0; x < a.width; ++x) {
            bool diff = false;
            for (int c = 0; c < 4; ++c) {
                const int d = std::abs(int(pa[x * 4 + c]) - int(pb[x * 4 + c]));
                if (d) { diff = true; maxDelta = std::max(maxDelta, d); }
            }
            n += diff;
        }
    }
    return n;
}

}  // namespace

int main()
{
    std::printf("== engine.gray_expansion: the shipped gray maps, worker-expanded, against the old "
                "UI-thread bytes, every texel of every mip\n");
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gray-expansion-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    View *view = engine->createOffscreenView("gray", 64, 64, Colour(0, 0, 0));
    Scene *scene = engine->createScene("gray");
    view->setScene(scene);
    OgreScene *os = static_cast<OgreScene *>(scene);

    const std::string presets = JAHSHAKA_SOURCE_DIR "/app/content/materials/presets/";
    const char *gray[] = {
        "brick_ground/Brick_Ground_01_UV_H_CM_1_SPEC.png", "concrete/Old_Painted_UV_H_CM_1_SPEC.png",
        "grass/Grass_03_UV_H_CM_1_SPEC.png",               "leather/Leather_02_UV_H_CM_1_SPEC.png",
        "marble_tile/Marble_Tiles_01_UV_H_CM_1_SPEC.png",  "patchy_grass/patchygrass_1_SPEC.png",
        "sand/Ground10_1_SPEC.png",                         "stone/Stone_04_UV_H_CM_1_SPEC.png",
        "wood/wood_planks_03_1_SPEC.png" };

    struct Loaded { std::string path; bool srgb; TextureId id; };
    std::vector<Loaded> loaded;
    for (const char *rel : gray)
        for (bool srgb : { false, true }) {
            const std::string p = presets + rel;
            loaded.push_back({ p, srgb, scene->loadTexture(p, srgb) });
        }
    // ...and a colour map beside them: the filter must pass it through.
    const std::string colour = presets + "wood/wood_planks_03_1_COLOR.png";
    const TextureId colourId = scene->loadTexture(colour, true);
    for (int i = 0; i < 4; ++i) engine->renderOneFrame();   // the frame edge waits for the streams

    int texturesSame = 0, texturesChecked = 0;
    for (const Loaded &l : loaded) {
        char msg[512];
        Ogre::TextureGpu *tex = os->ogreTextureFor(l.id);
        Ogre::Image2 want;
        const bool haveWant = oldExpansion(l.path, l.srgb, want);
        std::snprintf(msg, sizeof msg, "%s (%s): loaded, and the old path expands it",
                      l.path.substr(presets.size()).c_str(), l.srgb ? "sRGB" : "linear");
        CHECK(tex && haveWant, msg);
        if (!tex || !haveWant) continue;
        ++texturesChecked;
        Ogre::Image2 got;
        got.convertFromTexture(tex, 0u, Ogre::uint8(tex->getNumMipmaps() - 1u));
        bool same = got.getPixelFormat() == want.getPixelFormat() &&
                    got.getNumMipmaps() == want.getNumMipmaps() && got.getWidth() == want.getWidth() &&
                    got.getHeight() == want.getHeight();
        std::printf("    %-52s %s fmt %s/%s mips %u/%u", l.path.substr(presets.size()).c_str(),
                    l.srgb ? "sRGB  " : "linear", Ogre::PixelFormatGpuUtils::toString(got.getPixelFormat()),
                    Ogre::PixelFormatGpuUtils::toString(want.getPixelFormat()), unsigned(got.getNumMipmaps()),
                    unsigned(want.getNumMipmaps()));
        for (Ogre::uint8 m = 0; same && m < want.getNumMipmaps(); ++m) {
            int maxDelta = 0;
            const size_t d = differingTexels(got.getData(m), want.getData(m), maxDelta);
            if (d) { std::printf(" | mip %u: %zu texels differ (max %d)", unsigned(m), d, maxDelta); same = false; }
        }
        std::printf(same ? " | identical\n" : "\n");
        texturesSame += same;
    }
    char msg[256];
    std::snprintf(msg, sizeof msg, "every gray map, both colour spaces, is the old path's bytes at every mip (%d of %d)",
                  texturesSame, texturesChecked);
    CHECK(texturesChecked == int(loaded.size()) && texturesSame == texturesChecked, msg);

    Ogre::TextureGpu *ct = os->ogreTextureFor(colourId);
    CHECK(ct && Ogre::PixelFormatGpuUtils::getNumberOfComponents(ct->getPixelFormat()) >= 3u &&
              ct->getPixelFormat() != Ogre::PFG_R8_UNORM,
          "a colour map keeps its own (multi-channel) format");

    engine->destroyScene(scene);
    std::printf(gFailures ? "FAILED: %d\n" : "all passed\n", gFailures);
    return gFailures ? 1 : 0;
}
