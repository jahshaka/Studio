// atom.density_dolly — THE DENSITY CAP, TRIANGLES AGAINST DISTANCE (ATOM-5 stage 3; SPECS/ATOM_5_SPEC.md).
//
// THE RULE UNDER TEST (Types.h clusterGroupVerdict, its GLSL twin JahClusterCut.glsl jahClusterGroupVerdict): a view's
// cluster group is affordable when error < allowed, OR when error < allowed x cap AND every cluster it replaces is
// SUB-SAMPLE (its mean triangle, 2 r / sqrt(n), times the instance's scale under one sample's footprint at the group).
//
// THE FIXTURE, DERIVED (the builder's call). The cap's clause needs a group whose error is >= tol x footprint while the
// triangles it replaces are < footprint — an error LARGER THAN tol TIMES THE TRIANGLES' SIZE, i.e. relief deeper than
// the triangles are wide (Bistro's erased shutters and islands, spikes/atom-5/PREFLIGHT.md). Smooth or gently rolling
// content never has it (the next paragraph), and neither had the first two fixtures tried here (flat fins, a bilinear
// relief: the bake keeps their error under 0.6 x the replaced triangles, so the cap never engaged — the twin's sweep).
// So the facade is ROUGH: a 1 m panel of 64 x 64 quads (8,192 triangles, 1.56 cm cells) whose every vertex is
// displaced by a deterministic random height in +-5 cm (3.2 cells) — the bake measures 0.047-0.075 m at depths 0-3 and
// 0.108 m at the root: an error that hardly grows per halving, the slow-falling content of the preflight's curve.
// The view is 960 x 540 at 45 degrees (one pixel = 1.534e-3 m per metre of distance): the cells are sub-sample from
// ~10 m (their 2 r / sqrt(n) estimate ~ 1.5 cells) and the 0.075 m error is 2 px at High from ~24 m, 1 px at Epic
// from ~49 m. So at 24 m High's cap 2 takes groups the exact rule refuses (the replaced triangles sub-sample, the
// error under 4 px); the five stops (6, 12, 24, 48, 96 m: d .. 16 d, a factor 2 apart) straddle that window.
// WHAT THE CAP CAN BUY, DERIVED: where the clause holds the cut at distance d is the exact rule's cut at ~cap x d (the
// footprint is linear in d), so on content whose drawn count falls as d^-k the cap divides the count by ~cap^k — it
// SHIFTS the curve, it does not steepen it: N ~ d^-k keeps its k. A count falling ~1/d where the exact rule's falls
// d^-0.38 is not a property of a constant cap (the twin and the device print the exponents; the row gates the shift's
// sign and place, not a slope).
// THE INERT FIXTURE: a smooth 8,192-triangle sphere beside it. On a smooth surface the exact rule's frontier triangles
// are LARGER than a pixel — a chord h deviates h^2 / 8R, so at error ~ tol x footprint the replaced triangles are
// ~ sqrt(8 R tol footprint), above the footprint whenever 8 R tol > footprint — so the cap's clause never holds and the
// cap must draw exactly the exact rule's clusters at every stop.
//
// THE ARMS (each a dolly of 5 stops, 30 frames of settle each, then one probed frame — FRAMES, never time):
//   high      tolerance 2, cap 1 (the cap OFF: the exact rule at High)
//   high-cap  tolerance 2, cap 2 (High's World Mode row)
//   epic      tolerance 1, cap 1 (Epic's row: the exact rule — the selftest's tier)
// THE LAWS:
//   (a) THE DEVICE IS THE RULE: every group's verdict the cut probe recorded = Types.h clusterGroupVerdict from the
//       probe's own request (a group within 1e-4 of one of its three comparisons is counted apart and still fails),
//       and each object's drawn triangles = the C++ cut over those verdicts;
//   (b) NEVER RISES: along each arm the facade's drawn triangles do not increase;
//   (c) THE CAP ONLY COARSENS, AND ONLY WHERE IT SHOULD: at every stop high-cap <= high on the facade, strictly at one
//       stop at least; on the sphere high-cap == high at every stop (inert);
//   (d) EPIC IS EXACT: the request carries cap 1 and the drawn set equals the exact twin;
// and it PRINTS per stop and arm the triangles and clusters drawn, the mean projected triangle size (px), the
// per-interval exponent (N ~ d^-k), and the SILHOUETTE DELTA (the facade's id-image coverage at high-cap XOR high) —
// the caveat the owner judges at the smoke, not gated.
// `--twin-only` runs the C++ dolly alone (no engine), the fixture's curve before any GPU work.
#include "EnginePrivate.h"
#include "GpuScene.h"
#include "HlmsAtom.h"

#include <Compositor/OgreCompositorNode.h>
#include <Compositor/OgreCompositorWorkspace.h>
#include <OgreImage2.h>
#include <OgreTextureBox.h>

#include "jahshaka/engine/Engine.h"
#include "cluster_fixtures.h"
#include "../support/enginetesthelpers.h"

#include <QCoreApplication>

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
        char buf_[720];                                                         \
        std::snprintf(buf_, sizeof(buf_), fmt, __VA_ARGS__);                    \
        CHECK(cond, buf_);                                                      \
    } while (0)

static const unsigned kW = 960, kH = 540;
static const float kFovDeg = 45.0f;                 // CameraDesc's default (vertical)
static const int kStops = 5;
static const int kSettle = 30;

// THE FIXTURE'S SHAPE (the row's comment derives it): kN x kN quads over kWall metres in the XY plane, facing +Z, its
// height a ROUGH RELIEF — random heights in [-kRelief, kRelief] at every kLattice-th vertex (1: every vertex),
// bilinear between them.
static int kN = 64, kLattice = 1;
static float kWall = 1.0f, kRelief = 0.05f, kD0 = 6.0f;   // kD0: the stops are kD0 x 1, 2, 4, 8, 16
// The camera's line (from the fixture's centre): yawed 35 degrees.
static const float kYaw = 0.6109f;
static const Vec3 kFacadeAt(-1.0f, 0.0f, 0.0f), kSphereAt(1.0f, 0.0f, 0.0f);

static iris::MeshPtr facade()
{
    const int L = kN / kLattice + 1;
    std::vector<float> node(size_t(L * L));
    unsigned seed = 12345u;
    for (float &h : node) {
        seed = seed * 1664525u + 1013904223u;
        h = kRelief * (float(seed >> 8) / float(1u << 24) * 2.0f - 1.0f);
    }
    std::vector<float> pos, nrm;
    std::vector<unsigned> idx;
    const float h = kWall / float(kN);
    for (int j = 0; j <= kN; ++j)
        for (int i = 0; i <= kN; ++i) {
            const int li = std::min(i / kLattice, L - 2), lj = std::min(j / kLattice, L - 2);
            const float fu = float(i - li * kLattice) / float(kLattice), fv = float(j - lj * kLattice) / float(kLattice);
            const float z = (1 - fu) * (1 - fv) * node[size_t(lj * L + li)] + fu * (1 - fv) * node[size_t(lj * L + li + 1)] +
                            (1 - fu) * fv * node[size_t((lj + 1) * L + li)] + fu * fv * node[size_t((lj + 1) * L + li + 1)];
            pos.insert(pos.end(), { -0.5f * kWall + float(i) * h, -0.5f * kWall + float(j) * h, z });
            nrm.insert(nrm.end(), { 0.0f, 0.0f, 1.0f });
        }
    for (int j = 0; j < kN; ++j)
        for (int i = 0; i < kN; ++i) {
            const unsigned a = unsigned(j * (kN + 1) + i), b = a + 1u, c = a + unsigned(kN + 1), d = c + 1u;
            idx.insert(idx.end(), { a, b, d, a, d, c });
        }
    return clusterfix::meshFromArrays(pos, nrm, {}, idx);
}

/// A UV sphere whose seam is WRAPPED BY INDEX (DOCS/traps/ENGINE.md: procedural seams do not weld).
static iris::MeshPtr wrappedSphere(int seg, int ring, float r)
{
    std::vector<float> pos, nrm;
    std::vector<unsigned> idx;
    pos.insert(pos.end(), { 0.0f, r, 0.0f });
    nrm.insert(nrm.end(), { 0.0f, 1.0f, 0.0f });
    for (int j = 1; j < ring; ++j) {
        const float th = float(j) / float(ring) * 3.14159265f;
        for (int i = 0; i < seg; ++i) {
            const float ph = float(i) / float(seg) * 6.28318531f;
            const float x = std::sin(th) * std::cos(ph), y = std::cos(th), z = -std::sin(th) * std::sin(ph);
            pos.insert(pos.end(), { r * x, r * y, r * z });
            nrm.insert(nrm.end(), { x, y, z });
        }
    }
    const unsigned south = unsigned(pos.size() / 3);
    pos.insert(pos.end(), { 0.0f, -r, 0.0f });
    nrm.insert(nrm.end(), { 0.0f, -1.0f, 0.0f });
    auto at = [&](int j, int i) { return unsigned(1 + (j - 1) * seg + (i % seg)); };
    for (int i = 0; i < seg; ++i) idx.insert(idx.end(), { 0u, at(1, i), at(1, i + 1) });
    for (int j = 1; j < ring - 1; ++j)
        for (int i = 0; i < seg; ++i) {
            const unsigned a = at(j, i), b = at(j, i + 1), c = at(j + 1, i), e = at(j + 1, i + 1);
            idx.insert(idx.end(), { a, c, b, b, c, e });
        }
    for (int i = 0; i < seg; ++i) idx.insert(idx.end(), { at(ring - 1, i), south, at(ring - 1, i + 1) });
    return clusterfix::meshFromArrays(pos, nrm, {}, idx);
}

static Vec3 eyeAt(int stop)
{
    const float d = kD0 * float(1 << stop);
    return Vec3(kFacadeAt.x + d * std::sin(kYaw), 0.0f, d * std::cos(kYaw));
}

struct Arm { const char *name; float tol, cap; };
static const Arm kArms[3] = { { "high", 2.0f, 1.0f }, { "high-cap", 2.0f, 2.0f }, { "epic", 1.0f, 1.0f } };

/// What one cut drew: triangles, clusters, the mean projected triangle size (px, triangle-weighted) and the share of
/// the drawn triangles in sub-sample clusters.
struct Drawn {
    unsigned long long tris = 0;
    size_t clusters = 0;
    double meanPx = 0.0, subShare = 0.0;
};

static Drawn measure(const MeshData &md, const ClusterCutView &v, const std::vector<unsigned> &cut)
{
    Drawn out;
    double wsum = 0.0, sub = 0.0;
    for (unsigned ci : cut) {
        const MeshCluster &c = md.clusters[ci];
        const unsigned n = c.indexCount / 3u;
        float w[3];
        for (int r = 0; r < 3; ++r)
            w[r] = v.worldRow[r][0] * c.centre[0] + v.worldRow[r][1] * c.centre[1] + v.worldRow[r][2] * c.centre[2] +
                   v.worldRow[r][3];
        const float dx = w[0] - v.eye[0], dy = w[1] - v.eye[1], dz = w[2] - v.eye[2];
        const float d = std::max(0.0f, std::sqrt(dx * dx + dy * dy + dz * dz) - c.radius * v.scale);
        const float fp = sampleFootprintPerspective(d, v.projScaleY, v.viewportHeight);
        const double px = fp > 0.0f ? double(clusterMeanTriangleSize(c) * v.scale / fp) : 0.0;
        out.tris += n;
        wsum += px * n;
        if (px < 1.0) sub += n;
    }
    out.clusters = cut.size();
    out.meanPx = out.tris ? wsum / double(out.tris) : 0.0;
    out.subShare = out.tris ? sub / double(out.tris) : 0.0;
    return out;
}

static ClusterCutView viewFor(const Vec3 &at, const Vec3 &eye, float tol, float cap)
{
    ClusterCutView v;
    v.worldRow[0][3] = at.x; v.worldRow[1][3] = at.y; v.worldRow[2][3] = at.z;
    v.scale = 1.0f;
    v.eye[0] = eye.x; v.eye[1] = eye.y; v.eye[2] = eye.z;
    v.tolerance = tol;
    v.densityCap = cap;
    v.projScaleY = 1.0f / std::tan(0.5f * kFovDeg * 3.14159265f / 180.0f);
    v.viewportHeight = float(kH);
    return v;
}

static void printCurve(const char *what, const Drawn (&d)[kStops])
{
    std::printf("   %-22s", what);
    for (int s = 0; s < kStops; ++s)
        std::printf(" | %6llu t %4zu c %5.2f px", d[s].tris, d[s].clusters, d[s].meanPx);
    std::printf("\n   %-22s", "  exponent k (d^-k)");
    for (int s = 0; s + 1 < kStops; ++s)
        std::printf("   %5.2f", d[s + 1].tris && d[s].tris ? std::log2(double(d[s].tris) / double(d[s + 1].tris)) : 0.0);
    std::printf("   overall %.2f\n", d[kStops - 1].tris ? std::log2(double(d[0].tris) / double(d[kStops - 1].tris)) / 4.0 : 0.0);
}

static bool readIds(OgreView *v, std::vector<uint32_t> &x)
{
    Ogre::CompositorWorkspace *ws = v->workspace();
    if (!ws) return false;
    Ogre::TextureGpu *tex = nullptr;
    for (Ogre::CompositorNode *n : ws->getNodeSequence())
        if ((tex = n->getDefinedTexture(Ogre::IdString(detail::kAtomIdTexture))) != nullptr) break;
    if (!tex || tex->getWidth() != kW || tex->getHeight() != kH) return false;
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
    const detail::GpuScene &gs = os->gpuScene();
    for (unsigned s = 0; s < gs.slotCount(); ++s)
        if (os->nodeOfGpuKey(gs.entry(s).ids[0]) == n) return s;
    return 0xFFFFFFFFu;
}

static bool probeFrame(Engine *e, Scene *scene, AtomCutProbe &p)
{
    if (!scene->armAtomCutProbe()) return false;
    for (int f = 0; f < 8; ++f) {
        enginetest::frames(e, 1);
        if (scene->atomCutProbe(p)) return true;
    }
    return false;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const bool twinOnly = argc > 1 && std::strcmp(argv[1], "--twin-only") == 0;
    if (twinOnly && argc >= 7) {   // the derivation's sweep: N lattice wall relief d0
        kN = std::atoi(argv[2]); kLattice = std::atoi(argv[3]); kWall = float(std::atof(argv[4]));
        kRelief = float(std::atof(argv[5])); kD0 = float(std::atof(argv[6]));
    }
    std::printf("== atom.density_dolly: the density cap, triangles against distance\n");
    clusterfix::Fixture wall, ball;
    if (!clusterfix::bake(wall, facade(), "facade") || wall.data.clusterGroups.empty() ||
        !clusterfix::bake(ball, wrappedSphere(64, 65, 0.5f), "sphere") || ball.data.clusterGroups.empty()) {
        std::printf("FAIL: a fixture baked no DAG\n");
        return 1;
    }
    std::printf("   facade: %zu triangles, %zu clusters, %zu groups; sphere: %zu triangles, %zu clusters, %zu groups\n",
                wall.triangles, wall.data.clusters.size(), wall.data.clusterGroups.size(), ball.triangles,
                ball.data.clusters.size(), ball.data.clusterGroups.size());
    {
        std::map<int, std::pair<float, float>> byDepth;   // depth -> (min, max) finite error
        for (const MeshClusterGroup &g : wall.data.clusterGroups) {
            if (!(g.error < FLT_MAX)) continue;
            auto it = byDepth.find(g.depth);
            if (it == byDepth.end()) byDepth[g.depth] = { g.error, g.error };
            else { it->second.first = std::min(it->second.first, g.error); it->second.second = std::max(it->second.second, g.error); }
        }
        std::printf("   facade group error by depth (m):");
        for (auto &kv : byDepth) std::printf(" d%d %.4f-%.4f", kv.first, kv.second.first, kv.second.second);
        std::printf("\n");
    }

    // ---- THE C++ TWIN'S CURVES (the fixture's own, before any GPU work) ----------------------------
    Drawn twinWall[3][kStops], twinBall[3][kStops];
    for (int a = 0; a < 3; ++a)
        for (int s = 0; s < kStops; ++s) {
            std::vector<unsigned> cut;
            const ClusterCutView vw = viewFor(kFacadeAt, eyeAt(s), kArms[a].tol, kArms[a].cap);
            clusterCut(wall.data.clusterGroups, wall.data.clusters, vw, cut);
            twinWall[a][s] = measure(wall.data, vw, cut);
            const ClusterCutView vb = viewFor(kSphereAt, eyeAt(s), kArms[a].tol, kArms[a].cap);
            clusterCut(ball.data.clusterGroups, ball.data.clusters, vb, cut);
            twinBall[a][s] = measure(ball.data, vb, cut);
        }
    std::printf("   THE TWIN, stops %.0f / %.0f / %.0f / %.0f / %.0f m (triangles, clusters, mean projected px):\n", kD0,
                2 * kD0, 4 * kD0, 8 * kD0, 16 * kD0);
    for (int a = 0; a < 3; ++a) {
        char what[64];
        std::snprintf(what, sizeof(what), "facade %s", kArms[a].name);
        printCurve(what, twinWall[a]);
    }
    for (int a = 0; a < 3; ++a) {
        char what[64];
        std::snprintf(what, sizeof(what), "sphere %s", kArms[a].name);
        printCurve(what, twinBall[a]);
    }
    if (twinOnly) return 0;

    // ---- THE DEVICE --------------------------------------------------------------------------------
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-atom-density-dolly-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->submitFrame(jahshaka::engine::FrameInput::delta(1.0f / 60.0f));
    Engine *e = engine.get();
    View *view = e->createOffscreenView("densitydolly", kW, kH, Colour(0, 0, 0));
    view->setOffscreenContract(OffscreenContract::StillPicture);
    Scene *scene = e->createScene("densitydolly");
    view->setScene(scene);
    auto *os = static_cast<OgreScene *>(scene);
    auto *ov = static_cast<OgreView *>(view);
    {
        PostFxDesc fx;
        fx.allowOffscreen = true;
        fx.ssr = 0;
        view->setPostFx(fx);
    }
    scene->setLodBias(1.0f);
    scene->setAmbient(Colour(0.10f, 0.11f, 0.13f), Colour(0.05f, 0.05f, 0.05f));
    enginetest::addDirectionalLight(scene, Vec3(-0.3f, -0.8f, -0.5f), 2.0f);
    PbrParams pp;
    pp.albedo = Colour(0.6f, 0.6f, 0.6f);
    const MaterialId mat = scene->createPbrMaterial(pp);
    const MeshId meshW = scene->createMesh(wall.data), meshB = scene->createMesh(ball.data);
    const NodeId nW = scene->createNode(), nB = scene->createNode();
    scene->setNodeTransform(nW, kFacadeAt, Quat(), Vec3(1, 1, 1));
    scene->setNodeTransform(nB, kSphereAt, Quat(), Vec3(1, 1, 1));
    scene->attachMesh(nW, meshW, mat);
    scene->attachMesh(nB, meshB, mat);
    enginetest::testCameraLookAt(view, eyeAt(0), kFacadeAt);
    enginetest::frames(e, 12);
    os->ensureGpuScene(false);
    const uint32_t sW = slotOf(os, nW), sB = slotOf(os, nB);
    const detail::GpuScene &gs = os->gpuScene();
    CHECK_MSG(sW != 0xFFFFFFFFu && sB != 0xFFFFFFFFu, "both fixtures are in the GPU scene (slots %u, %u)", sW, sB);

    Drawn dev[3][kStops][2];
    std::vector<unsigned char> cover[2][kStops];   // the facade's id coverage at high / high-cap
    unsigned long long verdictMiss = 0, nearMiss = 0, drawnMiss = 0, groupsJudged = 0;
    bool probesOk = true, requestOk = true;
    for (int a = 0; a < 3; ++a) {
        scene->setPixelTolerance(kArms[a].tol);
        scene->setDensityCap(kArms[a].cap);
        for (int s = 0; s < kStops; ++s) {
            enginetest::testCameraLookAt(view, eyeAt(s), kFacadeAt);
            enginetest::frames(e, kSettle);
            AtomCutProbe p;
            if (!probeFrame(e, scene, p) || !p.valid) { probesOk = false; continue; }
            if (p.tolerance != kArms[a].tol || p.densityCap != kArms[a].cap || p.viewportHeight != float(kH))
                requestOk = false;
            for (int k = 0; k < 2; ++k) {
                const uint32_t slot = k == 0 ? sW : sB;
                const MeshData &md = k == 0 ? wall.data : ball.data;
                ClusterCutView v;
                const detail::GpuInstance &ent = gs.entry(slot);
                for (int r = 0; r < 3; ++r)
                    for (int c = 0; c < 4; ++c) v.worldRow[r][c] = ent.world[r * 4 + c];
                v.scale = worldMaxAxisScale(ent.world);
                for (int i = 0; i < 3; ++i) v.eye[i] = p.eye[i];
                v.tolerance = p.tolerance;
                v.densityCap = p.densityCap;
                v.projScaleY = p.projScaleY;
                v.viewportHeight = p.viewportHeight;
                v.orthographic = p.orthographic;
                std::vector<float> replaced;
                clusterGroupReplacedSizes(md.clusterGroups, md.clusters, replaced);
                std::vector<unsigned char> aff(md.clusterGroups.size(), 0);
                for (const AtomCutProbeGroup &g : p.groups) {
                    if (g.slot != slot || g.late || g.group >= md.clusterGroups.size()) continue;
                    aff[g.group] = g.affordable ? 1 : 0;
                    ++groupsJudged;
                    const MeshClusterGroup &cg = md.clusterGroups[g.group];
                    if (clusterGroupVerdict(cg, replaced[g.group], v) == g.affordable) continue;
                    const float fp = clusterGroupFootprint(cg, v), al = clusterGroupAllowed(cg, v);
                    const bool near = std::fabs(al - cg.error) <= 1e-4f * cg.error ||
                                      std::fabs(al * v.densityCap - cg.error) <= 1e-4f * cg.error ||
                                      std::fabs(replaced[g.group] * v.scale - fp) <= 1e-4f * fp;
                    if (near) ++nearMiss; else ++verdictMiss;
                }
                std::vector<unsigned> cut;
                clusterCutFromAffordable(md.clusters, aff, cut);
                dev[a][s][k] = measure(md, v, cut);
                for (const AtomCutProbeObject &o : p.objects)
                    if (o.slot == slot && o.drawnTriangles != dev[a][s][k].tris) ++drawnMiss;
            }
            if (a < 2) {
                std::vector<uint32_t> ids;
                if (readIds(ov, ids)) {
                    cover[a][s].assign(ids.size(), 0);
                    for (size_t i = 0; i < ids.size(); ++i)
                        cover[a][s][i] = ids[i] != AtomId::kEmpty && (ids[i] & AtomId::kSlotMask) == sW;
                }
            }
        }
    }
    scene->setDensityCap(1.0f);
    scene->setPixelTolerance(kLodBudgetPixels);

    std::printf("   THE DEVICE (the probe's frames):\n");
    for (int k = 0; k < 2; ++k)
        for (int a = 0; a < 3; ++a) {
            Drawn d[kStops];
            for (int s = 0; s < kStops; ++s) d[s] = dev[a][s][k];
            char what[64];
            std::snprintf(what, sizeof(what), "%s %s", k == 0 ? "facade" : "sphere", kArms[a].name);
            printCurve(what, d);
        }
    std::printf("   facade silhouette delta, high-cap XOR high (px):");
    for (int s = 0; s < kStops; ++s) {
        unsigned x = 0, on = 0;
        if (cover[0][s].size() == cover[1][s].size())
            for (size_t i = 0; i < cover[0][s].size(); ++i) { x += cover[0][s][i] != cover[1][s][i]; on += cover[0][s][i]; }
        std::printf(" %u of %u", x, on);
    }
    std::printf("\n");

    CHECK_MSG(probesOk && requestOk, "every stop's probe is valid and its request carries the arm (tolerance, cap) (%d, %d)",
              int(probesOk), int(requestOk));
    CHECK_MSG(verdictMiss == 0u && nearMiss == 0u && drawnMiss == 0u && groupsJudged > 0u,
              "(a) THE DEVICE IS THE RULE: %llu group verdicts, %llu differ from clusterGroupVerdict (+%llu at a "
              "threshold), %llu objects' drawn triangles differ from the C++ cut over the verdicts",
              groupsJudged, verdictMiss, nearMiss, drawnMiss);
    bool rises = false;
    for (int a = 0; a < 3; ++a)
        for (int s = 0; s + 1 < kStops; ++s) rises = rises || dev[a][s + 1][0].tris > dev[a][s][0].tris;
    CHECK_MSG(!rises, "(b) NEVER RISES: the facade's triangles do not increase along any arm (%s)", rises ? "ROSE" : "held");
    bool capLe = true, ballSame = true;
    int capFewer = 0;
    for (int s = 0; s < kStops; ++s) {
        capLe = capLe && dev[1][s][0].tris <= dev[0][s][0].tris;
        capFewer += dev[1][s][0].tris < dev[0][s][0].tris ? 1 : 0;
        ballSame = ballSame && dev[1][s][1].tris == dev[0][s][1].tris && dev[1][s][1].clusters == dev[0][s][1].clusters;
    }
    CHECK_MSG(capLe && capFewer >= 1, "(c) THE CAP COARSENS THE DENSE FACADE: high-cap <= high at every stop (%s), fewer at %d stop(s)",
              capLe ? "yes" : "NO", capFewer);
    CHECK_MSG(ballSame, "(c) THE CAP IS INERT WHERE THE TRIANGLES ARE NOT SUB-SAMPLE: the sphere draws the same at every stop (%s)",
              ballSame ? "yes" : "NO");
    bool epicExact = true;
    for (int s = 0; s < kStops; ++s)
        epicExact = epicExact && dev[2][s][0].tris == twinWall[2][s].tris && dev[2][s][1].tris == twinBall[2][s].tris;
    CHECK_MSG(epicExact, "(d) EPIC IS EXACT: cap 1 in the request and the drawn triangles the exact twin's at every stop (%s)",
              epicExact ? "yes" : "NO");

    e->destroyView(view);
    e->destroyScene(scene);
    engine.reset();
    std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
