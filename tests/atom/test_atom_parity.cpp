// engine.atom_parity — THE PARITY GRID, the permanent canary of the visibility
// buffer's decode (SPECS/atom/D1_HLMS_ATOM_AND_VISBUF_DESIGN.md §1, lane
// ATOM-S3-PARITY; the construction is stage 0's P-D, spikes/atom-stage0/FINDINGS.md §2).
//
// THE CLAIM. `HlmsAtom` — a derived HlmsPbs that replaces exactly one upstream piece
// (LoadMaterial) and inserts every other lighting piece of PBS's unchanged — decodes
// a pixel's (item slot, level, triangle) into the same interpolants the rasteriser
// hands PBS, so the SAME objects shaded by the decode and by stock HlmsPbs are the
// same picture: per cell, mean <= 1.0 code and <= 0.5 % of interior pixels beyond 8
// codes (the spike's bar), a GRAZING cell included (stage 0's one numerical debt:
// the tail was the interpolated normal at grazing incidence).
//
// THE GRID. Cells = the day-one front-end materials (base / metal / rough /
// normal-map / emissive, each untextured and textured) + a grazing cell (a wavy
// floor seen at ~80 degrees from its normal), under each light arm (the sun, one
// point, one spot, all three; shadows on, Forward Clustered) and each Photon arm (GI
// off; the VCT chain + the irradiance field; + the gather; SSR off). "Cards at hits"
// is a NAMED HOLE until PHOTON-CARDS-2 lands (printed, never stubbed). A Photon
// column must be LIVE in the compared picture: its reference must differ from the
// column before it, or the column proves nothing.
//
// THE CONSTRUCTION (the spike's). One engine scene; its offscreen view renders the
// frame, and with it the gather's irradiance (keyed by the scene manager, read at the
// fragment's own pixel). Two test workspaces on the SAME scene manager and camera,
// appended after the view's and so run in the same frame, render into two RGBA8
// sRGB targets of the view's size:
//   REFERENCE  the objects, drawn by stock HlmsPbs (the render queues the view draws)
//   DECODE     one full-screen AtomDecodeRenderable per decode twin, over a HAND-MADE
//              id buffer: the CPU rasterisation of the same instances (snapped to the
//              device's 1/256 sub-pixel grid, the top-left rule, P-A's reference)
// With the gather on, both carry the prepass the gather's piece needs
// (setUseDepthPrePass: the colour pass then reads the normal and the shadow term
// from the G-buffer its OWN prepass wrote — the decode's prepass is the decode).
//
// FRAMES, NEVER TIME: after every change the frame is re-rendered until both
// pictures stop moving (read every 4 frames; stable = two consecutive reads each
// within 1 code at every pixel of the reference and the decode), and only then
// compared. Textures are SINGLE-MIP and magnified (FINDINGS 2.3: a mipmapped grid
// can never be bit-close to a rasterised reference — the reference's LOD is a quad
// difference, the decode's the true derivative).
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include "EnginePrivate.h"
#include "HlmsAtom.h"
#include "validation_probe.h"

#include <Compositor/OgreCompositorManager2.h>
#include <Compositor/OgreCompositorNodeDef.h>
#include <Compositor/OgreCompositorWorkspace.h>
#include <Compositor/OgreCompositorWorkspaceDef.h>
#include <Compositor/OgreCompositorWorkspaceListener.h>
#include <Compositor/Pass/PassScene/OgreCompositorPassSceneDef.h>
#include <OgreCamera.h>
#include <OgreHlmsManager.h>
#include <OgreHlmsPbsDatablock.h>
#include <OgreImage2.h>
#include <OgreRenderSystem.h>
#include <OgreRoot.h>
#include <OgreSceneManager.h>
#include <OgreStagingTexture.h>
#include <OgreTextureBox.h>
#include <OgreTextureGpuManager.h>
#include <OgreMesh2.h>
#include <OgreSubMesh2.h>
#include <Vao/OgreAsyncTicket.h>
#include <Vao/OgreIndexBufferPacked.h>
#include <Vao/OgreVertexArrayObject.h>
#include <Vao/OgreUavBufferPacked.h>
#include <Vao/OgreVaoManager.h>
#include <Vct/OgreVctVoxelizer.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace jahshaka::engine;
using jahshaka::engine::detail::AtomDecodeRenderable;
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
/// The decode renderables' queue. They are VISIBLE only while the decode workspace
/// runs (a workspace listener), so no pass of the view's ever draws them whatever
/// its range. It must be a FAST (v2) queue: Ogre's default modes make [100, 200)
/// V1_FAST and [225, 256) V1_LEGACY, where a v2-only renderable is asked for v1
/// world transforms (measured: the first run drew nothing but exceptions at 230).
static const Ogre::uint8 kDecodeRq = 99;
/// The spike's bar.
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
                    ids0[p] = AtomId::pack(cellSlot, 0u);
                    ids1[p] = uint32_t(t);
                }
            }
    }
    return true;
}

// ---------------------------------------------------------------------------
// THE TWO WORKSPACES
// ---------------------------------------------------------------------------
/// One node: [prepass] + colour pass over [firstRq, lastRq), into the external
/// target, with the view's shadow node. `prepass` mirrors what the view's chain does
/// when the gather is on (OgreChain.cpp's prepass + setUseDepthPrePass).
static std::string defineArm(Ogre::CompositorManager2 *cm, const std::string &name, bool prepass,
                             Ogre::uint8 firstRq, Ogre::uint8 lastRq)
{
    const std::string nodeName = name + "/Node", wsName = name + "/Ws";
    if (cm->hasWorkspaceDefinition(wsName)) return wsName;
    Ogre::CompositorNodeDef *n = cm->addNodeDefinition(nodeName);
    n->addTextureSourceName("rt0", 0, Ogre::TextureDefinitionBase::TEXTURE_INPUT);
    n->setNumLocalTextureDefinitions(3);
    {
        auto *td = n->addTextureDefinition("depth");
        td->format = Ogre::PFG_D32_FLOAT;
        td->fsaa = "1";
        td->preferDepthTexture = true;
    }
    if (prepass) {
        auto *tn = n->addTextureDefinition("gbufNormals");
        tn->format = Ogre::PFG_R10G10B10A2_UNORM;
        tn->fsaa = "1";
        tn->depthBufferId = 0;
        auto *ts = n->addTextureDefinition("gbufShadowRough");
        ts->format = Ogre::PFG_RG16_UNORM;
        ts->fsaa = "1";
        ts->depthBufferId = 0;
        Ogre::RenderTargetViewDef *rtv = n->addRenderTextureView("prepassRtv");
        Ogre::RenderTargetViewEntry a, b;
        a.textureName = "gbufNormals";
        b.textureName = "gbufShadowRough";
        rtv->colourAttachments.push_back(a);
        rtv->colourAttachments.push_back(b);
        rtv->depthAttachment.textureName = "depth";
    }
    {
        Ogre::RenderTargetViewDef *rtv = n->addRenderTextureView("sceneRtv");
        Ogre::RenderTargetViewEntry c0;
        c0.textureName = "rt0";
        rtv->colourAttachments.push_back(c0);
        rtv->depthAttachment.textureName = "depth";
    }
    n->setNumTargetPass(2);
    if (prepass) {
        Ogre::CompositorTargetDef *t = n->addTargetPass("prepassRtv");
        t->setNumPasses(1);
        auto *p = static_cast<Ogre::CompositorPassSceneDef *>(t->addPass(Ogre::PASS_SCENE));
        p->mPrePassMode = Ogre::PrePassCreate;
        p->mShadowNode = Ogre::IdString(OgreView::kShadowNodeName);
        p->setAllClearColours(Ogre::ColourValue::White);
        p->setAllLoadActions(Ogre::LoadAction::Clear);
        p->mStoreActionColour[0] = Ogre::StoreAction::Store;
        p->mStoreActionColour[1] = Ogre::StoreAction::Store;
        p->mStoreActionDepth = Ogre::StoreAction::Store;
        p->mStoreActionStencil = Ogre::StoreAction::DontCare;
        p->mFirstRQ = firstRq;
        p->mLastRQ = lastRq;
        p->mIncludeOverlays = false;
    }
    {
        Ogre::CompositorTargetDef *t = n->addTargetPass("sceneRtv");
        t->setNumPasses(1);
        auto *p = static_cast<Ogre::CompositorPassSceneDef *>(t->addPass(Ogre::PASS_SCENE));
        p->mShadowNode = Ogre::IdString(OgreView::kShadowNodeName);
        p->setAllClearColours(Ogre::ColourValue(0.0f, 0.0f, 0.0f, 1.0f));
        p->setAllLoadActions(Ogre::LoadAction::Clear);
        if (prepass) {
            Ogre::IdStringVec pre;
            pre.push_back(Ogre::IdString("gbufNormals"));
            pre.push_back(Ogre::IdString("gbufShadowRough"));
            p->setUseDepthPrePass(pre, Ogre::IdString("depth"), Ogre::IdString());
            p->mLoadActionDepth = Ogre::LoadAction::Load;
        }
        p->mStoreActionColour[0] = Ogre::StoreAction::Store;
        p->mStoreActionDepth = Ogre::StoreAction::DontCare;
        p->mStoreActionStencil = Ogre::StoreAction::DontCare;
        p->mFirstRQ = firstRq;
        p->mLastRQ = lastRq;
        p->mIncludeOverlays = false;
    }
    Ogre::CompositorWorkspaceDef *wd = cm->addWorkspaceDefinition(wsName);
    wd->connectExternal(0, nodeName, 0);
    return wsName;
}

/// Shows the decode renderables ONLY while the decode workspace runs, and hands
/// HlmsAtom its source right before (the GPU scene's tables are re-read, never
/// cached across a frame).
struct DecodeListener final : public Ogre::CompositorWorkspaceListener {
    std::vector<AtomDecodeRenderable *> *decodes = nullptr;
    HlmsAtom *atom = nullptr;
    OgreScene *scene = nullptr;
    Ogre::TextureGpu *ids = nullptr;
    void workspacePreUpdate(Ogre::CompositorWorkspace *) override {
        HlmsAtom::DecodeSource src;
        src.ids = ids;
        src.instances = scene->gpuScene().instanceBuffer();
        src.levels = scene->gpuScene().levelBuffer();
        src.geomRows = scene->gpuScene().geomBuffer();
        atom->setDecodeSource(src);
        for (AtomDecodeRenderable *d : *decodes) d->setVisible(true);
    }
    void workspacePosUpdate(Ogre::CompositorWorkspace *) override {
        for (AtomDecodeRenderable *d : *decodes) d->setVisible(false);
    }
};

static bool readTarget(Ogre::TextureGpu *tex, std::vector<unsigned char> &out)
{
    Ogre::Image2 img;
    img.convertFromTexture(tex, 0u, 0u);
    const Ogre::TextureBox box = img.getData(0u);
    out.resize(size_t(kW) * kH * 4u);
    for (unsigned y = 0; y < kH; ++y)
        std::memcpy(&out[size_t(y) * kW * 4u], box.at(0, y, 0), size_t(kW) * 4u);
    return true;
}

static void writePpm(const std::vector<unsigned char> &rgba, const std::string &path)
{
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%u %u\n255\n", kW, kH);
    for (size_t i = 0; i + 3 < rgba.size(); i += 4) std::fwrite(&rgba[i], 1, 3, f);
    std::fclose(f);
}

struct Stat {
    size_t n = 0, beyond = 0, saturated = 0;
    double mean = 0.0;
    int worst = 0;
};

static int maxDiff(const unsigned char *a, const unsigned char *b)
{
    int m = 0;
    for (int c = 0; c < 3; ++c) m = std::max(m, std::abs(int(a[c]) - int(b[c])));
    return m;
}

int main()
{
    std::printf("== engine.atom_parity: HlmsAtom's decode over a hand-made id buffer against stock "
                "HlmsPbs, per cell (mean <= %.1f code, <= %.1f %% beyond %d codes)\n",
                kMeanBar, kTailBar * 100.0, kTailCodes);
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-atom-parity-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();

    View *view = e->createOffscreenView("parity", kW, kH, Colour(0, 0, 0));
    Scene *scene = e->createScene("parity");
    view->setScene(scene);
    enginetest::testCameraLookAt(view, Vec3(0.0f, 1.55f, 7.2f), Vec3(0.0f, 0.55f, 0.0f));
    {
        PostFxDesc fx;
        fx.allowOffscreen = true;   // the chain the gather's prepass lives in
        fx.ssr = 0;                 // SSR OFF (the brief)
        view->setPostFx(fx);
    }
    scene->setLodBias(0.0f);        // level 0 everywhere: the hand-made ids name level 0
    scene->setAmbient(Colour(0.10f, 0.11f, 0.13f), Colour(0.05f, 0.05f, 0.05f));

    auto *ogreScene = static_cast<OgreScene *>(scene);
    auto *ogreView = static_cast<OgreView *>(view);
    Ogre::Root &root = Ogre::Root::getSingleton();
    Ogre::HlmsManager *hm = root.getHlmsManager();
    auto *atom = dynamic_cast<HlmsAtom *>(hm->getHlms(HlmsAtom::kType));
    CHECK(atom != nullptr, "HlmsAtom is registered beside HlmsPbs (HLMS_USER0)");
    CHECK(atomtest::validationProbe(), "the validation layer is ACTIVE when the entry asks for it");
    if (!atom) return 1;

    // ---- the textures (single mip, magnified) --------------------------------
    TextureId tAlbedo, tNormal, tRough, tEmissive;
    {
        const auto a = smoothTexture(0), nm = smoothTexture(1), r = smoothTexture(2), em = smoothTexture(3);
        tAlbedo = scene->createTexture(64, 64, a.data(), true, false);
        tNormal = scene->createTexture(64, 64, nm.data(), false, false);
        tRough = scene->createTexture(64, 64, r.data(), false, false);
        tEmissive = scene->createTexture(64, 64, em.data(), true, false);
    }
    CHECK(tAlbedo && tNormal && tRough && tEmissive, "the grid's four single-mip textures exist");

    // ---- the cells ----------------------------------------------------------
    Geometry sphereGeom, sheetGeom;
    const MeshId sphere = scene->createMesh(sphereMesh(96, 48, 0.42f, sphereGeom));
    const MeshId sheet = scene->createMesh(wavySheet(160, 16.0f, 0.06f, 12.0f, sheetGeom));
    CHECK(sphere && sheet, "the sphere and the wavy sheet meshes exist");

    std::vector<Cell> cells;
    const char *kinds[5] = { "base", "metal", "rough", "normal-map", "emissive" };
    for (int textured = 0; textured < 2; ++textured)
        for (int k = 0; k < 5; ++k) {
            PbrParams p;
            p.albedo = Colour(0.70f, 0.62f, 0.52f);
            p.metalness = 0.0f;
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

    // ---- the lights: the sun, one point, one spot — shadows on ----------------
    const NodeId sunNode = enginetest::addDirectionalLight(scene, Vec3(-0.45f, -0.8f, -0.4f), 2.2f);
    const NodeId pointNode = scene->createNode();
    scene->setNodeTransform(pointNode, Vec3(1.6f, 1.9f, 1.4f), Quat(), Vec3(1, 1, 1));
    const NodeId spotNode = scene->createNode();
    {
        // A spot aimed down and back (-Y is the light's direction): pitched 55 deg about X.
        const float a = 0.5f * 0.96f;
        scene->setNodeTransform(spotNode, Vec3(-1.4f, 3.2f, 2.2f), Quat(std::sin(a), 0.0f, 0.0f, std::cos(a)),
                                Vec3(1, 1, 1));
    }
    LightDesc sunL, pointL, spotL;
    sunL.type = LightType::Directional;
    sunL.intensity = 2.2f / 3.14159265f;
    pointL.type = LightType::Point;
    pointL.colour = Colour(1.0f, 0.85f, 0.7f);
    pointL.range = 8.0f;
    spotL.type = LightType::Spot;
    spotL.colour = Colour(0.7f, 0.85f, 1.0f);
    spotL.range = 10.0f;
    spotL.spotAngleDegrees = 50.0f;
    auto setLights = [&](bool sun, bool point, bool spot) {
        // A dark light keeps its SLOT (and its shadow map): the arms differ by
        // radiance, never by shader permutation.
        // Lit to sit INSIDE the 8-bit range (dolly_gate's rule: a saturated cell hides
        // a difference by clamping it — the grid asserts it below).
        sunL.intensity = sun ? 0.24f / 3.14159265f : 0.0f;
        pointL.intensity = point ? 0.42f : 0.0f;
        spotL.intensity = spot ? 0.75f : 0.0f;
        scene->setLight(sunNode, sunL);
        scene->setLight(pointNode, pointL);
        scene->setLight(spotNode, spotL);
    };
    setLights(true, true, true);

    // ---- two frames so the GPU scene holds every slot ------------------------
    for (int i = 0; i < 4; ++i) e->renderOneFrame();
    ogreScene->ensureGpuScene(false);
    ogreScene->gpuScene().flushGeomRows();
    const detail::GpuScene &gs = ogreScene->gpuScene();
    CHECK(gs.live(), "the GPU scene is live");
    for (Cell &c : cells) {
        for (unsigned s = 0; s < gs.slotCount(); ++s)
            if (gs.entry(s).ids[0] == unsigned(c.node)) { c.slot = s; break; }
        if (c.slot != 0xFFFFFFFFu) std::memcpy(c.world, gs.entry(c.slot).world, sizeof(c.world));
    }
    bool slotsOk = true;
    for (const Cell &c : cells) slotsOk = slotsOk && c.slot != 0xFFFFFFFFu &&
                                            gs.entry(c.slot).raster[0] != HlmsAtom::kNoMaterialWord;
    CHECK(slotsOk, "every cell has a GPU scene slot carrying a PBS material word (raster.x)");
    if (!slotsOk) return 1;

    // ---- THE TABLES THE DECODE WILL DEREFERENCE, checked on the CPU first ------
    // A wrong device address in a shader is a GPU hang (Xid 109), never a
    // validation error, so every row the decode can reach is read back and
    // compared with Ogre's own describer before one decode pixel runs.
    {
        auto readBack = [](Ogre::UavBufferPacked *b, std::vector<uint32_t> &out) {
            const size_t bytes = b->getTotalSizeBytes();
            Ogre::AsyncTicketPtr t = b->readRequest(0, b->getNumElements());
            out.resize(bytes / 4u);
            std::memcpy(out.data(), t->map(), bytes);
            t->unmap();
        };
        std::vector<uint32_t> inst, lev, rows;
        readBack(gs.instanceBuffer(), inst);
        readBack(gs.levelBuffer(), lev);
        readBack(gs.geomBuffer(), rows);
        Ogre::VaoManager *vaoMgr = Ogre::Root::getSingleton().getRenderSystem()->getVaoManager();
        bool tablesOk = true;
        for (const Cell &c : cells) {
            const uint32_t *in = &inst[size_t(c.slot) * 40u];
            const uint32_t mesh = in[6 * 4 + 3];
            const uint32_t *lv = &lev[(size_t(mesh) * detail::GpuScene::kLevelsPerMesh) * 8u];
            const uint32_t geomRow = lv[3];
            const Ogre::MeshPtr &m = gs.meshAt(mesh);
            Ogre::VctVoxelizer::GeometryRow want;
            const bool described = m && Ogre::VctVoxelizer::describeGeometryRow(m, 0u, 0u, vaoMgr, want);
            const uint32_t *row = geomRow != detail::GpuScene::kNoGeomRow && size_t(geomRow) * 12u + 12u <= rows.size()
                                      ? &rows[size_t(geomRow) * 12u] : nullptr;
            const bool same = described && row && std::memcmp(row, &want, 48) == 0;
            std::printf("  table %-18s slot %u mesh %u level0 first %u count %u geomRow %u stride %u pos %u nrm %u uv %u "
                        "flags 0x%x tangent %u -> %s\n",
                        c.name.c_str(), c.slot, mesh, lv[0], lv[1], geomRow, row ? row[4] : 0u, row ? row[5] : 0u,
                        row ? row[6] : 0u, row ? row[7] : 0u, row ? row[8] : 0u, in[9 * 4 + 1],
                        same ? "matches Ogre's describer" : "MISMATCH");
            tablesOk = tablesOk && same && in[9 * 4] == gs.entry(c.slot).raster[0] &&
                       lv[1] == uint32_t(c.geom->idx.size()) && row[6] != 0xFFFFFFFFu;
        }
        CHECK(tablesOk, "the device tables the decode reads name the raster's own geometry (instance -> "
                        "level -> geometry row, read back and compared with VctVoxelizer::describeGeometryRow)");
        if (!tablesOk) return 1;
    }

    // ---- THE GEOMETRY THE RASTER DRAWS ---------------------------------------
    // The hand-made id buffer must name the triangles in the ENGINE's order: the
    // engine's mesh build reorders a mesh's indices (measured: the first grid run
    // rasterised MeshData's own order and the decode shaded the wrong triangles, a
    // confetti of normals). So the CPU rasteriser reads level 0 of each mesh back
    // from Ogre's own vertex and index buffers — the buffers the geometry rows
    // point at.
    std::map<uint32_t, Geometry> drawnGeometry;
    for (Cell &c : cells) {
        uint32_t meshIndex = 0;
        std::memcpy(&meshIndex, &gs.entry(c.slot).boundsMin[3], sizeof(meshIndex));
        auto it = drawnGeometry.find(meshIndex);
        if (it == drawnGeometry.end()) {
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
            it = drawnGeometry.emplace(meshIndex, std::move(g)).first;
        }
        c.geom = &it->second;
    }

    // ---- THE HAND-MADE ID BUFFER -------------------------------------------
    Ogre::Camera *cam = ogreView->camera();
    const Ogre::Matrix4 viewProj = cam->getProjectionMatrixWithRSDepth() * cam->getViewMatrix(true);
    float vp[4][4];
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) vp[r][c] = float(viewProj[size_t(r)][size_t(c)]);
    std::vector<uint32_t> ids0(size_t(kW) * kH, AtomId::kEmpty), ids1(size_t(kW) * kH, 0u);
    std::vector<double> invW(size_t(kW) * kH, 0.0);
    for (const Cell &c : cells) rasteriseCell(c, vp, c.slot, ids0, ids1, invW);
    size_t covered = 0;
    for (uint32_t v : ids0) covered += v != AtomId::kEmpty;
    CHECK_MSG(covered > size_t(kW) * kH / 5, "the hand-made id buffer covers %zu of %u pixels", covered, kW * kH);

    Ogre::RenderSystem *rs = root.getRenderSystem();
    Ogre::TextureGpuManager *tm = rs->getTextureGpuManager();
    Ogre::TextureGpu *idTex = tm->createTexture("atomParityIds", Ogre::GpuPageOutStrategy::Discard,
                                                Ogre::TextureFlags::ManualTexture, Ogre::TextureTypes::Type2D);
    idTex->setResolution(kW, kH);
    idTex->setPixelFormat(Ogre::PFG_RG32_UINT);
    idTex->setNumMipmaps(1u);
    // A ManualTexture goes Resident by an immediate transition and is NEVER
    // notifyDataIsReady'd (DOCS/traps/ENGINE.md).
    idTex->_transitionTo(Ogre::GpuResidency::Resident, nullptr);
    {
        Ogre::StagingTexture *st = tm->getStagingTexture(kW, kH, 1u, 1u, Ogre::PFG_RG32_UINT);
        st->startMapRegion();
        Ogre::TextureBox box = st->mapRegion(kW, kH, 1u, 1u, Ogre::PFG_RG32_UINT);
        for (unsigned y = 0; y < kH; ++y) {
            auto *row = reinterpret_cast<uint32_t *>(box.at(0, y, 0));
            for (unsigned x = 0; x < kW; ++x) {
                row[x * 2u + 0u] = ids0[size_t(y) * kW + x];
                row[x * 2u + 1u] = ids1[size_t(y) * kW + x];
            }
        }
        st->stopMapRegion();
        st->upload(box, idTex, 0, nullptr, nullptr, true);
        tm->removeStagingTexture(st);
    }

    // ---- the decode twins and their full-screen draws ------------------------
    std::map<uint32_t, Ogre::HlmsPbsDatablock *> pbsByWord;
    {
        Ogre::Hlms *pbs = hm->getHlms(Ogre::HLMS_PBS);
        for (const auto &kv : pbs->getDatablockMap())
            pbsByWord[HlmsAtom::materialWordOf(kv.second.datablock)] =
                static_cast<Ogre::HlmsPbsDatablock *>(kv.second.datablock);
    }
    Ogre::SceneManager *sm = static_cast<Ogre::SceneManager *>(scene->nativeSceneManager());
    std::vector<AtomDecodeRenderable *> decodes;
    Ogre::SceneNode *decodeNode = sm->getRootSceneNode()->createChildSceneNode();
    bool twinsOk = true;
    for (const Cell &c : cells) {
        const uint32_t word = gs.entry(c.slot).raster[0];
        auto it = pbsByWord.find(word);
        if (it == pbsByWord.end()) { twinsOk = false; continue; }
        std::string terr;
        Ogre::HlmsPbsDatablock *twin = atom->decodeTwinFor(it->second, terr);
        if (!twin) { std::printf("  twin for %s: %s\n", c.name.c_str(), terr.c_str()); twinsOk = false; continue; }
        bool have = false;
        for (AtomDecodeRenderable *d : decodes) have = have || d->getDatablock() == twin;
        if (have) continue;
        auto *d = new AtomDecodeRenderable(Ogre::Id::generateNewId<Ogre::MovableObject>(),
                                           &sm->_getEntityMemoryManager(Ogre::SCENE_DYNAMIC), sm, kDecodeRq);
        d->setDatablock(twin);
        decodeNode->attachObject(d);
        // Hidden AFTER the attach: set before it, the first frame's view pass still
        // drew them (measured — the host's sourceless-draw log named the view's camera).
        d->setVisible(false);
        decodes.push_back(d);
    }
    // ATOM_PARITY_DEBUG=<what>: stage 0's debug hook, inherited — both hosts write the
    // same intermediate of the shading to the colour target through a per-datablock
    // custom piece (no media edited), so a failing cell is localised in minutes.
    // A diagnosis switch, never the gate.
    if (const char *what = std::getenv("ATOM_PARITY_DEBUG")) {
        std::string expr = "pixelData.normal * _h( 0.5 ) + _h( 0.5 )";
        const std::string w(what);
        if (w == "viewdir") expr = "pixelData.viewDir * _h( 0.5 ) + _h( 0.5 )";
        else if (w == "pos") expr = "midf3_c( fract( abs( inPs.pos.xyz ) ) )";
        else if (w == "geomnormal") expr = "pixelData.geomNormal * _h( 0.5 ) + _h( 0.5 )";
        else if (w == "worldnorm") expr = "midf3_c( normalize( inPs.worldNorm ) * 0.5 + 0.5 )";
        else if (w == "worldpos") expr = "midf3_c( fract( abs( inPs.worldPos.xyz ) ) )";
        else if (w == "depth") expr = "midf3_c( fract( inPs.depth ), 0.0, 0.0 )";
        else if (w == "diffuse") expr = "pixelData.diffuse.xyz";
        else if (w == "rough") expr = "midf3_c( pixelData.roughness )";
        else if (w == "shadow") expr = "midf3_c( fShadow )";
        else if (w == "uv") expr = "midf3_c( fract( inPs.uv0.x ), fract( inPs.uv0.y ), 0.0 )";
        else if (w == "nan")   // red: a non-finite position; green: interpolated normal; blue: shading normal
            expr = "midf3_c( any( greaterThanEqual( floatBitsToUint( inPs.pos.xyz ) & uvec3( 0x7FFFFFFFu ), "
                   "uvec3( 0x7F800000u ) ) ) ? 1.0 : 0.0, any( greaterThanEqual( floatBitsToUint( vec3( inPs.normal ) ) "
                   "& uvec3( 0x7FFFFFFFu ), uvec3( 0x7F800000u ) ) ) ? 1.0 : 0.0, any( greaterThanEqual( "
                   "floatBitsToUint( vec3( pixelData.normal ) ) & uvec3( 0x7FFFFFFFu ), uvec3( 0x7F800000u ) ) ) ? 1.0 : 0.0 )";
        const std::string piece = "@property( !hlms_shadowcaster && !hlms_prepass )\n"
                                  "@piece( custom_ps_posExecution )\n\toutPs_colour0.xyzw = midf4_c( " + expr +
                                  ", 1.0 );\n@end\n@end\n";
        for (const auto &kv : pbsByWord)
            if (kv.second) kv.second->setCustomPieceCodeFromMemory("atom_parity_dbg", piece,
                                                                   Ogre::CustomPieceStage::PixelShader);
        for (AtomDecodeRenderable *d : decodes)
            d->getDatablock()->setCustomPieceCodeFromMemory("atom_parity_dbg", piece,
                                                            Ogre::CustomPieceStage::PixelShader);
        std::printf("  DEBUG HOOK: both hosts write '%s'\n", what);
    }
    CHECK_MSG(twinsOk && decodes.size() == cells.size(),
              "one decode twin (HlmsAtom datablock, JSON round trip) per cell material: %zu twins",
              decodes.size());

    // ---- the targets and the two workspaces ----------------------------------
    auto makeTarget = [&](const char *name) {
        Ogre::TextureGpu *t = tm->createTexture(name, Ogre::GpuPageOutStrategy::Discard,
                                                Ogre::TextureFlags::RenderToTexture, Ogre::TextureTypes::Type2D);
        t->setResolution(kW, kH);
        t->setPixelFormat(Ogre::PFG_RGBA8_UNORM_SRGB);
        t->setNumMipmaps(1u);
        t->_transitionTo(Ogre::GpuResidency::Resident, nullptr);
        return t;
    };
    Ogre::TextureGpu *refTex = makeTarget("atomParityRef");
    Ogre::TextureGpu *decTex = makeTarget("atomParityDec");
    Ogre::CompositorManager2 *cm = root.getCompositorManager2();
    DecodeListener listener;
    listener.decodes = &decodes;
    listener.atom = atom;
    listener.scene = ogreScene;
    listener.ids = idTex;
    Ogre::CompositorWorkspace *refWs = nullptr, *decWs = nullptr;
    auto arm = [&](bool prepass) {
        if (refWs) cm->removeWorkspace(refWs);
        if (decWs) cm->removeWorkspace(decWs);
        const std::string tag = prepass ? "Pre" : "Fwd";
        const std::string refDef = defineArm(cm, "AtomParityRef" + tag, prepass, 0u, kDecodeRq - 1u);
        const std::string decDef = defineArm(cm, "AtomParityDec" + tag, prepass, kDecodeRq, kDecodeRq + 1u);
        refWs = cm->addWorkspace(sm, refTex, cam, refDef, true);
        // ATOM_PARITY_NODECODE=1: the reference alone (a diagnosis arm: attributes a
        // validation report to the stock host or to the decode).
        decWs = cm->addWorkspace(sm, decTex, cam, decDef, std::getenv("ATOM_PARITY_NODECODE") == nullptr);
        decWs->addListener(&listener);
    };

    // ---- THE GRID ------------------------------------------------------------
    struct LightArm { const char *name; bool sun, point, spot; };
    const LightArm lightArms[4] = { { "sun", true, false, false }, { "point", false, true, false },
                                    { "spot", false, false, true }, { "all", true, true, true } };
    // THE PCC ARM is the product's configuration whenever a probe grid exists (the
    // VCT+probes hybrid binds a ParallaxCorrectedCubemap to HlmsPbs, OgreGi.cpp): the
    // decode must read the same probes with the same two blend distances.
    // Probes keep only what they see enclosed (an open scene is sky), so the PCC is
    // measured in a CLOSED ROOM, against the same room without probes: the pair of
    // arms isolates the probes' own term.
    struct PhotonArm { const char *name; bool gi, gather, pcc, room; int liveVs; };
    const PhotonArm photonArms[5] = { { "direct", false, false, false, false, -1 },
                                      { "vct+field", true, false, false, false, 0 },
                                      { "vct+field+gather", true, true, false, false, 1 },
                                      { "room/vct+field", true, false, false, true, 1 },
                                      { "room/vct+field+pcc", true, false, true, true, 3 } };
    bool roomBuilt = false;
    std::printf("  cards-at-hits column: HOLE — lands with PHOTON-CARDS-2 (no stub)\n");

    std::vector<unsigned char> ref, dec, prevRef, prevDec;
    std::vector<unsigned char> lastRefOfPhoton[5];
    int cellFails = 0, cellsRun = 0;
    const char *outDir = std::getenv("ATOM_PARITY_OUT");
    // ATOM_PARITY_QUICK=1: the first cell row only (a diagnosis arm, never the gate).
    const bool quick = std::getenv("ATOM_PARITY_QUICK") != nullptr;
    for (int pi = 0; pi < (quick ? 1 : 5); ++pi) {
        const PhotonArm &P = photonArms[pi];
        if (P.room && !roomBuilt) {
            // Floor, ceiling and four walls around the fixture and the camera; matte.
            // Not in the id buffer (the grid compares the cells only).
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
            }
            roomBuilt = true;
        }
        GiParams gi;
        if (P.gi) {
            gi.mode = P.pcc ? GiMode::VctPccHybrid : GiMode::Vct;
            gi.quality = GiQuality::Medium;
            gi.cascades = true;
            gi.ddgi = GiToggle::On;
            gi.gather = P.gather ? GiToggle::On : GiToggle::Off;
        } else {
            gi.mode = GiMode::Off;
        }
        scene->setGlobalIllumination(gi);
        if (P.gather) {
            GatherTuning t;
            t.freezeFrameIndex = true;   // a still frame of the gather is deterministic
            scene->setGatherTuning(t);
        }
        arm(P.gather);
        for (int li = 0; li < (quick ? 1 : 4); ++li) {
            const LightArm &L = lightArms[li];
            setLights(L.sun, L.point, L.spot);
            // READ UNTIL STABLE — frames, never time.
            int frames = 0;
            int stableReads = 0;
            prevRef.clear();
            prevDec.clear();
            while (frames < 1200) {
                for (int i = 0; i < 4; ++i) e->renderOneFrame();
                frames += 4;
                readTarget(refTex, ref);
                readTarget(decTex, dec);
                if (frames >= 24 && !prevRef.empty()) {
                    int worst = 0;
                    for (size_t i = 0; i < ref.size(); i += 4) {
                        worst = std::max(worst, maxDiff(&ref[i], &prevRef[i]));
                        worst = std::max(worst, maxDiff(&dec[i], &prevDec[i]));
                    }
                    stableReads = worst <= 1 ? stableReads + 1 : 0;
                    if (stableReads >= 2) break;
                }
                prevRef = ref;
                prevDec = dec;
            }
            const bool settled = stableReads >= 2;
            CHECK_MSG(settled, "[%s | %s] settled after %d frames", P.name, L.name, frames);
            if (outDir) {
                const std::string base = std::string(outDir) + "/" + P.name + "_" + L.name;
                writePpm(ref, base + "_ref.ppm");
                writePpm(dec, base + "_dec.ppm");
            }
            // PER CELL: interior pixels (the 3x3 neighbourhood is one object).
            std::vector<Stat> stats(cells.size());
            for (unsigned y = 1; y + 1 < kH; ++y)
                for (unsigned x = 1; x + 1 < kW; ++x) {
                    const size_t p = size_t(y) * kW + x;
                    const uint32_t id = ids0[p];
                    if (id == AtomId::kEmpty) continue;
                    bool interior = true;
                    for (int dy = -1; dy <= 1 && interior; ++dy)
                        for (int dx = -1; dx <= 1; ++dx)
                            if (ids0[size_t(int(y) + dy) * kW + size_t(int(x) + dx)] != id) { interior = false; break; }
                    if (!interior) continue;
                    size_t ci = 0;
                    while (ci < cells.size() && cells[ci].slot != (id & AtomId::kSlotMask)) ++ci;
                    if (ci == cells.size()) continue;
                    Stat &s = stats[ci];
                    const unsigned char *a = &ref[p * 4u], *b = &dec[p * 4u];
                    const int d = maxDiff(a, b);
                    s.mean += (std::abs(int(a[0]) - int(b[0])) + std::abs(int(a[1]) - int(b[1])) +
                               std::abs(int(a[2]) - int(b[2]))) / 3.0;
                    s.beyond += d > kTailCodes;
                    s.worst = std::max(s.worst, d);
                    s.saturated += (a[0] == 255 || a[1] == 255 || a[2] == 255);
                    ++s.n;
                }
            for (size_t ci = 0; ci < cells.size(); ++ci) {
                Stat &s = stats[ci];
                if (s.n) s.mean /= double(s.n);
                const double tail = s.n ? double(s.beyond) / double(s.n) : 1.0;
                // THE INSTRUMENT HOLDS: a cell with more than 5 % of its interior at 255
                // in the reference would compare clamped pixels, not shading.
                const double sat = s.n ? double(s.saturated) / double(s.n) : 1.0;
                const bool pass = s.n > 200 && s.mean <= kMeanBar && tail <= kTailBar && sat <= 0.05;
                ++cellsRun;
                if (!pass) ++cellFails;
                std::printf("  cell %-18s %-6s %-17s interior %7zu  mean %6.3f  >8 %6.3f %%  max %3d  sat %5.2f %%  %s\n",
                            cells[ci].name.c_str(), L.name, P.name, s.n, s.mean, tail * 100.0, s.worst,
                            s.n ? 100.0 * double(s.saturated) / double(s.n) : 0.0, pass ? "ok" : "FAIL");
            }
            if (li == 3) lastRefOfPhoton[pi] = ref;
        }
        // THE COLUMN IS LIVE: each Photon term must move the compared picture.
        if (P.liveVs >= 0 && !lastRefOfPhoton[P.liveVs].empty() && !lastRefOfPhoton[pi].empty()) {
            const std::vector<unsigned char> &was = lastRefOfPhoton[P.liveVs], &now = lastRefOfPhoton[pi];
            double sum = 0.0;
            for (size_t i = 0; i < now.size(); i += 4) sum += maxDiff(&now[i], &was[i]);
            const double meanMove = sum / double(now.size() / 4u);
            CHECK_MSG(meanMove > 0.25, "the '%s' column is LIVE in the compared picture (mean move %.3f codes vs '%s')",
                      P.name, meanMove, photonArms[P.liveVs].name);
            if (P.pcc) {
                // ...and on the GLOSSY METAL cell itself: the probes are in the
                // reflection the grid compares there, not only somewhere in the frame.
                double msum = 0.0;
                size_t mn = 0;
                for (size_t ci = 0; ci < cells.size(); ++ci) {
                    if (cells[ci].name != "metal/const") continue;
                    for (size_t p = 0; p < ids0.size(); ++p)
                        if ((ids0[p] & AtomId::kSlotMask) == cells[ci].slot && ids0[p] != AtomId::kEmpty) {
                            msum += maxDiff(&now[p * 4u], &was[p * 4u]);
                            ++mn;
                        }
                }
                const double metalMove = mn ? msum / double(mn) : 0.0;
                CHECK_MSG(metalMove > 0.25, "[%s] the probes reach the glossy metal cell (mean move %.3f codes over %zu px)",
                          P.name, metalMove, mn);
            }
        }
        if (P.gi) {
            const GiStatus st = scene->giStatus();
            CHECK_MSG(st.vctBound, "[%s] the VCT chain is bound to HlmsPbs", P.name);
            auto *pbs = static_cast<Ogre::HlmsPbs *>(hm->getHlms(Ogre::HLMS_PBS));
            CHECK_MSG(atom->getVctLighting() == pbs->getVctLighting() &&
                          atom->getIrradianceField() == pbs->getIrradianceField() && pbs->getIrradianceField(),
                      "[%s] tellEveryHlms: HlmsAtom holds PBS's VctLighting and IrradianceField", P.name);
            if (P.gather)
                CHECK_MSG(st.gather.on && st.gather.running,
                          "[%s] the gather ran on the view's frame (on %d, running %d, probes %u, %ux%u, error '%s')",
                          P.name, int(st.gather.on), int(st.gather.running), st.gather.probes,
                          st.gather.targetW, st.gather.targetH, st.gather.error.c_str());
        }
        {
            auto *pbs = static_cast<Ogre::HlmsPbs *>(hm->getHlms(Ogre::HLMS_PBS));
            const bool samePcc = atom->getParallaxCorrectedCubemap() == pbs->getParallaxCorrectedCubemap() &&
                                 atom->getPccVctMinDistance() == pbs->getPccVctMinDistance() &&
                                 atom->getPccVctMaxDistance() == pbs->getPccVctMaxDistance() &&
                                 atom->getMaxSpecIblMipmap() == pbs->getMaxSpecIblMipmap();
            CHECK_MSG(samePcc && (!P.pcc || pbs->getParallaxCorrectedCubemap()),
                      "[%s] tellEveryHlms: HlmsAtom holds PBS's PCC (%s), its distances (%.3f, %.3f) and IBL mips (%.0f)",
                      P.name, pbs->getParallaxCorrectedCubemap() ? "bound" : "none", pbs->getPccVctMinDistance(),
                      pbs->getPccVctMaxDistance(), pbs->getMaxSpecIblMipmap());
        }
    }
    CHECK_MSG(cellFails == 0, "the parity grid: %d of %d cells inside the bar", cellsRun - cellFails, cellsRun);

    // ---- A MATERIAL DIES DURING THE GRID -------------------------------------
    // Its decode twin must die with it (forgetDecodeTwinOf at every destruction
    // site): a twin keeps the PBS datablock's pointer and binds its pool. The owner
    // detaches the draw that wears the twin first (Ogre asserts on a datablock with
    // linked renderables); the other decodes keep drawing — under validation (the
    // `_validation` entry) that is a frame with no fault and no report.
    {
        Cell &victim = cells[cells.size() - 2];   // emissive/tex
        const uint32_t word = gs.entry(victim.slot).raster[0];
        Ogre::HlmsPbsDatablock *pbsDb = pbsByWord.count(word) ? pbsByWord[word] : nullptr;
        std::string terr;
        Ogre::HlmsPbsDatablock *twin = pbsDb ? atom->decodeTwinFor(pbsDb, terr) : nullptr;
        const size_t twinsBefore = atom->decodeTwinCount();
        for (auto it = decodes.begin(); it != decodes.end(); ++it)
            if ((*it)->getDatablock() == twin) {
                decodeNode->detachObject(*it);
                delete *it;
                decodes.erase(it);
                break;
            }
        const bool destroyed = scene->destroyMaterial(victim.material);
        for (int i = 0; i < 8; ++i) e->renderOneFrame();
        readTarget(decTex, dec);
        CHECK_MSG(twin && destroyed && atom->decodeTwinCount() + 1 == twinsBefore,
                  "a destroyed material takes its decode twin with it (%zu -> %zu twins), and the decode "
                  "keeps drawing (8 frames)", twinsBefore, atom->decodeTwinCount());
    }

    // ---- teardown: our workspaces and draws die while Root lives --------------
    if (refWs) cm->removeWorkspace(refWs);
    if (decWs) cm->removeWorkspace(decWs);
    for (AtomDecodeRenderable *d : decodes) {
        decodeNode->detachObject(d);
        delete d;
    }
    sm->destroySceneNode(decodeNode);
    atom->setDecodeSource(HlmsAtom::DecodeSource());
    atom->destroyDecodeTwins();
    tm->destroyTexture(refTex);
    tm->destroyTexture(decTex);
    tm->destroyTexture(idTex);
    e->destroyView(view);
    e->destroyScene(scene);

    std::printf("  decode draws recorded with no source (stand-ins bound, nothing shaded): %llu\n",
                atom->sourcelessDraws());
    std::printf("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
