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
};

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
        if (dense) {   // THE DENSE CASTER: level 0 is the only level
            d.lodIndices.clear();
            d.lodErrors.clear();
            d.lodBounds.clear();
        }
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
    if (dense) w.scene->setAtomDrawEnabled(false);
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
                        unsigned mapHeight, CutTally &all)
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
    CHECK_MSG(t.instances > 0u, "%s: the map drew Atom casters (%u instances)", label, t.instances);
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

static bool probe(World &w, unsigned map, unsigned face, OgreScene::CasterProbe &out)
{
    auto *os = static_cast<OgreScene *>(w.scene);
    os->armCasterProbeForTest(map, face);
    for (int i = 0; i < 24; ++i) {
        gE->renderOneFrame();
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
    for (int i = 0; i < 30; ++i) gE->renderOneFrame();
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
            checkOneCut(w, p, label, ortho, mapHeightOf(sn, m.first, isPoint), all);
        }
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
    for (int i = 0; i < 40; ++i) gE->renderOneFrame();
    {
        const AtomDrawStatus sa = a.scene->atomDrawStatus(), sb = b.scene->atomDrawStatus();
        CHECK_MSG(sa.on && sa.atomItems >= 35u && !sb.on && sb.atomItems == 0u,
                  "one world through the caster cut (%u atom items), the other all stock PBS (%u)", sa.atomItems,
                  sb.atomItems);
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
    for (int i = 0; i < 4; ++i) gE->renderOneFrame();
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
        unsigned both = 0, silhouette = 0, flips = 0;
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
                if (errWorld > bias) ++flips;
            }
        std::sort(errTexels.begin(), errTexels.end());
        const float mx = errTexels.empty() ? 0.0f : errTexels.back();
        const float p99 = errTexels.empty() ? 0.0f : errTexels[size_t(double(errTexels.size() - 1) * 0.99)];
        const unsigned covered = both + silhouette;
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
    for (World *w : { &a, &b }) {
        gE->destroyView(w->view);
        gE->destroyScene(w->scene);
    }
    return 0;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const std::string mode = argc > 1 ? argv[1] : "--cut";
    if (!loadFixtures()) { std::printf("FAIL: the fixtures (the shipped models did not bake)\n"); return 1; }
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = mode == "--parity" ? "test-shadow-atom-parity-ogre.log" : "test-shadow-atom-cut-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    gE = engine.get();
    const int rc = mode == "--parity" ? parityMain() : cutMain();
    engine.reset();
    std::printf("%s (%d failure%s)\n", failures || rc ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
    return failures || rc ? 1 : 0;
}
