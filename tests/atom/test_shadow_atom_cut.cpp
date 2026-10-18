// shadow.atom_cut / shadow.atom_parity — THE SHADOW MAPS' ATOM CASTERS ARE THE LIGHT'S OWN
// CLUSTER CUT (ATOM-SHADOWS-1; SPECS/briefs/ATOM-SHADOWS-1.md, the lead's bar of 2026-09-27).
//
// The bar is PHYSICS, never agreement with an old picture: before this lane a caster drew
// the level its VIEW's pass had picked for the view's camera (the pin's shadow scene
// passes never compute LOD: "we just use them"), so "today's bytes" were an accident.
// A shadow map is a depth image of the world as the LIGHT sees it, and the right grain for
// it is one TEXEL OF THE MAP: a surface displaced by less than one texel's footprint moves
// no texel's depth by more than the rasteriser's own quantisation.
//
//   --cut (shadow.atom_cut): every map of the view's shadow node — the sun's three PSSM
//     splits (orthographic), a spot light's map (perspective) and a point light's six cube
//     faces (perspective, 90 degrees) — has its cut captured by the engine's test door
//     (OgreScene::armCasterProbeForTest), and:
//       1. THE REQUEST IS THE LIGHT'S: the cut's viewport height is the MAP'S height in
//          texels (its rectangle in the atlas / the cube face), its projection the shadow
//          camera's (the probe's viewProj row 1 against its projScaleY), its eye the
//          light's camera;
//       2. THE CUT IS THE RULE'S: for every drawn instance the drawn clusters are exactly
//          `clusterCut` (Types.h, the C++ half) at that request — the set, not a count;
//       3. THE BOUND: every drawn cluster that is not level 0 was PRODUCED by a group whose
//          measured displacement bound, in world units (x the instance's scale), is below
//          ONE TEXEL of that map at the group's distance from the light — the texel
//          computed INDEPENDENTLY of the rule's currency, from the map's own matrix: an
//          orthographic split's texel is 2 / (|row1| x height) everywhere; a perspective
//          map's at distance d is 2 d / (|row1| x height);
//       4. nothing drew coarse or nothing (the stream fits), and some map drew coarser than
//          level 0 (the far split — the claim is not vacuous).
//   --parity (shadow.atom_parity): TWO lockstep worlds, the same camera every frame: one
//     through the caster cut, the other through the stock PBS caster with DENSE casters
//     (the split's door shut and every mesh given level 0 only). The two views' shadow
//     ATLASES are read back and compared texel by texel over every active map, in LIGHT
//     SPACE: the depth difference as a world distance along the light, in units of that
//     map's texel footprint (max and 99th percentile over the texels both cover), the
//     silhouette texels (one covered, the other not), and the texels a receiver would
//     FLIP between lit and shadowed at the shipped constant bias (the depth difference
//     past the bias, or a silhouette texel). Bar: p99 <= 1 texel on every map; flips
//     <= 0.5 % of the covered texels.
// Frames, never time.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"
#include "cluster_fixtures.h"

#include "EnginePrivate.h"

#include <Compositor/OgreCompositorNode.h>
#include <Compositor/OgreCompositorShadowNode.h>
#include <Compositor/OgreCompositorShadowNodeDef.h>
#include <Compositor/OgreCompositorWorkspace.h>
#include <OgreImage2.h>
#include <OgreLight.h>
#include <OgreTextureBox.h>
#include <OgreTextureGpu.h>

#include <QCoreApplication>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace jahshaka::engine;
using jahshaka::engine::detail::OgreScene;
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
static Engine *gE = nullptr;

// ---------------------------------------------------------------------------
// THE WORLD: a ground, the shipped high-poly models and primitives at 4 - 70 m from
// the camera (the PSSM splits reach the far ones through their coarse texels), a sun,
// a spot light over the near group and a point light beside it. Built identically in
// both worlds of the parity arm, `dense` giving every mesh level 0 only.
struct Placed {
    NodeId node = 0;
    size_t fixture = 0;
};
struct World {
    Scene *scene = nullptr;
    View *view = nullptr;
    OgreView *ov = nullptr;
    std::vector<Placed> placed;
    struct Lamp { NodeId node; Vec3 pos; };
    std::vector<Lamp> lamps;   ///< the spot and the point: CACHED maps (re-rendered on a change only)
};

/// THE LAMP MAPS ARE CACHED (ENGINE_CACHE_POLICY_SPEC): a still lamp's map renders once and
/// is reused. A test that wants the map drawn by the code under test NOW moves the lamps by
/// `dy` (a millimetre: past the pose key's 1/65536 m, no visible change) — the next frame
/// re-renders their maps.
static void nudgeLamps(World &w, float dy)
{
    for (const World::Lamp &l : w.lamps)
        w.scene->setNodeTransform(l.node, Vec3(l.pos.x, l.pos.y + dy, l.pos.z), Quat(), Vec3(1, 1, 1));
}

static std::vector<clusterfix::Fixture> gFixtures;

static bool loadFixtures()
{
    const std::string prim = std::string(JAHSHAKA_TEST_SOURCE_DIR) + "/app/content/primitives/";
    std::vector<std::pair<std::string, iris::MeshPtr>> meshes;
    for (const char *name : { "teapot.obj", "torus.obj", "hp_sphere.obj" }) {
        const auto ms = clusterfix::loadModel(prim + name);
        if (!ms.empty()) meshes.push_back({ name, ms.front() });
    }
    for (const char *name : { "/matcaps_dragon.obj", "/physics_model.obj" }) {
        const auto ms = clusterfix::loadModel(std::string(CLUSTER_FIXTURE_DIR) + name);
        if (!ms.empty()) meshes.push_back({ name + 1, ms.front() });
    }
    meshes.push_back({ "uv-sphere-20k", clusterfix::uvSphere() });
    for (auto &m : meshes) {
        clusterfix::Fixture f;
        if (!clusterfix::bake(f, m.second, m.first) || f.data.clusters.empty()) continue;
        std::printf("   fixture %-16s %7zu triangles, %5zu clusters, %4zu groups, extent %.2f\n", f.name.c_str(),
                    f.triangles, f.data.clusters.size(), f.data.clusterGroups.size(), double(f.extent));
        gFixtures.push_back(std::move(f));
    }
    return gFixtures.size() >= 3;
}

static void buildWorld(World &w, bool dense)
{
    Scene *s = w.scene;
    s->setLodBias(1.0f);
    s->setAmbient(Colour(0.10f, 0.11f, 0.13f), Colour(0.05f, 0.05f, 0.05f));
    PbrParams gp;
    gp.albedo = Colour(0.45f, 0.45f, 0.42f);
    gp.roughness = 0.8f;
    const MaterialId gm = s->createPbrMaterial(gp);
    PbrParams op;
    op.albedo = Colour(0.7f, 0.35f, 0.25f);
    op.roughness = 0.5f;
    const MaterialId om = s->createPbrMaterial(op);
    {
        MeshData g;
        g.positions = { -150.0f, 0.0f, -150.0f, 150.0f, 0.0f, -150.0f, 150.0f, 0.0f, 150.0f, -150.0f, 0.0f, 150.0f };
        g.normals = { 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0 };
        g.uvs = { 0, 0, 20, 0, 20, 20, 0, 20 };
        g.indices = { 0, 2, 1, 0, 3, 2 };
        const NodeId n = s->createNode();
        s->setNodeTransform(n, Vec3(0, 0, 0), Quat(), Vec3(1, 1, 1));
        s->attachMesh(n, s->createMesh(g), gm);
    }
    std::vector<MeshId> meshes;
    for (const clusterfix::Fixture &f : gFixtures) {
        MeshData d = f.data;
        meshes.push_back(s->createMesh(d));
    }
    // A FIELD from 4 m to 70 m down -Z, each object scaled to ~1.6 m, lifted clear of
    // the ground by its own extent.
    int k = 0;
    for (int zi = 0; zi < 7; ++zi)
        for (int xi = -2; xi <= 2; ++xi, ++k) {
            const size_t fi = size_t(k) % gFixtures.size();
            const float sc = 1.6f / std::max(0.01f, gFixtures[fi].extent);
            const float z = -4.0f - 11.0f * float(zi);
            const float x = 2.6f * float(xi) + 0.4f * float(zi % 2);
            const NodeId n = s->createNode();
            const float a = 0.37f * float(k);
            s->setNodeTransform(n, Vec3(x, 1.0f, z), Quat(0.0f, std::sin(a), 0.0f, std::cos(a)), Vec3(sc, sc, sc));
            s->attachMesh(n, meshes[fi], om);
            w.placed.push_back({ n, fi });
        }
    enginetest::addDirectionalLight(s, Vec3(-0.45f, -0.8f, -0.4f), 2.2f);
    {   // THE SPOT: over the near group, straight down (-Y, the helpers' light convention)
        const NodeId n = s->createNode();
        s->setNodeTransform(n, Vec3(0.5f, 7.0f, -9.0f), Quat(), Vec3(1, 1, 1));
        w.lamps.push_back({ n, Vec3(0.5f, 7.0f, -9.0f) });
        LightDesc l;
        l.type = LightType::Spot;
        l.intensity = 30.0f;
        l.range = 20.0f;
        l.spotAngleDegrees = 40.0f;
        s->setLight(n, l);
    }
    {   // THE POINT: beside the second row
        const NodeId n = s->createNode();
        s->setNodeTransform(n, Vec3(-4.0f, 2.8f, -15.0f), Quat(), Vec3(1, 1, 1));
        w.lamps.push_back({ n, Vec3(-4.0f, 2.8f, -15.0f) });
        LightDesc l;
        l.type = LightType::Point;
        l.intensity = 20.0f;
        l.range = 14.0f;
        s->setLight(n, l);
    }
}

static bool makeWorld(World &w, const char *name, bool dense)
{
    w.view = gE->createOffscreenView(name, kW, kH, Colour(0, 0, 0));
    w.scene = gE->createScene(name);
    if (!w.view || !w.scene) return false;
    buildWorld(w, dense);
    // THE DENSE CASTER: the split's door shut (every item on the stock PBS caster) and a LOD
    // bias of 0 (no level affordable: every object at level 0 in every pass).
    if (dense) {
        w.scene->setAtomDrawEnabled(false);
        w.scene->setLodBias(0.0f);
    }
    w.view->setOffscreenContract(OffscreenContract::StillPicture);
    w.view->setScene(w.scene);
    w.view->setShadows(true);
    PostFxDesc fx;
    fx.allowOffscreen = true;
    fx.ssr = 0;
    w.view->setPostFx(fx);
    w.ov = static_cast<OgreView *>(w.view);
    return true;
}

static Ogre::CompositorShadowNode *shadowNodeOf(OgreView *v)
{
    Ogre::CompositorWorkspace *ws = v ? v->workspace() : nullptr;
    return ws ? ws->findShadowNode(Ogre::IdString(OgreView::kShadowNodeName)) : nullptr;
}

/// The active maps of the view's node: (map index, light type).
static std::vector<std::pair<unsigned, Ogre::Light::LightTypes>> activeMaps(Ogre::CompositorShadowNode *sn)
{
    std::vector<std::pair<unsigned, Ogre::Light::LightTypes>> out;
    const size_t n = sn->getDefinition()->getNumShadowTextureDefinitions();
    for (size_t i = 0; i < n; ++i) {
        if (!sn->isShadowMapIdxActive(i)) continue;
        const Ogre::Light *l = sn->getLightAssociatedWith(i);
        if (l) out.push_back({ unsigned(i), l->getType() });
    }
    return out;
}

// ---------------------------------------------------------------------------
// --cut
static float rowLen(const float *row) { return std::sqrt(row[0] * row[0] + row[1] * row[1] + row[2] * row[2]); }

struct CutTally {
    unsigned instances = 0, clusters = 0, coarse = 0, mismatched = 0, nearMismatched = 0, overBound = 0;
    double worstBoundRatio = 0.0;   // the drawn clusters' producer error / one texel, worst
};

static void checkOneCut(World &w, const OgreScene::CasterProbe &p, const char *label, bool orthographicExpected,
                        unsigned mapHeight, CutTally &all, bool mayBeEmpty = false)
{
    auto *os = static_cast<OgreScene *>(w.scene);
    CutTally t;
    // 1. THE REQUEST IS THE LIGHT'S
    const float row1 = rowLen(&p.viewProj[4]);
    CHECK_MSG(p.orthographic == orthographicExpected, "%s: the cut's projection is the shadow camera's (%s)", label,
              p.orthographic ? "orthographic" : "perspective");
    CHECK_MSG(unsigned(p.viewportHeight + 0.5f) == mapHeight && p.rect[3] == mapHeight,
              "%s: the cut's viewport is the MAP's height (%.0f px; the map %u, the view %u)", label,
              double(p.viewportHeight), mapHeight, kH);
    CHECK_MSG(std::fabs(row1 - std::fabs(p.projScaleY)) <= 1e-3f * std::fabs(p.projScaleY),
              "%s: the currency's proj[1][1] is the map camera's (%.5f vs |row1| %.5f)", label, double(p.projScaleY),
              double(row1));
    CHECK_MSG(p.overflow == 0u && p.missing == 0u, "%s: nothing drawn coarse (%u) or nothing (%u)", label, p.overflow,
              p.missing);
    // 2 + 3. PER INSTANCE: the rule's set, and the bound against the map's texel
    std::map<unsigned, std::vector<const OgreScene::CasterProbe::Drawn *>> bySlot;
    for (const auto &d : p.drawn) bySlot[d.slot].push_back(&d);
    std::map<NodeId, size_t> fixtureOf;
    for (const Placed &pl : w.placed) fixtureOf[pl.node] = pl.fixture;
    for (auto &kv : bySlot) {
        GpuSceneEntry e;
        if (!os->gpuSceneEntry(kv.first, e)) continue;
        auto fit = fixtureOf.find(NodeId(e.nodeId));
        if (fit == fixtureOf.end()) continue;   // the ground: a flat DAG, level 0 only
        const clusterfix::Fixture &f = gFixtures[fit->second];
        ++t.instances;
        ClusterCutView v;
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 4; ++c) v.worldRow[r][c] = e.world[r * 4 + c];
        v.scale = worldMaxAxisScale(e.world);
        for (int i = 0; i < 3; ++i) v.eye[i] = p.eye[i];
        v.tolerance = p.tolerance;
        v.projScaleY = p.projScaleY;
        v.viewportHeight = p.viewportHeight;
        v.orthographic = p.orthographic;
        // ...and the request's frustum: the cut drops a cluster wholly outside the map's
        // view (SPEED-VR-MEM, Types.h clusterInFrustum).
        v.cullPlanes = true;
        for (int i = 0; i < 6; ++i)
            for (int k = 0; k < 4; ++k) v.planes[i][k] = p.planes[i * 4 + k];
        std::vector<unsigned> cpu;
        clusterCut(f.data.clusterGroups, f.data.clusters, v, cpu);
        std::set<unsigned> gpuSet, cpuSet(cpu.begin(), cpu.end());
        for (const auto *d : kv.second) gpuSet.insert(d->cluster);
        if (gpuSet != cpuSet) {
            // A disagreement ONLY at a group whose error sits within 1e-4 of its allowance
            // is float ordering between two compilers, reported apart (lod_rule_parity's rule).
            bool nearOnly = true;
            std::vector<unsigned> diff;
            std::set_symmetric_difference(gpuSet.begin(), gpuSet.end(), cpuSet.begin(), cpuSet.end(),
                                          std::back_inserter(diff));
            for (unsigned c : diff) {
                const MeshCluster &mc = f.data.clusters[c];
                for (int g : { mc.group, mc.refined }) {
                    if (g < 0) continue;
                    const float allowed = clusterGroupAllowed(f.data.clusterGroups[size_t(g)], v);
                    const float err = f.data.clusterGroups[size_t(g)].error;
                    if (std::fabs(err - allowed) > 1e-4f * std::max(1e-6f, std::fabs(allowed))) nearOnly = false;
                }
            }
            (nearOnly ? t.nearMismatched : t.mismatched)++;
            if (!nearOnly && t.mismatched <= 3)
                std::printf("   %s slot %u (%s): the device drew %zu clusters, the rule %zu\n", label, kv.first,
                            f.name.c_str(), gpuSet.size(), cpuSet.size());
        }
        for (const auto *d : kv.second) {
            ++t.clusters;
            if (d->cluster >= f.data.clusters.size()) { ++t.mismatched; continue; }
            const MeshCluster &mc = f.data.clusters[d->cluster];
            if (mc.refined < 0) continue;   // level 0: no displacement at all
            ++t.coarse;
            const MeshClusterGroup &g = f.data.clusterGroups[size_t(mc.refined)];
            // THE TEXEL, from the map's own matrix (never the rule's currency): the
            // group's distance from the light as the rule measures it.
            float dist = 1.0f;
            if (!p.orthographic) {
                float c[3];
                for (int r = 0; r < 3; ++r)
                    c[r] = v.worldRow[r][0] * g.centre[0] + v.worldRow[r][1] * g.centre[1] +
                           v.worldRow[r][2] * g.centre[2] + v.worldRow[r][3];
                const float dx = c[0] - p.eye[0], dy = c[1] - p.eye[1], dz = c[2] - p.eye[2];
                dist = std::max(0.0f, std::sqrt(dx * dx + dy * dy + dz * dz) - g.radius * v.scale);
            }
            const float texel = 2.0f * dist / (row1 * float(mapHeight));
            const float errWorld = g.error * v.scale;
            const double ratio = texel > 0.0f ? double(errWorld) / double(texel) : 1e9;
            t.worstBoundRatio = std::max(t.worstBoundRatio, ratio);
            if (!(errWorld < texel * p.tolerance * (1.0f + 1e-4f))) ++t.overBound;
        }
    }
    std::printf("   %-18s survivors %4u | instances checked %3u | clusters %6u (not level 0: %6u) | triangles %8llu | "
                "worst producer bound %.3f texel\n",
                label, p.survivors, t.instances, t.clusters, t.coarse, p.triangles, t.worstBoundRatio);
    // A point light's face may look at nothing (the face towards the sky): the six faces are
    // checked together by the caller.
    if (!mayBeEmpty) CHECK_MSG(t.instances > 0u, "%s: the map drew Atom casters (%u instances)", label, t.instances);
    CHECK_MSG(t.mismatched == 0u, "%s: every instance's drawn clusters are the rule's set at the light's request (%u "
              "differ; %u only at a threshold)", label, t.mismatched, t.nearMismatched);
    CHECK_MSG(t.overBound == 0u, "%s: every drawn cluster's displacement bound is under ONE texel of the map (%u over; "
              "worst %.3f)", label, t.overBound, t.worstBoundRatio);
    all.instances += t.instances;
    all.clusters += t.clusters;
    all.coarse += t.coarse;
    all.mismatched += t.mismatched;
    all.overBound += t.overBound;
    all.worstBoundRatio = std::max(all.worstBoundRatio, t.worstBoundRatio);
}

static int gNudge = 0;
static bool probe(World &w, unsigned map, unsigned face, OgreScene::CasterProbe &out)
{
    auto *os = static_cast<OgreScene *>(w.scene);
    os->armCasterProbeForTest(map, face);
    nudgeLamps(w, 0.001f * float(++gNudge % 2));   // a lamp map renders only when dirty
    for (int i = 0; i < 24; ++i) {
        enginetest::frames(gE, 1);
        if (os->casterProbeForTest(out)) return true;
    }
    return false;
}

/// The map's height in texels: its rectangle in the atlas, or the cube's face.
static unsigned mapHeightOf(Ogre::CompositorShadowNode *sn, unsigned map, bool point)
{
    const Ogre::ShadowTextureDefinition *td = sn->getDefinition()->getShadowTextureDefinition(map);
    if (point) {
        Ogre::TextureGpu *cube = sn->getDefinedTexture(Ogre::IdString("tmpCubemap"));
        return cube ? cube->getHeight() : 0u;
    }
    Ogre::TextureGpu *atlas = sn->getDefinedTexture(Ogre::IdString("atlas0"));
    return atlas ? unsigned(int(float(td->uvLength.y) * float(atlas->getHeight()))) : 0u;
}

static int cutMain()
{
    std::printf("== shadow.atom_cut: every shadow map's Atom casters are the LIGHT's cluster cut at one texel of "
                "the map\n");
    World w;
    if (!makeWorld(w, "shadow-cut", false)) { std::printf("FAIL: the world\n"); return 1; }
    w.view->setCamera(enginetest::testCameraDescLookAt(Vec3(0.0f, 2.2f, 4.0f), Vec3(0.0f, 0.8f, -30.0f)));
    enginetest::frames(gE, 30);
    const AtomDrawStatus st = w.scene->atomDrawStatus();
    CHECK_MSG(st.on && st.atomItems >= 35u, "the split is live (%u atom items)", st.atomItems);
    CHECK_MSG(st.casterValid && st.casterMaps > 0u && st.casterMissing == 0u,
              "the caster counters read back: %u maps, %llu triangles, %u clusters, %u instances, budget %u",
              st.casterMaps, st.casterTriangles, st.casterClusters, st.casterInstances, st.casterIndexBudget);
    Ogre::CompositorShadowNode *sn = shadowNodeOf(w.ov);
    if (!sn) { std::printf("FAIL: the view has no shadow node\n"); return 1; }
    const auto maps = activeMaps(sn);
    unsigned pssm = 0, spot = 0, point = 0;
    CutTally all;
    for (const auto &m : maps) {
        const bool isPoint = m.second == Ogre::Light::LT_POINT;
        const bool ortho = m.second == Ogre::Light::LT_DIRECTIONAL;
        const unsigned faces = isPoint ? 6u : 1u;
        unsigned pointInstances = 0;
        for (unsigned face = 0; face < faces; ++face) {
            OgreScene::CasterProbe p;
            char label[64];
            std::snprintf(label, sizeof(label), "map %u %s%s%u", m.first,
                          ortho ? "pssm" : isPoint ? "point face " : "spot", ortho || !isPoint ? "" : "",
                          isPoint ? face : 0u);
            if (!probe(w, m.first, face, p)) {
                std::printf("FAIL: %s: the probe read nothing back\n", label);
                ++failures;
                continue;
            }
            const unsigned before = all.instances;
            checkOneCut(w, p, label, ortho, mapHeightOf(sn, m.first, isPoint), all, isPoint);
            pointInstances += isPoint ? all.instances - before : 0u;
        }
        if (isPoint)
            CHECK_MSG(pointInstances > 0u, "map %u: the point light's faces drew Atom casters (%u instance-faces)",
                      m.first, pointInstances);
        (ortho ? pssm : isPoint ? point : spot)++;
    }
    CHECK_MSG(pssm == 3u && spot >= 1u && point >= 1u, "the maps: 3 PSSM splits, %u spot, %u point", spot, point);
    CHECK_MSG(all.coarse > 0u, "the light's cut draws coarser than level 0 where one texel affords it (%u of %u "
              "clusters)", all.coarse, all.clusters);
    std::printf("   ALL: %u instance-maps, %u clusters (%u coarse), worst producer bound %.3f texel\n", all.instances,
                all.clusters, all.coarse, all.worstBoundRatio);
    gE->destroyView(w.view);
    gE->destroyScene(w.scene);
    return 0;
}

// ---------------------------------------------------------------------------
// --parity
struct Atlas {
    std::vector<float> d;
    unsigned w = 0, h = 0;
};

static bool readAtlas(Ogre::CompositorShadowNode *sn, Atlas &out)
{
    Ogre::TextureGpu *tex = sn->getDefinedTexture(Ogre::IdString("atlas0"));
    if (!tex) return false;
    Ogre::Image2 img;
    img.convertFromTexture(tex, 0u, 0u);
    const Ogre::TextureBox box = img.getData(0u);
    out.w = tex->getWidth();
    out.h = tex->getHeight();
    out.d.assign(size_t(out.w) * out.h, 0.0f);
    for (unsigned r = 0; r < out.h; ++r)
        std::memcpy(&out.d[size_t(r) * out.w], box.at(0, r, 0), size_t(out.w) * sizeof(float));
    return true;
}

static int parityMain()
{
    std::printf("== shadow.atom_parity: the caster cut's shadow maps against DENSE stock casters, texel by texel in "
                "light space\n");
    World a, b;
    if (!makeWorld(a, "shadow-cut", false) || !makeWorld(b, "shadow-dense", true)) {
        std::printf("FAIL: the worlds\n");
        return 1;
    }
    const CameraDesc cam = enginetest::testCameraDescLookAt(Vec3(0.0f, 2.2f, 4.0f), Vec3(0.0f, 0.8f, -30.0f));
    a.view->setCamera(cam);
    b.view->setCamera(cam);
    enginetest::frames(gE, 40);
    {
        const AtomDrawStatus sa = a.scene->atomDrawStatus(), sb = b.scene->atomDrawStatus();
        CHECK_MSG(sa.on && sa.atomItems >= 35u && !sb.on,
                  "one world through the caster cut (%u atom items), the other all stock PBS (split off)", sa.atomItems);
    }
    Ogre::CompositorShadowNode *sa = shadowNodeOf(a.ov), *sb = shadowNodeOf(b.ov);
    if (!sa || !sb) { std::printf("FAIL: shadow nodes\n"); return 1; }
    // The probe: each map's bias scale and depth range (the cut world's recorder saw them).
    const auto maps = activeMaps(sa);
    std::map<unsigned, OgreScene::CasterProbe> probes;
    for (const auto &m : maps) {
        OgreScene::CasterProbe p;
        if (probe(a, m.first, 0u, p)) probes[m.first] = p;
    }
    // BOTH WORLDS' LAMP MAPS RE-RENDERED NOW, identically (the cut world's by the caster
    // cut: its first render was before the GPU scene was live, through PBS).
    nudgeLamps(a, 0.002f);
    nudgeLamps(b, 0.002f);
    enginetest::frames(gE, 6);
    Atlas A, B;
    CHECK(readAtlas(sa, A) && readAtlas(sb, B) && A.w == B.w && A.h == B.h, "both atlases read back");
    if (A.d.empty() || A.w != B.w || A.h != B.h) return 1;
    const Ogre::CompositorShadowNodeDef *def = sa->getDefinition();
    for (const auto &m : maps) {
        const unsigned idx = m.first;
        const bool point = m.second == Ogre::Light::LT_POINT;
        const bool ortho = m.second == Ogre::Light::LT_DIRECTIONAL;
        // THE SAME LIGHT CAMERA in both worlds (a precondition: a different split is no
        // comparison at all).
        const Ogre::Matrix4 ma = sa->getViewProjectionMatrix(idx), mb = sb->getViewProjectionMatrix(idx);
        float camDiff = 0.0f;
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c) camDiff = std::max(camDiff, float(std::fabs(ma[r][c] - mb[r][c])));
        char label[48];
        std::snprintf(label, sizeof(label), "map %u %s", idx, ortho ? "pssm" : point ? "point (dpsm)" : "spot");
        CHECK_MSG(camDiff < 1e-4f, "%s: both worlds render it from the same light camera (%.2g)", label,
                  double(camDiff));
        const Ogre::ShadowTextureDefinition *td = def->getShadowTextureDefinition(idx);
        const unsigned x0 = unsigned(float(td->uvOffset.x) * float(A.w)), y0 = unsigned(float(td->uvOffset.y) * float(A.h));
        const unsigned mw = unsigned(float(td->uvLength.x) * float(A.w)), mh = unsigned(float(td->uvLength.y) * float(A.h));
        Ogre::Real nearD = 0, farD = 1;
        sa->getMinMaxDepthRange(idx, nearD, farD);
        // THE CLIP ROW 0, without the atlas's translation (getViewProjectionMatrix is
        // clipToImageSpace x proj x view; the translation mixes row 3 into row 0).
        const float tx = 0.5f * float(td->uvLength.x) + float(td->uvOffset.x);
        const float r0[3] = { float(ma[0][0] - tx * ma[3][0]), float(ma[0][1] - tx * ma[3][1]),
                              float(ma[0][2] - tx * ma[3][2]) };
        const float r2[4] = { float(ma[2][0]), float(ma[2][1]), float(ma[2][2]), float(ma[2][3]) };
        const float r3[4] = { float(ma[3][0]), float(ma[3][1]), float(ma[3][2]), float(ma[3][3]) };
        const float r0len = std::sqrt(r0[0] * r0[0] + r0[1] * r0[1] + r0[2] * r0[2]);
        // The perspective linearisation (the RS-depth rows: z = A zv' + B, w = zv').
        const float r3len = std::sqrt(r3[0] * r3[0] + r3[1] * r3[1] + r3[2] * r3[2]);
        const float pA = r3len > 0 ? (r2[0] * r3[0] + r2[1] * r3[1] + r2[2] * r3[2]) / (r3len * r3len) : 0.0f;
        const float pB = r2[3] - pA * r3[3];
        auto linear = [&](float z) {   // the view distance along the light's axis
            return std::fabs(z - pA) > 1e-12f ? pB / (z - pA) : 0.0f;
        };
        const float bias = probes.count(idx) ? 0.01f * probes[idx].biasScale : 0.01f;
        // THE EMPTY VALUE: the map's clear, exact and the most frequent value where no
        // caster drew (the sky's texels) — read off the dense map, never assumed.
        float empty = 0.0f;
        {
            std::map<float, unsigned> freq;
            for (unsigned y = y0; y < y0 + mh; ++y)
                for (unsigned x = x0; x < x0 + mw; ++x) ++freq[B.d[size_t(y) * B.w + x]];
            unsigned best = 0;
            for (const auto &kv : freq)
                if (kv.second > best) { best = kv.second; empty = kv.first; }
        }
        std::vector<float> errTexels;
        unsigned both = 0, silhouette = 0, flips = 0, aGreater = 0;
        double signedSum = 0.0;
        for (unsigned y = y0; y < y0 + mh; ++y)
            for (unsigned x = x0; x < x0 + mw; ++x) {
                const float da = A.d[size_t(y) * A.w + x], db = B.d[size_t(y) * B.w + x];
                const bool ca = da != empty, cb = db != empty;
                if (!ca && !cb) continue;
                if (ca != cb) {
                    ++silhouette;
                    ++flips;
                    continue;
                }
                ++both;
                float errWorld = 0.0f, texel = 1.0f;
                if (ortho) {
                    const float r2len = std::sqrt(r2[0] * r2[0] + r2[1] * r2[1] + r2[2] * r2[2]);
                    errWorld = std::fabs(da - db) / std::max(1e-12f, r2len);
                    texel = 1.0f / (r0len * float(A.w));
                } else if (point) {
                    errWorld = std::fabs(da - db) * float(farD - nearD);
                    const float dist = (1.0f - db) * float(farD - nearD) + float(nearD);
                    // a cube face's texel at that distance (90 degrees over the face)
                    Ogre::TextureGpu *cube = sa->getDefinedTexture(Ogre::IdString("tmpCubemap"));
                    texel = 2.0f * dist / float(cube ? cube->getHeight() : mh);
                } else {
                    const float za = linear(da), zb = linear(db);
                    errWorld = std::fabs(za - zb);
                    texel = std::fabs(zb) / (r0len * float(A.w));
                }
                errTexels.push_back(texel > 0.0f ? errWorld / texel : 0.0f);
                signedSum += double(da) - double(db);
                if (da > db) ++aGreater;
                if (errWorld > bias) ++flips;
            }
        std::sort(errTexels.begin(), errTexels.end());
        const float mx = errTexels.empty() ? 0.0f : errTexels.back();
        const float p99 = errTexels.empty() ? 0.0f : errTexels[size_t(double(errTexels.size() - 1) * 0.99)];
        const unsigned covered = both + silhouette;
        std::printf("   %-14s signed: mean (cut - dense) %.3g in the map's units, cut greater at %u of %u texels\n",
                    label, both ? signedSum / double(both) : 0.0, aGreater, both);
        std::printf("   %-14s %4ux%-4u covered %7u | depth error in texels: max %.3f p99 %.3f | silhouette %6u | "
                    "flips at the shipped bias (%.4f m) %6u (%.3f %%)\n",
                    label, mw, mh, covered, double(mx), double(p99), silhouette, double(bias), flips,
                    covered ? 100.0 * double(flips) / double(covered) : 0.0);
        CHECK_MSG(covered > 100u, "%s: the map holds casters (%u texels)", label, covered);
        CHECK_MSG(p99 <= 1.0f, "%s: the 99th-percentile depth difference is within one texel of the map (%.3f)",
                  label, double(p99));
        CHECK_MSG(double(flips) <= 0.005 * double(covered), "%s: at most 0.5 %% of its texels flip lit/shadowed "
                  "(%u of %u)", label, flips, covered);
    }
    // ---- THE CASTER IN FRONT OF THE LIGHT'S NEAR PLANE (SPEED-VR-MEM fix round) ----------
    // A shadow camera draws with DEPTH CLAMP (Camera::getNeedsDepthClamp): a caster between
    // the light and its near plane still writes the near depth and shadows what lies below.
    // The Atom cull used to test caster requests against all six planes, near included, so
    // such a caster vanished from the map at INSTANCE level while the stock caster drew it.
    // Three small casters high up the sun's ray over three visible ground points, in both
    // worlds: every one must cover its own texel in every PSSM split that sees its ground
    // point, in the cut world as in the dense one.
    {
        const Vec3 sun(-0.45f, -0.8f, -0.4f);
        const float sl = std::sqrt(sun.x * sun.x + sun.y * sun.y + sun.z * sun.z);
        const Vec3 up(-sun.x / sl, -sun.y / sl, -sun.z / sl);
        const float heights[3] = { 12.0f, 40.0f, 120.0f };
        const Vec3 grounds[3] = { Vec3(-3.0f, 0.0f, -1.5f), Vec3(0.0f, 0.0f, -2.0f), Vec3(3.0f, 0.0f, -1.5f) };
        Vec3 at[3];
        for (int i = 0; i < 3; ++i) {
            at[i] = Vec3(grounds[i].x + up.x * heights[i], grounds[i].y + up.y * heights[i],
                         grounds[i].z + up.z * heights[i]);
            for (World *w : { &a, &b }) {
                const float sc = 0.8f / std::max(0.01f, gFixtures[0].extent);
                const NodeId n = w->scene->createNode();
                w->scene->setNodeTransform(n, at[i], Quat(), Vec3(sc, sc, sc));
                PbrParams cp;
                w->scene->attachMesh(n, w->scene->createMesh(gFixtures[0].data), w->scene->createPbrMaterial(cp));
            }
        }
        enginetest::frames(gE, 40);
        Atlas A2, B2;
        CHECK(readAtlas(sa, A2) && readAtlas(sb, B2) && A2.w == B2.w, "both atlases read back (high casters)");
        unsigned seen = 0, cutMissing = 0, denseMissing = 0;
        for (const auto &m : maps) {
            if (m.second != Ogre::Light::LT_DIRECTIONAL || A2.d.empty()) continue;
            const unsigned idx = m.first;
            const Ogre::ShadowTextureDefinition *td = def->getShadowTextureDefinition(idx);
            const Ogre::Matrix4 vp = sa->getViewProjectionMatrix(idx);
            const unsigned x0 = unsigned(float(td->uvOffset.x) * float(A2.w)), y0 = unsigned(float(td->uvOffset.y) * float(A2.h));
            const unsigned mw = unsigned(float(td->uvLength.x) * float(A2.w)), mh = unsigned(float(td->uvLength.y) * float(A2.h));
            // the map's clear: the most frequent value of the dense map's rectangle
            float empty = 0.0f;
            {
                std::map<float, unsigned> freq;
                for (unsigned y = y0; y < y0 + mh; ++y)
                    for (unsigned x = x0; x < x0 + mw; ++x) ++freq[B2.d[size_t(y) * B2.w + x]];
                unsigned best = 0;
                for (const auto &kv : freq)
                    if (kv.second > best) { best = kv.second; empty = kv.first; }
            }
            for (int i = 0; i < 3; ++i) {
                const Ogre::Vector4 c = vp * Ogre::Vector4(at[i].x, at[i].y, at[i].z, 1.0f);
                if (std::fabs(c.w) < 1e-12f) continue;
                const float u = float(c.x / c.w), v = float(c.y / c.w);
                const int tx = int(u * float(A2.w)), ty = int(v * float(A2.h));
                if (tx < int(x0) + 2 || ty < int(y0) + 2 || tx >= int(x0 + mw) - 2 || ty >= int(y0 + mh) - 2) continue;
                ++seen;
                bool ca = false, cb = false;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const size_t o = size_t(ty + dy) * A2.w + size_t(tx + dx);
                        ca = ca || A2.d[o] != empty;
                        cb = cb || B2.d[o] != empty;
                    }
                if (!ca) ++cutMissing;
                if (!cb) ++denseMissing;
                std::printf("   high caster %d (%.0f m up) in map %u at texel (%d,%d): cut %s, dense %s\n", i,
                            double(heights[i]), idx, tx, ty, ca ? "drawn" : "MISSING", cb ? "drawn" : "MISSING");
            }
        }
        CHECK_MSG(seen >= 3u, "the high casters land inside a PSSM split (%u placements)", seen);
        CHECK_MSG(denseMissing == 0u, "the stock caster draws every high caster (%u missing)", denseMissing);
        CHECK_MSG(cutMissing == 0u, "the caster cut draws every high caster in front of the light's near plane, as "
                  "depth clamp does (%u missing)", cutMissing);
    }
    for (World *w : { &a, &b }) {
        gE->destroyView(w->view);
        gE->destroyScene(w->scene);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// shadow.caster_first_frame — CASTER-USES-1: THE FIRST FRAME RECORDS EVERY MAP. The caster
// pass's stats ring held a fixed 64 maps a frame, and a first frame renders every map at
// once, so the overflow went unrecorded. EIGHT views of the one scene (each with its own
// shadow node: the PSSM splits, the spot, the point's six faces) render their first
// frame together — past 64 caster maps in one frame for the scene's one ring — and the
// counters must say every one was recorded: the peak above 64, nothing unrecorded.
static int firstFrameMain()
{
    std::printf("== shadow.caster_first_frame: a first frame with more than 64 caster maps records them all\n");
    World w;
    if (!makeWorld(w, "uses-0", false)) { std::printf("FAIL: the world\n"); return 1; }
    const CameraDesc cam = enginetest::testCameraDescLookAt(Vec3(0.0f, 2.2f, 4.0f), Vec3(0.0f, 0.8f, -30.0f));
    w.view->setCamera(cam);
    std::vector<View *> more;
    for (int i = 1; i < 8; ++i) {
        const std::string name = "uses-" + std::to_string(i);
        View *v = gE->createOffscreenView(name, kW, kH, Colour(0, 0, 0));
        if (!v) { std::printf("FAIL: view %d\n", i); return 1; }
        v->setOffscreenContract(OffscreenContract::StillPicture);
        v->setScene(w.scene);
        v->setShadows(true);
        PostFxDesc fx;
        fx.allowOffscreen = true;
        fx.ssr = 0;
        v->setPostFx(fx);
        v->setCamera(cam);
        more.push_back(v);
    }
    enginetest::frames(gE, 12);// the first frame, then its read-back
    const AtomDrawStatus st = w.scene->atomDrawStatus();
    std::printf("   caster maps: the most in one frame %u, unrecorded %llu (last read: %u maps)\n",
                st.casterMapsPeak, st.casterUnrecorded, st.casterMaps);
    CHECK_MSG(st.casterValid && st.casterMapsPeak > 64u,
              "one frame rendered more than 64 caster maps and recorded them (%u)", st.casterMapsPeak);
    CHECK_MSG(st.casterUnrecorded == 0ull, "no map went unrecorded (%llu)", st.casterUnrecorded);
    for (View *v : more) gE->destroyView(v);
    return 0;
}

// ---------------------------------------------------------------------------
// --light-cull (shadow.caster_cull) — THE CASTER CULL BY THE LIGHT'S OWN DEPTH (ENGINE-UPGRADE-U5
// stage 4, the lead's decision 10). The parity world under a ROOF: a 26 x 0.3 x 38 m slab 6 m up
// over the field's three near rows, between them and the sun. A sun map's Atom casters are tested
// against the previous frame's depth pyramid of that map, the rejected again against this frame's
// once the map holds the rest, and only the ones not wholly behind the map's depth are drawn. The
// laws:
//   1. AT REST the three sun maps are BIT-EQUAL with the cull on and off (shadow.casterCull 1 / 0;
//      Scene::shadowMapDigests: every texel of each map);
//   2. THE CULL IS NOT VACUOUS: EVERY field object the roof hides from the sun with a margin (all
//      eight corners of its box, pushed toward the sun to the roof's underside, inside the roof's
//      footprint shrunk by the box's light-space width - the cull's grain: the instance box against
//      the pyramid's mip where it is <= 2 texels wide, read over <= 3 x 3 of them) is drawn by
//      NEITHER half of ANY sun map's cut (per object, the test door's two drawn sets), the mirror's
//      maps are culled too, and the maps draw fewer caster triangles with the cull on;
//   3. IN MOTION the maps stay bit-equal: the roof slides off the field 0.75 m a step (each "on"
//      frame predicts from the pyramid of the roof's PREVIOUS place, so what the slide uncovered is
//      rejected by the first half and must be drawn by the late half - which it is: the late half's
//      draws are counted), then the camera pans (the splits' bounds move under the history's
//      matrix). Frames, never time.
static NodeId addBox(Scene *s, const Vec3 &c, const Vec3 &half, MaterialId m)
{
    MeshData g;
    const float sx = half.x, sy = half.y, sz = half.z;
    // six faces, four corners each, outward normals, counter-clockwise from outside
    const float n[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    for (int f = 0; f < 6; ++f) {
        const float nx = n[f][0], ny = n[f][1], nz = n[f][2];
        // two tangents
        const float ux = ny != 0.0f ? 1.0f : nz != 0.0f ? 1.0f : 0.0f;
        const float uy = 0.0f;
        const float uz = nx != 0.0f ? 1.0f : 0.0f;
        const float vx = ny * uz - nz * uy, vy = nz * ux - nx * uz, vz = nx * uy - ny * ux;
        const float cs[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
        const unsigned base = unsigned(g.positions.size() / 3);
        for (int k = 0; k < 4; ++k) {
            const float a = cs[k][0], b = cs[k][1];
            g.positions.push_back((nx + a * ux + b * vx) * sx);
            g.positions.push_back((ny + a * uy + b * vy) * sy);
            g.positions.push_back((nz + a * uz + b * vz) * sz);
            g.normals.push_back(nx);
            g.normals.push_back(ny);
            g.normals.push_back(nz);
            g.uvs.push_back(a * 0.5f + 0.5f);
            g.uvs.push_back(b * 0.5f + 0.5f);
        }
        // the winding the ground quad uses (0, 2, 1 / 0, 3, 2 with an upward normal)
        g.indices.insert(g.indices.end(), { base, base + 2, base + 1, base, base + 3, base + 2 });
    }
    const NodeId node = s->createNode();
    s->setNodeTransform(node, c, Quat(), Vec3(1, 1, 1));
    s->attachMesh(node, s->createMesh(g), m);
    return node;
}

static bool mapsEqual(const std::vector<ShadowMapDigest> &a, const std::vector<ShadowMapDigest> &b,
                      std::string &why)
{
    if (a.size() != b.size() || a.empty()) { why = "different map sets"; return false; }
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].map != b[i].map || a[i].width != b[i].width || a[i].height != b[i].height) {
            why = "map " + std::to_string(a[i].map) + ": a different rectangle";
            return false;
        }
        if (a[i].digest != b[i].digest) {
            char buf[160];
            std::snprintf(buf, sizeof(buf), "map %u: %016llx vs %016llx (covered %u vs %u)", a[i].map, a[i].digest,
                          b[i].digest, a[i].covered, b[i].covered);
            why = buf;
            return false;
        }
    }
    return true;
}

struct CullTally {
    unsigned long long triangles = 0, lateTriangles = 0;
    unsigned occluded = 0, lateInstances = 0, lateOccluded = 0, tested = 0, maps = 0;
};
static CullTally tallyOf(const AtomDrawStatus &st)
{
    CullTally t;
    for (const AtomCasterMapStats &m : st.casterPerMap) {
        if (m.kind != 0u) continue;
        ++t.maps;
        t.triangles += m.triangles;
        t.lateTriangles += m.lateTriangles;
        t.occluded += m.occluded;
        t.lateInstances += m.lateInstances;
        t.lateOccluded += m.lateOccluded;
        if (m.lightTested) ++t.tested;
    }
    return t;
}

/// A solid-colour TGA (the decal's image).
static bool writeSolidTga(const std::string &path, unsigned char r, unsigned char g, unsigned char b)
{
    const unsigned n = 32;
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    unsigned char hdr[18] = {};
    hdr[2] = 2;
    hdr[12] = (unsigned char)n;
    hdr[14] = (unsigned char)n;
    hdr[16] = 32;
    hdr[17] = 0x28;
    std::fwrite(hdr, 1, sizeof(hdr), f);
    for (unsigned i = 0; i < n * n; ++i) {
        const unsigned char px[4] = { b, g, r, 255 };
        std::fwrite(px, 1, 4, f);
    }
    std::fclose(f);
    return true;
}

/// A RIGGED COLUMN (decision 19.5; atom.cpu_walks' rig): two 0.4 x 1 x 0.4 m boxes, the lower on the root bone, the
/// upper on the tip bone at 1 m, and a clip that bends the tip 90 degrees - posed at the clip's end the upper box lies
/// OUTSIDE the mesh's bind-pose box, which is the box the GPU scene carries.
static void addRigBox(MeshData &d, float x0, float x1, float y0, float y1, float z0, float z1, unsigned char bone)
{
    auto quad = [&](const float q[4][3], const float n[3]) {
        const unsigned base = unsigned(d.positions.size() / 3u);
        for (int k = 0; k < 4; ++k) {
            d.positions.insert(d.positions.end(), { q[k][0], q[k][1], q[k][2] });
            d.normals.insert(d.normals.end(), { n[0], n[1], n[2] });
            d.uvs.insert(d.uvs.end(), { float(k & 1), float(k >> 1) });
            d.blendIndices.insert(d.blendIndices.end(), { bone, 0, 0, 0 });
            d.blendWeights.insert(d.blendWeights.end(), { 1.0f, 0.0f, 0.0f, 0.0f });
        }
        d.indices.insert(d.indices.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
    };
    const float fz[4][3] = { { x0, y0, z1 }, { x1, y0, z1 }, { x1, y1, z1 }, { x0, y1, z1 } };
    const float bz[4][3] = { { x1, y0, z0 }, { x0, y0, z0 }, { x0, y1, z0 }, { x1, y1, z0 } };
    const float px[4][3] = { { x1, y0, z1 }, { x1, y0, z0 }, { x1, y1, z0 }, { x1, y1, z1 } };
    const float nx[4][3] = { { x0, y0, z0 }, { x0, y0, z1 }, { x0, y1, z1 }, { x0, y1, z0 } };
    const float py[4][3] = { { x0, y1, z1 }, { x1, y1, z1 }, { x1, y1, z0 }, { x0, y1, z0 } };
    const float ny[4][3] = { { x0, y0, z0 }, { x1, y0, z0 }, { x1, y0, z1 }, { x0, y0, z1 } };
    const float nFz[3] = { 0, 0, 1 }, nBz[3] = { 0, 0, -1 }, nPx[3] = { 1, 0, 0 }, nNx[3] = { -1, 0, 0 },
                nPy[3] = { 0, 1, 0 }, nNy[3] = { 0, -1, 0 };
    quad(fz, nFz); quad(bz, nBz); quad(px, nPx); quad(nx, nNx); quad(py, nPy); quad(ny, nNy);
}
static bool addSwungColumn(Scene *s, const Vec3 &at, MaterialId m)
{
    MeshData d;
    addRigBox(d, -0.2f, 0.2f, 0.0f, 1.0f, -0.2f, 0.2f, 0);
    addRigBox(d, -0.2f, 0.2f, 1.0f, 2.0f, -0.2f, 0.2f, 1);
    SkeletonDesc rig;
    rig.id = "shadow.caster_cull swung column v1";
    BoneDesc root;
    root.name = "root";
    rig.bones.push_back(root);
    BoneDesc tip;
    tip.name = "tip";
    tip.parent = 0;
    tip.bindPosition = Vec3(0.0f, 1.0f, 0.0f);
    rig.bones.push_back(tip);
    ClipDesc c;
    c.id = rig.id + " / swing";
    c.name = "Swing";
    c.length = 1.0f;
    BoneTrack t;
    t.bone = 1;
    for (int i = 0; i <= 2; ++i) {
        BoneKey k;
        k.time = 0.5f * float(i);
        k.position = Vec3(0.0f, 1.0f, 0.0f);
        const float a = -1.5707963f * k.time;
        k.rotation = Quat(0.0f, 0.0f, std::sin(0.5f * a), std::cos(0.5f * a));
        t.keys.push_back(k);
    }
    c.tracks.push_back(t);
    const NodeId n = s->createNode();
    const MeshId mesh = s->createMesh(d);
    if (!s->attachSkinnedMesh(n, mesh, m, rig) || !s->attachClips(n, &c, 1)) return false;
    enginetest::setNodePosition(s, n, at);
    ClipState st;
    st.name = "Swing";
    st.enabled = true;
    st.time = 1.0f;   // the clip's end: the tip lies along +x, out of the bind pose's box
    st.weight = 1.0f;
    st.looping = false;
    return s->setClipStates(n, &st, 1);
}

/// THE RECEIVER TEST'S LAW (stage 4b, decision 17): two lockstep worlds, the same roof and field, a particle
/// system and a decal in view, the same camera every frame - one with the sun maps' receiver test, the other
/// with it off (Scene::setShadowReceiverCull) - and THE PICTURES bit-equal (the maps themselves may differ where
/// no receiver samples them). A control first: both on, the pictures equal (the fixture is deterministic).
static int receiverSection(unsigned long long &framesOut)
{
    std::printf("\n== the receiver test: the picture with the sun maps' receiver test on and off ==\n");
    World a, b;
    if (!makeWorld(a, "recv-on", false) || !makeWorld(b, "recv-off", false)) { std::printf("FAIL: the worlds\n"); return 1; }
    const bool tga = writeSolidTga("recv_decal_red.tga", 230, 30, 20);
    Vec3 roofCentre(1.0f, 6.0f, -15.0f);
    const Vec3 roofHalf(13.0f, 0.15f, 19.0f);
    NodeId roofs[2] = {};
    World *ws[2] = { &a, &b };
    for (int i = 0; i < 2; ++i) {
        Scene *sc = ws[i]->scene;
        PbrParams rp;
        rp.albedo = Colour(0.5f, 0.5f, 0.5f);
        rp.roughness = 0.9f;
        roofs[i] = addBox(sc, roofCentre, roofHalf, sc->createPbrMaterial(rp));
        // A PARTICLE SYSTEM in view (unlit: it samples no shadow map) and a DECAL on the ground under it (shaded on
        // the surface it lies on).
        ParticleSystemDesc pd;
        pd.additive = true;
        ParticleEmitterDesc em;
        em.shape = ParticleEmitterShape::Box;
        em.extents = Vec3(0.6f, 0.6f, 0.6f);
        em.rate = 60.0f;
        em.velocityMin = em.velocityMax = 0.0f;
        em.ttlMin = em.ttlMax = 30.0f;
        em.sizeWidth = em.sizeHeight = 0.3f;
        pd.emitters.push_back(em);
        const NodeId ps = sc->createNode();
        enginetest::setNodePosition(sc, ps, Vec3(-1.0f, 1.2f, -2.0f));
        CHECK(sc->setParticleSystem(ps, pd), "a particle system in view");
        if (tga) {
            const TextureId t = sc->loadDecalTexture("recv_decal_red.tga", DecalMap::Diffuse);
            const NodeId dn = sc->createNode();
            sc->setNodeTransform(dn, Vec3(1.0f, 0.0f, -2.0f), Quat(), Vec3(1, 1, 1));
            DecalDesc dd;
            dd.diffuse = t;
            dd.width = dd.height = 1.5f;
            dd.depth = 1.0f;
            CHECK(t && sc->setDecal(dn, dd), "a decal on the ground in view");
        }
        // AN ALPHA-TESTED RECEIVER (decision 19.4): a cut-out plate on the stock route - the id pass does not draw
        // it, so it is a receiver by its box - and A RIGGED COLUMN whose tip swings out of its bind-pose box
        // (decision 19.5: the stock route's skinned receiver, its box grown to a sphere of twice its half-diagonal),
        // each with an Atom block above it toward the sun so their own shadows fall on them.
        PbrParams cp;
        cp.albedo = Colour(0.8f, 0.8f, 0.7f);
        cp.roughness = 0.8f;
        cp.alphaMode = PbrAlphaMode::Cutout;
        cp.alphaCutoff = 0.5f;
        addBox(sc, Vec3(2.2f, 0.6f, -1.5f), Vec3(0.6f, 0.02f, 0.6f), sc->createPbrMaterial(cp));
        PbrParams sp;
        sp.albedo = Colour(0.7f, 0.6f, 0.5f);
        sp.roughness = 0.7f;
        CHECK(addSwungColumn(sc, Vec3(-2.4f, 0.0f, -1.5f), sc->createPbrMaterial(sp)),
              "a rigged column, its tip swung out of its bind pose");
        PbrParams bp;
        bp.albedo = Colour(0.4f, 0.4f, 0.45f);
        bp.roughness = 0.9f;
        const MaterialId bm = sc->createPbrMaterial(bp);
        // toward the sun from each receiver (the fixture's sun: light travelling -0.45, -0.8, -0.4)
        addBox(sc, Vec3(2.2f + 0.56f, 0.6f + 1.0f, -1.5f + 0.5f), Vec3(0.25f, 0.05f, 0.25f), bm);
        addBox(sc, Vec3(-2.4f + 1.5f + 0.56f, 1.2f + 1.0f, -1.5f + 0.5f), Vec3(0.25f, 0.05f, 0.25f), bm);
    }
    // THE CAMERA LOOKS DOWN AT THE NEAR GROUND: most of the field's shadows fall outside the picture.
    CameraDesc cam = enginetest::testCameraDescLookAt(Vec3(0.0f, 3.0f, 3.0f), Vec3(0.0f, 0.0f, -3.0f));
    a.view->setCamera(cam);
    b.view->setCamera(cam);
    enginetest::frames(gE, 40);
    const auto same = [&](const char *what) {
        Image ia, ib;
        const bool read = a.view->readPixels(ia) && b.view->readPixels(ib);
        size_t diff = 0;
        if (read && ia.rgba.size() == ib.rgba.size())
            for (size_t i = 0; i < ia.rgba.size(); ++i) diff += ia.rgba[i] != ib.rgba[i] ? 1u : 0u;
        else
            diff = size_t(-1);
        if (diff) std::printf("   %s: %zu bytes differ\n", what, diff);
        return read && diff == 0;
    };
    CHECK(same("control"), "CONTROL: both worlds with the receiver test, the pictures bit-equal (a deterministic fixture)");
    b.scene->setShadowReceiverCull(false);
    enginetest::frames(gE, 12);
    const AtomDrawStatus sa = a.scene->atomDrawStatus(), sb = b.scene->atomDrawStatus();
    unsigned long long ta = 0ull, tb = 0ull;
    unsigned tested = 0u, testedOff = 0u;
    for (const AtomCasterMapStats &m : sa.casterPerMap)
        if (m.kind == 0u) { ta += m.triangles; tested += m.receiverTested ? 1u : 0u; }
    for (const AtomCasterMapStats &m : sb.casterPerMap)
        if (m.kind == 0u) { tb += m.triangles; testedOff += m.receiverTested ? 1u : 0u; }
    std::printf("   sun maps' Atom caster triangles: %llu with the receiver test, %llu without (maps tested %u / %u)\n",
                ta, tb, tested, testedOff);
    CHECK_MSG(tested >= 3u && testedOff == 0u, "the receiver test ran on the three sun maps of one world only (%u / %u)",
              tested, testedOff);
    CHECK_MSG(ta < tb, "IT CUTS: fewer caster triangles with the receiver test (%llu < %llu)", ta, tb);
    CHECK(same("at rest"), "AT REST THE PICTURE IS BIT-EQUAL with the receiver test on and off");
    // EVERY STEP RAN THE TEST (decision 19.4): the receiver test on the sun maps of world a, never of world b.
    unsigned testedSteps = 0;
    const auto testedNow = [&]() {
        unsigned on = 0u, off = 0u;
        for (const AtomCasterMapStats &m : a.scene->atomDrawStatus().casterPerMap)
            if (m.kind == 0u) on += m.receiverTested ? 1u : 0u;
        for (const AtomCasterMapStats &m : b.scene->atomDrawStatus().casterPerMap)
            if (m.kind == 0u) off += m.receiverTested ? 1u : 0u;
        return on >= 3u && off == 0u;
    };
    // IN MOTION: the roof slides off, then the camera pans - the last steps along the field at a grazing pitch (the
    // focused splits' windows long and narrow: map 2's x and y rows differ, decision 19.1).
    unsigned steps = 0, equal = 0;
    for (int i = 0; i < 12; ++i) {
        roofCentre.x += 1.0f;
        for (int k = 0; k < 2; ++k) ws[k]->scene->setNodeTransform(roofs[k], roofCentre, Quat(), Vec3(1, 1, 1));
        enginetest::frames(gE, 1);
        ++steps;
        equal += same("slide") ? 1u : 0u;
        testedSteps += testedNow() ? 1u : 0u;
    }
    for (int i = 0; i < 12; ++i) {
        const CameraDesc c2 =
            i < 8 ? enginetest::testCameraDescLookAt(Vec3(0.5f * float(i + 1), 3.0f, 3.0f - 0.6f * float(i + 1)),
                                                     Vec3(0.3f * float(i + 1), 0.0f, -3.0f - 0.8f * float(i + 1)))
                  : enginetest::testCameraDescLookAt(Vec3(0.0f, 1.2f, 4.0f), Vec3(0.2f * float(i - 8), 0.6f, -60.0f));
        a.view->setCamera(c2);
        b.view->setCamera(c2);
        enginetest::frames(gE, 1);
        ++steps;
        equal += same("pan") ? 1u : 0u;
        testedSteps += testedNow() ? 1u : 0u;
    }
    // THE WIDEST FILTER (decision 19.4): PCF 6 x 6, its 3-texel reach the dilation's first term.
    gE->setShadowFilter(ShadowFilter::VerySoft);
    for (int i = 0; i < 4; ++i) {
        enginetest::frames(gE, 1);
        ++steps;
        equal += same("pcf 6x6") ? 1u : 0u;
        testedSteps += testedNow() ? 1u : 0u;
    }
    gE->setShadowFilter(ShadowFilter::Soft);
    enginetest::frames(gE, 2);
    CHECK_MSG(equal == steps, "IN MOTION the picture is bit-equal every frame of a slide, a pan, a grazing look along "
              "the field and PCF 6 x 6 (%u of %u)", equal, steps);
    CHECK_MSG(testedSteps == steps, "every one of those frames ran the receiver test on world a's sun maps and none of "
              "world b's (%u of %u)", testedSteps, steps);
    framesOut = a.scene->published().frame;   // the engine's frame count: every frame of this process
    gE->destroyView(a.view);
    gE->destroyScene(a.scene);
    gE->destroyView(b.view);
    gE->destroyScene(b.scene);
    return 0;
}

static int lightCullMain()
{
    std::printf("== shadow.caster_cull: the sun maps' casters culled by the light's own depth, the maps unchanged\n");
    World w;
    if (!makeWorld(w, "light-cull", false)) { std::printf("FAIL: the world\n"); return 1; }
    PbrParams rp;
    rp.albedo = Colour(0.5f, 0.5f, 0.5f);
    rp.roughness = 0.9f;
    const MaterialId rm = w.scene->createPbrMaterial(rp);
    const Vec3 roofHalf(13.0f, 0.15f, 19.0f);
    Vec3 roofCentre(1.0f, 6.0f, -15.0f);
    const NodeId roof = addBox(w.scene, roofCentre, roofHalf, rm);
    // THE LIGHT-DEPTH LAWS ARE ABOUT THE WHOLE MAP: the receiver test (stage 4b, the section after) is off here.
    w.scene->setShadowReceiverCull(false);
    // A PLANAR MIRROR in front of the camera (decision 16.5): its node's sun maps ride the same cull and are
    // compared with the view's.
    const NodeId mirror = addBox(w.scene, Vec3(0.0f, 0.02f, -2.0f), Vec3(2.0f, 0.01f, 2.0f), rm);
    {
        PlanarReflectionParams pr;
        pr.budget = 1;
        pr.resolution = 256;
        pr.shadows = true;
        CHECK(w.scene->setPlanarReflections(pr) && w.scene->setNodePlanarReflector(mirror, true),
              "a planar mirror with shadows (its own shadow node)");
    }
    const CameraDesc cam = enginetest::testCameraDescLookAt(Vec3(0.0f, 2.2f, 4.0f), Vec3(0.0f, 0.8f, -30.0f));
    w.view->setCamera(cam);
    enginetest::frames(gE, 40);

    // THE OBJECTS THE ROOF CERTAINLY HIDES from the sun (law 2's floor), from the field's own layout.
    const float lx = -0.45f, ly = -0.8f, lz = -0.4f;
    const float ll = std::sqrt(lx * lx + ly * ly + lz * lz);
    const float tx = -lx / ll, ty = -ly / ll, tz = -lz / ll;   // toward the sun
    const float under = roofCentre.y - roofHalf.y;
    unsigned certain = 0;
    std::vector<NodeId> hidden;
    {
        int k = 0;
        for (int zi = 0; zi < 7; ++zi)
            for (int xi = -2; xi <= 2; ++xi, ++k) {
                const float x = 2.6f * float(xi) + 0.4f * float(zi % 2), z = -4.0f - 11.0f * float(zi);
                const float r = 0.8f * std::sqrt(3.0f);   // ~1.6 m objects: the box's half-diagonal
                bool inside = true;
                for (int c = 0; c < 8 && inside; ++c) {
                    const float px = x + ((c & 1) ? r : -r), py = 1.0f + ((c & 2) ? r : -r),
                                pz = z + ((c & 4) ? r : -r);
                    const float t = (under - py) / ty;
                    const float hx = px + tx * t, hz = pz + tz * t;
                    // THE CULL'S GRAIN (stated): an instance's box against the pyramid at the mip where its
                    // light-space rectangle is <= 2 texels wide, read over <= 3 x 3 of them - a footprint up
                    // to one rectangle width past each side. The box's light-space width is at most its
                    // diagonal, 2 r sqrt 3: so a box that deep inside the roof's shadow is hidden at that grain.
                    const float margin = 2.0f * r * std::sqrt(3.0f) + 0.5f;
                    inside = hx > roofCentre.x - roofHalf.x + margin && hx < roofCentre.x + roofHalf.x - margin &&
                             hz > roofCentre.z - roofHalf.z + margin && hz < roofCentre.z + roofHalf.z - margin;
                }
                if (inside) {
                    ++certain;
                    hidden.push_back(w.placed[size_t(k)].node);
                }
            }
    }
    std::printf("   the roof certainly hides %u of the field's 35 objects from the sun\n", certain);
    CHECK_MSG(certain >= 4u, "the fixture hides something (%u objects)", certain);

    // ---- 1 + 2: AT REST ----------------------------------------------------------------------
    std::vector<ShadowMapDigest> on, off;
    gE->setArm("shadow.casterCull", 1.0);
    enginetest::frames(gE, 12);
    const AtomDrawStatus sOn = w.scene->atomDrawStatus();
    CHECK(w.scene->shadowMapDigests(on) && on.size() >= 6u, "the view's three sun maps and the mirror's read back (cull on)");
    gE->setArm("shadow.casterCull", 0.0);
    enginetest::frames(gE, 12);
    const AtomDrawStatus sOff = w.scene->atomDrawStatus();
    CHECK(w.scene->shadowMapDigests(off) && off.size() >= 6u, "the view's three sun maps and the mirror's read back (cull off)");
    std::string why;
    CHECK_MSG(mapsEqual(on, off, why), "AT REST the sun maps are bit-equal with the cull on and off (%s)",
              why.empty() ? "every texel" : why.c_str());
    for (const ShadowMapDigest &d : on)
        std::printf("   %s map %u: %ux%u at (%u, %u), %u texels covered, digest %016llx\n", d.kind ? "mirror" : "view  ",
                    d.map, d.width, d.height, d.x, d.y, d.covered, d.digest);
    const CullTally tOn = tallyOf(sOn), tOff = tallyOf(sOff);
    std::printf("   cull on : %u maps (%u tested by the light's depth), %llu triangles; first half rejected %u, "
                "late half drew %u (%llu triangles), neither drew %u\n",
                tOn.maps, tOn.tested, tOn.triangles, tOn.occluded, tOn.lateInstances, tOn.lateTriangles,
                tOn.lateOccluded);
    std::printf("   cull off: %u maps (%u tested), %llu triangles\n", tOff.maps, tOff.tested, tOff.triangles);
    for (const AtomCasterMapStats &m : sOn.casterPerMap)
        if (m.kind == 0u)
            std::printf("     map %u face %u: %u instances %llu triangles (late %u / %llu), rejected %u, hidden %u\n",
                        m.map, m.face, m.instances, m.triangles, m.lateInstances, m.lateTriangles, m.occluded,
                        m.lateOccluded);
    CHECK_MSG(tOn.tested >= 3u && tOff.tested == 0u, "the three sun maps were tested by the light's depth with the "
              "cull on, none with it off (%u / %u)", tOn.tested, tOff.tested);
    {
        unsigned mirrorTested = 0u;
        for (const AtomCasterMapStats &m : sOn.casterPerMap) mirrorTested += (m.kind == 1u && m.lightTested) ? 1u : 0u;
        CHECK_MSG(mirrorTested >= 1u, "the mirror's sun maps were culled by the light's depth too (%u)", mirrorTested);
    }
    // LAW 2 PER OBJECT (decision 16.5): every object the roof certainly hides is drawn by NEITHER half of ANY
    // sun map's cut - the test door's drawn sets, the first half's and the late half's.
    {
        gE->setArm("shadow.casterCull", 1.0);
        enginetest::frames(gE, 4);
        auto *os = static_cast<OgreScene *>(w.scene);
        unsigned drawnHidden = 0, probed = 0;
        for (unsigned m = 0; m < 3u; ++m) {
            OgreScene::CasterProbe p;
            if (!probe(w, m, 0u, p)) continue;
            ++probed;
            std::map<NodeId, int> drawnNodes;   // 1 the first half drew it, 2 the late half
            int half = 1;
            for (const auto *set : { &p.drawn, &p.lateDrawn }) {
                for (const OgreScene::CasterProbe::Drawn &d : *set) {
                    GpuSceneEntry e;
                    if (os->gpuSceneEntry(d.slot, e)) drawnNodes[NodeId(e.nodeId)] |= half;
                }
                half = 2;
            }
            for (size_t i = 0; i < hidden.size(); ++i) {
                const auto it = drawnNodes.find(hidden[i]);
                if (it == drawnNodes.end()) continue;
                ++drawnHidden;
                std::printf("   hidden object %zu (node %llu) drawn in map %u by the %s half\n", i,
                            (unsigned long long)hidden[i], m, it->second & 1 ? "first" : "late");
            }
        }
        CHECK_MSG(probed == 3u && drawnHidden == 0u,
                  "EVERY OBJECT THE ROOF CERTAINLY HIDES is drawn by neither half of any sun map (%u of %u objects "
                  "x 3 maps drawn)", drawnHidden, certain);
    }
    CHECK_MSG(tOn.triangles < tOff.triangles, "the maps draw fewer caster triangles with the cull (%llu < %llu)",
              tOn.triangles, tOff.triangles);

    // ---- 3: IN MOTION ------------------------------------------------------------------------
    unsigned stepsEqual = 0, steps = 0;
    unsigned long long lateDrawn = 0ull;
    std::string firstWhy;
    // `lead`: frames rendered with the cull on before the compared one - a pushed CAMERA reaches the
    // light's splits one frame after it is set (measured: two cull-off frames after a pan differ, the
    // second and third agree), so a pan's compared frame is its second, and its history the first's
    // pyramid - drawn from the previous pose's splits.
    const auto step = [&](int lead) {
        std::vector<ShadowMapDigest> a, b;
        gE->setArm("shadow.casterCull", 1.0);
        if (lead > 0) enginetest::frames(gE, lead);
        enginetest::frames(gE, 1);
        lateDrawn += tallyOf(w.scene->atomDrawStatus()).lateInstances;
        w.scene->shadowMapDigests(a);
        gE->setArm("shadow.casterCull", 0.0);
        enginetest::frames(gE, 1);
        lateDrawn += tallyOf(w.scene->atomDrawStatus()).lateInstances;
        w.scene->shadowMapDigests(b);
        std::string wy;
        ++steps;
        if (mapsEqual(a, b, wy)) ++stepsEqual;
        else if (firstWhy.empty()) firstWhy = "step " + std::to_string(steps) + ": " + wy;
    };
    for (int i = 0; i < 16; ++i) {
        roofCentre.x += 0.75f;
        w.scene->setNodeTransform(roof, roofCentre, Quat(), Vec3(1, 1, 1));
        step(0);
    }
    for (int i = 0; i < 8; ++i) {
        const CameraDesc c2 = enginetest::testCameraDescLookAt(Vec3(0.6f * float(i + 1), 2.2f, 4.0f - 0.8f * float(i + 1)),
                                                            Vec3(0.0f, 0.8f, -30.0f));
        w.view->setCamera(c2);
        step(1);
    }
    gE->setArm("shadow.casterCull", 1.0);
    enginetest::frames(gE, 8);
    lateDrawn += tallyOf(w.scene->atomDrawStatus()).lateInstances;
    std::printf("   in motion: %u of %u steps bit-equal; the late half drew %llu instances over the steps\n", stepsEqual,
                steps, lateDrawn);
    CHECK_MSG(stepsEqual == steps, "IN MOTION every step's sun maps are bit-equal on and off (%s)",
              firstWhy.empty() ? "all" : firstWhy.c_str());
    CHECK_MSG(lateDrawn > 0ull, "the slide made the late half draw what the first half's prediction rejected (%llu)",
              lateDrawn);
    gE->setArm("shadow.casterCull", 1.0);
    gE->destroyView(w.view);
    gE->destroyScene(w.scene);
    unsigned long long frames = 0ull;
    const int rc = receiverSection(frames);
    std::printf("\n   frames rendered: %llu\n", frames);
    return rc;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const std::string mode = argc > 1 ? argv[1] : "--cut";
    if (!loadFixtures()) { std::printf("FAIL: the fixtures (the shipped models did not bake)\n"); return 1; }
    std::string err;
    const auto cfg = enginetest::testEngineConfig(mode == "--parity"        ? "test-shadow-atom-parity-ogre.log"
                  : mode == "--first-frame" ? "test-shadow-caster-first-frame-ogre.log"
                  : mode == "--light-cull"  ? "test-shadow-caster-cull-ogre.log"
                                            : "test-shadow-atom-cut-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->submitFrame(jahshaka::engine::FrameInput::delta(1.0f / 60.0f));
    gE = engine.get();
    const int rc = mode == "--parity"        ? parityMain()
                 : mode == "--first-frame" ? firstFrameMain()
                 : mode == "--light-cull"  ? lightCullMain()
                                           : cutMain();
    engine.reset();
    std::printf("%s (%d failure%s)\n", failures || rc ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
    return failures || rc ? 1 : 0;
}
