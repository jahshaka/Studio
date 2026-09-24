// far_blas_measure — ATOM-FARBLAS-1's NUMBERS (a TOOL, EXCLUDE_FROM_ALL; asserts
// nothing). A5b §4: one TLAS whose instances are written TWICE, a near copy over
// the level-0 BLAS and a far copy over the mesh's COARSEST level. This prints what
// that costs, from the engine's own RayQueryStatus. The before/after A/B is the
// same rows printed by a binary built at the lane's base (whose status has no
// far fields: the far half is then the difference of the two runs) and by this
// one, which also prints the far copies and the coarse structures directly.
//
//   far_blas_measure assets  — per shipped mesh (the primitives the bake gives a
//       chain, the two shipped high-poly models, the 20k sphere; the same list
//       atom.cluster_cut measures), ONE instance in its own scene: the chain's
//       level count and triangles, and the BLAS count / triangles / bytes after
//       compaction has settled.
//   far_blas_measure picture — the gather's far field on CHAINED content against
//       the old near-only long ray, and its cost with the far query on/off
//       (pictureMode's own note).
//   far_blas_measure tlas    — THE LATTICE: 8,000 instances cycling over those
//       meshes (+ a ground), one instance moved every frame so every frame
//       rebuilds (or, with JAH_RQ_REFIT=1 in the environment, refits) the TLAS:
//       the TLAS bytes, the TLAS GPU ms (median of 120 frames, read back from the
//       tier's own timestamps), and the CPU ms of the instance write.
//
// CLOCKS: the GPU ms here are absolute and therefore provisional (an agent cannot
// lock the clocks); compare the two binaries' rows as a RATIO taken minutes apart.
#include "cluster_fixtures.h"

#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static void render(Engine *e, int frames)
{
    for (int i = 0; i < frames; ++i) e->renderOneFrame();
}

static float median(std::vector<float> v)
{
    if (v.empty()) return -1.0f;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

struct Baked { std::string name; MeshData data; float extent = 1.0f; };

static std::vector<Baked> bakedShipped()
{
    std::vector<Baked> out;
    for (auto &named : clusterfix::shippedMeshes(JAHSHAKA_TEST_SOURCE_DIR, CLUSTER_FIXTURE_DIR)) {
        clusterfix::Fixture f;
        if (!clusterfix::bake(f, named.mesh, named.name)) continue;
        Baked b;
        b.name = named.name;
        b.data = f.data;
        b.extent = f.extent > 0.0f ? f.extent : 1.0f;
        out.push_back(std::move(b));
    }
    return out;
}

static unsigned tris(const std::vector<unsigned> &idx) { return unsigned(idx.size() / 3u); }

static int assetsMode(Engine *e, View *view)
{
    const std::vector<Baked> meshes = bakedShipped();
    std::printf("== per shipped mesh: one instance, compaction settled (60 frames) ==\n");
    std::printf("%-22s %6s %8s %8s | %5s %9s %11s | %6s %11s | %5s %11s\n", "mesh", "levels",
                "tris L0", "tris Lc", "blas", "blas tris", "blas bytes", "coarse", "coarse B",
                "@400m", "coarsest B");
    for (const Baked &b : meshes) {
        Scene *s = e->createScene("farblas-asset-" + b.name);
        view->setScene(s);
        const NodeId n = s->createNode();
        const MeshId m = s->createMesh(b.data);
        PbrParams p; p.albedo = Colour(0.6f, 0.6f, 0.6f); p.metalness = 0.0f; p.roughness = 0.7f;
        const MaterialId mat = s->createPbrMaterial(p);
        if (!n || !m || !mat || !s->attachMesh(n, m, mat)) {
            std::printf("%-22s !! attach failed: %s\n", b.name.c_str(), e->lastError().c_str());
            e->destroyScene(s);
            continue;
        }
        const float k = 1.0f / b.extent;
        s->setNodeTransform(n, Vec3{ 0, 0, 0 }, Quat(), Vec3{ k, k, k });
        enginetest::addDirectionalLight(s, Vec3{ -0.4f, -1.0f, -0.55f }, 3.0f);
        view->setCamera(enginetest::testCameraDescLookAt(Vec3{ 1.5f, 1.0f, 2.0f }, Vec3{ 0, 0, 0 }));
        render(e, 60);
        const RayQueryStatus st = s->rayQueryStatus();
        const unsigned levels = unsigned(b.data.lodIndices.size()) + 1u;
        const unsigned coarse =
            b.data.lodIndices.empty() ? tris(b.data.indices) : tris(b.data.lodIndices.back());
        e->destroyScene(s);
        // THE COARSEST STRUCTURE ALONE: the same mesh 400 m away in a FRESH scene,
        // where the ray rule sends the near copy to the coarsest level too, so the
        // only level>0 structure alive is the one the far copies use.
        Scene *s2 = e->createScene("farblas-asset-far-" + b.name);
        view->setScene(s2);
        const NodeId n2 = s2->createNode();
        const MeshId m2 = s2->createMesh(b.data);
        const MaterialId mat2 = s2->createPbrMaterial(p);
        int farCount = -1;
        unsigned long long farBytes = 0;
        if (n2 && m2 && mat2 && s2->attachMesh(n2, m2, mat2)) {
            s2->setNodeTransform(n2, Vec3{ 0, 0, -400.0f }, Quat(), Vec3{ k, k, k });
            view->setCamera(enginetest::testCameraDescLookAt(Vec3{ 0, 0, 0 }, Vec3{ 0, 0, -400.0f }));
            render(e, 60);
            const RayQueryStatus st2 = s2->rayQueryStatus();
            farCount = st2.levelBlasCount;
            farBytes = st2.levelBlasBytes;
        }
        e->destroyScene(s2);
        std::printf("%-22s %6u %8u %8u | %5d %9d %11llu | %6d %11llu | %5d %11llu\n", b.name.c_str(),
                    levels, tris(b.data.indices), coarse, st.blasCount, st.triangles,
                    (unsigned long long)st.blasBytes, st.levelBlasCount,
                    (unsigned long long)st.levelBlasBytes, farCount, farBytes);
        std::fflush(stdout);
    }
    return 0;
}

static int tlasMode(Engine *e, View *view)
{
    const std::vector<Baked> meshes = bakedShipped();
    if (meshes.empty()) { std::printf("FAIL: no shipped meshes\n"); return 1; }
    Scene *s = e->createScene("farblas-lattice");
    view->setScene(s);
    std::vector<MeshId> ids;
    std::vector<float> scale;
    for (const Baked &b : meshes) {
        const MeshId m = s->createMesh(b.data);
        if (!m) continue;
        ids.push_back(m);
        scale.push_back(0.8f / b.extent);
    }
    PbrParams p; p.albedo = Colour(0.6f, 0.55f, 0.5f); p.metalness = 0.0f; p.roughness = 0.6f;
    const MaterialId mat = s->createPbrMaterial(p);
    const unsigned kInstances = 8000u;
    const int side = 20;
    const float spacing = 1.5f, half = 0.5f * spacing * float(side - 1);
    std::vector<NodeId> nodes;
    for (unsigned i = 0; i < kInstances; ++i) {
        const int gx = int(i) % side, gy = (int(i) / side) % side, gz = int(i) / (side * side);
        const NodeId n = s->createNode();
        const size_t mi = i % ids.size();
        if (!n || !s->attachMesh(n, ids[mi], mat)) { std::printf("FAIL: node %u\n", i); return 1; }
        const float k = scale[mi];
        s->setNodeTransform(n, Vec3{ float(gx) * spacing - half, float(gy) * spacing,
                                     float(gz) * spacing - half }, Quat(), Vec3{ k, k, k });
        nodes.push_back(n);
    }
    const NodeId ground = enginetest::addTestCube(s, Colour(0.5f, 0.5f, 0.5f), 0.0f, 0.9f);
    enginetest::setNodePosition(s, ground, Vec3(0.0f, -1.0f, 0.0f));
    enginetest::setNodeScale(s, ground, Vec3(200.0f, 1.0f, 200.0f));
    enginetest::addDirectionalLight(s, Vec3{ -0.4f, -1.0f, -0.55f }, 4.0f);
    view->setCamera(enginetest::testCameraDescLookAt(Vec3{ 0.0f, 20.0f, 45.0f }, Vec3{ 0, 10, 0 }));
    render(e, 90);   // BLAS builds + compaction settle

    std::vector<float> tlasMs, gatherMs;
    const bool refit = std::getenv("JAH_RQ_REFIT") != nullptr;
    for (int f = 0; f < 150; ++f) {
        // ONE instance moves every frame: the epoch moves, so the tier re-writes
        // every instance and rebuilds (or refits) the TLAS — the per-frame cost
        // of a scene with one mover.
        const float dx = 0.01f * float(f % 2 ? 1 : -1);
        s->setNodeTransform(nodes[0], Vec3{ -half + dx, 0.0f, -half }, Quat(),
                            Vec3{ scale[0], scale[0], scale[0] });
        e->renderOneFrame();
        if (f < 30) continue;
        const RayQueryStatus st = s->rayQueryStatus();
        if (st.tlasMs >= 0.0f) tlasMs.push_back(st.tlasMs);
        if (st.gatherMs >= 0.0f) gatherMs.push_back(st.gatherMs);
    }
    const RayQueryStatus st = s->rayQueryStatus();
    std::printf("== the lattice: %u instances over %zu meshes + a ground; %s every frame ==\n",
                kInstances, ids.size(), refit ? "REFIT (JAH_RQ_REFIT)" : "REBUILD");
    std::printf("   tlas instances %d (+%d far), blas %d (%d tris, %llu bytes; coarse %d, %llu B), "
                "tlas bytes %llu, lastWasRefit %d, builds %llu refits %llu\n",
                st.instances, st.farInstances, st.blasCount, st.triangles,
                (unsigned long long)st.blasBytes, st.levelBlasCount,
                (unsigned long long)st.levelBlasBytes,
                (unsigned long long)st.tlasBytes, int(st.lastWasRefit),
                (unsigned long long)st.tlasBuilds, (unsigned long long)st.tlasRefits);
    std::printf("   tlas GPU ms median %.4f (n=%zu); instance write CPU ms median %.4f (n=%zu)\n",
                double(median(tlasMs)), tlasMs.size(), double(median(gatherMs)), gatherMs.size());
    return 0;
}

// ---------------------------------------------------------------------------
// far_blas_measure picture — THE FAR FIELD'S PICTURE ON CHAINED CONTENT. The
// gather traces near (fine, to the outer half extent) and, for an escaping ray,
// far (each mesh's COARSEST level, to the far plane). The reference is the old
// long ray: a NEAR-ONLY trace of the fine geometry to the outer box's diagonal
// (207.85 m at a 60 m half extent), the far query off. The fixture is the open
// scene built from SHIPPED, CHAINED meshes: the endless plane (8 levels) as a
// 400 m ground, and a ring of the shipped meshes at 10 m scale between 70 and
// 150 m — so every far hit lands on a coarse level whose geometry differs from
// the reference's. Three arms, frozen, High tier (the gather at stride 16):
// half+FAR (shipped), half+SKY (the far query off), and the reference twice (the
// instrument floor). Then the gather's GPU ms with the far query on vs off,
// interleaved on a 1920x1080 view (ratios; clocks not locked).
struct Delta { unsigned moved = 0, total = 0, worst = 0; double meanMoved = 0, meanAll = 0; };
static Delta deltaOf(const Image &a, const Image &b)
{
    Delta d;
    if (a.width != b.width || a.height != b.height) return d;
    d.total = a.width * a.height;
    double sum = 0.0;
    for (size_t i = 0; i + 3 < a.rgba.size(); i += 4) {
        unsigned w = 0;
        for (int c = 0; c < 3; ++c)
            w = std::max(w, unsigned(std::abs(int(a.rgba[i + c]) - int(b.rgba[i + c]))));
        if (w) { ++d.moved; sum += w; d.worst = std::max(d.worst, w); }
    }
    d.meanMoved = d.moved ? sum / d.moved : 0.0;
    d.meanAll = d.total ? sum / d.total : 0.0;
    return d;
}
static void tuneFar(Scene *s, float rayLength, bool farOff)
{
    GatherTuning t;
    t.freezeFrameIndex = true;
    t.rayLength = rayLength;
    t.farQueryOff = farOff;
    s->setGatherTuning(t);
}

static int pictureMode(Engine *e, View *view)
{
    const std::vector<Baked> meshes = bakedShipped();
    const Baked *plane = nullptr;
    for (const Baked &b : meshes)
        if (b.name.find("endlessplane") != std::string::npos) plane = &b;
    if (!plane) { std::printf("FAIL: no endless plane among the shipped meshes\n"); return 1; }
    Scene *s = e->createScene("farblas-picture");
    view->setScene(s);
    PostFxDesc fx; fx.allowOffscreen = true; fx.ssr = 1;   // High's row
    view->setPostFx(fx);
    view->setShadows(true);
    PbrParams p; p.albedo = Colour(0.45f, 0.45f, 0.45f); p.metalness = 0.0f; p.roughness = 0.9f;
    const MaterialId grey = s->createPbrMaterial(p);
    p.albedo = Colour(0.6f, 0.3f, 0.25f);
    const MaterialId red = s->createPbrMaterial(p);
    const MeshId groundMesh = s->createMesh(plane->data);
    const NodeId ground = s->createNode();
    if (!groundMesh || !ground || !s->attachMesh(ground, groundMesh, grey)) {
        std::printf("FAIL: ground: %s\n", e->lastError().c_str());
        return 1;
    }
    const float gk = 400.0f / plane->extent;
    s->setNodeTransform(ground, Vec3{ 0, 0, 0 }, Quat(), Vec3{ gk, gk, gk });
    unsigned ring = 0;
    for (const Baked &b : meshes) {
        if (&b == plane) continue;
        const MeshId m = s->createMesh(b.data);
        if (!m) continue;
        for (int k = 0; k < 3; ++k, ++ring) {
            const float ang = 0.61f * float(ring);
            const float r = 70.0f + 80.0f * float((ring * 37u) % 11u) / 10.0f;
            const NodeId n = s->createNode();
            if (!n || !s->attachMesh(n, m, red)) continue;
            const float sk = 10.0f / b.extent;
            s->setNodeTransform(n, Vec3{ r * std::cos(ang), 4.0f, r * std::sin(ang) }, Quat(),
                                Vec3{ sk, sk, sk });
        }
    }
    const NodeId cube = enginetest::addTestCube(s, Colour(0.6f, 0.25f, 0.2f), 0.0f, 0.9f);
    enginetest::setNodePosition(s, cube, Vec3(0.0f, 0.5f, 0.0f));
    enginetest::addDirectionalLight(s, Vec3{ -0.4f, -1.0f, -0.55f }, 4.0f);
    SkyDesc sky; sky.mode = SkyMode::Atmosphere;
    s->setSky(sky);
    s->setAmbient(Colour(0.05f, 0.05f, 0.06f), Colour(0.03f, 0.03f, 0.035f));
    const CameraDesc cam = enginetest::testCameraDescLookAt(Vec3{ 4.0f, 2.2f, 6.0f }, Vec3{ 0.0f, 0.6f, 0.0f });
    view->setCamera(cam);
    GiParams gi;
    gi.mode = GiMode::VctPccHybrid;
    gi.quality = GiQuality::High;
    gi.cascades = true;
    gi.ddgi = GiToggle::Off;
    gi.numBounces = 1;
    gi.gather = GiToggle::On;
    s->setGlobalIllumination(gi);
    tuneFar(s, 0.0f, false);
    render(e, 90);
    const GiStatus gs = s->giStatus();
    const float half = gs.cascades.empty() ? 0.0f : gs.cascades.back().halfSize;
    const float diag = std::sqrt(3.0f) * 2.0f * half;
    const RayQueryStatus rq = s->rayQueryStatus();
    std::printf("== the far field's picture on chained content: %u ring objects at 70-150 m, "
                "ground %s x%.1f; outer half %.2f m, reference %.2f m ==\n",
                ring, plane->name.c_str(), double(gk), double(half), double(diag));
    std::printf("   gather on %d running %d; rays: %d near + %d far copies, %d blas (%d coarse)\n",
                int(gs.gather.on), int(gs.gather.running), rq.instances, rq.farInstances,
                rq.blasCount, rq.levelBlasCount);
    Image ship, nofar, ref, ref2;
    tuneFar(s, 0.0f, false); render(e, 4); view->readPixels(ship);
    tuneFar(s, 0.0f, true);  render(e, 4); view->readPixels(nofar);
    tuneFar(s, diag, true);  render(e, 4); view->readPixels(ref);
    tuneFar(s, diag, true);  render(e, 4); view->readPixels(ref2);
    const Delta fl = deltaOf(ref, ref2), dShip = deltaOf(ref, ship), dNo = deltaOf(ref, nofar),
                dAB = deltaOf(nofar, ship);
    std::printf("   instrument floor (reference twice) %u/%u px\n", fl.moved, fl.total);
    std::printf("   vs the near-only reference: half+FAR %u px (mean %.2f, worst %u, meanAll %.4f)"
                " | half+SKY %u px (mean %.2f, worst %u, meanAll %.4f) | FAR vs SKY %u px\n",
                dShip.moved, dShip.meanMoved, dShip.worst, dShip.meanAll, dNo.moved, dNo.meanMoved,
                dNo.worst, dNo.meanAll, dAB.moved);
    std::fflush(stdout);

    View *big = e->createOffscreenView("farblas-cost", 1920u, 1080u, Colour(0, 0, 0));

    if (big) big->setOffscreenContract(OffscreenContract::StillPicture);   // a measured picture
    if (!big) return 1;
    big->setShadows(true);
    view->setEnabled(false);
    big->setScene(s);
    big->setPostFx(fx);
    big->setCamera(cam);
    tuneFar(s, 0.0f, false);
    render(e, 120);
    std::vector<float> ms[3];
    for (int round = 0; round < 10; ++round)
        for (int a = 0; a < 3; ++a) {
            tuneFar(s, a == 2 ? diag : 0.0f, a != 0);
            for (int fr = 0; fr < 36; ++fr) {
                e->renderOneFrame();
                if (fr < 6) continue;
                const GatherStatus q = s->giStatus().gather;
                if (q.placeMs >= 0.0f && q.traceMs >= 0.0f && q.integrateMs >= 0.0f)
                    ms[a].push_back(q.placeMs + q.traceMs + q.integrateMs);
            }
        }
    const float m0 = median(ms[0]), m1 = median(ms[1]), m2 = median(ms[2]);
    std::printf("   gather GPU ms (median of %zu/%zu/%zu): far ON %.4f | far OFF %.4f | near-only "
                "%.0f m %.4f -> ratio ON/OFF %.3f, ON/ref %.3f\n",
                ms[0].size(), ms[1].size(), ms[2].size(), double(m0), double(m1), double(diag),
                double(m2), m1 > 0 ? double(m0 / m1) : -1.0, m2 > 0 ? double(m0 / m2) : -1.0);
    return fl.moved ? 1 : 0;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const std::string mode = argc > 1 ? argv[1] : "assets";
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "far-blas-measure-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();
    View *view = e->createOffscreenView("farblas", 640u, 360u, Colour(0, 0, 0));
    if (view) view->setOffscreenContract(OffscreenContract::StillPicture);   // a measured picture
    if (!view) { std::printf("FAIL: view: %s\n", e->lastError().c_str()); return 1; }
    if (!e->rayQueryAvailable() || !e->rayTracing()) {
        std::printf("FAIL: this machine has no ray queries\n");
        return 1;
    }
    if (mode == "tlas") return tlasMode(e, view);
    if (mode == "picture") return pictureMode(e, view);
    return assetsMode(e, view);
}
