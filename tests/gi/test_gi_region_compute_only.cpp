// gi.region_compute_only — THE GI REGION IS COMPUTE-LEGAL (ENGINE-UPGRADE-ASYNC-3 stage 1).
//
// The async compute bracket wraps Photon's COMPUTE region (photon/PhotonComponent.h GiRegion, the GI
// step at engine.pre; the arm builds and the cards' pass are Graphics regions, in order): on a
// compute-only family nothing inside it may be a graphics-queue command — no blit, no render
// pass, no draw, no copy or upload on the graphics queue, no texture creation. The fork's
// Component refuses every such command inside a bracket in EITHER arm (VulkanDevice::
// _jahRequireGraphics throws, naming it), so a bracket opened around the region on the
// IN-ORDER arm (the graphics queue still records; setPhotonRegionBracketCheck) is the
// instrument: every refusal is an Ogre exception, and Ogre logs each one through the engine's
// log sink ("...inside an async compute bracket...").
//
// On BOTH scene kinds — a carded scene with rays, and a plain VCT scene — 300 frames each with
// a mover and a light tick (the sun turns and dims every 30 frames: re-injection, the mips, the
// field), then a rebuild (refreshGlobalIllumination: re-voxelisation of every cascade):
//   1. ZERO graphics-only commands refused inside the region;
//   2. the check really opened its bracket around the region (the count of brackets > 0);
//   3. every Photon compute dispatch of the scene's life went through the door inside a
//      region (none outside), the mip chains' dispatches among them; and in the COMPUTE region
//      the door's count equals the render system's own (the fork's) - nothing dispatched around it;
//   4. THE MIPS ARE REAL: after the rebuild, cascade 0's albedo volume read back at every
//      level — every level non-zero, and EVERY TEXEL of every level the blit's own filter of the
//      level below as stored (on an even axis the 2 x 2 x 2 box mean of a premultiplied volume),
//      within the 8-bit store's half code.
// RED ON THE BASE: the voxeliser's and the lighting's mip chains were image blits
// (_autogenerateMipmaps -> vkCmdBlitImage), refused inside the bracket.
#include "jahshaka/engine/Engine.h"
#include "jahshaka/engine/PhotonStatus.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)

static const unsigned kSize = 96;
static const int kFrames = 300;

// The log sink runs on any thread Ogre logs from: counts only.
static std::atomic<unsigned> gRefused{0};
static std::string gFirstRefusal;
static std::atomic<bool> gHaveFirst{false};

static void sink(int, const std::string &m) {
    if (m.find("inside an async compute bracket") == std::string::npos) return;
    if (!gHaveFirst.exchange(true)) gFirstRefusal = m;
    ++gRefused;
}

static void sunAt(Scene *s, NodeId sun, int tick) {
    const float a = 0.35f * float(tick);
    const Vec3 d(-0.3f + 0.2f * std::sin(a), -1.0f, -0.6f + 0.2f * std::cos(a));
    const float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    const Vec3 n(d.x / len, d.y / len, d.z / len);
    const Vec3 from(0.f, -1.f, 0.f);
    const Vec3 c(from.y * n.z - from.z * n.y, from.z * n.x - from.x * n.z, from.x * n.y - from.y * n.x);
    const float w = 1.f + (from.x * n.x + from.y * n.y + from.z * n.z);
    const float q = std::sqrt(w * w + c.x * c.x + c.y * c.y + c.z * c.z);
    s->setNodeTransform(sun, Vec3(0, 0, 0), Quat(c.x / q, c.y / q, c.z / q, w / q), Vec3(1, 1, 1));
    LightDesc l;
    l.type = LightType::Directional;
    l.colour = Colour(1.f, 1.f, 1.f);
    l.intensity = (3.0f - 0.4f * float(tick % 3)) / 3.14159265358979323846f;
    s->setLight(sun, l);
}

// 300 frames: the mover crosses, the sun ticks every 30; then a rebuild and its settle.
static void drive(Engine *e, Scene *s, NodeId mover, NodeId sun) {
    for (int f = 0; f < kFrames; ++f) {
        enginetest::setNodePosition(s, mover, Vec3(-2.0f + 4.0f * float(f) / float(kFrames), 0.3f, 1.0f));
        if (f % 30 == 0) sunAt(s, sun, f / 30);
        e->renderOneFrame();
    }
    s->refreshGlobalIllumination();
    for (int i = 0; i < 12; ++i) e->renderOneFrame();
}

// THE BLIT'S FILTER IN CLOSED FORM: level L's texel (i, j, k) is the linear read of level L - 1 at
// the texel's centre mapped into it, (i + 0.5) x src / dst in texel units, clamped to the edge — on
// an even axis the equal-weight mean of the 2 x 2 x 2 block.
static void blitFilter(const std::vector<float> &src, int sw, int sh, int sd, int dw, int dh, int dd, int i, int j,
                       int k, float out[4]) {
    const auto axis = [](int d, int sN, int dN, int &a, int &b, float &t) {
        float u = (float(d) + 0.5f) * float(sN) / float(dN) - 0.5f;
        if (u < 0.0f) u = 0.0f;
        a = int(std::floor(u));
        if (a > sN - 1) a = sN - 1;
        b = a + 1 < sN ? a + 1 : sN - 1;
        t = u - float(a);
    };
    int x0, x1, y0, y1, z0, z1;
    float tx, ty, tz;
    axis(i, sw, dw, x0, x1, tx);
    axis(j, sh, dh, y0, y1, ty);
    axis(k, sd, dd, z0, z1, tz);
    for (int c = 0; c < 4; ++c) out[c] = 0.0f;
    for (int n = 0; n < 8; ++n) {
        const int x = (n & 1) ? x1 : x0, y = (n & 2) ? y1 : y0, z = (n & 4) ? z1 : z0;
        const float w = ((n & 1) ? tx : 1.0f - tx) * ((n & 2) ? ty : 1.0f - ty) * ((n & 4) ? tz : 1.0f - tz);
        const float *t = &src[((size_t(z) * sh + y) * sw + x) * 4u];
        for (int c = 0; c < 4; ++c) out[c] += w * t[c];
    }
}

static void checkMips(Engine *e, Scene *s, const char *kind) {
    GiVoxelVolume v;
    const bool ok = s->giVoxelVolume(0, v) && v.available;
    std::string m = std::string(kind) + ": cascade 0's voxels read back";
    CHECK(ok, m.c_str());
    if (!ok) { std::printf("   %s\n", e->lastError().c_str()); return; }
    const size_t levels = 1u + v.albedoLevels.size();
    m = std::string(kind) + ": the albedo volume carries a mip chain (more than one level)";
    CHECK(levels > 1u, m.c_str());
    // THE LAW: each stored texel is the filter's value rounded into the 8-bit store - within half a
    // code (1/510) of the closed form computed from the level below as stored.
    const float kHalfCode = 0.5f / 255.0f;
    bool nonZero = true, filtered = true;
    float worst = 0.0f;
    int sw = v.width, sh = v.height, sd = v.depth;
    const std::vector<float> *below = &v.albedo;
    std::printf("   %s: albedo %dx%dx%d, %zu levels; mean alpha per level: ", kind, v.width, v.height, v.depth,
                levels);
    {
        double a = 0.0;
        for (size_t t = 3; t < v.albedo.size(); t += 4) a += v.albedo[t];
        std::printf("%.6f", a / double(v.albedo.size() / 4u));
    }
    for (size_t l = 1; l < levels; ++l) {
        const std::vector<float> &lev = v.albedoLevels[l - 1u];
        const int dw = std::max(1, v.width >> int(l)), dh = std::max(1, v.height >> int(l)),
                  dd = std::max(1, v.depth >> int(l));
        if (lev.size() != size_t(dw) * dh * dd * 4u) { filtered = false; break; }
        double alpha = 0.0;
        for (int k = 0; k < dd; ++k)
            for (int j = 0; j < dh; ++j)
                for (int i = 0; i < dw; ++i) {
                    float want[4];
                    blitFilter(*below, sw, sh, sd, dw, dh, dd, i, j, k, want);
                    const float *got = &lev[((size_t(k) * dh + j) * dw + i) * 4u];
                    alpha += got[3];
                    for (int c = 0; c < 4; ++c) {
                        const float d = std::fabs(got[c] - want[c]);
                        if (d > worst) worst = d;
                    }
                }
        if (!(alpha > 0.0)) nonZero = false;
        std::printf(" %.6f", alpha / double(size_t(dw) * dh * dd));
        below = &lev;
        sw = dw; sh = dh; sd = dd;
    }
    std::printf("\n   %s: the worst texel against the filter of the level below: %.3f codes\n", kind,
                double(worst) * 255.0);
    if (!(worst <= kHalfCode + 1e-6f)) filtered = false;
    m = std::string(kind) + ": every albedo level is non-zero (the mips were written)";
    CHECK(nonZero, m.c_str());
    m = std::string(kind) + ": every texel of every level is the blit's filter of the level below, within the "
        "store's half code";
    CHECK(filtered, m.c_str());
}

int main()
{
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-gi-region-compute-only-ogre.log",
                                                  enginetest::EngineOverrides().logSink(sink));
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->submitFrame(jahshaka::engine::FrameInput::delta(1.0f / 60.0f));
    Engine *e = engine.get();
    setPhotonRegionBracketCheck(true);

    // ---- THE CARDED SCENE (rays: the reflection, the gather, the cards' photon_update) ----------
    View *view = e->createOffscreenView("regioncards", kSize, kSize, Colour(0, 0, 0));
    if (e->rayQueryAvailable() && e->rayTracing()) {
        Scene *s = e->createScene("regioncards");
        view->setScene(s);
        view->setShadows(true);
        s->setAmbient(Colour(0.05f, 0.05f, 0.06f), Colour(0.04f, 0.04f, 0.04f));
        MeshData md = enginetest::unitCubeMesh();
        md.cards = enginetest::boxCards(0.5f);
        PbrParams fp;
        fp.albedo = Colour(0.6f, 0.6f, 0.6f);
        fp.roughness = 0.9f;
        const NodeId floorN = s->createNode();
        s->attachMesh(floorN, s->createMesh(md), s->createPbrMaterial(fp));
        s->setNodeTransform(floorN, Vec3(0.0f, -0.55f, 0.0f), Quat(), Vec3(20.0f, 0.1f, 20.0f));
        PbrParams mp;
        mp.albedo = Colour(1, 1, 1);
        mp.metalness = 1.0f;
        mp.roughness = 0.05f;
        const NodeId mirror = s->createNode();
        s->attachMesh(mirror, s->createMesh(enginetest::unitCubeMesh()), s->createPbrMaterial(mp));
        enginetest::setNodePosition(s, mirror, Vec3(0.0f, 0.5f, -2.0f));
        const NodeId mov = enginetest::addTestCube(s, Colour(0.7f, 0.4f, 0.2f), 0.0f, 0.7f);
        s->setNodeMovable(mov, true);
        const NodeId sun = enginetest::addDirectionalLight(s, Vec3(-0.3f, -1.0f, -0.6f), 3.0f);
        enginetest::testCameraLookAt(view, Vec3(0.0f, 2.0f, 6.0f), Vec3(0.0f, 0.3f, -1.0f));
        GiParams gi;
        gi.mode = GiMode::Vct;
        gi.quality = GiQuality::High;
        gi.numBounces = 1;
        gi.cards = true;
        const PhotonDispatchStatus d0 = photonDispatchStatus();
        const unsigned r0 = gRefused.load();
        s->setGlobalIllumination(gi);
        PostFxDesc fx;
        fx.allowOffscreen = true;
        fx.ssr = 2;
        view->setPostFx(fx);
        drive(e, s, mov, sun);
        const PhotonDispatchStatus d1 = photonDispatchStatus();
        const unsigned refused = gRefused.load() - r0;
        std::printf("   cards: %u refused; %llu brackets; dispatches %llu in the region (%llu mip levels), "
                    "%llu outside\n", refused, d1.regionBrackets - d0.regionBrackets, d1.inRegion - d0.inRegion,
                    d1.mipDispatches - d0.mipDispatches, d1.outsideRegion - d0.outsideRegion);
        if (refused) std::printf("   the first: %s\n", gFirstRefusal.c_str());
        CHECK(s->rayQueryStatus().enabled, "cards: the view traces (rays on)");
        CHECK(refused == 0u, "cards: NO graphics-only command inside the GI region (300 frames, a mover, a light tick, a rebuild)");
        CHECK(d1.regionBrackets > d0.regionBrackets, "cards: the check opened its bracket around the region");
        CHECK(d1.inRegion > d0.inRegion && d1.outsideRegion == d0.outsideRegion,
              "cards: every Photon dispatch through the door, inside the region");
        CHECK(d1.mipDispatches > d0.mipDispatches, "cards: the mip chains dispatched (compute, through the door)");
        std::printf("   cards: the Compute region: %llu door dispatches, %llu render-system dispatches\n",
                    d1.doorInCompute - d0.doorInCompute, d1.renderSystemInCompute - d0.renderSystemInCompute);
        CHECK(d1.doorInCompute > d0.doorInCompute &&
                  d1.doorInCompute - d0.doorInCompute == d1.renderSystemInCompute - d0.renderSystemInCompute,
              "cards: the Compute region's dispatches = the door's (the render system's own count)");
        checkMips(e, s, "cards");
        e->destroyView(view);
        e->destroyScene(s);
    } else {
        e->destroyView(view);
        std::printf("ok: no ray queries on this machine — the carded scene skips\n");
    }

    // ---- A PLAIN VCT SCENE (no cards, no rays) ----------------------------------------------------
    {
        View *v2 = e->createOffscreenView("regionplain", kSize, kSize, Colour(0, 0, 0));
        Scene *s2 = e->createScene("regionplain");
        v2->setScene(s2);
        s2->setAmbient(Colour(0.05f, 0.05f, 0.06f), Colour(0.04f, 0.04f, 0.04f));
        const NodeId floorN = enginetest::addTestCube(s2, Colour(0.6f, 0.6f, 0.6f), 0.0f, 0.9f);
        s2->setNodeTransform(floorN, Vec3(0.0f, -0.55f, 0.0f), Quat(), Vec3(20.0f, 0.1f, 20.0f));
        const NodeId still = enginetest::addTestCube(s2, Colour(0.7f, 0.4f, 0.2f), 0.0f, 0.7f);
        enginetest::setNodePosition(s2, still, Vec3(0.0f, 0.3f, 0.0f));
        const NodeId mov = enginetest::addTestCube(s2, Colour(0.2f, 0.5f, 0.7f), 0.0f, 0.7f);
        s2->setNodeMovable(mov, true);
        const NodeId sun = enginetest::addDirectionalLight(s2, Vec3(-0.3f, -1.0f, -0.6f), 3.0f);
        enginetest::testCameraLookAt(v2, Vec3(0.0f, 2.0f, 6.0f), Vec3(0.0f, 0.3f, -1.0f));
        GiParams g2;
        g2.mode = GiMode::Vct;
        g2.quality = GiQuality::Medium;
        g2.numBounces = 1;
        const PhotonDispatchStatus p0 = photonDispatchStatus();
        const unsigned r0 = gRefused.load();
        s2->setGlobalIllumination(g2);
        drive(e, s2, mov, sun);
        const PhotonDispatchStatus p1 = photonDispatchStatus();
        const unsigned refused = gRefused.load() - r0;
        std::printf("   plain VCT: %u refused; %llu brackets; dispatches %llu in the region (%llu mip levels), "
                    "%llu outside\n", refused, p1.regionBrackets - p0.regionBrackets, p1.inRegion - p0.inRegion,
                    p1.mipDispatches - p0.mipDispatches, p1.outsideRegion - p0.outsideRegion);
        if (refused) std::printf("   the first: %s\n", gFirstRefusal.c_str());
        CHECK(refused == 0u, "plain VCT: NO graphics-only command inside the GI region (300 frames, a mover, a light tick, a rebuild)");
        CHECK(p1.regionBrackets > p0.regionBrackets, "plain VCT: the check opened its bracket around the region");
        CHECK(p1.inRegion > p0.inRegion && p1.outsideRegion == p0.outsideRegion,
              "plain VCT: every Photon dispatch through the door, inside the region");
        CHECK(p1.mipDispatches > p0.mipDispatches, "plain VCT: the mip chains dispatched (compute, through the door)");
        std::printf("   plain VCT: the Compute region: %llu door dispatches, %llu render-system dispatches\n",
                    p1.doorInCompute - p0.doorInCompute, p1.renderSystemInCompute - p0.renderSystemInCompute);
        CHECK(p1.doorInCompute > p0.doorInCompute &&
                  p1.doorInCompute - p0.doorInCompute == p1.renderSystemInCompute - p0.renderSystemInCompute,
              "plain VCT: the Compute region's dispatches = the door's (the render system's own count)");
        checkMips(e, s2, "plain VCT");
        e->destroyView(v2);
        e->destroyScene(s2);
    }
    setPhotonRegionBracketCheck(false);

    std::printf(failures ? "\n%d CHECK(S) FAILED\n" : "\nall checks passed\n", failures);
    return failures ? 1 : 0;
}
