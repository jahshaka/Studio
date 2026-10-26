// atom.hidden_cut — THE ID PASS CULLS WHAT A STOCK WALL HIDES (ATOM-4 stage 0; SPECS/ATOM_4_SPEC.md).
//
// A STOCK opaque wall (a front-culled box: the route `cullFront`, drawn by HlmsPbs, writing depth) in front of a
// field of Atom spheres the wall hides wholly, two Atom spheres in front of the wall, a sun; the camera still. Three
// worlds in lockstep, built in the same order (every slot and cluster has the same id in each):
//   on   — the two-pass occlusion (the default);
//   ref  — the same chain with its first cull's history thrown away before every frame (the first cull tests
//          nothing, the late pass records nothing: the frustum-only frame of THIS chain, the exactness twin);
//   shut — the occlusion door shut (Scene::setAtomOcclusionEnabled(false): no pyramid, no late pass).
// From the second measured frame, at both chain shapes (no prepass: ssr 0; the SSR prepass: ssr 1):
//   (a) the FIRST list's survivors behind the wall and (b) the LATE list's draws behind the wall (the cut probe's
//       groups by list, over the field's slots) are 0 — the hidden share is CULLED, not moved to the late list;
//   (c) the frame's depth test rejects every field sphere (AtomDrawStatus::occluded == the field);
//   (d) wherever an Atom surface is the picture's — the id is not empty and the id pass's own depth (kAtomViewDepth)
//       is the final depth — `on` and `ref` agree on the pixel being one, and their ids are word-equal there (the
//       cull changes no id the picture shows; behind the wall `on` holds nothing where `ref` names what it drew), and
//   (e) the PICTURE of `on` equals `shut`'s byte for byte (the occlusion and the pass order change no pixel);
//   (f) the stock depth pass and the merge record every frame while the wall is in view, and NOT AT ALL once it is
//       hidden (stage 1b: no stock item in view, today's frame) — (d) and (e) holding there too.
// At the base (ATOM-3's tip) (a) is the field and (c) is 0: the late pyramid holds no stock depth, the first cull's
// history neither, so every hidden sphere is drawn (spikes/atom-3/stage3-stop.txt). The monitor's GPU ms of the id
// passes, the stock depth pass, the decode and the opaque pass are printed per world and shape (information: the
// bars are the counts). Frames, never time: the engine's fixed clock.
#include "jahshaka/engine/Engine.h"
#include "jahshaka/engine/EngineInspect.h"
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
#include <map>
#include <set>
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
        char buf_[640];                                                         \
        std::snprintf(buf_, sizeof(buf_), fmt, __VA_ARGS__);                    \
        CHECK(cond, buf_);                                                      \
    } while (0)

static const unsigned kW = 960, kH = 540;
static const int kFieldX = 6, kFieldZ = 5;   // 30 spheres behind the wall

/// A UV sphere, the last column and ring WRAPPED to the first by index (ENGINE.md, "procedural seams do not weld").
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

struct World {
    const char *name = "";
    Scene *scene = nullptr;
    View *view = nullptr;
    OgreView *ov = nullptr;
    std::set<NodeId> field;
    NodeId wall = 0;
};

/// The wall (stock: front-culled), the field behind it, two spheres in front of it, a sun.
static void buildWorld(World &w)
{
    Scene *s = w.scene;
    s->setLodBias(1.0f);
    s->setAmbient(Colour(0.10f, 0.11f, 0.13f), Colour(0.05f, 0.05f, 0.05f));
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
    // THE WALL: 16 x 6 m, 1 m thick, 12 m in front of the eye; front-culled, so HlmsPbs draws its far face
    // (z = -6.5) and the route is `cullFront` — a stock surface that writes depth.
    {
        const NodeId n = s->createNode();
        s->setNodeTransform(n, Vec3(0.0f, 2.0f, -6.0f), Quat(), Vec3(16.0f, 6.0f, 1.0f));
        s->attachMesh(n, cube, wm);
        s->setNodeFaceCull(n, FaceCull::Front);
        w.wall = n;
    }
    // THE FIELD, 12 to 24 m deep: wholly inside the wall's silhouette from the eye (x <= 5 m at 30 m is 0.17 of the
    // depth, the wall's edge 0.64 at 12.5 m), no sphere touching another.
    for (int zi = 0; zi < kFieldZ; ++zi)
        for (int xi = 0; xi < kFieldX; ++xi) {
            const NodeId n = s->createNode();
            s->setNodeTransform(n, Vec3(-5.0f + 2.0f * float(xi), 0.8f + 0.4f * float((xi + zi) % 3),
                                        -12.0f - 3.0f * float(zi)),
                                Quat(), Vec3(0.9f, 0.9f, 0.9f));
            s->attachMesh(n, sphere, om);
            w.field.insert(n);
        }
    // IN FRONT OF THE WALL: two Atom spheres the picture shows (the exactness has Atom pixels to compare).
    for (int k = 0; k < 2; ++k) {
        const NodeId n = s->createNode();
        s->setNodeTransform(n, Vec3(k ? 3.0f : -3.0f, 1.2f, -2.0f), Quat(), Vec3(1.2f, 1.2f, 1.2f));
        s->attachMesh(n, sphere, om);
    }
    enginetest::addDirectionalLight(s, Vec3(-0.45f, -0.8f, -0.4f), 2.2f);
}

static bool readIds(OgreView *v, std::vector<uint32_t> &out)
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
    const unsigned w = tex->getWidth(), h = tex->getHeight();
    out.assign(size_t(w) * h * 2u, 0u);
    for (unsigned r = 0; r < h; ++r)
        std::memcpy(&out[size_t(r) * w * 2u], box.at(0, r, 0), size_t(w) * 2u * sizeof(uint32_t));
    return true;
}

/// A depth texture of the view's chain (D32: one float a texel).
static bool readDepth(OgreView *v, const char *name, std::vector<float> &out)
{
    Ogre::CompositorWorkspace *ws = v->workspace();
    if (!ws) return false;
    Ogre::TextureGpu *tex = nullptr;
    for (Ogre::CompositorNode *n : ws->getNodeSequence())
        if ((tex = n->getDefinedTexture(Ogre::IdString(name))) != nullptr) break;
    if (!tex) return false;
    Ogre::Image2 img;
    img.convertFromTexture(tex, 0u, 0u);
    const Ogre::TextureBox box = img.getData(0u);
    const unsigned w = tex->getWidth(), h = tex->getHeight();
    out.assign(size_t(w) * h, 0.0f);
    for (unsigned r = 0; r < h; ++r) std::memcpy(&out[size_t(r) * w], box.at(0, r, 0), size_t(w) * sizeof(float));
    return true;
}

/// THE PIXELS WHERE AN ATOM SURFACE IS THE PICTURE'S (the readers' rule: JahAtomView_ps.glsl): the id is not empty
/// and the id pass's own depth is the final depth, bit for bit.
/// With no stock item in view (`depthTest` false) the frame is today's: nothing but Atom draws before the opaque pass,
/// every id is the picture's, and the Atom view's depth is copied only for its readers — the ids alone decide.
static bool atomVisible(OgreView *v, const std::vector<uint32_t> &ids, std::vector<unsigned char> &vis,
                        bool depthTest = true)
{
    if (!depthTest) {
        vis.assign(ids.size() / 2u, 0u);
        for (size_t i = 0; i < vis.size(); ++i) vis[i] = ids[2 * i] != 0xFFFFFFFFu;
        return true;
    }
    std::vector<float> idDepth, finalDepth;
    if (!readDepth(v, detail::kAtomViewDepthTexture, idDepth) || !readDepth(v, detail::kSceneDepthTexture, finalDepth) ||
        idDepth.size() * 2u != ids.size() || finalDepth.size() != idDepth.size())
        return false;
    vis.assign(idDepth.size(), 0u);
    for (size_t i = 0; i < idDepth.size(); ++i)
        vis[i] = ids[2 * i] != 0xFFFFFFFFu && std::memcmp(&idDepth[i], &finalDepth[i], sizeof(float)) == 0;
    return true;
}

static Engine *gE = nullptr;

/// One frame of the three worlds; `ref`'s history is thrown away first (its first cull tests nothing).
static void step(World &ref)
{
    ref.ov->atomOcclusionHistory().valid = false;
    enginetest::frames(gE, 1);
}

/// The cut probe's field slots by list: the first list's survivors and the late list's.
struct ListCounts {
    bool valid = false;
    unsigned first = 0, late = 0;
};
static ListCounts probeField(World &w, World &ref)
{
    ListCounts c;
    if (!w.scene->armAtomCutProbe()) return c;
    AtomCutProbe p;
    for (int i = 0; i < 4 && !c.valid; ++i) {
        step(ref);
        if (w.scene->atomCutProbe(p) && p.valid) c.valid = true;
    }
    if (!c.valid) {
        std::printf("   %s: the cut probe did not read back (%s)\n", w.name, p.error.c_str());
        w.scene->disarmAtomCutProbe();
        return c;
    }
    std::map<unsigned, NodeId> nodeOfSlot;
    for (const AtomCutProbeObject &o : p.objects) nodeOfSlot[o.slot] = o.node;
    std::set<unsigned> first, late;
    for (const AtomCutProbeGroup &g : p.groups) {
        auto it = nodeOfSlot.find(g.slot);
        if (it == nodeOfSlot.end() || !w.field.count(it->second)) continue;
        (g.late ? late : first).insert(g.slot);
    }
    c.first = unsigned(first.size());
    c.late = unsigned(late.size());
    return c;
}

/// The monitor's GPU ms of the passes that matter, per workspace, medians over the measured frames.
static bool printPassMs(const std::vector<MonitorRecord> &recs, const char *shape, const std::string &onWs,
                        unsigned &stockRows, unsigned &stockAny, unsigned &onRecords)
{
    // THE ARM, PER RECORD: of the records holding on's id pass, how many hold BOTH the stock depth pass and the
    // merge (stockRows), and how many hold either (stockAny); onRecords counts the first.
    stockRows = stockAny = onRecords = 0u;
    for (const MonitorRecord &r : recs) {
        bool id = false, depth = false, merge = false;
        for (const FramePass &p : r.passes) {
            if (p.workspace.find(onWs) == std::string::npos) continue;
            id = id || p.pass == "Jahshaka atom id";
            depth = depth || p.pass == "Jahshaka atom stock depth";
            merge = merge || p.pass == "Jahshaka atom id depth merge";
        }
        if (!id) continue;
        ++onRecords;
        if (depth && merge) ++stockRows;
        if (depth || merge) ++stockAny;
    }
    static const char *kPasses[] = { "Jahshaka atom id", "Jahshaka atom id late", "Jahshaka atom stock depth",
                                     "Jahshaka atom id depth merge", "Jahshaka atom decode",
                                     "Jahshaka atom decode prepass", "Jahshaka SSR prepass", "Jahshaka opaque" };
    std::map<std::string, std::map<std::string, std::vector<float>>> ms;
    for (const MonitorRecord &r : recs)
        for (const FramePass &p : r.passes)
            for (const char *k : kPasses)
                if (p.pass == k && p.gpuMs >= 0.0f) ms[p.workspace][k].push_back(p.gpuMs);
    for (auto &ws : ms) {
        std::printf("   [%s] %s:", shape, ws.first.c_str());
        for (const char *k : kPasses) {
            auto it = ws.second.find(k);
            if (it == ws.second.end()) continue;
            std::vector<float> v = it->second;
            std::sort(v.begin(), v.end());
            std::printf(" | %s %.4f ms", k + 9, double(v[v.size() / 2]));
        }
        std::printf("\n");
    }
    return true;
}

/// `stockInView`: the wall shown (the stock arm: the stock depth pass and the merge record) or hidden (ATOM-4 stage
/// 1b: no stock item in view — neither records, the late pass draws on the scene's depth; the hidden-share bars
/// do not apply, the exactness and the picture still do).
static void runShape(World &on, World &ref, World &shut, int ssr, const char *shape, bool stockInView = true)
{
    std::printf("== %s (ssr %d)\n", shape, ssr);
    for (World *w : { &on, &ref, &shut }) {
        PostFxDesc fx;
        fx.allowOffscreen = true;
        fx.ssr = ssr;
        w->view->setPostFx(fx);
        w->scene->setNodeVisible(w->wall, stockInView);
    }
    // THE SETTLE: the chain (re)built, the first frame of the history, the counters a few frames late.
    for (int i = 0; i < 8; ++i) step(ref);
    CHECK_MSG(on.ov->chainAtomOcclusion() && ref.ov->chainAtomOcclusion() && !shut.ov->chainAtomOcclusion(),
              "%s: on and ref carry the occlusion, shut does not", shape);
    {
        enginetest::GpuTimingWindow timing(gE);   // the monitor's timestamps for the measured frames
        std::vector<MonitorRecord> drop;
        gE->popMonitorRecords(drop);
        int idsDiffer = 0, picDiffer = 0, compared = 0;
        size_t worstIds = 0, worstPic = 0;
        for (int f = 0; f < 4; ++f) {
            step(ref);
            std::vector<uint32_t> a, b;
            std::vector<unsigned char> va, vb;
            Image pa, pb;
            if (!readIds(on.ov, a) || !readIds(ref.ov, b) || a.size() != b.size() || !atomVisible(on.ov, a, va, stockInView) ||
                !atomVisible(ref.ov, b, vb, stockInView) || !on.view->readPixels(pa) || !shut.view->readPixels(pb) ||
                pa.rgba.size() != pb.rgba.size())
                continue;
            ++compared;
            size_t d = 0, shown = 0, hidden = 0;
            for (size_t i = 0; i < a.size(); i += 2) {
                const size_t px = i / 2;
                if (vb[px]) ++shown;
                else if (b[i] != 0xFFFFFFFFu) ++hidden;   // ref drew an id the picture does not show
                if (va[px] != vb[px] || (vb[px] && (a[i] != b[i] || a[i + 1] != b[i + 1]))) {
                    if (d < 3)
                        std::printf("   ids differ at (%zu, %zu): on %08x %08x (shown %d) | ref %08x %08x (shown %d)\n",
                                    px % kW, px / kW, a[i], a[i + 1], int(va[px]), b[i], b[i + 1], int(vb[px]));
                    ++d;
                }
            }
            if (f == 0)
                std::printf("   the Atom pixels the picture shows %zu; ids ref drew behind a nearer surface %zu\n",
                            shown, hidden);
            if (d) { ++idsDiffer; worstIds = std::max(worstIds, d); }
            size_t q = 0;
            for (size_t i = 0; i < pa.rgba.size(); i += 4)
                if (std::memcmp(&pa.rgba[i], &pb.rgba[i], 3) != 0) {
                    if (q < 3)
                        std::printf("   picture differs at (%zu, %zu): on %u %u %u | shut %u %u %u\n", (i / 4) % pa.width,
                                    (i / 4) / pa.width, pa.rgba[i], pa.rgba[i + 1], pa.rgba[i + 2], pb.rgba[i],
                                    pb.rgba[i + 1], pb.rgba[i + 2]);
                    ++q;
                }
            if (q) { ++picDiffer; worstPic = std::max(worstPic, q); }
        }
        std::vector<MonitorRecord> recs;
        gE->popMonitorRecords(recs);
        unsigned stockRows = 0u, stockAny = 0u, onRecords = 0u;
        printPassMs(recs, shape, std::string("hidden-cut-on"), stockRows, stockAny, onRecords);
        if (stockInView)
            CHECK_MSG(onRecords > 0u && stockRows == onRecords,
                      "%s: (f) the stock arm ran - the stock depth pass and the merge in every frame on's record "
                      "holds (%u of %u)", shape, stockRows, onRecords);
        else
            CHECK_MSG(onRecords > 0u && stockAny == 0u,
                      "%s: (f) no stock item in view - neither the stock depth pass nor the merge recorded (%u of %u "
                      "frames)", shape, stockAny, onRecords);
        CHECK_MSG(compared == 4, "%s: four frames' id images and pictures read (%d)", shape, compared);
        CHECK_MSG(idsDiffer == 0, "%s: (d) wherever an Atom surface is the picture's, on's id equals ref's word for word (%d frames differ, worst %zu px)",
                  shape, idsDiffer, worstIds);
        CHECK_MSG(picDiffer == 0, "%s: (e) on's picture equals shut's byte for byte (%d frames differ, worst %zu px)",
                  shape, picDiffer, worstPic);
    }
    const AtomDrawStatus so = on.scene->atomDrawStatus(), sr = ref.scene->atomDrawStatus(),
                         ss = shut.scene->atomDrawStatus();
    std::printf("   on: occluded %u disoccluded %u triangles %llu | ref: triangles %llu | shut: triangles %llu | "
                "atom items %u, stock cullFront %u\n",
                so.occluded, so.disoccluded, so.cutTriangles, sr.cutTriangles, ss.cutTriangles, so.atomItems,
                so.cullFront);
    if (!stockInView) return;
    const ListCounts lc = probeField(on, ref);
    std::printf("   on's field (%zu spheres behind the wall): FIRST list survivors %u, LATE list draws %u\n",
                on.field.size(), lc.first, lc.late);
    CHECK_MSG(so.cutValid && so.atomItems == unsigned(on.field.size()) + 2u && so.cullFront == 1u,
              "%s: the split draws the field and the two front spheres, the wall stays stock (%u atom, %u cullFront)",
              shape, so.atomItems, so.cullFront);
    CHECK_MSG(lc.valid && lc.first == 0u,
              "%s: (a) the FIRST list draws none of the spheres the stock wall hides (%u of %zu)", shape, lc.first,
              on.field.size());
    CHECK_MSG(lc.valid && lc.late == 0u,
              "%s: (b) the LATE list draws none of them either - culled, not moved (%u of %zu)", shape, lc.late,
              on.field.size());
    // ...AND NOT AS CLUSTERS (ATOM-8 stage 1's read): the late pass's per-cluster draws write no probe record, so (b)
    // alone cannot see them; the still camera reveals nothing - the late pass draws no cluster.
    CHECK_MSG(so.cutLateClusters == 0u,
              "%s: (b) the late pass draws no rejected cluster either (cutLateClusters %u)", shape, so.cutLateClusters);
    CHECK_MSG(so.occluded == unsigned(on.field.size()),
              "%s: (c) the frame's depth test rejects every hidden sphere (occluded %u of %zu)", shape, so.occluded,
              on.field.size());
}

int main()
{
    std::printf("== atom.hidden_cut: a stock wall in front of an Atom field; the id pass culls what it hides\n");
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-atom-hidden-cut-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->submitFrame(jahshaka::engine::FrameInput::delta(1.0f / 60.0f));
    gE = engine.get();

    World on, ref, shut;
    on.name = "on";
    ref.name = "ref";
    shut.name = "shut";
    // THE VIEWS FIRST (Ogre's startup order: a render target before any scene manager).
    for (World *w : { &on, &ref, &shut })
        w->view = gE->createOffscreenView(std::string("hidden-cut-") + w->name, kW, kH, Colour(0.2f, 0.3f, 0.5f), jahshaka::engine::OffscreenContract::Live);
    for (World *w : { &on, &ref, &shut }) {
        w->scene = gE->createScene(std::string("hidden-cut-") + w->name);
        buildWorld(*w);
    }
    shut.scene->setAtomOcclusionEnabled(false);
    for (World *w : { &on, &ref, &shut }) {
        w->view->setOffscreenContract(OffscreenContract::StillPicture);
        w->view->setScene(w->scene);
        w->view->setCamera(enginetest::testCameraDescLookAt(Vec3(0.0f, 1.5f, 6.0f), Vec3(0.0f, 1.5f, -20.0f)));
        w->ov = static_cast<OgreView *>(w->view);
    }
    runShape(on, ref, shut, 0, "no prepass");
    runShape(on, ref, shut, 1, "the SSR prepass");
    runShape(on, ref, shut, 0, "no stock in view", false);

    for (World *w : { &on, &ref, &shut }) gE->destroyView(w->view);
    for (World *w : { &on, &ref, &shut }) gE->destroyScene(w->scene);
    engine.reset();
    std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
