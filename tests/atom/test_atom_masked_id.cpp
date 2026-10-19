// atom.masked_id — A CUT-OUT IS AN ATOM ITEM: ITS IDS ARE ITS MASK (ENGINE-POLISH-ATOM-1 stage 3; shipped by ATOM-3
// stage 2 with the mask's coverage-preserving pyramid, SPECS/ATOM-3.md).
//
// A cut-out used to stay on stock PBS (the split's `alphaTested` reason): the id pass had no mask. Now the cut writes
// an alpha-tested item's command into the list's MASKED range, the id pass draws that range with a fragment stage
// that discards where the material's mask (the engine's store, AlphaMasks.h) is clear AT THE PYRAMID LEVEL the
// fragment's UV footprint asks for, and the decode's twin drops the alpha test: the id pass owns the coverage. The
// stock cut-out route is gone for an unskinned item. The fixture, at 1x: an ORTHOGRAPHIC view where one metre is 128
// pixels, a 2 x 2 m cut-out plane facing it on whole pixels (256 x 256, its corners on pixel edges), its albedo map an
// 8 x 8 CHECKER of 8-texel cells, alpha 0 / 1 at cutoff 0.5 — a pass fraction of one half by construction, every cell
// 32 x 32 pixels — over a lit floor, the sun casting.
//   (a) THE ROUTE: the plane is an Atom item, masked (atomMasked 1, alphaTested 0, pending 0);
//   (b) THE COVERAGE IS THE MASK'S: the plane's id pixels = 1/2 x its opaque twin's (the closed form 256 x 256 =
//       65,536, measured on the twin) within 0.5 %, and every hole pixel holds the floor's id;
//   (c) NO HOLE IN THE DECODE: every pixel the plane's ids cover is shaded (never the clear colour);
//   (d) THE SILHOUETTE AGAINST LEVEL 0 (the spec's bar): the plane MINIFIED (0.1 of its size: 2.5 mask texels a
//       pixel, the pyramid's level 1) through the shipped pyramid against the level-0 reference (the arm
//       atom.maskPyramid 0: the base's one-level nearest mask), the final depth and the sun maps through the coverage
//       door (Scene::coverageCompare): the view's mismatch bands <= 1 px and every sun map's <= 1 texel at the 99th
//       percentile. (THE BASE, ENGINE-POLISH-ATOM-1 stage 3's STOP 2 on Bistro: the one-level mask against the stock
//       cut-out, 2-5 px in the view and 3-19 texels in the maps.)
// FRAMES, NEVER TIME: every picture is read until two consecutive reads agree to a code.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

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
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace jahshaka::engine;
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

/// A 2 x 2 m plane in XY facing +z (counter-clockwise from +z), an n x n grid (enough triangles for a DAG), UVs 0..1
/// left to right and top to bottom.
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

static int maxDiff(const unsigned char *a, const unsigned char *b)
{
    int m = 0;
    for (int c = 0; c < 3; ++c) m = std::max(m, std::abs(int(a[c]) - int(b[c])));
    return m;
}

/// The frames this row rendered (its JAHSHAKA_TEST_FRAMES line, printed at the end).
static int gFrames = 0;

/// Frames until two consecutive reads agree to a code everywhere (<= 600 frames).
static bool settle(Engine *e, View *view, Image &img)
{
    Image prev;
    int stable = 0, frames = 0;
    while (frames < 600) {
        enginetest::frames(e, 4);
        frames += 4;
        gFrames += 4;
        if (!view->readPixels(img)) return false;
        if (frames >= 16 && prev.rgba.size() == img.rgba.size()) {
            int worst = 0;
            for (size_t i = 0; i < img.rgba.size(); i += 4) worst = std::max(worst, maxDiff(&img.rgba[i], &prev.rgba[i]));
            stable = worst <= 1 ? stable + 1 : 0;
            if (stable >= 2) return true;
        }
        prev = img;
    }
    return false;
}

static uint32_t slotOf(OgreScene *os, NodeId n)
{
    os->ensureGpuScene(false);
    const detail::GpuScene &gs = os->gpuScene();
    for (unsigned s = 0; s < gs.slotCount(); ++s)
        if (os->nodeOfGpuKey(gs.entry(s).ids[0]) == n) return s;
    return 0xFFFFFFFFu;
}

int main()
{
    std::printf("== atom.masked_id: a cut-out through the id pass's masked range - its ids are its mask, its decode "
                "has no hole, its minified silhouette level 0's\n");
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-atom-masked-id-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->submitFrame(jahshaka::engine::FrameInput::delta(1.0f / 60.0f));
    Engine *e = engine.get();
    View *view = e->createOffscreenView("maskedid", kW, kH, Colour(0, 0, 0));
    view->setOffscreenContract(OffscreenContract::StillPicture);
    Scene *scene = e->createScene("maskedid");
    view->setScene(scene);
    view->setShadows(true);   // the masked caster's map: the coverage door's second half
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
    NodeId floorNode = 0;
    {
        MeshData g;
        g.positions = { -20.0f, -20.0f, 0.0f, 20.0f, -20.0f, 0.0f, 20.0f, 20.0f, 0.0f, -20.0f, 20.0f, 0.0f };
        g.normals = { 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1 };
        g.uvs = { 0, 0, 1, 0, 1, 1, 0, 1 };
        g.indices = { 0, 1, 2, 0, 2, 3 };
        PbrParams fp;
        fp.albedo = Colour(0.30f, 0.45f, 0.30f);
        fp.roughness = 0.8f;
        floorNode = scene->createNode();
        scene->setNodeTransform(floorNode, Vec3(0.0f, 0.0f, -1.0f), Quat(), Vec3(1, 1, 1));
        scene->attachMesh(floorNode, scene->createMesh(g), scene->createPbrMaterial(fp));
    }
    // THE MASK: 64 x 64, an 8 x 8 checker of 8-texel cells, alpha 0 / 255 (pass fraction 1/2).
    std::vector<unsigned char> px(64u * 64u * 4u);
    for (unsigned y = 0; y < 64u; ++y)
        for (unsigned x = 0; x < 64u; ++x) {
            unsigned char *q = &px[(y * 64u + x) * 4u];
            q[0] = 210; q[1] = 140; q[2] = 70;
            q[3] = (((x / 8u) + (y / 8u)) & 1u) ? 255 : 0;
        }
    const TextureId mask = scene->createTexture(64, 64, px.data(), true);
    PbrParams cp;
    cp.alphaMode = PbrAlphaMode::Cutout;
    cp.alphaCutoff = 0.5f;
    cp.roughness = 0.6f;
    const MaterialId cutMat = scene->createPbrMaterial(cp);
    scene->setPbrTexture(cutMat, PbrTextureSlot::Albedo, mask);
    PbrParams op;
    op.albedo = Colour(0.82f, 0.55f, 0.27f);
    op.roughness = 0.6f;
    const MaterialId opaqueMat = scene->createPbrMaterial(op);
    const MeshId planeMesh = scene->createMesh(plane(24));
    const NodeId planeNode = scene->createNode();
    scene->setNodeTransform(planeNode, Vec3(0.0f, 0.0f, 0.0f), Quat(), Vec3(1, 1, 1));
    scene->attachMesh(planeNode, planeMesh, opaqueMat);
    enginetest::addDirectionalLight(scene, Vec3(-0.2f, -0.3f, -1.0f), 2.2f);

    // ---- THE OPAQUE TWIN: the plane's own pixel count (the closed form 256 x 256) -------------------------------
    Image img;
    CHECK(settle(e, view, img), "the opaque twin's picture settles");
    const uint32_t planeSlot = slotOf(os, planeNode), floorSlot = slotOf(os, floorNode);
    std::vector<uint32_t> ids;
    unsigned opaque = 0;
    unsigned x0 = kW, y0 = kH, x1 = 0, y1 = 0;
    if (readIds(ov, ids))
        for (unsigned r = 0; r < kH; ++r)
            for (unsigned c = 0; c < kW; ++c)
                if ((ids[size_t(r) * kW + c] & AtomId::kSlotMask) == planeSlot) {
                    ++opaque;
                    x0 = std::min(x0, c); y0 = std::min(y0, r); x1 = std::max(x1, c); y1 = std::max(y1, r);
                }
    std::printf("   the opaque twin: %u id pixels (closed form %u), x %u..%u y %u..%u\n", opaque, 256u * 256u, x0, x1,
                y0, y1);
    CHECK_MSG(opaque == 256u * 256u, "the plane covers 256 x 256 whole pixels (%u)", opaque);

    // ---- (a) THE CUT-OUT ON THE MASKED ROUTE -------------------------------------------------------------------
    scene->setNodeMaterial(planeNode, cutMat);
    CHECK(settle(e, view, img), "the cut-out's picture settles");
    {
        const AtomDrawStatus st = scene->atomDrawStatus();
        std::printf("   route: atom %u masked %u alphaTested %u pending %u\n", st.atomItems, st.atomMasked,
                    st.alphaTested, st.pending);
        CHECK_MSG(st.atomMasked == 1u && st.alphaTested == 0u && st.pending == 0u && st.atomItems == 2u,
                  "(a) the cut-out is an Atom item through the masked range (masked %u, alphaTested %u, pending %u)",
                  st.atomMasked, st.alphaTested, st.pending);
    }
    // ---- (b) THE COVERAGE ---------------------------------------------------------------------------------------
    std::vector<uint32_t> mids;
    unsigned masked = 0, holes = 0, holesFloor = 0;
    std::vector<unsigned char> coveredByPlane(size_t(kW) * kH, 0u);
    if (readIds(ov, mids))
        for (unsigned r = y0; r <= y1 && r < kH; ++r)
            for (unsigned c = x0; c <= x1 && c < kW; ++c) {
                const uint32_t s = mids[size_t(r) * kW + c] & AtomId::kSlotMask;
                if (s == planeSlot) {
                    ++masked;
                    coveredByPlane[size_t(r) * kW + c] = 1u;
                } else {
                    ++holes;
                    if (s == floorSlot) ++holesFloor;
                }
            }
    const double fraction = opaque ? double(masked) / double(opaque) : 0.0;
    std::printf("   the cut-out: %u id pixels of %u = %.5f (the mask's pass fraction 0.5); %u holes, %u of them the "
                "floor's\n", masked, opaque, fraction, holes, holesFloor);
    CHECK_MSG(std::fabs(fraction - 0.5) <= 0.0025, "(b) the id pass's coverage is the mask's: %.5f of the plane "
              "(0.5 within 0.5 %%)", fraction);
    CHECK_MSG(holes > 0u && holesFloor == holes, "(b) every hole holds the floor's id (%u of %u)", holesFloor, holes);
    // ---- (c) NO HOLE IN THE DECODE -------------------------------------------------------------------------------
    Image atomPic = img;
    unsigned unshaded = 0;
    for (size_t i = 0; i < coveredByPlane.size(); ++i)
        if (coveredByPlane[i] && atomPic.rgba[i * 4u] == 0 && atomPic.rgba[i * 4u + 1u] == 0 &&
            atomPic.rgba[i * 4u + 2u] == 0)
            ++unshaded;
    CHECK_MSG(unshaded == 0u, "(c) every pixel the cut-out's ids cover is shaded by the decode (%u unshaded)",
              unshaded);
    // ---- (d) THE SILHOUETTE AGAINST THE LEVEL-0 REFERENCE, minified -----------------------------------------------
    scene->setNodeTransform(planeNode, Vec3(0.13f, -0.07f, 0.0f), Quat(), Vec3(0.1f, 0.1f, 1.0f));
    Image minPic;
    CHECK(settle(e, view, minPic), "the minified cut-out's picture settles");
    CHECK(scene->coverageSnapshot(0u), "(d) the pyramid's depth and maps snapshot");
    e->setArm("atom.maskPyramid", 0.0);
    CHECK(settle(e, view, minPic), "the level-0 reference's picture settles");
    {
        CHECK(scene->coverageSnapshot(1u), "(d) the level-0 reference's depth and maps snapshot");
        CoverageCompare cc;
        CHECK(scene->coverageCompare(0.02f, 2.0f, false, cc) && cc.valid, "(d) the two snapshots compare");
        std::printf("   coverage, the pyramid vs level 0 (minified 10x): view %ux%u, %llu mismatches (band p50 %.1f p99 "
                    "%.1f max %.1f)\n", cc.width, cc.height, cc.viewMismatch, double(cc.viewBandP50),
                    double(cc.viewBandP99), double(cc.viewBandMax));
        unsigned mapsCompared = 0;
        bool mapsOk = true;
        for (const CoverageCompare::Map &m : cc.maps) {
            std::printf("   map %u %ux%u: same light %d, %llu mismatches, band p50 %.1f p99 %.1f max %.1f\n", m.map,
                        m.width, m.height, int(m.sameLight), m.mismatch, double(m.bandP50), double(m.bandP99),
                        double(m.bandMax));
            if (!m.sameLight) continue;
            ++mapsCompared;
            mapsOk = mapsOk && m.bandP99 <= 1.0f;
        }
        CHECK_MSG(cc.viewBandP99 <= 1.0f, "(d) the view's silhouette is level 0's within 1 px at the 99th percentile "
                  "(p99 %.1f, %llu mismatches)", double(cc.viewBandP99), cc.viewMismatch);
        CHECK_MSG(mapsCompared > 0u && mapsOk, "(d) every sun map's silhouette is level 0's within 1 texel at the 99th "
                  "percentile (%u maps)", mapsCompared);
    }
    e->setArm("atom.maskPyramid", 1.0);
    enginetest::frames(e, 2);
    gFrames += 2;
    std::printf("   frames rendered: %d\n", gFrames);
    e->destroyView(view);
    e->destroyScene(scene);
    engine.reset();
    std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
