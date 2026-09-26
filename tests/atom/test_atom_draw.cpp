// engine.atom_draw — THE VISIBILITY BUFFER AS THE PRODUCT'S OPAQUE PATH (ATOM S3-DRAW;
// SPECS/atom/D3_S3_DRAW_DESIGN.md §3, SPECS/briefs/ATOM-S3-DRAW.md §6).
//
// engine.atom_parity proves the DECODE over a hand-made id buffer in workspaces of its
// own; this suite proves the PRODUCT chain: an ordinary offscreen view of the parity
// grid, whose chain carries the id pass (the GPU cull's indirect draws, no material)
// and whose prepass and opaque pass skip the Atom queue and shade it through the
// screen decode. The reference is the SAME view with the split's measurement door
// shut (Scene::setAtomDrawEnabled(false): every item drawn by stock PBS).
//   (a) the id pass's ids equal the CPU rasteriser's (P-A's reference: the 1/256 snap,
//       the top-left rule, back faces culled) on the covered interior;
//   (b) the decoded product picture against the PBS picture, per cell, inside the
//       parity grid's own bar (mean <= 1.0 code, <= 0.5 % of interior pixels beyond 8)
//       under the direct, VCT + field and VCT + field + gather arms — the gather arm
//       is the PREPASS chain (the decode drawn in the prepass for the G-buffer and in
//       the opaque pass for colour), the direct arm the PASSTHROUGH shape;
//   (c) the split's stat: every grid cell atom, and a blended, an alpha-tested, a
//       two-sided, a row-less (line) and a backdrop item each counted where it belongs;
//   (d) buckets against materials over the grid;
//   (e) at Medium, the closed room's probe grid reaches the glossy metal cell through
//       the decode exactly as through PBS (the parity audit's PCC blocker, closed by
//       the per-scene binding: HlmsAtom binds SceneGiBinding.pcc like PBS);
//   (f) 640 buckets — past one 512-slot pool of twins — each shading only its own
//       pixels (the bucket id, never the twin's slot);
//   (g) an ORTHOGRAPHIC view: the id pass's CLUSTER CUT takes no distance term and draws
//       the depth whose group the window affords (a hand-made DAG whose group errors are
//       the chain's bounds, so it agrees with the CPU strategy's level);
//   (h) so does a LETTERBOXED one (the inset's rows are the currency).
// FRAMES, NEVER TIME: every picture is read until two consecutive reads agree to a code.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include "EnginePrivate.h"
#include "GpuScene.h"
#include "HlmsAtom.h"

#include <Compositor/OgreCompositorNode.h>
#include <Compositor/OgreCompositorWorkspace.h>
#include <OgreImage2.h>
#include <OgreItem.h>
#include <OgreSceneNode.h>
#include <OgreSceneManager.h>
#include <OgreRoot.h>
#include <OgreTextureBox.h>
#include <OgreMesh2.h>
#include <OgreSubMesh2.h>
#include <Vao/OgreAsyncTicket.h>
#include <Vao/OgreIndexBufferPacked.h>
#include <Vao/OgreVertexArrayObject.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace jahshaka::engine;
using jahshaka::engine::detail::AtomId;
using jahshaka::engine::detail::HlmsAtom;
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
        char buf_[512];                                                         \
        std::snprintf(buf_, sizeof(buf_), fmt, __VA_ARGS__);                    \
        CHECK(cond, buf_);                                                      \
    } while (0)

static const unsigned kW = 960, kH = 540;
static const double kMeanBar = 1.0, kTailBar = 0.005;
static const int kTailCodes = 8;

// ---------------------------------------------------------------------------
// THE FIXTURE
// ---------------------------------------------------------------------------
struct Geometry {
    std::vector<float> pos;          // xyz per vertex (object space)
    std::vector<unsigned> idx;
};

/// A UV sphere, the last column and the last ring WRAPPED to the first by index
/// (DOCS/traps/ENGINE.md, "procedural seams do not weld").
static MeshData sphereMesh(int seg, int ring, float r, Geometry &g)
{
    MeshData d;
    for (int j = 0; j <= ring; ++j) {
        const float th = float(j) / float(ring) * 3.14159265f;
        for (int i = 0; i <= seg; ++i) {
            const float ph = float(i) / float(seg) * 6.28318531f;
            const float x = std::sin(th) * std::cos(ph), y = std::cos(th), z = -std::sin(th) * std::sin(ph);
            d.positions.insert(d.positions.end(), { r * x, r * y, r * z });
            d.normals.insert(d.normals.end(), { x, y, z });
            d.uvs.insert(d.uvs.end(), { float(i) / float(seg), float(j) / float(ring) });
        }
    }
    for (int j = 0; j < ring; ++j)
        for (int i = 0; i < seg; ++i) {
            const unsigned a = unsigned(j * (seg + 1) + i), b = a + 1u;
            const unsigned c = unsigned((j + 1) * (seg + 1) + i), e = c + 1u;
            d.indices.insert(d.indices.end(), { a, c, b, b, c, e });
        }
    g.pos = d.positions;
    g.idx = d.indices;
    return d;
}

/// THE GRAZING CELL: a dense wavy sheet (y = A sin(kx x) sin(kz z)) so the NORMAL
/// varies across it, laid as a floor the camera sees at ~80 degrees from its normal.
static MeshData wavySheet(int n, float size, float amp, float waves, Geometry &g)
{
    MeshData d;
    const float k = waves * 6.28318531f / size;
    for (int j = 0; j <= n; ++j)
        for (int i = 0; i <= n; ++i) {
            const float x = (float(i) / float(n) - 0.5f) * size;
            const float z = (float(j) / float(n) - 0.5f) * size;
            const float y = amp * std::sin(k * x) * std::sin(k * z);
            const float dx = amp * k * std::cos(k * x) * std::sin(k * z);
            const float dz = amp * k * std::sin(k * x) * std::cos(k * z);
            const float l = std::sqrt(dx * dx + 1.0f + dz * dz);
            d.positions.insert(d.positions.end(), { x, y, z });
            d.normals.insert(d.normals.end(), { -dx / l, 1.0f / l, -dz / l });
            d.uvs.insert(d.uvs.end(), { float(i) / float(n) * 4.0f, float(j) / float(n) * 4.0f });
        }
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            const unsigned a = unsigned(j * (n + 1) + i), b = a + 1u;
            const unsigned c = unsigned((j + 1) * (n + 1) + i), e = c + 1u;
            d.indices.insert(d.indices.end(), { a, c, b, b, c, e });
        }
    g.pos = d.positions;
    g.idx = d.indices;
    return d;
}

/// A HAND-MADE THREE-DEPTH CLUSTER DAG over `m`'s level 0 (ATOM-CLUSTER-CUT): the
/// leaves (level 0 in runs of 128 triangles) are members of group 0 (error 0.002),
/// the SAME triangles again as depth-1 clusters produced by group 0 and members of
/// group 1 (error 0.02), and again at depth 2, produced by group 1 and members of
/// the terminal group 2 — the level test's two bounds as the cut's two group errors,
/// so the depth the id pass draws is the level the old rule drew. Every group's
/// sphere is the mesh box's (centre, half-diagonal): the distance the level rule
/// measured. The TRIANGLES are the subject's, not the geometry's.
static void addHandDag(MeshData &m)
{
    float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
    for (size_t v = 0; v * 3 < m.positions.size(); ++v)
        for (int k = 0; k < 3; ++k) {
            lo[k] = std::min(lo[k], m.positions[v * 3 + size_t(k)]);
            hi[k] = std::max(hi[k], m.positions[v * 3 + size_t(k)]);
        }
    MeshClusterGroup g;
    for (int k = 0; k < 3; ++k) g.centre[k] = 0.5f * (lo[k] + hi[k]);
    g.radius = 0.5f * std::sqrt((hi[0] - lo[0]) * (hi[0] - lo[0]) + (hi[1] - lo[1]) * (hi[1] - lo[1]) +
                                (hi[2] - lo[2]) * (hi[2] - lo[2]));
    const float errors[3] = { 0.002f, 0.02f, FLT_MAX };
    for (int d = 0; d < 3; ++d) {
        g.depth = d;
        g.error = g.estimate = errors[d];
        m.clusterGroups.push_back(g);
    }
    const size_t run = 128u * 3u;
    for (int d = 0; d < 3; ++d)
        for (size_t first = 0; first < m.indices.size(); first += run) {
            MeshCluster c;
            c.firstIndex = unsigned(m.clusterIndices.size());
            const size_t n = std::min(run, m.indices.size() - first);
            m.clusterIndices.insert(m.clusterIndices.end(), m.indices.begin() + ptrdiff_t(first),
                                    m.indices.begin() + ptrdiff_t(first + n));
            c.indexCount = unsigned(n);
            c.group = d;
            c.refined = d == 0 ? -1 : d - 1;
            std::memcpy(c.centre, g.centre, sizeof(c.centre));
            c.radius = g.radius;
            m.clusters.push_back(c);
        }
}

/// Smooth, LOW-FREQUENCY textures (64x64, magnified on screen): a hard texel edge
/// under minification is aliasing both hosts sample differently, not a decode error.
static std::vector<unsigned char> smoothTexture(int kind)
{
    const int n = 64;
    std::vector<unsigned char> px(size_t(n) * n * 4u);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            const float u = float(x) / float(n) * 6.28318531f, v = float(y) / float(n) * 6.28318531f;
            unsigned char *p = &px[(size_t(y) * n + size_t(x)) * 4u];
            float r = 0, gch = 0, b = 0;
            switch (kind) {
            case 0:   // albedo: soft warm/cool bands
                r = 0.55f + 0.30f * std::sin(u * 2.0f);
                gch = 0.50f + 0.25f * std::sin(v * 3.0f + 1.0f);
                b = 0.45f + 0.30f * std::cos(u + v);
                break;
            case 1: { // normal map: gentle bumps, tangent space
                const float nx = 0.35f * std::cos(u * 3.0f), ny = 0.35f * std::cos(v * 3.0f);
                const float nz = std::sqrt(std::max(0.0f, 1.0f - nx * nx - ny * ny));
                r = nx * 0.5f + 0.5f; gch = ny * 0.5f + 0.5f; b = nz * 0.5f + 0.5f;
                break;
            }
            case 2:   // roughness (and metalness): a slow ramp
                r = gch = b = 0.35f + 0.45f * (0.5f + 0.5f * std::sin(u * 2.0f + v));
                break;
            default:  // emissive: a soft glow pattern
                r = 0.9f * (0.5f + 0.5f * std::sin(u * 2.0f));
                gch = 0.5f * (0.5f + 0.5f * std::cos(v * 2.0f));
                b = 0.2f;
                break;
            }
            p[0] = (unsigned char)std::lround(std::min(1.0f, std::max(0.0f, r)) * 255.0f);
            p[1] = (unsigned char)std::lround(std::min(1.0f, std::max(0.0f, gch)) * 255.0f);
            p[2] = (unsigned char)std::lround(std::min(1.0f, std::max(0.0f, b)) * 255.0f);
            p[3] = 255;
        }
    return px;
}

struct Cell {
    std::string name;
    NodeId node = 0;
    MaterialId material = 0;
    unsigned slot = 0xFFFFFFFFu;
    const Geometry *geom = nullptr;
    float world[12] = {};
};

// ---------------------------------------------------------------------------
// THE HAND-MADE ID BUFFER — P-A's reference rasteriser (spikes/atom-stage0/src/
// AtomFixture.cpp): the vertex transform in the vertex shader's own order (world,
// then viewProj — never a premultiplied matrix), snapped to the device's 1/256 grid,
// the top-left rule, back faces culled (the default macroblock), nearest wins.
// ---------------------------------------------------------------------------
static const int64_t kSub = 256;

static bool rasteriseCell(const Cell &c, const float vp[4][4], unsigned cellSlot,
                          std::vector<uint32_t> &ids0, std::vector<uint32_t> &ids1,
                          std::vector<double> &invW)
{
    const Geometry &g = *c.geom;
    const size_t tris = g.idx.size() / 3u;
    for (size_t t = 0; t < tris; ++t) {
        float sx[3], sy[3], iw[3];
        bool ok = true;
        for (int k = 0; k < 3; ++k) {
            const float *p = &g.pos[size_t(g.idx[t * 3u + size_t(k)]) * 3u];
            float w4[4] = { 0, 0, 0, 1 };
            for (int r = 0; r < 3; ++r)
                w4[r] = c.world[r * 4 + 0] * p[0] + c.world[r * 4 + 1] * p[1] +
                        c.world[r * 4 + 2] * p[2] + c.world[r * 4 + 3];
            float c4[4];
            for (int r = 0; r < 4; ++r)
                c4[r] = vp[r][0] * w4[0] + vp[r][1] * w4[1] + vp[r][2] * w4[2] + vp[r][3] * w4[3];
            if (c4[3] <= 1e-5f) { ok = false; break; }
            // Ogre's Vulkan viewport has a NEGATIVE height: y is flipped here.
            sx[k] = (c4[0] / c4[3] * 0.5f + 0.5f) * float(kW);
            sy[k] = (0.5f - c4[1] / c4[3] * 0.5f) * float(kH);
            iw[k] = 1.0f / c4[3];
        }
        if (!ok) continue;
        const float area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]);
        if (area >= 0.0f) continue;   // a back face (CULL_CLOCKWISE) or degenerate
        const int ord[3] = { 0, 2, 1 };
        int64_t fx[3], fy[3];
        double w[3];
        for (int k = 0; k < 3; ++k) {
            fx[k] = int64_t(std::llround(double(sx[ord[k]]) * double(kSub)));
            fy[k] = int64_t(std::llround(double(sy[ord[k]]) * double(kSub)));
            w[k] = double(iw[ord[k]]);
        }
        const int64_t area2 = (fx[1] - fx[0]) * (fy[2] - fy[0]) - (fx[2] - fx[0]) * (fy[1] - fy[0]);
        if (area2 <= 0) continue;
        int64_t ex[3], ey[3], bias[3];
        for (int k = 0; k < 3; ++k) {
            const int k1 = (k + 1) % 3;
            ex[k] = fx[k1] - fx[k];
            ey[k] = fy[k1] - fy[k];
            bias[k] = (ey[k] < 0 || (ey[k] == 0 && ex[k] > 0)) ? 0 : -1;
        }
        const float xmin = std::min({ sx[0], sx[1], sx[2] }), xmax = std::max({ sx[0], sx[1], sx[2] });
        const float ymin = std::min({ sy[0], sy[1], sy[2] }), ymax = std::max({ sy[0], sy[1], sy[2] });
        const int x0 = std::max(0, int(std::floor(xmin)) - 1), x1 = std::min(int(kW) - 1, int(std::ceil(xmax)) + 1);
        const int y0 = std::max(0, int(std::floor(ymin)) - 1), y1 = std::min(int(kH) - 1, int(std::ceil(ymax)) + 1);
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                const int64_t px = int64_t(x) * kSub + kSub / 2, py = int64_t(y) * kSub + kSub / 2;
                int64_t e[3];
                bool in = true;
                for (int k = 0; k < 3; ++k) {
                    e[k] = ex[k] * (py - fy[k]) - ey[k] * (px - fx[k]);
                    if (e[k] + bias[k] < 0) in = false;
                }
                if (!in) continue;
                // The weight of corner k is the edge function opposite it; the
                // nearest surface has the LARGEST interpolated 1/w.
                const double inv = (double(e[1]) * w[0] + double(e[2]) * w[1] + double(e[0]) * w[2]) /
                                   double(area2);
                const size_t p = size_t(y) * kW + size_t(x);
                if (inv > invW[p]) {
                    invW[p] = inv;
                    // THE CUT'S ENCODING (HlmsAtom.h AtomId): these meshes carry no baked
                    // DAG, so the engine gives each a FLAT one — level 0 in runs of
                    // GpuScene::kFlatClusterTriangles, every run a leaf (depth 0) — and
                    // level-0 triangle t is triangle t % run of cluster t / run.
                    ids0[p] = AtomId::packX(cellSlot, 0u);
                    ids1[p] = AtomId::packY(uint32_t(t) / jahshaka::engine::detail::GpuScene::kFlatClusterTriangles,
                                            uint32_t(t) % jahshaka::engine::detail::GpuScene::kFlatClusterTriangles);
                }
            }
    }
    return true;
}


// ---------------------------------------------------------------------------
// READBACKS
// ---------------------------------------------------------------------------
static bool readIds(OgreView *v, std::vector<uint32_t> &x, std::vector<uint32_t> &y)
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
    x.assign(size_t(kW) * kH, 0u);
    y.assign(size_t(kW) * kH, 0u);
    for (unsigned r = 0; r < kH; ++r) {
        const auto *row = reinterpret_cast<const uint32_t *>(box.at(0, r, 0));
        for (unsigned c = 0; c < kW; ++c) {
            x[size_t(r) * kW + c] = row[c * 2u];
            y[size_t(r) * kW + c] = row[c * 2u + 1u];
        }
    }
    return true;
}

static int maxDiff(const unsigned char *a, const unsigned char *b)
{
    int m = 0;
    for (int c = 0; c < 3; ++c) m = std::max(m, std::abs(int(a[c]) - int(b[c])));
    return m;
}

/// Frames until two consecutive reads agree to a code everywhere (<= 1200 frames).
static bool settle(Engine *e, View *view, Image &img, int &frames)
{
    Image prev;
    int stable = 0;
    frames = 0;
    while (frames < 1200) {
        for (int i = 0; i < 4; ++i) e->renderOneFrame();
        frames += 4;
        if (!view->readPixels(img)) return false;
        if (frames >= 24 && prev.rgba.size() == img.rgba.size()) {
            int worst = 0;
            for (size_t i = 0; i < img.rgba.size(); i += 4) worst = std::max(worst, maxDiff(&img.rgba[i], &prev.rgba[i]));
            stable = worst <= 1 ? stable + 1 : 0;
            if (stable >= 2) return true;
        }
        prev = img;
    }
    return false;
}

struct Stat {
    size_t n = 0, beyond = 0;
    double mean = 0.0;
    int worst = 0;
};

int main()
{
    std::printf("== engine.atom_draw: the product chain's id pass + screen decode against the same view "
                "through stock PBS (per cell: mean <= %.1f code, <= %.1f %% beyond %d codes)\n",
                kMeanBar, kTailBar * 100.0, kTailCodes);
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-atom-draw-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();

    View *view = e->createOffscreenView("atomdraw", kW, kH, Colour(0, 0, 0));
    view->setOffscreenContract(OffscreenContract::StillPicture);
    Scene *scene = e->createScene("atomdraw");
    view->setScene(scene);
    enginetest::testCameraLookAt(view, Vec3(0.0f, 1.55f, 7.2f), Vec3(0.0f, 0.55f, 0.0f));
    {
        PostFxDesc fx;
        fx.allowOffscreen = true;
        fx.ssr = 0;
        view->setPostFx(fx);
    }
    scene->setLodBias(0.0f);   // level 0 everywhere: the CPU raster names level 0
    scene->setAmbient(Colour(0.10f, 0.11f, 0.13f), Colour(0.05f, 0.05f, 0.05f));
    auto *ogreScene = static_cast<OgreScene *>(scene);
    auto *ogreView = static_cast<OgreView *>(view);

    TextureId tAlbedo, tNormal, tRough, tEmissive;
    {
        const auto a = smoothTexture(0), nm = smoothTexture(1), r = smoothTexture(2), em = smoothTexture(3);
        tAlbedo = scene->createTexture(64, 64, a.data(), true, false);
        tNormal = scene->createTexture(64, 64, nm.data(), false, false);
        tRough = scene->createTexture(64, 64, r.data(), false, false);
        tEmissive = scene->createTexture(64, 64, em.data(), true, false);
    }
    Geometry sphereGeom, sheetGeom;
    const MeshId sphere = scene->createMesh(sphereMesh(96, 48, 0.42f, sphereGeom));
    const MeshId sheet = scene->createMesh(wavySheet(160, 16.0f, 0.06f, 12.0f, sheetGeom));

    // ---- the parity grid's eleven cells ---------------------------------------
    std::vector<Cell> cells;
    const char *kinds[5] = { "base", "metal", "rough", "normal-map", "emissive" };
    for (int textured = 0; textured < 2; ++textured)
        for (int k = 0; k < 5; ++k) {
            PbrParams p;
            p.albedo = Colour(0.70f, 0.62f, 0.52f);
            p.roughness = 0.55f;
            if (k == 1) { p.albedo = Colour(0.62f, 0.50f, 0.32f); p.metalness = 1.0f; p.roughness = 0.30f; }
            if (k == 2) { p.roughness = 0.95f; }
            if (k == 3) { p.roughness = 0.40f; }
            if (k == 4) { p.emissive = Colour(0.20f, 0.10f, 0.03f); }
            const MaterialId m = scene->createPbrMaterial(p);
            if (k == 3) scene->setPbrTexture(m, PbrTextureSlot::Normal, tNormal);
            if (textured) {
                scene->setPbrTexture(m, PbrTextureSlot::Albedo, tAlbedo);
                if (k == 1) scene->setPbrTexture(m, PbrTextureSlot::Metalness, tRough);
                if (k == 1 || k == 2) scene->setPbrTexture(m, PbrTextureSlot::Roughness, tRough);
                if (k == 4) scene->setPbrTexture(m, PbrTextureSlot::Emissive, tEmissive);
            }
            Cell c;
            c.name = std::string(kinds[k]) + (textured ? "/tex" : "/const");
            c.node = scene->createNode();
            scene->setNodeTransform(c.node, Vec3(-2.2f + 1.1f * float(k), 0.45f + 1.0f * float(textured), 0.0f),
                                    Quat(), Vec3(1, 1, 1));
            scene->attachMesh(c.node, sphere, m);
            c.material = m;
            c.geom = &sphereGeom;
            cells.push_back(c);
        }
    {
        PbrParams p;
        p.albedo = Colour(0.55f, 0.58f, 0.60f);
        p.roughness = 0.35f;
        const MaterialId m = scene->createPbrMaterial(p);
        Cell c;
        c.name = "grazing/const";
        c.node = scene->createNode();
        scene->setNodeTransform(c.node, Vec3(0.0f, -0.05f, -3.0f), Quat(), Vec3(1, 1, 1));
        scene->attachMesh(c.node, sheet, m);
        c.material = m;
        c.geom = &sheetGeom;
        cells.push_back(c);
    }
    // ---- (c)'s four stay-on-PBS items, off the grid (behind the camera's frame) --
    auto extra = [&](const PbrParams &p, const Vec3 &pos, MeshId mesh) {
        const NodeId n = scene->createNode();
        scene->setNodeTransform(n, pos, Quat(), Vec3(0.3f, 0.3f, 0.3f));
        scene->attachMesh(n, mesh, scene->createPbrMaterial(p));
        return n;
    };
    {
        PbrParams blend;
        blend.alphaMode = PbrAlphaMode::Blend;
        blend.alpha = 0.5f;
        extra(blend, Vec3(-3.4f, 2.8f, -1.0f), sphere);
        PbrParams cut;
        cut.alphaMode = PbrAlphaMode::Cutout;
        extra(cut, Vec3(-2.4f, 2.8f, -1.0f), sphere);
        PbrParams twoSided;
        twoSided.twoSided = true;
        extra(twoSided, Vec3(-1.4f, 2.8f, -1.0f), sphere);
        const MeshId line = scene->createLineMesh({ Vec3(0, 0, 0), Vec3(1, 0, 0), Vec3(1, 1, 0) }, true);
        extra(PbrParams(), Vec3(2.4f, 2.8f, -1.0f), line);
        // A BACKDROP (the default ground's horizon plane is one): drawn by every
        // view, but in no world channel — the id pass's cull draws those only.
        scene->setNodeBackdrop(extra(PbrParams(), Vec3(3.4f, 2.8f, -1.0f), sphere), true);
    }

    // THE GROUND THAT RUNS PAST THE CAMERA: two triangles 200 m across under the
    // grid, so the id pass and the decode meet triangles with vertices BEHIND THE
    // EYE (clip w <= 0) — every product scene's ground does. Not a cell (the CPU
    // raster leaves it out); the whole-frame comparison below covers it.
    {
        MeshData g;
        g.positions = { -100.0f, 0.0f, -100.0f, 100.0f, 0.0f, -100.0f, 100.0f, 0.0f, 100.0f, -100.0f, 0.0f, 100.0f };
        g.normals = { 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0 };
        g.uvs = { 0, 0, 20, 0, 20, 20, 0, 20 };
        g.indices = { 0, 2, 1, 0, 3, 2 };
        PbrParams gp;
        gp.albedo = Colour(0.35f, 0.40f, 0.30f);
        gp.roughness = 0.7f;
        // ...TILED, like the default ground (the document's texture scale rides
        // the UV modifier macros, JahFog_piece_vs_piece_ps.any).
        gp.uvScale[0] = gp.uvScale[1] = 8.0f;
        const MaterialId gm = scene->createPbrMaterial(gp);
        // A FILE texture, streamed like the default scene's ground (tile.png): its
        // datablock bakes its textures frames after it is worn.
        const TextureId tile = scene->loadTexture(std::string(JAHSHAKA_TEST_SOURCE_DIR) +
                                                  "/app/content/textures/tile.png", true);
        scene->setPbrTexture(gm, PbrTextureSlot::Albedo, tile ? tile : tAlbedo);
        const NodeId gn = scene->createNode();
        scene->setNodeTransform(gn, Vec3(0.0f, -0.45f, 0.0f), Quat(), Vec3(1, 1, 1));
        scene->attachMesh(gn, scene->createMesh(g), gm);
    }

    const NodeId sunNode = enginetest::addDirectionalLight(scene, Vec3(-0.45f, -0.8f, -0.4f), 2.2f);
    const NodeId pointNode = scene->createNode();
    scene->setNodeTransform(pointNode, Vec3(1.6f, 1.9f, 1.4f), Quat(), Vec3(1, 1, 1));
    LightDesc sunL, pointL;
    sunL.type = LightType::Directional;
    sunL.intensity = 0.24f / 3.14159265f;
    pointL.type = LightType::Point;
    pointL.colour = Colour(1.0f, 0.85f, 0.7f);
    pointL.range = 8.0f;
    pointL.intensity = 0.42f;
    scene->setLight(sunNode, sunL);
    scene->setLight(pointNode, pointL);

    for (int i = 0; i < 4; ++i) e->renderOneFrame();
    ogreScene->ensureGpuScene(false);
    const detail::GpuScene &gs = ogreScene->gpuScene();
    for (Cell &c : cells) {
        for (unsigned s = 0; s < gs.slotCount(); ++s)
            if (gs.entry(s).ids[0] == unsigned(c.node)) { c.slot = s; break; }
        if (c.slot != 0xFFFFFFFFu) std::memcpy(c.world, gs.entry(c.slot).world, sizeof(c.world));
    }

    // ---- (c) + (d): the split's stat ------------------------------------------
    {
        const AtomDrawStatus st = scene->atomDrawStatus();
        std::printf("  stat: on %d atom %u pbs %u (notWorld %u notPbs %u customPiece %u blended %u twoSided %u alphaTested %u "
                    "skinned %u noRow %u) stock %u | materials %u buckets %u twins %u screenDraws %u\n",
                    int(st.on), st.atomItems, st.pbsItems, st.notWorld, st.notPbs, st.customPiece, st.blended, st.twoSided,
                    st.alphaTested, st.skinned, st.noRow, st.stockItems, st.materials, st.buckets, st.twins,
                    st.screenDraws);
        CHECK(st.on, "(c) the split is live on this device (the id pass runs)");
        CHECK_MSG(st.atomItems == cells.size() + 1u, "(c) every grid cell and the ground are Atom items (%u of %zu)",
                  st.atomItems, cells.size() + 1u);
        CHECK_MSG(st.blended == 1 && st.alphaTested == 1 && st.twoSided == 1 && st.noRow == 1 && st.notWorld == 1 &&
                      st.pbsItems == 5,
                  "(c) the blended, alpha-tested, two-sided, row-less (line) and backdrop items stay on PBS, each "
                  "under its reason (pbs %u)", st.pbsItems);
        CHECK_MSG(st.materials == cells.size() + 1u && st.buckets < st.materials && st.buckets > 0,
                  "(d) the grid's and the ground's %u materials need %u decode draws (buckets)", st.materials,
                  st.buckets);
        CHECK_MSG(st.screenDraws == st.buckets, "(d) one screen decode draw per bucket (%u)", st.screenDraws);
        std::printf("  (c) multi-submesh item: HOLE — no mesh this engine builds has more than one submesh "
                    "(buildMeshV2), so none can be constructed; the route folds it into noRow\n");
    }

    // ---- the CPU raster of the cells, from the engine's own index order ---------
    std::map<uint32_t, Geometry> drawn;
    for (Cell &c : cells) {
        uint32_t meshIndex = 0;
        std::memcpy(&meshIndex, &gs.entry(c.slot).boundsMin[3], sizeof(meshIndex));
        auto it = drawn.find(meshIndex);
        if (it == drawn.end()) {
            Geometry g;
            const Ogre::MeshPtr &m = gs.meshAt(meshIndex);
            Ogre::VertexArrayObject *vao = m->getSubMesh(0)->mVao[Ogre::VpNormal][0];
            size_t src = 0, off = 0;
            vao->findBySemantic(Ogre::VES_POSITION, src, off);
            Ogre::VertexBufferPacked *vb = vao->getVertexBuffers()[src];
            {
                Ogre::AsyncTicketPtr t = vb->readRequest(0, vb->getNumElements());
                const auto *bytes = static_cast<const unsigned char *>(t->map());
                g.pos.resize(vb->getNumElements() * 3u);
                for (size_t v = 0; v < vb->getNumElements(); ++v)
                    std::memcpy(&g.pos[v * 3u], bytes + v * vb->getBytesPerElement() + off, 12u);
                t->unmap();
            }
            Ogre::IndexBufferPacked *ib = vao->getIndexBuffer();
            {
                const size_t first = vao->getPrimitiveStart(), count = vao->getPrimitiveCount();
                Ogre::AsyncTicketPtr t = ib->readRequest(first, count);
                const void *data = t->map();
                g.idx.resize(count);
                for (size_t i = 0; i < count; ++i)
                    g.idx[i] = ib->getIndexType() == Ogre::IndexBufferPacked::IT_16BIT
                                   ? unsigned(static_cast<const Ogre::uint16 *>(data)[i])
                                   : static_cast<const Ogre::uint32 *>(data)[i];
                t->unmap();
            }
            it = drawn.emplace(meshIndex, std::move(g)).first;
        }
        c.geom = &it->second;
    }
    Ogre::Camera *cam = ogreView->camera();
    const Ogre::Matrix4 viewProj = cam->getProjectionMatrixWithRSDepth() * cam->getViewMatrix(true);
    float vp[4][4];
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) vp[r][c] = float(viewProj[size_t(r)][size_t(c)]);
    std::vector<uint32_t> cpu0(size_t(kW) * kH, AtomId::kEmpty), cpu1(size_t(kW) * kH, 0u);
    std::vector<double> invW(size_t(kW) * kH, 0.0);
    for (const Cell &c : cells) rasteriseCell(c, vp, c.slot, cpu0, cpu1, invW);
    auto interior = [&](size_t p) {
        const unsigned x = unsigned(p % kW), y = unsigned(p / kW);
        if (x == 0 || y == 0 || x + 1 >= kW || y + 1 >= kH) return false;
        const uint32_t id = cpu0[p];
        if (id == AtomId::kEmpty) return false;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
                if (cpu0[size_t(int(y) + dy) * kW + size_t(int(x) + dx)] != id) return false;
        return true;
    };

    // ---- the arms --------------------------------------------------------------
    struct PhotonArm { const char *name; bool gi, gather, hdr; int ssr; };
    const PhotonArm arms[5] = { { "direct", false, false, false, 0 }, { "vct+field", true, false, false, 0 },
                                { "vct+field+gather", true, true, false, 0 },
                                { "vct+field+gather+hdr", true, true, true, 0 },
                                { "vct+field+ssr", true, false, false, 2 } };
    const bool quick = std::getenv("ATOM_DRAW_QUICK") != nullptr;
    int cellFails = 0, cellsRun = 0;
    const char *outDir = std::getenv("ATOM_DRAW_OUT");
    for (int ai = 0; ai < (quick ? 1 : 5); ++ai) {
        const PhotonArm &P = arms[ai];
        GiParams gi;
        if (P.gi) {
            gi.mode = GiMode::Vct;
            gi.quality = GiQuality::Medium;
            gi.cascades = true;
            gi.ddgi = GiToggle::On;
            gi.gather = P.gather ? GiToggle::On : GiToggle::Off;
        } else {
            gi.mode = GiMode::Off;
        }
        scene->setGlobalIllumination(gi);
        {
            PostFxDesc fx;
            fx.allowOffscreen = true;
            fx.ssr = P.ssr;
            fx.hdr = P.hdr;
            view->setPostFx(fx);
        }
        if (P.gather) {
            GatherTuning t;
            t.freezeFrameIndex = true;
            scene->setGatherTuning(t);
        }
        Image atomPic, pbsPic;
        int fa = 0, fb = 0;
        scene->setAtomDrawEnabled(true);
        const bool sa = settle(e, view, atomPic, fa);
        // (a) THE IDS, on the first arm (the passthrough shape) and the prepass arm.
        if (ai == 0 || P.gather || P.ssr) {
            std::vector<uint32_t> gx, gy;
            const bool read = readIds(ogreView, gx, gy);
            size_t n = 0, slotEq = 0, triEq = 0, covered = 0, gpuOnly = 0;
            for (size_t p = 0; p < cpu0.size(); ++p) {
                if (read && gx[p] != AtomId::kEmpty && cpu0[p] == AtomId::kEmpty) ++gpuOnly;
                if (cpu0[p] != AtomId::kEmpty) ++covered;
                if (!read || !interior(p)) continue;
                ++n;
                slotEq += gx[p] == cpu0[p];
                triEq += gx[p] == cpu0[p] && gy[p] == cpu1[p];
            }
            std::printf("  (a) [%s] ids: interior %zu, slot+level equal %zu, triangle equal %zu, gpu-only px %zu of %zu covered\n",
                        P.name, n, slotEq, triEq, gpuOnly, covered);
            CHECK_MSG(read && n > 10000 && slotEq == n && double(triEq) >= 0.995 * double(n),
                      "(a) [%s] the id pass's ids equal the CPU rasteriser's: slot+level on every interior pixel "
                      "(%zu/%zu), the triangle on %.3f %% (bar 99.5 %%)",
                      P.name, slotEq, n, n ? 100.0 * double(triEq) / double(n) : 0.0);
        }
        // THE PASSES REALLY SKIP THE QUEUE (a negative control, first arm): with the
        // decode left unarmed nothing draws the Atom items, so every cell reads the
        // background — a split whose skip did nothing would still show them (PBS).
        if (ai == 0) {
            setenv("JAHSHAKA_ATOM_DECODE_OFF", "1", 1);
            Image off;
            int fo = 0;
            settle(e, view, off, fo);
            unsetenv("JAHSHAKA_ATOM_DECODE_OFF");
            size_t n = 0, dark = 0;
            for (size_t p = 0; p < cpu0.size() && off.rgba.size() == size_t(kW) * kH * 4u; ++p) {
                if (!interior(p)) continue;
                ++n;
                const unsigned char *px = &off.rgba[p * 4u];
                dark += px[0] == 0 && px[1] == 0 && px[2] == 0;
            }
            CHECK_MSG(n > 10000 && dark == n,
                      "(b) the view's passes skip the Atom queue: with the decode unarmed %zu of %zu interior "
                      "cell pixels read the background", dark, n);
            Image back;
            settle(e, view, back, fo);
        }
        scene->setAtomDrawEnabled(false);
        const bool sb = settle(e, view, pbsPic, fb);
        CHECK_MSG(sa && sb, "[%s] both pictures settled (%d / %d frames)", P.name, fa, fb);
        if (outDir) {
            for (int w = 0; w < 2; ++w) {
                const Image &im = w ? pbsPic : atomPic;
                const std::string path = std::string(outDir) + "/" + P.name + (w ? "_pbs.ppm" : "_atom.ppm");
                if (FILE *f = std::fopen(path.c_str(), "wb")) {
                    std::fprintf(f, "P6\n%u %u\n255\n", im.width, im.height);
                    for (size_t i = 0; i + 3 < im.rgba.size(); i += 4) std::fwrite(&im.rgba[i], 1, 3, f);
                    std::fclose(f);
                }
            }
        }
        // (b) PER CELL, interior pixels.
        std::vector<Stat> stats(cells.size());
        double whole = 0.0;
        size_t wholeN = 0;
        for (size_t p = 0; p < cpu0.size() && atomPic.rgba.size() == pbsPic.rgba.size(); ++p) {
            const unsigned char *a = &atomPic.rgba[p * 4u], *b = &pbsPic.rgba[p * 4u];
            whole += maxDiff(a, b);
            ++wholeN;
            if (!interior(p)) continue;
            size_t ci = 0;
            while (ci < cells.size() && cells[ci].slot != (cpu0[p] & AtomId::kSlotMask)) ++ci;
            if (ci == cells.size()) continue;
            Stat &s = stats[ci];
            const int d = maxDiff(a, b);
            s.mean += (std::abs(int(a[0]) - int(b[0])) + std::abs(int(a[1]) - int(b[1])) +
                       std::abs(int(a[2]) - int(b[2]))) / 3.0;
            s.beyond += d > kTailCodes;
            s.worst = std::max(s.worst, d);
            ++s.n;
        }
        for (size_t ci = 0; ci < cells.size(); ++ci) {
            Stat &s = stats[ci];
            if (s.n) s.mean /= double(s.n);
            const double tail = s.n ? double(s.beyond) / double(s.n) : 1.0;
            const bool pass = s.n > 200 && s.mean <= kMeanBar && tail <= kTailBar;
            ++cellsRun;
            if (!pass) ++cellFails;
            std::printf("  cell %-18s %-17s interior %7zu  mean %6.3f  >8 %6.3f %%  max %3d  %s\n",
                        cells[ci].name.c_str(), P.name, s.n, s.mean, tail * 100.0, s.worst, pass ? "ok" : "FAIL");
        }
        size_t wholeBeyond = 0;
        for (size_t p = 0; p < cpu0.size() && atomPic.rgba.size() == pbsPic.rgba.size(); ++p)
            wholeBeyond += maxDiff(&atomPic.rgba[p * 4u], &pbsPic.rgba[p * 4u]) > kTailCodes;
        const double wholeTail = wholeN ? double(wholeBeyond) / double(wholeN) : 1.0;
        std::printf("  [%s] whole frame: mean max-channel difference %.3f codes, %.3f %% beyond %d\n", P.name,
                    wholeN ? whole / double(wholeN) : -1.0, wholeTail * 100.0, kTailCodes);
        CHECK_MSG(wholeN && whole / double(wholeN) <= kMeanBar && wholeTail <= kTailBar,
                  "(b) [%s] the WHOLE frame (the ground behind the eye, the stay-on-PBS items, silhouettes) "
                  "inside the bar: mean %.3f, %.3f %% beyond %d", P.name, whole / double(wholeN),
                  wholeTail * 100.0, kTailCodes);
    }
    CHECK_MSG(cellFails == 0, "(b) the product decode against stock PBS: %d of %d cells inside the bar",
              cellsRun - cellFails, cellsRun);

    // ---- (e) THE PROBE GRID THROUGH THE DECODE (Medium, a CLOSED ROOM) ----------
    // The PCC is the product's configuration wherever a probe grid exists (the
    // VCT + probes hybrid binds a ParallaxCorrectedCubemap per scene, SceneGiBinding),
    // and probes keep only what they see enclosed, so the grid is measured in a
    // closed room against the same room without probes. The decode must show the
    // probes' term on the glossy metal cells exactly as PBS does: per cell inside the
    // bar in both arms, and the metal cells MOVE between the arms (the term is live).
    {
        std::vector<NodeId> room;
        const struct { Vec3 pos, scale; } slabs[6] = {
            { Vec3(0.0f, -0.35f, -1.0f), Vec3(17.0f, 0.2f, 21.0f) },
            { Vec3(0.0f, 5.0f, -1.0f), Vec3(17.0f, 0.2f, 21.0f) },
            { Vec3(-8.5f, 2.3f, -1.0f), Vec3(0.2f, 5.6f, 21.0f) },
            { Vec3(8.5f, 2.3f, -1.0f), Vec3(0.2f, 5.6f, 21.0f) },
            { Vec3(0.0f, 2.3f, -11.5f), Vec3(17.0f, 5.6f, 0.2f) },
            { Vec3(0.0f, 2.3f, 9.5f), Vec3(17.0f, 5.6f, 0.2f) } };
        for (const auto &sl : slabs) {
            const NodeId n = enginetest::addTestCube(scene, Colour(0.55f, 0.35f, 0.30f), 0.0f, 0.9f);
            enginetest::setNodePosition(scene, n, sl.pos);
            enginetest::setNodeScale(scene, n, sl.scale);
            room.push_back(n);
        }
        {
            PostFxDesc fx;
            fx.allowOffscreen = true;
            fx.ssr = 0;
            view->setPostFx(fx);
        }
        Image atomArm[2], pbsArm[2];
        for (int pcc = 0; pcc < 2; ++pcc) {
            GiParams gi;
            gi.mode = pcc ? GiMode::VctPccHybrid : GiMode::Vct;
            gi.quality = GiQuality::Medium;
            gi.cascades = true;
            gi.ddgi = GiToggle::On;
            gi.gather = GiToggle::Off;
            scene->setGlobalIllumination(gi);
            const char *name = pcc ? "room/vct+field+pcc" : "room/vct+field";
            int fa = 0, fb = 0;
            scene->setAtomDrawEnabled(true);
            const bool sa = settle(e, view, atomArm[pcc], fa);
            scene->setAtomDrawEnabled(false);
            const bool sb = settle(e, view, pbsArm[pcc], fb);
            CHECK_MSG(sa && sb, "(e) [%s] both pictures settled (%d / %d frames)", name, fa, fb);
            if (outDir) {
                for (int w = 0; w < 2; ++w) {
                    const Image &im = w ? pbsArm[pcc] : atomArm[pcc];
                    const std::string path = std::string(outDir) + "/" + (pcc ? "room_pcc" : "room") +
                                             (w ? "_pbs.ppm" : "_atom.ppm");
                    if (FILE *f = std::fopen(path.c_str(), "wb")) {
                        std::fprintf(f, "P6\n%u %u\n255\n", im.width, im.height);
                        for (size_t i = 0; i + 3 < im.rgba.size(); i += 4) std::fwrite(&im.rgba[i], 1, 3, f);
                        std::fclose(f);
                    }
                }
            }
            const Image &a = atomArm[pcc], &b = pbsArm[pcc];
            for (const Cell &c : cells) {
                Stat st;
                for (size_t p = 0; p < cpu0.size() && a.rgba.size() == b.rgba.size() && !a.rgba.empty(); ++p) {
                    if (!interior(p) || (cpu0[p] & AtomId::kSlotMask) != c.slot) continue;
                    const unsigned char *x = &a.rgba[p * 4u], *y = &b.rgba[p * 4u];
                    const int d = maxDiff(x, y);
                    st.mean += (std::abs(int(x[0]) - int(y[0])) + std::abs(int(x[1]) - int(y[1])) +
                                std::abs(int(x[2]) - int(y[2]))) / 3.0;
                    st.beyond += d > kTailCodes;
                    st.worst = std::max(st.worst, d);
                    ++st.n;
                }
                if (st.n) st.mean /= double(st.n);
                const double tail = st.n ? double(st.beyond) / double(st.n) : 1.0;
                const bool pass = st.n > 200 && st.mean <= kMeanBar && tail <= kTailBar;
                std::printf("  cell %-18s %-19s interior %7zu  mean %6.3f  >8 %6.3f %%  max %3d  %s\n",
                            c.name.c_str(), name, st.n, st.mean, tail * 100.0, st.worst, pass ? "ok" : "FAIL");
                CHECK_MSG(pass, "(e) [%s] cell %s through the decode inside the bar (mean %.3f, %.3f %% beyond %d)",
                          name, c.name.c_str(), st.mean, tail * 100.0, kTailCodes);
            }
        }
        // THE TERM IS LIVE ON THE METAL CELLS, in both pictures, by the same amount.
        for (const Cell &c : cells) {
            if (c.name.rfind("metal/", 0) != 0) continue;
            double moveAtom = 0.0, movePbs = 0.0;
            size_t n = 0;
            for (size_t p = 0; p < cpu0.size() && atomArm[0].rgba.size() == atomArm[1].rgba.size() &&
                               pbsArm[0].rgba.size() == pbsArm[1].rgba.size() && !atomArm[0].rgba.empty();
                 ++p) {
                if (!interior(p) || (cpu0[p] & AtomId::kSlotMask) != c.slot) continue;
                moveAtom += maxDiff(&atomArm[1].rgba[p * 4u], &atomArm[0].rgba[p * 4u]);
                movePbs += maxDiff(&pbsArm[1].rgba[p * 4u], &pbsArm[0].rgba[p * 4u]);
                ++n;
            }
            if (n) { moveAtom /= double(n); movePbs /= double(n); }
            CHECK_MSG(n > 200 && moveAtom > 0.25 && std::fabs(moveAtom - movePbs) <= kMeanBar,
                      "(e) the probes reach the glossy %s cell through the decode (mean move %.3f codes, PBS %.3f, "
                      "over %zu px)", c.name.c_str(), moveAtom, movePbs, n);
        }
        for (NodeId n : room) scene->setNodeVisible(n, false);
    }

    // ---- (f) PAST ONE POOL OF TWINS: 640 buckets ----------------------------------
    // A twin is a datablock in HlmsAtom's const-buffer pools, 512 slots each, so a
    // twin's SLOT repeats from the 513th bucket on. The decode must tell the buckets
    // apart by a whole id (the table's entry against the draw's own), or two draws
    // claim one pixel and the later one paints it. 640 tiles, each its own 1x1
    // texture — a texture set each, so a bucket each — against the same tiles through
    // stock PBS.
    {
        for (const Cell &c : cells) scene->setNodeVisible(c.node, false);
        GiParams off;
        off.mode = GiMode::Off;
        scene->setGlobalIllumination(off);
        {
            PostFxDesc fx;
            fx.allowOffscreen = true;
            fx.ssr = 0;
            view->setPostFx(fx);
        }
        const MeshId tileMesh = scene->createMesh(enginetest::unitCubeMesh());
        std::vector<NodeId> tiles;
        const int kCols = 32, kRows = 20;
        for (int r = 0; r < kRows; ++r)
            for (int q = 0; q < kCols; ++q) {
                const int i = r * kCols + q;
                const unsigned char rgba[4] = { (unsigned char)(40 + (i * 37) % 200),
                                                (unsigned char)(40 + (i * 91) % 200),
                                                (unsigned char)(40 + (i * 53) % 200), 255 };
                PbrParams tp;
                tp.roughness = 0.8f;
                const MaterialId m = scene->createPbrMaterial(tp);
                scene->setPbrTexture(m, PbrTextureSlot::Albedo, scene->createTexture(1, 1, rgba, true, false));
                const NodeId n = scene->createNode();
                scene->setNodeTransform(n, Vec3(-3.1f + 0.2f * float(q), 2.9f - 0.2f * float(r), -2.0f), Quat(),
                                        Vec3(0.18f, 0.18f, 0.18f));
                scene->attachMesh(n, tileMesh, m);
                tiles.push_back(n);
            }
        enginetest::testCameraLookAt(view, Vec3(0.0f, 1.0f, 5.2f), Vec3(0.0f, 1.0f, -2.0f));
        Image atomPic, pbsPic;
        int fa = 0, fb = 0;
        scene->setAtomDrawEnabled(true);
        const bool sa = settle(e, view, atomPic, fa);
        const AtomDrawStatus st = scene->atomDrawStatus();
        scene->setAtomDrawEnabled(false);
        const bool sb = settle(e, view, pbsPic, fb);
        double whole = 0.0;
        size_t n = 0, beyond = 0;
        for (size_t p = 0; p + 3 < atomPic.rgba.size() && atomPic.rgba.size() == pbsPic.rgba.size(); p += 4) {
            const int d = maxDiff(&atomPic.rgba[p], &pbsPic.rgba[p]);
            whole += d;
            beyond += d > kTailCodes;
            ++n;
        }
        if (outDir) {
            for (int w = 0; w < 2; ++w) {
                const Image &im = w ? pbsPic : atomPic;
                const std::string path = std::string(outDir) + (w ? "/tiles_pbs.ppm" : "/tiles_atom.ppm");
                if (FILE *f = std::fopen(path.c_str(), "wb")) {
                    std::fprintf(f, "P6\n%u %u\n255\n", im.width, im.height);
                    for (size_t i = 0; i + 3 < im.rgba.size(); i += 4) std::fwrite(&im.rgba[i], 1, 3, f);
                    std::fclose(f);
                }
            }
        }
        CHECK_MSG(st.buckets > 512 && st.twins > 512 && st.screenDraws == st.buckets,
                  "(f) %u buckets, %u twins, %u screen draws: past one pool of twins", st.buckets, st.twins,
                  st.screenDraws);
        CHECK_MSG(sa && sb && n && whole / double(n) <= kMeanBar && double(beyond) / double(n) <= kTailBar,
                  "(f) %zu tiles, a bucket each, through the decode against PBS: mean %.3f codes, %.3f %% beyond %d",
                  tiles.size(), n ? whole / double(n) : -1.0, n ? 100.0 * double(beyond) / double(n) : 100.0,
                  kTailCodes);
        for (NodeId t : tiles) scene->setNodeVisible(t, false);
    }

    // ---- (g) AN ORTHOGRAPHIC VIEW: THE CUT TAKES NO DISTANCE TERM -------------------
    // One sample of an orthographic view is the same world length at every depth, so
    // the cluster rule has no distance term there (JahClusterCut.glsl's orthographic
    // case, the level walk's): the id pass's CUT must draw the depth whose group the
    // window affords — read from the id image's depth bits — and it agrees with the
    // level Ogre's CPU strategy takes for the same view (the chain's bounds are the hand
    // DAG's group errors). Two window sizes, two depths.
    {
        Geometry lg;
        MeshData lm = sphereMesh(48, 24, 0.5f, lg);
        lm.lodIndices = { lm.indices, lm.indices };      // same triangles: the LEVEL is the subject
        lm.lodErrors = { 0.002f, 0.02f };
        lm.lodBounds = { 0.002f, 0.02f };
        addHandDag(lm);
        PbrParams lp;
        lp.roughness = 0.6f;
        const NodeId ln = scene->createNode();
        scene->setNodeTransform(ln, Vec3(0.0f, 1.0f, -2.0f), Quat(), Vec3(1, 1, 1));
        scene->attachMesh(ln, scene->createMesh(lm), scene->createPbrMaterial(lp));
        Ogre::Item *litem = nullptr;
        {
            auto it = ogreScene->sceneManager()->getMovableObjectIterator("Item");
            while (it.hasMoreElements()) {
                auto *cand = static_cast<Ogre::Item *>(it.getNext());
                if (cand->getMesh() && cand->getMesh()->getNumLodLevels() == 3u) litem = cand;
            }
        }
        CHECK(litem != nullptr, "(g) the three-level sphere's Item is found");
        // The suite pins level 0 for the parity grid (lodBias 0, above); this arm is
        // about the rule, so the dial comes back to 1 for it and goes back to 0 after.
        scene->setLodBias(1.0f);
        // orthoSize is HALF the window: footprint = 2 * orthoSize / 540, so 2 m affords
        // 0.0074 (level 1: 0.002 <= e < 0.02) and 8 m affords 0.0296 (level 2).
        const struct { float half; unsigned want; } arms[2] = { { 2.0f, 1u }, { 8.0f, 2u } };
        for (const auto &A : arms) {
            CameraDesc oc = enginetest::testCameraDescLookAt(Vec3(0.0f, 1.0f, 6.0f), Vec3(0.0f, 1.0f, -2.0f));
            oc.orthographic = true;
            oc.orthoSize = A.half;
            view->setCamera(oc);
            scene->setAtomDrawEnabled(true);
            for (int i = 0; i < 8; ++i) e->renderOneFrame();
            ogreScene->ensureGpuScene(false);
            uint32_t slot = 0xFFFFFFFFu;
            for (unsigned sl = 0; sl < gs.slotCount(); ++sl)
                if (gs.entry(sl).ids[0] == unsigned(ln)) { slot = sl; break; }
            std::vector<uint32_t> gx, gy;
            const bool read = readIds(ogreView, gx, gy);
            std::map<unsigned, size_t> gpuLevels;
            for (size_t px = 0; read && px < gx.size(); ++px)
                if (gx[px] != AtomId::kEmpty && (gx[px] & AtomId::kSlotMask) == slot)
                    ++gpuLevels[AtomId::depthOf(gx[px])];
            scene->setAtomDrawEnabled(false);
            for (int i = 0; i < 8; ++i) e->renderOneFrame();
            const unsigned cpu = litem ? unsigned(litem->getCurrentMeshLod()) : 99u;
            const unsigned gpu = gpuLevels.size() == 1u ? gpuLevels.begin()->first : 98u;
            std::printf("  (g) ortho half-height %.1f m: the cut's depth %u (%zu depth(s) seen), CPU level %u\n",
                        A.half, gpu, gpuLevels.size(), cpu);
            CHECK_MSG(read && gpu == cpu && cpu == A.want,
                      "(g) orthographic, window %.0f m: the id pass's cut draws depth %u, the CPU strategy's level "
                      "(%u; wanted %u)", 2.0f * A.half, gpu, cpu, A.want);
        }
        scene->setNodeVisible(ln, false);
        scene->setLodBias(0.0f);
        enginetest::testCameraLookAt(view, Vec3(0.0f, 1.55f, 7.2f), Vec3(0.0f, 0.55f, 0.0f));
    }

    // The cut's DEPTH a node's pixels carry in the id image this frame (its slot's
    // depth bits over every pixel of it; 98 = more than one depth seen, 99 = no pixel).
    auto gpuLevelOf = [&](NodeId node) -> unsigned {
        ogreScene->ensureGpuScene(false);
        uint32_t slot = 0xFFFFFFFFu;
        for (unsigned sl = 0; sl < gs.slotCount(); ++sl)
            if (gs.entry(sl).ids[0] == unsigned(node)) { slot = sl; break; }
        std::vector<uint32_t> gx, gy;
        if (slot == 0xFFFFFFFFu || !readIds(ogreView, gx, gy)) return 99u;
        std::map<unsigned, size_t> seen;
        for (size_t px = 0; px < gx.size(); ++px)
            if (gx[px] != AtomId::kEmpty && (gx[px] & AtomId::kSlotMask) == slot) ++seen[AtomId::depthOf(gx[px])];
        return seen.empty() ? 99u : (seen.size() == 1u ? seen.begin()->first : 98u);
    };
    auto lodSphere = [&](const Vec3 &at, Ogre::Item **itemOut) -> NodeId {
        Geometry lg;
        MeshData lm = sphereMesh(48, 24, 0.5f, lg);
        lm.lodIndices = { lm.indices, lm.indices };
        lm.lodErrors = { 0.002f, 0.02f };
        lm.lodBounds = { 0.002f, 0.02f };
        addHandDag(lm);
        PbrParams lp;
        lp.roughness = 0.6f;
        const NodeId n = scene->createNode();
        scene->setNodeTransform(n, at, Quat(), Vec3(1, 1, 1));
        const MeshId mesh = scene->createMesh(lm);
        scene->attachMesh(n, mesh, scene->createPbrMaterial(lp));
        if (itemOut) {
            *itemOut = nullptr;
            auto it = ogreScene->sceneManager()->getMovableObjectIterator("Item");
            while (it.hasMoreElements()) {
                auto *cand = static_cast<Ogre::Item *>(it.getNext());
                if (cand->getParentSceneNode() && cand->getMesh() && cand->getMesh()->getNumLodLevels() == 3u &&
                    cand->getParentSceneNode()->_getDerivedPosition().distance(Ogre::Vector3(at.x, at.y, at.z)) < 1e-3f)
                    *itemOut = cand;
            }
        }
        return n;
    };

    // ---- (h) A LETTERBOXED VIEW: THE CUT SPENDS THE INSET'S ROWS ---------------------
    // The rule's currency is the pass's own rows (Viewport::getActualHeight, the inset
    // in a letterboxed chain), never the whole target's: the id pass once fed it the
    // full 540 rows while the casters' strategy read the 320-row inset. At 10 m from the
    // surface the inset affords 0.0259 (depth 2 / level 2) and the full target 0.0153
    // (depth 1).
    {
        scene->setLodBias(1.0f);
        Ogre::Item *hitem = nullptr;
        const NodeId hn = lodSphere(Vec3(0.0f, 1.0f, -2.0f), &hitem);
        CameraDesc lc = enginetest::testCameraDescLookAt(Vec3(0.0f, 1.0f, 8.5f), Vec3(0.0f, 1.0f, -2.0f));
        lc.constrainAspect = true;
        lc.aspect = 3.0f;
        view->setCamera(lc);
        scene->setAtomDrawEnabled(true);
        for (int i = 0; i < 8; ++i) e->renderOneFrame();
        const unsigned gpu = gpuLevelOf(hn);
        scene->setAtomDrawEnabled(false);
        for (int i = 0; i < 8; ++i) e->renderOneFrame();
        const unsigned cpu = hitem ? unsigned(hitem->getCurrentMeshLod()) : 97u;
        scene->setAtomDrawEnabled(true);
        std::printf("  (h) letterboxed 3:1: the cut's depth %u, CPU level %u\n", gpu, cpu);
        CHECK_MSG(gpu == cpu && cpu == 2u,
                  "(h) a letterboxed view: the id pass's cut draws depth %u, the CPU strategy's level (%u; wanted 2 "
                  "- the inset's rows)", gpu, cpu);
        scene->setNodeVisible(hn, false);
        scene->setLodBias(0.0f);
        view->setCamera(enginetest::testCameraDescLookAt(Vec3(0.0f, 1.55f, 7.2f), Vec3(0.0f, 0.55f, 0.0f)));
    }

    // (i) — THE LOD BAND'S PER-SLOT STATE — IS DELETED WITH THE BAND (ATOM-CLUSTER-CUT,
    // D4): the id pass draws the cut, which holds no state between frames at all (every
    // frame's frontier is a function of that frame's view), so there is nothing a
    // renumbered slot could inherit. The frontier's frame-to-frame stability is
    // atom.lod_switch's slow-zoom arm.

    // ---- (b) THE GROUND FROM A STANDING EYE: the grid hidden, the tiled, streamed
    // ground filling the frame and running past the camera (the default scene's
    // shot), GI off. Whole frame against PBS.
    {
        for (const Cell &c : cells) scene->setNodeVisible(c.node, false);
        GiParams off;
        off.mode = GiMode::Off;
        scene->setGlobalIllumination(off);
        {
            PostFxDesc fx;
            fx.allowOffscreen = true;
            fx.ssr = 0;
            view->setPostFx(fx);
        }
        enginetest::testCameraLookAt(view, Vec3(0.3f, 1.6f, 4.0f), Vec3(0.0f, 0.2f, -6.0f));
        // ...UNDER THE PRODUCT'S ENVIRONMENT: the analytic sky (its reflection
        // cube is every datablock's environment map) and distance fog.
        if (!std::getenv("ATOM_DRAW_NO_SKY")) {
            SkyDesc sky;
            sky.mode = SkyMode::Atmosphere;
            scene->setSky(sky);
        }
        if (!std::getenv("ATOM_DRAW_NO_FOG")) {
            FogDesc fog;
            fog.enabled = true;
            scene->setFog(fog);
        }
        Image atomPic, pbsPic;
        int fa = 0, fb = 0;
        scene->setAtomDrawEnabled(true);
        const bool sa = settle(e, view, atomPic, fa);
        scene->setAtomDrawEnabled(false);
        const bool sb = settle(e, view, pbsPic, fb);
        double whole = 0.0;
        size_t n = 0, beyond = 0;
        for (size_t p = 0; p + 3 < atomPic.rgba.size() && atomPic.rgba.size() == pbsPic.rgba.size(); p += 4) {
            const int d = maxDiff(&atomPic.rgba[p], &pbsPic.rgba[p]);
            whole += d;
            beyond += d > kTailCodes;
            ++n;
        }
        if (outDir) {
            for (int w = 0; w < 2; ++w) {
                const Image &im = w ? pbsPic : atomPic;
                const std::string path = std::string(outDir) + (w ? "/ground_pbs.ppm" : "/ground_atom.ppm");
                if (FILE *f = std::fopen(path.c_str(), "wb")) {
                    std::fprintf(f, "P6\n%u %u\n255\n", im.width, im.height);
                    for (size_t i = 0; i + 3 < im.rgba.size(); i += 4) std::fwrite(&im.rgba[i], 1, 3, f);
                    std::fclose(f);
                }
            }
        }
        CHECK_MSG(sa && sb && n && whole / double(n) <= kMeanBar && double(beyond) / double(n) <= kTailBar,
                  "(b) [ground] the tiled, streamed ground running past the eye, whole frame against PBS: mean "
                  "%.3f codes, %.3f %% beyond %d", n ? whole / double(n) : -1.0,
                  n ? 100.0 * double(beyond) / double(n) : 100.0, kTailCodes);
    }

    scene->setAtomDrawEnabled(true);
    for (int i = 0; i < 4; ++i) e->renderOneFrame();
    e->destroyView(view);
    e->destroyScene(scene);
    std::printf("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
