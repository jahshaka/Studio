// atom.occlusion_exact (the engine half) — THE ID PASS'S TWO-PASS OCCLUSION IS EXACT
// (ATOM-OCCLUSION-1; SPECS/briefs/ATOM-OCCLUSION-1.md §3.2, §3.5).
//
// TWO IDENTICAL WORLDS IN LOCKSTEP: the same meshes, materials and nodes created in the
// same order in two scenes (so every slot, cluster and triangle has the same id in
// both), each seen by its own view at the same camera every frame. One scene's id pass
// culls against the depth pyramid in the two-pass form (the default); the other's
// occlusion door is shut (Scene::setAtomOcclusionEnabled(false): frustum-only, the
// picture before this lane). After EVERY frame both id images are read back and
// compared word for word — slot, cluster depth, cluster and triangle:
//   (a) a still pose behind a wall: 0 differing pixels, and the depth test REJECTS
//       objects (atomDrawStatus().occluded > 0) and the id pass draws fewer triangles
//       than the frustum-only one;
//   (b) a walk past the wall's edge: objects come into sight frame by frame and are
//       drawn the frame they do (0 differing pixels on every frame), the late pass
//       disoccludes some (disoccluded > 0 on some frame);
//   (c) THE CAMERA CUT: a teleport to the other side of the wall and back — the FIRST
//       frame after each jump is compared like every other (the previous pyramid is
//       read with the matrix it was drawn with, and the late pass recovers whatever
//       its prediction rejected);
//   (d) a turn of 180 degrees over 30 frames;
//   (e) a LETTERBOXED view (the pass's inset is the pyramid's rectangle) and
//   (f) an ORTHOGRAPHIC one, each over a short walk.
// Frames, never time: the engine's fixed clock; every comparison is per frame.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include "EnginePrivate.h"

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
using jahshaka::engine::detail::OgreView;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
        std::fflush(stdout);                                                    \
    } while (0)
#define CHECK_MSG(cond, fmt, ...)                                               \
    do {                                                                        \
        char buf_[512];                                                         \
        std::snprintf(buf_, sizeof(buf_), fmt, __VA_ARGS__);                    \
        CHECK(cond, buf_);                                                      \
    } while (0)

static const unsigned kW = 960, kH = 540;

/// A UV sphere, the last column and ring WRAPPED to the first by index
/// (DOCS/traps/ENGINE.md, "procedural seams do not weld").
static MeshData sphereMesh(int seg, int ring, float r)
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
    return d;
}

static bool readIds(OgreView *v, std::vector<uint32_t> &out, unsigned &w, unsigned &h)
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
    w = tex->getWidth();
    h = tex->getHeight();
    out.assign(size_t(w) * h * 2u, 0u);
    for (unsigned r = 0; r < h; ++r) {
        const auto *row = reinterpret_cast<const uint32_t *>(box.at(0, r, 0));
        std::memcpy(&out[size_t(r) * w * 2u], row, size_t(w) * 2u * sizeof(uint32_t));
    }
    return true;
}

/// One world: ground, three walls and a field of objects, created in a fixed order.
static void buildWorld(Scene *s)
{
    s->setLodBias(1.0f);
    s->setAmbient(Colour(0.10f, 0.11f, 0.13f), Colour(0.05f, 0.05f, 0.05f));
    PbrParams gp;
    gp.albedo = Colour(0.35f, 0.40f, 0.30f);
    gp.roughness = 0.7f;
    const MaterialId gm = s->createPbrMaterial(gp);
    PbrParams wp;
    wp.albedo = Colour(0.6f, 0.55f, 0.5f);
    wp.roughness = 0.6f;
    const MaterialId wm = s->createPbrMaterial(wp);
    PbrParams op;
    op.albedo = Colour(0.7f, 0.3f, 0.2f);
    op.roughness = 0.4f;
    const MaterialId om = s->createPbrMaterial(op);

    const MeshId cube = s->createMesh(enginetest::unitCubeMesh());
    const MeshId sphere = s->createMesh(sphereMesh(32, 16, 0.5f));
    {
        MeshData g;
        g.positions = { -100.0f, 0.0f, -100.0f, 100.0f, 0.0f, -100.0f, 100.0f, 0.0f, 100.0f, -100.0f, 0.0f, 100.0f };
        g.normals = { 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0 };
        g.uvs = { 0, 0, 20, 0, 20, 20, 0, 20 };
        g.indices = { 0, 2, 1, 0, 3, 2 };
        const NodeId n = s->createNode();
        s->setNodeTransform(n, Vec3(0, 0, 0), Quat(), Vec3(1, 1, 1));
        s->attachMesh(n, s->createMesh(g), gm);
    }
    // THE WALLS: a long one across the view (the occluder), two short ones beside it.
    const struct { Vec3 pos, scale; } walls[3] = {
        { Vec3(0.0f, 2.0f, -8.0f), Vec3(14.0f, 4.0f, 0.5f) },
        { Vec3(-12.0f, 1.5f, -14.0f), Vec3(0.5f, 3.0f, 8.0f) },
        { Vec3(11.0f, 1.0f, -20.0f), Vec3(6.0f, 2.0f, 0.5f) },
    };
    for (const auto &w : walls) {
        const NodeId n = s->createNode();
        s->setNodeTransform(n, w.pos, Quat(), w.scale);
        s->attachMesh(n, cube, wm);
    }
    // THE FIELD: 12 x 10 objects from 10 m to 37 m deep, spheres and cubes alternating,
    // lifted clear of the ground (no coplanar surface anywhere: a tie between two ids
    // is decided by draw ORDER, which the occlusion changes by design).
    for (int zi = 0; zi < 10; ++zi)
        for (int xi = 0; xi < 12; ++xi) {
            const NodeId n = s->createNode();
            const float x = -16.5f + 3.0f * float(xi);
            const float z = -10.0f - 3.0f * float(zi);
            const float y = 0.55f + 0.35f * float((xi + zi) % 3);
            s->setNodeTransform(n, Vec3(x, y, z), Quat(), Vec3(0.9f, 0.9f, 0.9f));
            s->attachMesh(n, ((xi + zi) & 1) ? cube : sphere, om);
        }
    const NodeId sun = enginetest::addDirectionalLight(s, Vec3(-0.45f, -0.8f, -0.4f), 2.2f);
    (void)sun;
}

struct Arm {
    Scene *scene = nullptr;
    View *view = nullptr;
    OgreView *ov = nullptr;
};

struct Tally {
    int frames = 0, compared = 0, differingFrames = 0;
    size_t worstPixels = 0, firstBad = 0;
    unsigned occludedMax = 0, disoccludedMax = 0;
    unsigned long long trisOn = 0ull, trisOff = 0ull;
    int statFrames = 0;
};

static Engine *gE = nullptr;

static void setBoth(Arm &a, Arm &b, const CameraDesc &c)
{
    a.view->setCamera(c);
    b.view->setCamera(c);
}

/// One frame of both worlds, the two id images compared word for word.
static void stepCompare(Arm &on, Arm &off, Tally &t, const char *label)
{
    gE->renderOneFrame();
    ++t.frames;
    std::vector<uint32_t> a, b;
    unsigned aw = 0, ah = 0, bw = 0, bh = 0;
    if (!readIds(on.ov, a, aw, ah) || !readIds(off.ov, b, bw, bh) || aw != bw || ah != bh) return;
    ++t.compared;
    size_t diff = 0, missing = 0;
    for (size_t p = 0; p + 1 < a.size(); p += 2)
        if (a[p] != b[p] || a[p + 1] != b[p + 1]) {
            // THE DIAGNOSIS a red needs: where, and which id each world holds there
            // (0xFFFFFFFF = nothing drawn).
            if (diff < 4)
                std::printf("   %s frame %d px (%zu, %zu): with %08x %08x | frustum-only %08x %08x\n", label,
                            t.frames + 0, (p / 2) % aw, (p / 2) / aw, a[p], a[p + 1], b[p], b[p + 1]);
            if (a[p] == 0xFFFFFFFFu) ++missing;
            ++diff;
        }
    if (diff) {
        if (!t.differingFrames) t.firstBad = size_t(t.frames);
        ++t.differingFrames;
        t.worstPixels = std::max(t.worstPixels, diff);
        std::printf("   %s frame %d: %zu pixels differ (%zu of them empty with the occlusion)\n", label, t.frames,
                    diff, missing);
    }
    const AtomDrawStatus so = on.scene->atomDrawStatus(), sf = off.scene->atomDrawStatus();
    if (so.cutValid && sf.cutValid) {
        ++t.statFrames;
        t.occludedMax = std::max(t.occludedMax, so.occluded);
        t.disoccludedMax = std::max(t.disoccludedMax, so.disoccluded);
        t.trisOn += so.cutTriangles;
        t.trisOff += sf.cutTriangles;
    }
}

static void report(const char *label, const Tally &t)
{
    std::printf("  %-10s frames %4d compared %4d | differing frames %d (worst %zu px) | occluded max %u, "
                "disoccluded max %u | triangles drawn on/off %.3f (%llu / %llu over %d stat frames)\n",
                label, t.frames, t.compared, t.differingFrames, t.worstPixels, t.occludedMax, t.disoccludedMax,
                t.trisOff ? double(t.trisOn) / double(t.trisOff) : 0.0, t.trisOn, t.trisOff, t.statFrames);
    CHECK_MSG(t.compared == t.frames && t.compared > 0, "%s: every frame's two id images were read (%d of %d)", label,
              t.compared, t.frames);
    CHECK_MSG(t.differingFrames == 0,
              "%s: the id image with the occlusion equals the frustum-only one on EVERY frame (%d differ, first "
              "at frame %zu, worst %zu px)",
              label, t.differingFrames, t.firstBad, t.worstPixels);
}

int main()
{
    std::printf("== atom.occlusion_exact (engine): two lockstep worlds, the id pass with the two-pass occlusion "
                "against the frustum-only one, every frame's id image compared word for word\n");
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-atom-occlusion-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    gE = engine.get();

    Arm on, off;
    // THE VIEWS FIRST (Ogre's startup order: a render target before any scene manager).
    on.view = gE->createOffscreenView("occl-on", kW, kH, Colour(0, 0, 0));
    off.view = gE->createOffscreenView("occl-off", kW, kH, Colour(0, 0, 0));
    on.scene = gE->createScene("occl-on");
    off.scene = gE->createScene("occl-off");
    buildWorld(on.scene);
    buildWorld(off.scene);
    off.scene->setAtomOcclusionEnabled(false);
    CHECK(on.scene->atomOcclusionEnabled() && !off.scene->atomOcclusionEnabled(), "the door: on in one world, shut in the other");
    for (Arm *a : { &on, &off }) {
        a->view->setOffscreenContract(OffscreenContract::StillPicture);
        a->view->setScene(a->scene);
        PostFxDesc fx;
        fx.allowOffscreen = true;
        fx.ssr = 0;
        a->view->setPostFx(fx);
        a->ov = static_cast<OgreView *>(a->view);
    }
    const Vec3 eyeA(0.0f, 1.7f, 4.0f);
    setBoth(on, off, enginetest::testCameraDescLookAt(eyeA, Vec3(0.0f, 1.2f, -20.0f)));
    for (int i = 0; i < 12; ++i) gE->renderOneFrame();
    {
        const AtomDrawStatus st = on.scene->atomDrawStatus();
        CHECK_MSG(st.on && st.atomItems >= 120u, "the split is live and draws the world (%u atom items)", st.atomItems);
        CHECK(on.ov->chainAtomOcclusion() && !off.ov->chainAtomOcclusion(),
              "the occluding view's chain carries the pyramid and the late pass; the other's does not");
    }

    // (a) THE STILL POSE
    Tally still;
    for (int i = 0; i < 30; ++i) stepCompare(on, off, still, "(a) still");
    report("(a) still", still);
    CHECK_MSG(still.occludedMax > 0u, "(a) the depth test rejects objects behind the wall (occluded %u)", still.occludedMax);
    CHECK_MSG(still.statFrames > 0 && still.trisOn < still.trisOff,
              "(a) the id pass draws fewer triangles with the occlusion (%llu < %llu)", still.trisOn, still.trisOff);

    // (b) THE WALK past the wall's edge: 8 m to the right over 120 frames
    Tally walk;
    for (int f = 0; f <= 120; ++f) {
        const float x = 12.0f * float(f) / 120.0f;
        setBoth(on, off, enginetest::testCameraDescLookAt(Vec3(x, 1.7f, 4.0f), Vec3(x * 0.5f, 1.2f, -20.0f)));
        stepCompare(on, off, walk, "(b) walk");
    }
    report("(b) walk", walk);
    CHECK_MSG(walk.disoccludedMax > 0u, "(b) the late pass disoccludes objects the walk reveals (max %u a frame)",
              walk.disoccludedMax);

    // (c) THE CAMERA CUT: behind the wall looking back, then home, 12 frames each leg
    Tally cut;
    for (int leg = 0; leg < 4; ++leg) {
        if (leg & 1)
            setBoth(on, off, enginetest::testCameraDescLookAt(eyeA, Vec3(0.0f, 1.2f, -20.0f)));
        else
            setBoth(on, off, enginetest::testCameraDescLookAt(Vec3(2.0f, 1.7f, -40.0f), Vec3(0.0f, 1.2f, 0.0f)));
        for (int i = 0; i < 12; ++i) stepCompare(on, off, cut, "(c) cut");
    }
    report("(c) cut", cut);

    // (d) THE TURN: 180 degrees over 30 frames about the eye
    Tally turn;
    for (int f = 0; f <= 30; ++f) {
        const float a = 3.14159265f * float(f) / 30.0f;
        setBoth(on, off, enginetest::testCameraDescLookAt(eyeA, Vec3(eyeA.x + 20.0f * std::sin(a), 1.2f,
                                                                      eyeA.z - 20.0f * std::cos(a))));
        stepCompare(on, off, turn, "(d) turn");
    }
    report("(d) turn", turn);

    // (e) LETTERBOXED 3:1, a short walk
    Tally lb;
    for (int f = 0; f <= 40; ++f) {
        const float x = 6.0f * float(f) / 40.0f;
        CameraDesc c = enginetest::testCameraDescLookAt(Vec3(x, 1.7f, 4.0f), Vec3(x * 0.5f, 1.2f, -20.0f));
        c.constrainAspect = true;
        c.aspect = 3.0f;
        setBoth(on, off, c);
        stepCompare(on, off, lb, "(e) letterbox");
    }
    report("(e) letterbox", lb);
    CHECK_MSG(lb.occludedMax > 0u, "(e) the letterboxed view's depth test rejects objects (occluded %u)", lb.occludedMax);

    // (f) ORTHOGRAPHIC, a short walk
    Tally ortho;
    for (int f = 0; f <= 40; ++f) {
        const float x = 6.0f * float(f) / 40.0f;
        CameraDesc c = enginetest::testCameraDescLookAt(Vec3(x, 3.0f, 4.0f), Vec3(x, 1.5f, -20.0f));
        c.orthographic = true;
        c.orthoSize = 8.0f;
        setBoth(on, off, c);
        stepCompare(on, off, ortho, "(f) ortho");
    }
    report("(f) ortho", ortho);
    CHECK_MSG(ortho.occludedMax > 0u, "(f) the orthographic view's depth test rejects objects (occluded %u)",
              ortho.occludedMax);

    gE->destroyView(on.view);
    gE->destroyView(off.view);
    gE->destroyScene(on.scene);
    gE->destroyScene(off.scene);
    engine.reset();
    std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
