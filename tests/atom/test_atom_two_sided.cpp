// atom.two_sided — A TWO-SIDED MATERIAL RIDES ATOM (ATOM-TWO-SIDED-1,
// SPECS/briefs/ATOM-TWO-SIDED-1.md §6).
//
// A CULL_NONE material used to leave Atom for stock PBS (the split's `twoSided` reason):
// full resolution at every distance, no cut. Now the cut writes a two-sided item's
// command into the list's two-sided range (GpuCull::twoSidedFirst), the id pass and the
// caster cut draw that range with a no-cull pipeline, and the decode lights the side
// the triangle's winding says it sees (800.Atom_piece_ps.any, atomFacing). The fixture: a
// closed sphere and an OPEN plane, both two-sided, on a one-sided floor under a sun.
//   (a) THE ROUTE: both two-sided items are Atom items (atomTwoSided 2, cullFront 0, no
//       PBS item) — and a FRONT-culled node still stays on PBS under `cullFront`;
//   (b) THE PICTURE, front and back: the decoded picture against the same view through
//       stock PBS (the split's door shut), over the two objects' pixels, inside the
//       parity grid's bar (mean <= 1.0 code, <= 0.5 % beyond 8); from behind the open
//       plane is VISIBLE and lit as its back side (darker than its sunlit front);
//   (c) THE CLOSED MESH'S IDS equal its back-culled ones word for word (no back face
//       wins the depth test), front and back;
//   (d) THE SHADOW: the open plane casts from BOTH sides (its shadow on the floor, front
//       or back toward the sun, the same within 5 %), a back-culled plane with its back
//       toward the sun casts nothing (the arm's control), and the two-sided plane's lit
//       front equals the back-culled one's (the caster map is the same map: no new acne);
//   (e) the MASTER material's own two-sided flag toggled at run time moves the route and
//       the bucket within a bounded number of frames.
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
static const double kMeanBar = 1.0, kTailBar = 0.005;
static const int kTailCodes = 8;

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

/// An OPEN square in the XY plane, `n` x `n` quads, its front (normal) +Z.
static MeshData openPlane(int n, float size)
{
    MeshData d;
    for (int j = 0; j <= n; ++j)
        for (int i = 0; i <= n; ++i) {
            d.positions.insert(d.positions.end(),
                               { (float(i) / float(n) - 0.5f) * size, (float(j) / float(n) - 0.5f) * size, 0.0f });
            d.normals.insert(d.normals.end(), { 0.0f, 0.0f, 1.0f });
            d.uvs.insert(d.uvs.end(), { float(i) / float(n), float(j) / float(n) });
        }
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            const unsigned a = unsigned(j * (n + 1) + i), b = a + 1u;
            const unsigned c = unsigned((j + 1) * (n + 1) + i), e = c + 1u;
            d.indices.insert(d.indices.end(), { a, b, c, b, e, c });   // counter-clockwise seen from +Z
        }
    return d;
}

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

static double luma(const unsigned char *p) { return 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2]; }

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

struct Fixture {
    Engine *e = nullptr;
    View *view = nullptr;
    Scene *scene = nullptr;
    OgreScene *os = nullptr;
    OgreView *ov = nullptr;
    NodeId sphere = 0, plane = 0, floor = 0;

    unsigned slotOf(NodeId n) {
        os->ensureGpuScene(false);
        const detail::GpuScene &gs = os->gpuScene();
        for (unsigned s = 0; s < gs.slotCount(); ++s)
            if (gs.entry(s).ids[0] == unsigned(n)) return s;
        return 0xFFFFFFFFu;
    }
    /// The pixels whose id names `node` (the atom picture's own coverage).
    std::vector<uint8_t> maskOf(NodeId node) {
        std::vector<uint8_t> m(size_t(kW) * kH, 0u);
        std::vector<uint32_t> x, y;
        const unsigned s = slotOf(node);
        if (s == 0xFFFFFFFFu || !readIds(ov, x, y)) return m;
        for (size_t p = 0; p < m.size(); ++p)
            m[p] = x[p] != AtomId::kEmpty && (x[p] & AtomId::kSlotMask) == s;
        return m;
    }
};

struct Stat {
    size_t n = 0, beyond = 0;
    double mean = 0.0;
    int worst = 0;
};

static Stat compare(const Image &a, const Image &b, const std::vector<uint8_t> &mask)
{
    Stat st;
    double sum = 0.0;
    if (a.rgba.size() != b.rgba.size() || a.rgba.size() != mask.size() * 4u) return st;
    for (size_t p = 0; p < mask.size(); ++p) {
        if (!mask[p]) continue;
        // THE INTERIOR: a mask pixel whose four neighbours are in the mask too (a
        // silhouette pixel is the rasterisers' tie-breaking, not the decode).
        const size_t x = p % kW, y = p / kW;
        if (x == 0 || y == 0 || x + 1 >= kW || y + 1 >= kH) continue;
        if (!mask[p - 1] || !mask[p + 1] || !mask[p - kW] || !mask[p + kW]) continue;
        const int d = maxDiff(&a.rgba[p * 4u], &b.rgba[p * 4u]);
        ++st.n;
        sum += d;
        st.worst = std::max(st.worst, d);
        st.beyond += d > kTailCodes;
    }
    st.mean = st.n ? sum / double(st.n) : 0.0;
    return st;
}

static bool withinBar(const Stat &s) { return s.n > 0 && s.mean <= kMeanBar && double(s.beyond) <= kTailBar * double(s.n); }

static double meanLuma(const Image &img, const std::vector<uint8_t> &mask, size_t &n)
{
    double sum = 0.0;
    n = 0;
    for (size_t p = 0; p < mask.size() && img.rgba.size() == mask.size() * 4u; ++p)
        if (mask[p]) { sum += luma(&img.rgba[p * 4u]); ++n; }
    return n ? sum / double(n) : 0.0;
}

int main()
{
    std::printf("== atom.two_sided: a two-sided material on Atom — the route, the picture from both sides "
                "against stock PBS (mean <= %.1f code, <= %.1f %% beyond %d), the closed mesh's ids, the shadow\n",
                kMeanBar, kTailBar * 100.0, kTailCodes);
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-atom-two-sided-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Fixture F;
    F.e = engine.get();
    F.view = F.e->createOffscreenView("twosided", kW, kH, Colour(0, 0, 0));
    F.view->setOffscreenContract(OffscreenContract::StillPicture);
    F.scene = F.e->createScene("twosided");
    F.view->setScene(F.scene);
    F.view->setShadows(true);   // the sun's maps: the caster cut's two-sided range is under test
    F.os = static_cast<OgreScene *>(F.scene);
    F.ov = static_cast<OgreView *>(F.view);
    {
        PostFxDesc fx;
        fx.allowOffscreen = true;
        fx.ssr = 0;
        F.view->setPostFx(fx);
    }
    {
        GiParams gi;
        gi.mode = GiMode::Off;
        F.scene->setGlobalIllumination(gi);
    }
    F.scene->setLodBias(0.0f);
    F.scene->setAmbient(Colour(0.10f, 0.11f, 0.13f), Colour(0.05f, 0.05f, 0.05f));

    PbrParams twoSided;
    twoSided.albedo = Colour(0.70f, 0.55f, 0.35f);
    twoSided.roughness = 0.45f;
    twoSided.twoSided = true;
    const MaterialId twoSidedMat = F.scene->createPbrMaterial(twoSided);
    PbrParams planeP = twoSided;
    planeP.albedo = Colour(0.35f, 0.60f, 0.70f);
    const MaterialId planeMat = F.scene->createPbrMaterial(planeP);

    F.sphere = F.scene->createNode();
    F.scene->attachMesh(F.sphere, F.scene->createMesh(sphereMesh(48, 24, 0.6f)), twoSidedMat);
    enginetest::setNodePosition(F.scene, F.sphere, Vec3(-1.3f, 0.8f, 0.0f));
    F.plane = F.scene->createNode();
    F.scene->attachMesh(F.plane, F.scene->createMesh(openPlane(8, 1.6f)), planeMat);
    enginetest::setNodePosition(F.scene, F.plane, Vec3(1.2f, 1.0f, 0.0f));
    {
        MeshData g;
        g.positions = { -20.0f, 0.0f, -20.0f, 20.0f, 0.0f, -20.0f, 20.0f, 0.0f, 20.0f, -20.0f, 0.0f, 20.0f };
        g.normals = { 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0 };
        g.uvs = { 0, 0, 1, 0, 1, 1, 0, 1 };
        g.indices = { 0, 2, 1, 0, 3, 2 };
        PbrParams fp;
        fp.albedo = Colour(0.55f, 0.55f, 0.55f);
        fp.roughness = 0.8f;
        F.floor = F.scene->createNode();
        F.scene->attachMesh(F.floor, F.scene->createMesh(g), F.scene->createPbrMaterial(fp));
    }
    // THE SUN comes from +Z and above: the plane's FRONT (+Z) faces it.
    const NodeId sun = enginetest::addDirectionalLight(F.scene, Vec3(-0.25f, -0.80f, -0.55f), 2.2f);
    CHECK(sun != 0, "a sun");

    const Vec3 front(0.0f, 1.6f, 6.5f), back(0.0f, 1.6f, -6.5f), target(0.0f, 0.8f, 0.0f);
    enginetest::testCameraLookAt(F.view, front, target);
    for (int i = 0; i < 6; ++i) F.e->renderOneFrame();

    // ---- (a) THE ROUTE -------------------------------------------------------------
    {
        const AtomDrawStatus st = F.scene->atomDrawStatus();
        std::printf("  (a) stat: on %d atom %u (two-sided %u) pbs %u cullFront %u buckets %u\n", int(st.on), st.atomItems,
                    st.atomTwoSided, st.pbsItems, st.cullFront, st.buckets);
        CHECK(st.on, "(a) the split is live on this device");
        CHECK_MSG(st.atomItems == 3 && st.atomTwoSided == 2 && st.pbsItems == 0 && st.cullFront == 0,
                  "(a) the closed sphere and the open plane (both two-sided) and the floor are Atom items, two of them "
                  "two-sided (atom %u, two-sided %u, pbs %u)", st.atomItems, st.atomTwoSided, st.pbsItems);
        F.scene->setNodeFaceCull(F.sphere, FaceCull::Front);
        for (int i = 0; i < 3; ++i) F.e->renderOneFrame();
        const AtomDrawStatus fr = F.scene->atomDrawStatus();
        CHECK_MSG(fr.cullFront == 1 && fr.atomItems == 2 && fr.atomTwoSided == 1,
                  "(a) a FRONT-culled node stays on PBS under cullFront (cullFront %u, atom %u)", fr.cullFront,
                  fr.atomItems);
        F.scene->setNodeFaceCull(F.sphere, FaceCull::Material);
        for (int i = 0; i < 3; ++i) F.e->renderOneFrame();
    }

    // ---- (e) THE MASTER MATERIAL'S OWN FLAG, AT RUN TIME --------------------------------
    // Not a node cull (the twins, (a)): the two materials themselves turn one-sided and back.
    // The route (kGpuTwoSided on the slot, atomTwoSided) and the bucket (the two-sided
    // permutation's bucket empties: every item one-sided shares ONE bucket) follow within
    // a bounded number of frames.
    {
        auto flagsOf = [&](NodeId n) {
            const unsigned s = F.slotOf(n);
            uint32_t f = 0u;
            if (s != 0xFFFFFFFFu) std::memcpy(&f, &F.os->gpuScene().entry(s).boundsMax[3], sizeof(f));
            return f;
        };
        auto waitFor = [&](unsigned wantTwo, unsigned wantBuckets, int &frames) {
            for (frames = 0; frames < 60; ++frames) {
                F.e->renderOneFrame();
                const AtomDrawStatus st = F.scene->atomDrawStatus();
                if (st.atomItems == 3 && st.atomTwoSided == wantTwo && st.buckets == wantBuckets) return true;
            }
            return false;
        };
        PbrParams sphereOne = twoSided, planeOne = planeP;
        sphereOne.twoSided = planeOne.twoSided = false;
        F.scene->setPbrMaterial(twoSidedMat, sphereOne);
        F.scene->setPbrMaterial(planeMat, planeOne);
        int f1 = 0, f2 = 0;
        const bool off = waitFor(0u, 1u, f1);
        const bool offFlag = !(flagsOf(F.plane) & detail::kGpuTwoSided) && !(flagsOf(F.sphere) & detail::kGpuTwoSided);
        F.scene->setPbrMaterial(twoSidedMat, twoSided);
        F.scene->setPbrMaterial(planeMat, planeP);
        const bool on = waitFor(2u, 2u, f2);
        const bool onFlag = (flagsOf(F.plane) & detail::kGpuTwoSided) && (flagsOf(F.sphere) & detail::kGpuTwoSided);
        std::printf("  (e) master flag off: %s in %d frame(s); back on: %s in %d frame(s)\n", off ? "moved" : "STUCK", f1,
                    on ? "moved" : "STUCK", f2);
        CHECK_MSG(off && offFlag,
                  "(e) the masters turned one-sided: the items stay Atom, lose kGpuTwoSided and share ONE bucket "
                  "within %d frames", f1);
        CHECK_MSG(on && onFlag,
                  "(e) turned two-sided again: kGpuTwoSided back and the two-sided bucket back within %d frames", f2);
    }

    // ---- (b) + (c): front and back --------------------------------------------------
    double planeFrontLuma = 0.0;
    for (int side = 0; side < 2; ++side) {
        const char *name = side ? "back" : "front";
        enginetest::testCameraLookAt(F.view, side ? back : front, target);
        F.scene->setAtomDrawEnabled(true);
        Image atomPic, pbsPic;
        int fa = 0, fb = 0;
        const bool sa = settle(F.e, F.view, atomPic, fa);
        const std::vector<uint8_t> sphereMask = F.maskOf(F.sphere), planeMask = F.maskOf(F.plane);
        std::vector<uint32_t> ix, iy;
        const bool idsRead = readIds(F.ov, ix, iy);
        // (c) the closed sphere back-culled: its ids, word for word.
        F.scene->setNodeFaceCull(F.sphere, FaceCull::Back);
        Image culledPic;
        int fc = 0;
        settle(F.e, F.view, culledPic, fc);
        std::vector<uint32_t> cx, cy;
        const bool culledRead = readIds(F.ov, cx, cy);
        const unsigned sphereSlot = F.slotOf(F.sphere);
        F.scene->setNodeFaceCull(F.sphere, FaceCull::Material);
        {
            size_t covered = 0, differ = 0;
            for (size_t p = 0; idsRead && culledRead && p < ix.size(); ++p) {
                const bool a = ix[p] != AtomId::kEmpty && (ix[p] & AtomId::kSlotMask) == sphereSlot;
                const bool b = cx[p] != AtomId::kEmpty && (cx[p] & AtomId::kSlotMask) == sphereSlot;
                if (!a && !b) continue;
                ++covered;
                differ += a != b || ix[p] != cx[p] || iy[p] != cy[p];
            }
            std::printf("  (c) [%s] closed sphere ids: %zu px covered, %zu differ from the back-culled ids\n", name,
                        covered, differ);
            CHECK_MSG(idsRead && culledRead && covered > 5000 && differ == 0,
                      "(c) [%s] the two-sided closed sphere's ids equal its back-culled ones word for word "
                      "(%zu of %zu px differ)", name, differ, covered);
        }
        F.scene->setAtomDrawEnabled(false);
        const bool sb = settle(F.e, F.view, pbsPic, fb);
        F.scene->setAtomDrawEnabled(true);
        CHECK_MSG(sa && sb, "[%s] both pictures settled (%d / %d frames)", name, fa, fb);
        const Stat ss = compare(atomPic, pbsPic, sphereMask), ps = compare(atomPic, pbsPic, planeMask);
        std::printf("  (b) [%s] sphere: %zu px mean %.3f worst %d beyond %zu | plane: %zu px mean %.3f worst %d beyond %zu\n",
                    name, ss.n, ss.mean, ss.worst, ss.beyond, ps.n, ps.mean, ps.worst, ps.beyond);
        CHECK_MSG(withinBar(ss) && ss.n > 5000,
                  "(b) [%s] the two-sided sphere decodes as stock PBS draws it (mean %.3f, %zu beyond %d of %zu)", name,
                  ss.mean, ss.beyond, kTailCodes, ss.n);
        CHECK_MSG(withinBar(ps) && ps.n > 5000,
                  "(b) [%s] the two-sided open plane decodes as stock PBS draws it (mean %.3f, %zu beyond %d of %zu)",
                  name, ps.mean, ps.beyond, kTailCodes, ps.n);
        size_t n = 0;
        const double l = meanLuma(atomPic, planeMask, n);
        std::printf("  (b) [%s] the plane's mean luma %.2f over %zu px\n", name, l, n);
        if (!side) {
            planeFrontLuma = l;
        } else {
            CHECK_MSG(n > 5000 && l > 1.0 && l < planeFrontLuma - 10.0,
                      "(b) the open plane is VISIBLE from behind (%zu px, luma %.2f) and lit as its back side, turned "
                      "from the sun (front %.2f)", n, l, planeFrontLuma);
        }
    }

    // ---- (d) THE SHADOW --------------------------------------------------------------
    {
        // From behind and above: the plane's shadow falls toward -Z, between the camera
        // and the plane, on floor pixels the plane does not cover (counted through the
        // floor's own ids).
        enginetest::testCameraLookAt(F.view, Vec3(0.5f, 5.0f, -5.5f), Vec3(0.8f, 0.0f, -0.8f));
        F.scene->setNodeVisible(F.sphere, false);
        F.scene->setAtomDrawEnabled(true);
        auto shot = [&](Image &img, std::vector<uint8_t> &floorMask) {
            int f = 0;
            const bool ok = settle(F.e, F.view, img, f);
            floorMask = F.maskOf(F.floor);
            return ok;
        };
        const Quat facing(0.0f, 0.0f, 0.0f, 1.0f), turned(0.0f, 1.0f, 0.0f, 0.0f);   // (x,y,z,w): 180 deg about Y
        auto placePlane = [&](const Quat &q) {
            F.scene->setNodeTransform(F.plane, Vec3(1.2f, 1.0f, 0.0f), q, Vec3(1.0f, 1.0f, 1.0f));
        };
        Image ref, front2, back2, culled;
        std::vector<uint8_t> mRef, mFront, mBack, mCulled;
        F.scene->setNodeVisible(F.plane, false);
        const bool r0 = shot(ref, mRef);
        F.scene->setNodeVisible(F.plane, true);
        placePlane(facing);
        const bool r1 = shot(front2, mFront);
        placePlane(turned);
        const bool r2 = shot(back2, mBack);
        F.scene->setNodeFaceCull(F.plane, FaceCull::Back);
        const bool r3 = shot(culled, mCulled);
        F.scene->setNodeFaceCull(F.plane, FaceCull::Material);
        auto shadowed = [&](const Image &img, const std::vector<uint8_t> &m) {
            size_t count = 0;
            for (size_t p = 0; p < m.size() && img.rgba.size() == ref.rgba.size(); ++p)
                if (m[p] && mRef[p] && luma(&img.rgba[p * 4u]) < luma(&ref.rgba[p * 4u]) - 20.0) ++count;
            return count;
        };
        if (const char *outDir = std::getenv("ATOM_TWO_SIDED_OUT")) {
            const Image *pics[4] = { &ref, &front2, &back2, &culled };
            const char *names[4] = { "shadow_ref", "shadow_front", "shadow_back", "shadow_culled" };
            for (int k = 0; k < 4; ++k)
                if (FILE *f = std::fopen((std::string(outDir) + "/" + names[k] + ".ppm").c_str(), "wb")) {
                    std::fprintf(f, "P6\n%u %u\n255\n", pics[k]->width, pics[k]->height);
                    for (size_t i = 0; i + 3 < pics[k]->rgba.size(); i += 4) std::fwrite(&pics[k]->rgba[i], 1, 3, f);
                    std::fclose(f);
                }
        }
        const size_t s1 = shadowed(front2, mFront), s2 = shadowed(back2, mBack), s3 = shadowed(culled, mCulled);
        std::printf("  (d) shadowed floor px: front toward the sun %zu, back toward the sun %zu, back-culled back toward "
                    "the sun %zu\n", s1, s2, s3);
        CHECK(r0 && r1 && r2 && r3, "(d) every shadow picture settled");
        CHECK_MSG(s1 > 1000 && s2 > 1000 && double(std::max(s1, s2) - std::min(s1, s2)) <= 0.05 * double(s1),
                  "(d) the two-sided open plane casts from BOTH sides, the same shadow within 5 %% (%zu / %zu px)", s1,
                  s2);
        CHECK_MSG(double(s3) < 0.01 * double(std::max<size_t>(s1, 1)),
                  "(d) the control: back-culled with its back toward the sun it casts nothing (%zu px)", s3);
        // ...and the floor against stock PBS's two-sided caster, the plane back toward the sun.
        placePlane(turned);
        F.scene->setAtomDrawEnabled(false);
        Image pbsBack;
        int f = 0;
        settle(F.e, F.view, pbsBack, f);
        F.scene->setAtomDrawEnabled(true);
        const Stat fs = compare(back2, pbsBack, mBack);
        std::printf("  (d) floor vs stock PBS (back toward the sun): %zu px mean %.3f worst %d beyond %zu\n", fs.n, fs.mean,
                    fs.worst, fs.beyond);
        CHECK_MSG(withinBar(fs), "(d) the floor under the two-sided caster matches stock PBS's (mean %.3f, %zu beyond)",
                  fs.mean, fs.beyond);
        placePlane(facing);
        F.scene->setNodeVisible(F.sphere, true);
    }
    // (d) NO NEW ACNE: the plane's sunlit front, two-sided against back-culled (the same
    // caster map: its front faces the sun either way), from the front camera.
    {
        enginetest::testCameraLookAt(F.view, front, target);
        Image two, one;
        int f = 0;
        settle(F.e, F.view, two, f);
        const std::vector<uint8_t> m = F.maskOf(F.plane);
        F.scene->setNodeFaceCull(F.plane, FaceCull::Back);
        settle(F.e, F.view, one, f);
        F.scene->setNodeFaceCull(F.plane, FaceCull::Material);
        const Stat st = compare(two, one, m);
        std::printf("  (d) the plane's sunlit front, two-sided vs back-culled: %zu px mean %.3f worst %d\n", st.n, st.mean,
                    st.worst);
        CHECK_MSG(st.n > 5000 && st.worst <= 1,
                  "(d) the two-sided plane's lit front equals the back-culled one's (worst %d codes): no new acne",
                  st.worst);
    }

    F.e->destroyView(F.view);
    F.e->destroyScene(F.scene);
    std::printf("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
