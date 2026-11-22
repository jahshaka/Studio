// atom.masked_coverage — A CUT-OUT'S COVERAGE IS ITS LEVEL 0'S AT EVERY MINIFICATION (ATOM-3 stage 2; SPECS/ATOM-3.md).
// RE-REGISTERED by ATOM-TESTS-BACK-1 stage 3 (archived by TEST-CUT-1 for time, never for law; its device read
// AlphaMaskStore::readBits restored in irisgl by the same stage). The law as the trap file states it (atom-and-lod.md, CUT-OUTS RIDE
// ATOM THROUGH A COVERAGE-PRESERVING 1-BIT PYRAMID).
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
//       twin's = the level's coverage (= level 0's, 1/2) within one texel of the level at every one. (RED at ATOM-3's
//       base: the cut-out was on the stock route (no id pixel), and the one-level mask drew 0 % or 100 % from 2x on.)
//   (c) THE ADDRESS MODE (the lead's clause, 2026-11-22): the mask's coverage equals the material's OWN alpha test - its
//       sampler's address mode included - at texel centres across the whole quad. The mask is read NEAREST and WRAPPED
//       whatever the slot's mode (irisgl/engine/src/rayquery/include/jah_alpha_mask.glsl:82-95, `fract`); the material's
//       sampler takes the slot's mode (OgreMaterials.cpp materialSamplerblock). The fixture: a 16 x 16 texture, columns
//       0-7 holes and 8-15 solid (its opposite edges differ), the albedo slot CLAMP, uvScale 1.5, an orthographic quad of
//       24 x 24 texel cells at 8 px each. At a texel centre a bilinear sample is that texel exactly, so the law is closed
//       form: cell (i, j) reads texel (min(i, 15), min(j, 15)) - columns 8..23 solid, 384 of 576 cells = 0.66667. A mask
//       that wraps reads texel (i mod 16) at i >= 16: 8 of 24 columns, 0.33333.
//   RESIDUE (printed, never asserted; the lead takes it to the owner as a picture question, homed at 3.5): THE MASK IS
//       NEAREST, THE MATERIAL BILINEAR. At a CONVEX corner - a hole texel among solid ones - the bilinear alpha is
//       1 - (1 - s)(1 - t) between the four texel centres and its 0.5 line, (1 - s)(1 - t) = 1/2, cuts the square hole's
//       corner by 1 - 1/sqrt(2) = 0.293 of a texel along the diagonal (0.207 texel on each axis): magnified, Atom draws a
//       SQUARE hole where stock rounds it. One isolated hole texel at 8 px a texel: the pixels along each corner's diagonal
//       the stock picture (the door shut) covers and the mask does not - expected 0.207 x 8 = 1.66 px an axis, so the two
//       diagonal pixels whose centres lie under it.
// FRAMES, NEVER TIME: every reading follows counted frames.
#include "jahshaka/engine/Engine.h"
#include "jahshaka/engine/Atom.h"
#include "jahshaka/engine/GiParams.h"
#include "jahshaka/engine/Materials.h"
#include "jahshaka/engine/Mesh.h"
#include "jahshaka/engine/Pictures.h"
#include "../support/enginetesthelpers.h"

#include "AlphaMasks.h"
#include "EnginePrivate.h"
#include "GpuScene.h"
#include "HlmsAtom.h"

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

/// The id image's x words (Engine::readAtomIds: x = slot | depth << 24, AtomId::kEmpty where nothing drew).
static bool readIds(Engine *e, View *v, std::vector<uint32_t> &x)
{
    std::vector<uint32_t> words;
    unsigned w = 0, h = 0;
    if (!e->readAtomIds(v, words, w, h) || w != kW || h != kH || words.size() != size_t(kW) * kH * 2u) return false;
    x.resize(size_t(kW) * kH);
    for (size_t i = 0; i < x.size(); ++i) x[i] = words[2u * i];
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

int main(int argc, char **argv)
{
    // TWO ROWS, one per proof (the lead's ruling 2026-11-22): atom.masked_coverage = (a), (b) and the printed residue;
    // atom.masked_coverage.clamp (`clamp`) = (c) alone (DECLARED RED, SPECS/OPEN_REDS.md, 3.6: the mask wraps where a CLAMP
    // slot clamps).
    const bool clampRow = argc > 1 && std::string(argv[1]) == "clamp";
    std::printf("== atom.masked_coverage: the cut-out's mask pyramid keeps level 0's coverage on the device and in the "
                "id pass at 1x / 2x / 4x / 8x\n");
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-atom-masked-coverage-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    View *view = e->createOffscreenView("maskedcov", kW, kH, Colour(0, 0, 0), OffscreenContract::Live);
    Scene *scene = e->createScene("maskedcov");
    view->setScene(scene);
    auto *os = static_cast<OgreScene *>(scene);
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
    if (!clampRow) {
        const float scales[4] = { 1.0f, 0.5f, 0.25f, 0.125f };
        const unsigned mins[4] = { 1u, 2u, 4u, 8u };
        for (int k = 0; k < 4; ++k) {
            scene->setNodeTransform(planeNode, Vec3(0.0f, 0.0f, 0.0f), Quat(), Vec3(scales[k], scales[k], 1.0f));
            scene->setNodeMaterial(planeNode, opaqueMat);
            enginetest::frames(e, 6);
            std::vector<uint32_t> ids;
            unsigned opaque = 0;
            if (readIds(e, view, ids))
                for (uint32_t x : ids) opaque += (x & AtomId::kSlotMask) == planeSlot;
            scene->setNodeMaterial(planeNode, cutMat);
            // the mask is made the first frame its texture is resident and the slot re-routes the frame after
            enginetest::frames(e, 6);
            const AtomDrawStatus st = scene->atomDrawStatus();
            unsigned masked = 0;
            if (readIds(e, view, ids))
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

    }

    // (c) THE ADDRESS MODE: the clamped quad over the same floor, read at every texel cell's centre
    if (clampRow) {
        std::vector<unsigned char> cx(16u * 16u * 4u);
        for (unsigned y = 0; y < 16u; ++y)
            for (unsigned x = 0; x < 16u; ++x) {
                unsigned char *q = &cx[(y * 16u + x) * 4u];
                q[0] = 120; q[1] = 200; q[2] = 90;
                q[3] = x >= 8u ? 255 : 0;
            }
        PbrParams kp = cp;
        kp.uvScale[0] = kp.uvScale[1] = 1.5f;
        kp.address[size_t(PbrTextureSlot::Albedo)] = PbrParams::AddressMode::Clamp;
        const MaterialId clampMat = scene->createPbrMaterial(kp);
        scene->setPbrTexture(clampMat, PbrTextureSlot::Albedo, scene->createTexture(16, 16, cx.data(), true));
        // 24 cells x 8 px = 192 px = 1.5 m at 128 px a metre: the 2 m plane at scale 0.75, its left edge at x 384, top 174
        scene->setNodeTransform(planeNode, Vec3(0.0f, 0.0f, 0.0f), Quat(), Vec3(0.75f, 0.75f, 1.0f));
        scene->setNodeMaterial(planeNode, clampMat);
        enginetest::frames(e, 12);
        std::vector<uint32_t> ids;
        unsigned covered = 0, expected = 0, wrongCells = 0, band = 0, bandExpected = 0;
        const bool read = readIds(e, view, ids);
        for (unsigned j = 0; read && j < 24u; ++j)
            for (unsigned i = 0; i < 24u; ++i) {
                const unsigned px = 384u + 8u * i + 4u, py = 174u + 8u * j + 4u;
                const bool got = (ids[size_t(py) * kW + px] & AtomId::kSlotMask) == planeSlot &&
                                 ids[size_t(py) * kW + px] != AtomId::kEmpty;
                const bool want = std::min(i, 15u) >= 8u;   // the clamped texel's alpha
                covered += got;
                expected += want;
                wrongCells += got != want;
                if (i >= 16u) { band += got; bandExpected += want; }
            }
        std::printf("   (c) CLAMP, uv 0..1.5: %u of 576 texel cells covered (the material's alpha test: %u = %.5f; read %.5f); "
                    "the band u > 1: %u of %u expected; %u cells differ\n", covered, expected, double(expected) / 576.0,
                    double(covered) / 576.0, band, bandExpected, wrongCells);
        CHECK_MSG(read && wrongCells == 0u,
                  "(c) THE ADDRESS MODE: a CLAMP slot's mask equals its alpha test at every texel centre of a quad with uv to 1.5 "
                  "(%u of 576 cells differ; covered %.5f, the law %.5f)", wrongCells, double(covered) / 576.0,
                  double(expected) / 576.0);
    }

    // RESIDUE: the mask is nearest, the material bilinear - one convex-corner cell, printed
    if (!clampRow) {
        std::vector<unsigned char> hx1(16u * 16u * 4u);
        for (unsigned y = 0; y < 16u; ++y)
            for (unsigned x = 0; x < 16u; ++x) {
                unsigned char *q = &hx1[(y * 16u + x) * 4u];
                q[0] = 240; q[1] = 240; q[2] = 240;
                q[3] = (x == 8u && y == 8u) ? 0 : 255;
            }
        const MaterialId holeMat = scene->createPbrMaterial(cp);
        scene->setPbrTexture(holeMat, PbrTextureSlot::Albedo, scene->createTexture(16, 16, hx1.data(), true));
        // 16 texels x 8 px = 128 px = 1 m: the 2 m plane at scale 0.5, left edge x 416, top y 206; the hole texel (8, 8) is
        // the pixels [480, 488) x [270, 278)
        scene->setNodeTransform(planeNode, Vec3(0.0f, 0.0f, 0.0f), Quat(), Vec3(0.5f, 0.5f, 1.0f));
        scene->setNodeMaterial(planeNode, holeMat);
        enginetest::frames(e, 12);
        std::vector<uint32_t> ids;
        const bool idsRead = readIds(e, view, ids);
        scene->setAtomDrawEnabled(false);
        enginetest::frames(e, 8);
        Image stock;
        const bool stockRead = view->readPixels(stock);
        scene->setAtomDrawEnabled(true);
        enginetest::frames(e, 4);
        if (idsRead && stockRead && stock.rgba.size() == size_t(kW) * kH * 4u) {
            const unsigned char *floorPx = &stock.rgba[(size_t(100) * kW + 100) * 4u];
            auto stockCovers = [&](unsigned x, unsigned y) {
                const unsigned char *q = &stock.rgba[(size_t(y) * kW + x) * 4u];
                int d = 0;
                for (int c = 0; c < 3; ++c) d = std::max(d, std::abs(int(q[c]) - int(floorPx[c])));
                return d > 24;
            };
            auto maskCovers = [&](unsigned x, unsigned y) {
                const uint32_t w = ids[size_t(y) * kW + x];
                return w != AtomId::kEmpty && (w & AtomId::kSlotMask) == planeSlot;
            };
            const int corners[4][4] = { { 480, 270, 1, 1 }, { 487, 270, -1, 1 }, { 480, 277, 1, -1 }, { 487, 277, -1, -1 } };
            unsigned holeMask = 0, holeStock = 0;
            for (unsigned y = 270; y < 278; ++y)
                for (unsigned x = 480; x < 488; ++x) {
                    holeMask += !maskCovers(x, y);
                    holeStock += !stockCovers(x, y);
                }
            std::printf("   RESIDUE: the mask is nearest, the material bilinear - one hole texel at 8 px a texel: the mask's hole "
                        "%u px (64 = the square), the stock alpha test's %u px\n", holeMask, holeStock);
            for (const auto &c : corners) {
                unsigned n = 0;
                for (int k = 0; k < 8; ++k) {
                    const unsigned x = unsigned(c[0] + c[2] * k), y = unsigned(c[1] + c[3] * k);
                    if (!maskCovers(x, y) && stockCovers(x, y)) ++n;
                    else break;
                }
                std::printf("   RESIDUE: corner (%d, %d): %u diagonal px the stock covers and the mask does not = %.3f texel an "
                            "axis (the closed form 0.207 = 1.66 px; 0.293 texel along the diagonal)\n", c[0], c[1], n,
                            double(n) / 8.0);
            }
        } else {
            std::printf("   RESIDUE: not read (ids %d, stock picture %d)\n", int(idsRead), int(stockRead));
        }
    }

    enginetest::destroyView(e, view);
    e->destroyScene(scene);
    engine.reset();
    std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
