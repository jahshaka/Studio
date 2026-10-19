// atom.masked_coverage — A CUT-OUT'S COVERAGE IS ITS LEVEL 0'S AT EVERY MINIFICATION (ATOM-3 stage 2; SPECS/ATOM-3.md).
//
// A 1-bit mask sampled at one level is the point sample of level 0: a 1-texel checker minified 2x draws 50 % through
// it, or 0 % or 100 % (which texel the pixel centre lands on), and SOLID through the stock alpha test of a filtered
// mip (its alpha a uniform 0.5) — the polish night's fact (DOCS/traps/ENGINE.md). The mask is a PYRAMID now, each
// coarser level a coverage-preserving downsample of level 0 made on the device (JahAlphaMaskPyramid_cs), and the id
// pass samples the level its fragment's UV footprint asks for (jah_alpha_mask.glsl).
//   (a) THE LAW ON THE DEVICE: the pyramid read back (AlphaMaskStore::readBits), level by level — every level's set
//       count is the running coverage's rounding (|count - S| <= 1/2 + 1/64, S the sum of its parents' footprint
//       fractions of level 0, recomputed here from the level 0 read back), and on a power-of-two mask EQUAL to level
//       0's fraction x the level's texels within ONE texel; the 1-texel checker's levels bit for bit the closed form
//       (bit_i = round(S_i) - round(S_(i-1)), row-major). Two masks: the 256-square 1-texel checker, and a 200 x 120
//       hash pattern at ~37 % (a non-power-of-two pyramid, the law only).
//   (b) THE PICTURE: an orthographic view, a cut-out plane on whole pixels over a floor, the checker at 1x, 2x, 4x and
//       8x minification (the plane 256, 128, 64 and 32 pixels across): the id pass's pixels of the plane / its opaque
//       twin's = the level's coverage (= level 0's, 1/2) within one texel of the level at every one. RED AT THE BASE:
//       the cut-out was on the stock route (no id pixel), and the one-level mask drew 0 % or 100 % from 2x on.
// FRAMES, NEVER TIME: every reading follows counted frames.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include "AlphaMasks.h"
#include "EnginePrivate.h"
#include "GpuScene.h"
#include "HlmsAtom.h"

#include <Compositor/OgreCompositorNode.h>
#include <Compositor/OgreCompositorWorkspace.h>
#include <OgreImage2.h>
#include <OgreTextureBox.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace jahshaka::engine;
using jahshaka::engine::detail::AlphaMaskStore;
using jahshaka::engine::detail::AtomId;
using jahshaka::engine::detail::OgreScene;
using jahshaka::engine::detail::OgreView;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)
#define CHECK_MSG(cond, fmt, ...)                                               \
    do {                                                                        \
        char buf_[640];                                                         \
        std::snprintf(buf_, sizeof(buf_), fmt, __VA_ARGS__);                    \
        CHECK(cond, buf_);                                                      \
    } while (0)

static const unsigned kW = 960, kH = 540;
/// One metre = 128 pixels: the orthographic window's half height (540 / 128 / 2 m).
static const float kOrthoHalf = 540.0f / 128.0f / 2.0f;

/// A 2 x 2 m plane in XY facing +z, an n x n grid (enough triangles for a DAG), UVs 0..1 left to right, top to bottom.
static MeshData plane(int n)
{
    MeshData d;
    for (int j = 0; j <= n; ++j)
        for (int i = 0; i <= n; ++i) {
            const float u = float(i) / float(n), v = float(j) / float(n);
            d.positions.insert(d.positions.end(), { -1.0f + 2.0f * u, 1.0f - 2.0f * v, 0.0f });
            d.normals.insert(d.normals.end(), { 0.0f, 0.0f, 1.0f });
            d.uvs.insert(d.uvs.end(), { u, v });
        }
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            const unsigned a = unsigned(j * (n + 1) + i), b = a + 1u, c = a + unsigned(n + 1), e = c + 1u;
            d.indices.insert(d.indices.end(), { a, c, b, b, c, e });
        }
    return d;
}

static bool readIds(OgreView *v, std::vector<uint32_t> &x)
{
    Ogre::CompositorWorkspace *ws = v->workspace();
    if (!ws) return false;
    Ogre::TextureGpu *tex = nullptr;
    for (Ogre::CompositorNode *n : ws->getNodeSequence())
        if ((tex = n->getDefinedTexture(Ogre::IdString(detail::kAtomIdTexture))) != nullptr) break;
    if (!tex) return false;
    Ogre::Image2 img;
    img.convertFromTexture(tex, 0u, 0u);
    const Ogre::TextureBox box = img.getData(0u);
    x.assign(size_t(kW) * kH, AtomId::kEmpty);
    for (unsigned r = 0; r < kH; ++r) {
        const auto *row = reinterpret_cast<const uint32_t *>(box.at(0, r, 0));
        for (unsigned c = 0; c < kW; ++c) x[size_t(r) * kW + c] = row[c * 2u];
    }
    return true;
}

static uint32_t slotOf(OgreScene *os, NodeId n)
{
    os->ensureGpuScene(false);
    const detail::GpuScene &gs = os->gpuScene();
    for (unsigned s = 0; s < gs.slotCount(); ++s)
        if (os->nodeOfGpuKey(gs.entry(s).ids[0]) == n) return s;
    return 0xFFFFFFFFu;
}

static bool bitAt(const std::vector<uint32_t> &w, uint32_t offset, uint32_t i)
{
    return ((w[offset + (i >> 5u)] >> (i & 31u)) & 1u) != 0u;
}

/// (a) one mask's pyramid against the law. `pot`: level 0 is a power of two on both sides (the exact form holds).
/// `checker`: the 1-texel checker (each parent's coverage exactly 1/2: the closed form's bits are known).
static void checkPyramid(const AlphaMaskStore::Mask &m, const std::vector<uint32_t> &w, const char *label, bool pot,
                         bool checker)
{
    const uint32_t L = AlphaMaskStore::levelCount(m.w, m.h);
    CHECK_MSG(m.levels == L && w.size() >= AlphaMaskStore::levelOffsetWords(m.w, m.h, L),
              "%s: %u levels from %ux%u, %zu words read back (the pyramid's %u)", label, m.levels, m.w, m.h, w.size(),
              AlphaMaskStore::levelOffsetWords(m.w, m.h, L));
    if (w.size() < AlphaMaskStore::levelOffsetWords(m.w, m.h, L)) return;
    unsigned long long n0 = 0ull;
    for (uint32_t i = 0; i < m.w * m.h; ++i) n0 += bitAt(w, 0u, i);
    const double c0 = double(n0) / double(m.w * m.h);
    std::printf("   %s: level 0 %ux%u, %.5f set\n", label, m.w, m.h, c0);
    unsigned lawBad = 0, exactBad = 0, closedBad = 0;
    for (uint32_t k = 1u; k < L; ++k) {
        const uint32_t wk = std::max(m.w >> k, 1u), hk = std::max(m.h >> k, 1u);
        const uint32_t off = AlphaMaskStore::levelOffsetWords(m.w, m.h, k);
        unsigned long long nk = 0ull;
        double S = 0.0;
        unsigned long long before = 0ull;
        unsigned closedMiss = 0;
        for (uint32_t y = 0; y < hk; ++y)
            for (uint32_t x = 0; x < wk; ++x) {
                const uint32_t x0 = x * m.w / wk, x1 = (x + 1u) * m.w / wk, y0 = y * m.h / hk, y1 = (y + 1u) * m.h / hk;
                unsigned n = 0;
                for (uint32_t yy = y0; yy < y1; ++yy)
                    for (uint32_t xx = x0; xx < x1; ++xx) n += bitAt(w, 0u, yy * m.w + xx);
                S += double(n) / double((x1 - x0) * (y1 - y0));
                const unsigned long long now = (unsigned long long)std::floor(S + 0.5);
                const bool bit = bitAt(w, off, y * wk + x);
                nk += bit;
                if (bit != (now != before)) ++closedMiss;
                before = now;
            }
        const double frac = double(nk) / double(wk * hk);
        const double lawErr = std::fabs(double(nk) - S);
        const double exactErr = std::fabs(double(nk) - c0 * double(wk * hk));
        if (lawErr > 0.5 + 1.0 / 64.0) ++lawBad;
        if (pot && exactErr > 1.0) ++exactBad;
        if (checker && closedMiss) ++closedBad;
        std::printf("     level %2u %4ux%-4u set %8llu = %.5f (target %.2f, |count - S| %.3f, vs level 0's %.3f texels)%s\n",
                    k, wk, hk, nk, frac, S, lawErr, exactErr, closedMiss ? "  CLOSED FORM MISSES" : "");
    }
    CHECK_MSG(lawBad == 0u, "(a) %s: every level's count is its running coverage rounded (%u levels off)", label, lawBad);
    if (pot)
        CHECK_MSG(exactBad == 0u, "(a) %s: every level's coverage = level 0's within one texel of the level (%u off)",
                  label, exactBad);
    if (checker)
        CHECK_MSG(closedBad == 0u, "(a) %s: every level's bits are the closed form's, bit for bit (%u levels off)",
                  label, closedBad);
}

int main()
{
    std::printf("== atom.masked_coverage: the cut-out's mask pyramid keeps level 0's coverage on the device and in the "
                "id pass at 1x / 2x / 4x / 8x\n");
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-atom-masked-coverage-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->submitFrame(jahshaka::engine::FrameInput::delta(1.0f / 60.0f));
    Engine *e = engine.get();
    View *view = e->createOffscreenView("maskedcov", kW, kH, Colour(0, 0, 0));
    view->setOffscreenContract(OffscreenContract::StillPicture);
    Scene *scene = e->createScene("maskedcov");
    view->setScene(scene);
    auto *os = static_cast<OgreScene *>(scene);
    auto *ov = static_cast<OgreView *>(view);
    {
        PostFxDesc fx;
        fx.allowOffscreen = true;
        fx.ssr = 0;
        view->setPostFx(fx);
    }
    {
        GiParams gi;
        gi.mode = GiMode::Off;
        scene->setGlobalIllumination(gi);
    }
    scene->setLodBias(0.0f);
    scene->setAmbient(Colour(0.10f, 0.11f, 0.13f), Colour(0.05f, 0.05f, 0.05f));
    {
        CameraDesc c = enginetest::testCameraDescLookAt(Vec3(0.0f, 0.0f, 5.0f), Vec3(0.0f, 0.0f, 0.0f));
        c.orthographic = true;
        c.orthoSize = kOrthoHalf;
        view->setCamera(c);
    }
    // THE FLOOR behind the plane (a wall facing the camera, opaque): what a hole shows.
    {
        MeshData g;
        g.positions = { -20.0f, -20.0f, 0.0f, 20.0f, -20.0f, 0.0f, 20.0f, 20.0f, 0.0f, -20.0f, 20.0f, 0.0f };
        g.normals = { 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1 };
        g.uvs = { 0, 0, 1, 0, 1, 1, 0, 1 };
        g.indices = { 0, 1, 2, 0, 2, 3 };
        PbrParams fp;
        fp.albedo = Colour(0.30f, 0.45f, 0.30f);
        fp.roughness = 0.8f;
        const NodeId f = scene->createNode();
        scene->setNodeTransform(f, Vec3(0.0f, 0.0f, -1.0f), Quat(), Vec3(1, 1, 1));
        scene->attachMesh(f, scene->createMesh(g), scene->createPbrMaterial(fp));
    }
    // THE CHECKER: 256 x 256, a 1-texel checker, alpha 0 / 255 (level 0's fraction 1/2 exactly).
    std::vector<unsigned char> px(256u * 256u * 4u);
    for (unsigned y = 0; y < 256u; ++y)
        for (unsigned x = 0; x < 256u; ++x) {
            unsigned char *q = &px[(y * 256u + x) * 4u];
            q[0] = 210; q[1] = 140; q[2] = 70;
            q[3] = ((x + y) & 1u) ? 255 : 0;
        }
    const TextureId checker = scene->createTexture(256, 256, px.data(), true);
    // THE HASH: 200 x 120 (no side a power of two), ~37 % set.
    std::vector<unsigned char> hx(200u * 120u * 4u);
    for (unsigned y = 0; y < 120u; ++y)
        for (unsigned x = 0; x < 200u; ++x) {
            unsigned h = x * 73856093u ^ y * 19349663u;
            h ^= h >> 13u; h *= 0x5bd1e995u; h ^= h >> 15u;
            unsigned char *q = &hx[(y * 200u + x) * 4u];
            q[0] = 90; q[1] = 160; q[2] = 200;
            q[3] = (h % 1000u) < 370u ? 255 : 0;
        }
    const TextureId hash = scene->createTexture(200, 120, hx.data(), true);
    PbrParams cp;
    cp.alphaMode = PbrAlphaMode::Cutout;
    cp.alphaCutoff = 0.5f;
    cp.roughness = 0.6f;
    const MaterialId cutMat = scene->createPbrMaterial(cp), hashMat = scene->createPbrMaterial(cp);
    scene->setPbrTexture(cutMat, PbrTextureSlot::Albedo, checker);
    scene->setPbrTexture(hashMat, PbrTextureSlot::Albedo, hash);
    PbrParams op;
    op.albedo = Colour(0.82f, 0.55f, 0.27f);
    op.roughness = 0.6f;
    const MaterialId opaqueMat = scene->createPbrMaterial(op);
    const MeshId planeMesh = scene->createMesh(plane(24));
    const NodeId planeNode = scene->createNode();
    scene->setNodeTransform(planeNode, Vec3(0.0f, 0.0f, 0.0f), Quat(), Vec3(1, 1, 1));
    scene->attachMesh(planeNode, planeMesh, opaqueMat);
    // the hash pattern's plane, off to the side (its mask is the second pyramid of (a); its pixels are not counted)
    const NodeId hashNode = scene->createNode();
    scene->setNodeTransform(hashNode, Vec3(2.6f, 0.0f, 0.0f), Quat(), Vec3(0.5f, 0.5f, 0.5f));
    scene->attachMesh(hashNode, planeMesh, hashMat);
    enginetest::addDirectionalLight(scene, Vec3(-0.2f, -0.3f, -1.0f), 2.2f);
    enginetest::frames(e, 16);
    const uint32_t planeSlot = slotOf(os, planeNode);

    // (b) THE PICTURE, minification by minification: the opaque twin's pixels first, then the cut-out's.
    const float scales[4] = { 1.0f, 0.5f, 0.25f, 0.125f };
    const unsigned mins[4] = { 1u, 2u, 4u, 8u };
    for (int k = 0; k < 4; ++k) {
        scene->setNodeTransform(planeNode, Vec3(0.0f, 0.0f, 0.0f), Quat(), Vec3(scales[k], scales[k], 1.0f));
        scene->setNodeMaterial(planeNode, opaqueMat);
        enginetest::frames(e, 6);
        std::vector<uint32_t> ids;
        unsigned opaque = 0;
        if (readIds(ov, ids))
            for (uint32_t x : ids) opaque += (x & AtomId::kSlotMask) == planeSlot;
        scene->setNodeMaterial(planeNode, cutMat);
        // the mask is made the first frame its texture is resident and the slot re-routes the frame after
        enginetest::frames(e, 6);
        const AtomDrawStatus st = scene->atomDrawStatus();
        unsigned masked = 0;
        if (readIds(ov, ids))
            for (uint32_t x : ids) masked += (x & AtomId::kSlotMask) == planeSlot;
        const unsigned side = 256u / mins[k];
        const double frac = opaque ? double(masked) / double(opaque) : 0.0;
        // ONE TEXEL OF THE LEVEL: the level's texels are the plane's pixels (one texel a pixel by construction).
        const double oneTexel = 1.0 / double(side * side);
        std::printf("   %ux: the plane %u px across, the opaque twin %u pixels, the cut-out %u = %.5f (masked items %u)\n",
                    mins[k], side, opaque, masked, frac, st.atomMasked);
        CHECK_MSG(opaque == side * side, "(b) %ux: the opaque twin covers %u x %u whole pixels (%u)", mins[k], side,
                  side, opaque);
        CHECK_MSG(st.atomMasked >= 1u && std::fabs(frac - 0.5) <= oneTexel + 1e-9,
                  "(b) %ux: the id pass's coverage of the 1-texel checker is level 0's 1/2 within one texel of the level "
                  "(%.5f, masked items %u)", mins[k], frac, st.atomMasked);
    }

    // (a) THE LAW ON THE DEVICE, both pyramids read back.
    {
        const AlphaMaskStore &store = detail::alphaMaskStore();
        int checkerMask = -1, hashMask = -1;
        for (size_t i = 0; i < store.count(); ++i) {
            const AlphaMaskStore::Mask &m = store.mask(int(i));
            if (m.texW == 256u && m.texH == 256u) checkerMask = int(i);
            if (m.texW == 200u && m.texH == 120u) hashMask = int(i);
        }
        CHECK_MSG(checkerMask >= 0 && hashMask >= 0, "(a) both masks are in the engine's store (%d, %d)", checkerMask,
                  hashMask);
        std::vector<uint32_t> w;
        if (checkerMask >= 0 && store.readBits(checkerMask, w))
            checkPyramid(store.mask(checkerMask), w, "the 1-texel checker", true, true);
        else
            CHECK(false, "(a) the checker's pyramid reads back");
        if (hashMask >= 0 && store.readBits(hashMask, w))
            checkPyramid(store.mask(hashMask), w, "the 200x120 hash", false, false);
        else
            CHECK(false, "(a) the hash's pyramid reads back");
    }

    e->destroyView(view);
    e->destroyScene(scene);
    engine.reset();
    std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
