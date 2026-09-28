// shadow.stock_lod — A SHADOW MAP DRAWS ITS STOCK CASTERS AT THE LIGHT'S LEVEL (SHADOW-LOD-1;
// SPECS/briefs/SHADOW-LOD-1.md; the fork commit "A shadow caster pass computes LOD for its
// own camera").
//
// Before the fork change every shadow scene pass drew its stock-PBR casters (alpha-tested,
// skinned, two-sided — what the Atom caster cut does not take) at the level the VIEW's pass
// had chosen for the EYE: the pin's CompositorShadowNodeDef::_validateAndFinish turned the
// caster passes' LOD update off ("Regular nodes calculate the LOD values, we just use them").
// A light behind the camera, a far PSSM split or a point light's cube faces drew what the eye
// wanted. The rule now is the Atom casters' rule: one TEXEL OF THE MAP is the pixel.
//
// The probe is a workspace listener on the view (Ogre's own hooks, nothing added for the
// test): a caster pass's passPreExecute runs after the pass's LOD walk and before its cull,
// so the level read there is the level that map draws; a view pass's passPreExecute runs
// after ITS walk and before the shadow node's update, and passSceneAfterShadowMaps after the
// update, so the pair is the save/restore; passSceneAfterFrustumCulling is the level the
// view draws. The world: alpha-tested stock casters on a hand-made eight-level chain — one
// FAR from the eye under a spot light, one beside a point light, one near the eye — an
// Atom-routed copy of the same mesh near the eye, a ground and a sun.
//
//   (a) THE LIGHT'S LEVEL: in EVERY caster pass that ran (the sun's PSSM splits, the spot's
//       map, the point light's six cube faces), every stock caster's level is the level the
//       engine's LOD rule chooses at THAT pass's camera with the MAP's height in texels —
//       computed here from the pass's camera and viewport (the rule's public currency,
//       Types.h), never read back from the walk. An orthographic split's window is the
//       map's own projection (orthoWindowHeight = 2 / proj[1][1]).
//   (b) NOT THE EYE'S: the far caster's level in the spot map differs from the level the
//       view draws it at, and each map kind (PSSM, spot, point face) draws some caster at a
//       level other than the view's (the claim is not vacuous).
//   (c) THE VIEW'S LEVEL IS UNTOUCHED BY THE NODE: for the pass that updated the shadow
//       node, every stock caster's level after the node equals its level before it — with
//       the split ON (the screen decode updates the node) and OFF (the view's own opaque
//       pass walks LOD, updates the node, then draws: the restore is what it draws); and the
//       level every LOD-walking view pass draws is the view camera's rule level.
//   (d) THE VIEW'S WALK SKIPS THE ATOM QUEUE: an Atom-routed item's level, set by hand to
//       its coarsest while it stands 5 m from the eye (the view's rule: level 0), is still
//       the coarsest after frames (no pass of the view walks the queue the id pass draws);
//       a stock item given the same treatment is put back by the view's walk (the control).
// Frames, never time.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include "EnginePrivate.h"

#include <Compositor/OgreCompositorShadowNode.h>
#include <Compositor/OgreCompositorWorkspace.h>
#include <Compositor/OgreCompositorWorkspaceListener.h>
#include <Compositor/Pass/PassScene/OgreCompositorPassScene.h>
#include <Compositor/Pass/PassScene/OgreCompositorPassSceneDef.h>
#include <OgreCamera.h>
#include <OgreItem.h>
#include <OgreLight.h>
#include <OgreMesh2.h>
#include <OgreSceneNode.h>


#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
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
static const int kLevels = 8;   // level 0 + seven coarser

// ---------------------------------------------------------------------------
// THE MESH: a unit sphere (the last ring and column WRAP by index — ENGINE.md, procedural
// seams) with a hand-made chain whose bounds step by 4x from 0.4 mm: wide enough that a
// light at 3 m and an eye at 50 m, or a 1024-texel map and a 540-pixel view, land on
// different levels. The coarser levels are prefixes of level 0's triangles: what a level
// LOOKS like is not the subject, which level a pass takes is.
static MeshData lodSphere()
{
    const int rings = 16, segs = 32;
    MeshData d;
    for (int r = 0; r <= rings; ++r) {
        const float v = float(r) / float(rings), th = 3.14159265f * v;
        for (int s = 0; s < segs; ++s) {
            const float ph = 6.2831853f * float(s) / float(segs);
            const float x = std::sin(th) * std::cos(ph), y = std::cos(th), z = std::sin(th) * std::sin(ph);
            d.positions.insert(d.positions.end(), { x, y, z });
            d.normals.insert(d.normals.end(), { x, y, z });
            d.uvs.insert(d.uvs.end(), { float(s) / float(segs), v });
        }
    }
    for (int r = 0; r < rings; ++r)
        for (int s = 0; s < segs; ++s) {
            const unsigned a = unsigned(r * segs + s), b = unsigned(r * segs + (s + 1) % segs);
            const unsigned c = a + unsigned(segs), e = b + unsigned(segs);
            d.indices.insert(d.indices.end(), { a, c, b, b, c, e });
        }
    const size_t tris = d.indices.size() / 3;
    float bound = 0.0004f;
    for (int L = 1; L < kLevels; ++L, bound *= 4.0f) {
        const size_t keep = std::max<size_t>(8, tris >> L);
        d.lodIndices.push_back(std::vector<unsigned>(d.indices.begin(), d.indices.begin() + ptrdiff_t(keep * 3)));
        d.lodBounds.push_back(bound);
        d.lodErrors.push_back(bound);
    }
    return d;
}

// ---------------------------------------------------------------------------
// THE RULE, evaluated from outside the walk: the engine's LOD strategy (OgreMesh.cpp,
// JahWorldErrorLodStrategy) spelled from the public currency. `value` = the world error the
// camera affords at the object in MESH units; the level = the coarsest whose threshold is
// at or below it (LodStrategy::lodSet's lower_bound - 1). `nearEdge` = the value sits within
// 1e-4 of a threshold (float ordering between two spellings of one product).
struct RuleLevel {
    unsigned level = 0;
    bool nearEdge = false;
    float value = 0.0f;
};

static RuleLevel ruleLevel(const Ogre::Camera *cam, float height, float passBias, const Ogre::Item *item)
{
    RuleLevel out;
    const Ogre::FastArray<Ogre::Real> *vals = item->getMesh()->_getLodValueArray();
    const bool ortho = cam->getProjectionType() == Ogre::PT_ORTHOGRAPHIC;
    float perPixel = ortho ? allowedWorldError(kLodBudgetPixels,
                                               sampleFootprintOrtho(float(cam->getOrthoWindowHeight()), height))
                           : allowedWorldError(kLodBudgetPixels,
                                               sampleFootprintPerspective(1.0f, float(cam->getProjectionMatrix()[1][1]),
                                                                          height));
    perPixel *= float(cam->_getLodBiasInverse()) * passBias;
    const float lr = float(item->getLocalRadius()), wr = float(item->getWorldRadius());
    const float meshUnits = wr > 1e-6f ? lr / wr : 1.0f;
    float v = perPixel * meshUnits;
    if (!ortho) {
        const float d = float(item->getWorldAabb().mCenter.distance(cam->getDerivedPosition())) - wr;
        v *= std::max(d, 0.0f);
    }
    out.value = v;
    auto it = std::lower_bound(vals->begin(), vals->end(), Ogre::Real(v));
    out.level = unsigned(std::max<ptrdiff_t>(it - vals->begin() - 1, 0));
    for (size_t i = 1; i < vals->size(); ++i)
        if (std::fabs(float((*vals)[i]) - v) <= 1e-4f * std::max(1e-9f, std::fabs(v))) out.nearEdge = true;
    return out;
}

// ---------------------------------------------------------------------------
struct Probe {
    const char *name;
    NodeId node = 0;
    Ogre::Item *item = nullptr;
};

struct CasterRec {
    unsigned map = 0;
    Ogre::Light::LightTypes kind = Ogre::Light::LT_DIRECTIONAL;
    size_t probe = 0;
    unsigned level = 0;
    RuleLevel rule;
    bool orthoWindowIsMap = true;
    float orthoH = 0.0f, projH = 0.0f;
};
struct NodeRec {
    std::vector<unsigned> before, after;
    bool walks = false;   // the updating pass walks LOD itself (split off)
};
struct DrawRec {
    size_t probe = 0;
    unsigned level = 0;
    RuleLevel rule;
};

class LodListener : public Ogre::CompositorWorkspaceListener
{
public:
    std::vector<Probe> *probes = nullptr;
    bool armed = false;
    std::vector<CasterRec> casters;
    std::vector<NodeRec> nodes;
    std::vector<DrawRec> draws;

    void passPreExecute(Ogre::CompositorPass *pass) override
    {
        if (!armed || pass->getType() != Ogre::PASS_SCENE) return;
        auto *ps = static_cast<Ogre::CompositorPassScene *>(pass);
        const Ogre::CompositorPassSceneDef *def = ps->getDefinition();
        if (def->mShadowNodeRecalculation == Ogre::SHADOW_NODE_CASTER_PASS) {
            auto *sn = dynamic_cast<const Ogre::CompositorShadowNode *>(pass->getParentNode());
            if (!sn || !sn->isShadowMapIdxActive(def->mShadowMapIdx)) return;
            const Ogre::Light *light = sn->getLightAssociatedWith(def->mShadowMapIdx);
            if (!light) return;
            mCasterSince = true;
            const Ogre::Camera *cam = ps->getCamera();
            const float height = float(ps->getActualDimensions().y);
            for (size_t i = 0; i < probes->size(); ++i) {
                const Probe &p = (*probes)[i];
                if (!p.item || p.item->getRenderQueueGroup() == 11u) continue;   // the Atom queue: the cut's
                CasterRec r;
                r.map = unsigned(def->mShadowMapIdx);
                r.kind = light->getType();
                r.probe = i;
                r.level = p.item->getCurrentMeshLod();
                r.rule = ruleLevel(cam, height, float(def->mLodBias), p.item);
                if (cam->getProjectionType() == Ogre::PT_ORTHOGRAPHIC) {
                    r.orthoH = float(cam->getOrthoWindowHeight());
                    r.projH = 2.0f / std::fabs(float(cam->getProjectionMatrix()[1][1]));
                    r.orthoWindowIsMap = std::fabs(r.orthoH - r.projH) <= 1e-3f * r.projH;
                }
                casters.push_back(r);
            }
            return;
        }
        // A view pass: its levels after its own walk, before the shadow node it may update.
        mViewPass = ps;
        mCasterSince = false;
        mBefore.clear();
        for (const Probe &p : *probes) mBefore.push_back(p.item ? p.item->getCurrentMeshLod() : 0u);
    }
    void passSceneAfterShadowMaps(Ogre::CompositorPassScene *ps) override
    {
        if (!armed || ps != mViewPass || !mCasterSince) return;
        NodeRec n;
        n.before = mBefore;
        for (const Probe &p : *probes) n.after.push_back(p.item ? p.item->getCurrentMeshLod() : 0u);
        n.walks = ps->getDefinition()->mUpdateLodLists;
        nodes.push_back(n);
        mCasterSince = false;
    }
    void passSceneAfterFrustumCulling(Ogre::CompositorPassScene *ps) override
    {
        if (!armed) return;
        const Ogre::CompositorPassSceneDef *def = ps->getDefinition();
        if (def->mShadowNodeRecalculation == Ogre::SHADOW_NODE_CASTER_PASS || !def->mUpdateLodLists) return;
        if (def->mLodCameraName != Ogre::IdString()) return;
        const float height = float(ps->getActualDimensions().y);
        for (size_t i = 0; i < probes->size(); ++i) {
            const Probe &p = (*probes)[i];
            if (!p.item || p.item->getRenderQueueGroup() == 11u) continue;
            if (p.item->getRenderQueueGroup() < def->mFirstRQ || p.item->getRenderQueueGroup() >= def->mLastRQ) continue;
            DrawRec r;
            r.probe = i;
            r.level = p.item->getCurrentMeshLod();
            r.rule = ruleLevel(ps->getCamera(), height, float(def->mLodBias), p.item);
            draws.push_back(r);
        }
    }
    void clear()
    {
        casters.clear();
        nodes.clear();
        draws.clear();
        mViewPass = nullptr;
        mCasterSince = false;
    }

private:
    Ogre::CompositorPassScene *mViewPass = nullptr;
    bool mCasterSince = false;
    std::vector<unsigned> mBefore;
};

static Ogre::Item *itemOf(OgreScene *os, NodeId n)
{
    Ogre::SceneNode *sn = os->node(n);
    if (!sn) return nullptr;
    for (size_t i = 0; i < sn->numAttachedObjects(); ++i)
        if (auto *it = dynamic_cast<Ogre::Item *>(sn->getAttachedObject(i))) return it;
    return nullptr;
}

static const char *kindName(Ogre::Light::LightTypes k)
{
    return k == Ogre::Light::LT_DIRECTIONAL ? "pssm" : k == Ogre::Light::LT_SPOTLIGHT ? "spot" : "point face";
}

int main()
{
    std::printf("== shadow.stock_lod: a shadow map draws its stock casters at the level ITS camera's texel selects\n");
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-shadow-stock-lod-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();
    int rc = 0;
    {
        View *view = e->createOffscreenView("stock-lod", kW, kH, Colour(0, 0, 0));
        Scene *s = e->createScene("stock-lod");
        if (!view || !s) { std::printf("FAIL: view/scene\n"); return 1; }
        auto *os = static_cast<OgreScene *>(s);
        s->setLodBias(1.0f);
        s->setAmbient(Colour(0.10f, 0.11f, 0.13f), Colour(0.05f, 0.05f, 0.05f));
        {   // the ground
            PbrParams gp;
            gp.albedo = Colour(0.45f, 0.45f, 0.42f);
            gp.roughness = 0.8f;
            MeshData g;
            g.positions = { -150.0f, 0.0f, -150.0f, 150.0f, 0.0f, -150.0f, 150.0f, 0.0f, 150.0f, -150.0f, 0.0f, 150.0f };
            g.normals = { 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0 };
            g.uvs = { 0, 0, 20, 0, 20, 20, 0, 20 };
            g.indices = { 0, 2, 1, 0, 3, 2 };
            const NodeId n = s->createNode();
            s->setNodeTransform(n, Vec3(0, 0, 0), Quat(), Vec3(1, 1, 1));
            s->attachMesh(n, s->createMesh(g), s->createPbrMaterial(gp));
        }
        const MeshId sphere = s->createMesh(lodSphere());
        CHECK_MSG(sphere != 0, "the eight-level sphere builds (%s)", e->lastError().c_str());
        PbrParams cutout;   // THE STOCK CASTER: alpha-tested (no texture: every pixel passes)
        cutout.albedo = Colour(0.7f, 0.35f, 0.25f);
        cutout.roughness = 0.5f;
        cutout.alphaMode = PbrAlphaMode::Cutout;
        const MaterialId stockMat = s->createPbrMaterial(cutout);
        PbrParams opaque = cutout;
        opaque.alphaMode = PbrAlphaMode::Opaque;
        const MaterialId atomMat = s->createPbrMaterial(opaque);

        std::vector<Probe> probes = { { "far caster under the spot (50 m)" }, { "caster beside the point (14 m)" },
                                      { "near caster (6 m)" }, { "Atom-routed twin (5 m)" } };
        const Vec3 at[] = { Vec3(0.0f, 0.8f, -46.0f), Vec3(-2.2f, 0.8f, -10.0f), Vec3(1.6f, 0.8f, -2.0f),
                            Vec3(-1.6f, 0.8f, -1.0f) };
        for (size_t i = 0; i < probes.size(); ++i) {
            probes[i].node = s->createNode();
            s->setNodeTransform(probes[i].node, at[i], Quat(), Vec3(0.8f, 0.8f, 0.8f));
            s->attachMesh(probes[i].node, sphere, i == 3 ? atomMat : stockMat);
        }
        enginetest::addDirectionalLight(s, Vec3(-0.45f, -0.8f, -0.4f), 2.2f);
        std::vector<std::pair<NodeId, Vec3>> lamps;
        {   // THE SPOT: 3 m over the far caster, straight down
            const NodeId n = s->createNode();
            const Vec3 p(0.0f, 3.8f, -46.0f);
            s->setNodeTransform(n, p, Quat(), Vec3(1, 1, 1));
            LightDesc l;
            l.type = LightType::Spot;
            l.intensity = 30.0f;
            l.range = 12.0f;
            l.spotAngleDegrees = 50.0f;
            s->setLight(n, l);
            lamps.push_back({ n, p });
        }
        {   // THE POINT: beside the second caster
            const NodeId n = s->createNode();
            const Vec3 p(-4.0f, 1.8f, -10.0f);
            s->setNodeTransform(n, p, Quat(), Vec3(1, 1, 1));
            LightDesc l;
            l.type = LightType::Point;
            l.intensity = 20.0f;
            l.range = 10.0f;
            s->setLight(n, l);
            lamps.push_back({ n, p });
        }
        view->setOffscreenContract(OffscreenContract::StillPicture);
        view->setScene(s);
        view->setShadows(true);
        PostFxDesc fx;
        fx.allowOffscreen = true;
        fx.ssr = 0;
        view->setPostFx(fx);
        view->setCamera(enginetest::testCameraDescLookAt(Vec3(0.0f, 1.8f, 4.0f), Vec3(0.0f, 0.8f, -30.0f)));
        for (int i = 0; i < 40; ++i) e->renderOneFrame();
        for (Probe &p : probes) p.item = itemOf(os, p.node);
        CHECK(probes[0].item && probes[1].item && probes[2].item && probes[3].item, "every probe has its Item");
        if (!probes[0].item || !probes[1].item || !probes[2].item || !probes[3].item) return 1;
        {
            const AtomDrawStatus st = s->atomDrawStatus();
            CHECK_MSG(st.on && probes[3].item->getRenderQueueGroup() == 11u &&
                          probes[0].item->getRenderQueueGroup() != 11u,
                      "the split is on: the opaque twin rides the Atom queue, the alpha-tested casters stay on PBS "
                      "(%u atom items, %u alpha-tested)",
                      st.atomItems, st.alphaTested);
        }
        const unsigned levels = unsigned(probes[0].item->getMesh()->_getLodValueArray()->size());
        CHECK_MSG(levels == unsigned(kLevels), "the mesh carries %d levels (Ogre's value array: %u)", kLevels, levels);

        LodListener lis;
        lis.probes = &probes;
        auto *ov = static_cast<OgreView *>(view);

        // Runs `frames` frames with the lamps' maps re-rendered each frame (a still lamp's
        // map is cached: a millimetre nudge re-renders it and moves nothing).
        const auto measure = [&](int frames) {
            Ogre::CompositorWorkspace *ws = ov->workspace();
            lis.clear();
            lis.armed = true;
            ws->addListener(&lis);
            for (int f = 0; f < frames; ++f) {
                for (const auto &l : lamps)
                    s->setNodeTransform(l.first, Vec3(l.second.x, l.second.y + 0.001f * float(f % 2 + 1), l.second.z),
                                        Quat(), Vec3(1, 1, 1));
                e->renderOneFrame();
            }
            ws->removeListener(&lis);
            lis.armed = false;
        };

        for (int arm = 0; arm < 2; ++arm) {
            const bool split = arm == 0;
            const char *armName = split ? "split on" : "split off";
            if (!split) {
                s->setAtomDrawEnabled(false);
                for (int i = 0; i < 20; ++i) e->renderOneFrame();
                CHECK_MSG(!s->atomDrawStatus().on && probes[3].item->getRenderQueueGroup() != 11u,
                          "%s: every item is on the stock PBS path", armName);
            }
            measure(4);

            // (a) the light's level, in every caster pass that ran
            std::map<Ogre::Light::LightTypes, unsigned> passesOf, offEye;
            std::map<unsigned, bool> seenMap;
            unsigned bad = 0, nearEdge = 0, notMapWindow = 0, total = 0;
            unsigned viewLevel[4] = {};
            for (const DrawRec &d : lis.draws) viewLevel[d.probe] = d.level;
            for (const CasterRec &r : lis.casters) {
                ++total;
                seenMap[r.map] = true;
                if (!r.orthoWindowIsMap) ++notMapWindow;
                if (r.level != r.rule.level) {
                    if (r.rule.nearEdge) ++nearEdge;
                    else {
                        ++bad;
                        if (bad <= 6)
                            std::printf("   %s: map %u (%s) %s drew level %u, the light's rule says %u (value %.6g)\n",
                                        armName, r.map, kindName(r.kind), probes[r.probe].name, r.level, r.rule.level,
                                        double(r.rule.value));
                    }
                }
                if (r.level != viewLevel[r.probe]) offEye[r.kind]++;
            }
            for (const CasterRec &r : lis.casters) passesOf[r.kind]++;
            std::printf("   %s: %u caster readings over %zu maps (pssm %u, spot %u, point %u); near a threshold %u\n",
                        armName, total, seenMap.size(), passesOf[Ogre::Light::LT_DIRECTIONAL],
                        passesOf[Ogre::Light::LT_SPOTLIGHT], passesOf[Ogre::Light::LT_POINT], nearEdge);
            std::map<std::pair<unsigned, size_t>, bool> printed;   // one line per (map, caster)
            for (const CasterRec &r : lis.casters)
                if ((r.probe == 0 || (r.kind == Ogre::Light::LT_POINT && r.probe == 1)) &&
                    !printed[{ r.map, r.probe }] && (printed[{ r.map, r.probe }] = true))
                    std::printf("     map %2u %-10s %-36s level %u (rule %u, value %.5g), the view's %u\n", r.map,
                                kindName(r.kind), probes[r.probe].name, r.level, r.rule.level, double(r.rule.value),
                                viewLevel[r.probe]);
            CHECK_MSG(passesOf[Ogre::Light::LT_DIRECTIONAL] > 0 && passesOf[Ogre::Light::LT_SPOTLIGHT] > 0 &&
                          passesOf[Ogre::Light::LT_POINT] >= 6u * 3u,
                      "%s: the caster passes ran: PSSM splits, the spot's map and the point's cube faces (%u / %u / %u "
                      "readings)",
                      armName, passesOf[Ogre::Light::LT_DIRECTIONAL], passesOf[Ogre::Light::LT_SPOTLIGHT],
                      passesOf[Ogre::Light::LT_POINT]);
            CHECK_MSG(bad == 0 && total > 0,
                      "%s: (a) every stock caster in every caster pass drew the level the LIGHT's camera selects at one "
                      "texel of its map (%u of %u readings off the rule)",
                      armName, bad, total);
            CHECK_MSG(notMapWindow == 0, "%s: (a) each split's ortho window is its projection's (2 / proj[1][1])",
                      armName);
            // (b) not the eye's
            bool farDiffers = false;
            for (const CasterRec &r : lis.casters)
                if (r.probe == 0 && r.kind == Ogre::Light::LT_SPOTLIGHT && r.level != viewLevel[0]) farDiffers = true;
            CHECK_MSG(farDiffers, "%s: (b) the far caster's level in the spot map differs from the view's (view %u)",
                      armName, viewLevel[0]);
            CHECK_MSG(offEye[Ogre::Light::LT_DIRECTIONAL] > 0 && offEye[Ogre::Light::LT_SPOTLIGHT] > 0 &&
                          offEye[Ogre::Light::LT_POINT] > 0,
                      "%s: (b) every map kind draws some caster off the eye's level (pssm %u, spot %u, point %u)",
                      armName, offEye[Ogre::Light::LT_DIRECTIONAL], offEye[Ogre::Light::LT_SPOTLIGHT],
                      offEye[Ogre::Light::LT_POINT]);
            // (c) the view's level untouched by the node, and drawn at the view's rule
            unsigned moved = 0, walking = 0;
            for (const NodeRec &n : lis.nodes) {
                if (n.walks) ++walking;
                for (size_t i = 0; i < n.before.size(); ++i)
                    if (n.before[i] != n.after[i]) ++moved;
            }
            CHECK_MSG(!lis.nodes.empty() && moved == 0,
                      "%s: (c) the shadow node's update left every view level as the view chose it (%zu updates, %u "
                      "levels moved)",
                      armName, lis.nodes.size(), moved);
            if (!split)
                CHECK_MSG(walking == lis.nodes.size(),
                          "%s: (c) the pass that updates the node walks LOD itself and draws after it (%u of %zu)",
                          armName, walking, lis.nodes.size());
            unsigned drawBad = 0;
            for (const DrawRec &d : lis.draws)
                if (d.level != d.rule.level && !d.rule.nearEdge) {
                    ++drawBad;
                    if (drawBad <= 4)
                        std::printf("   %s: the view drew %s at %u, its rule %u\n", armName, probes[d.probe].name,
                                    d.level, d.rule.level);
                }
            CHECK_MSG(!lis.draws.empty() && drawBad == 0,
                      "%s: (c) every LOD-walking view pass drew its stock items at the view camera's rule level (%zu "
                      "readings, %u off)",
                      armName, lis.draws.size(), drawBad);

            if (split) {
                // (d) the view's walk skips the Atom queue; the stock control is put back
                Ogre::Item *atomItem = probes[3].item, *stockItem = probes[2].item;
                const Ogre::uint8 coarsest = Ogre::uint8(levels - 1u);
                atomItem->_setCurrentMeshLod(coarsest);
                stockItem->_setCurrentMeshLod(coarsest);
                for (int i = 0; i < 3; ++i) e->renderOneFrame();
                CHECK_MSG(atomItem->getCurrentMeshLod() == coarsest,
                          "(d) the Atom-routed item's level is no pass's to walk (still %u after 3 frames; the view's "
                          "rule would say %u)",
                          unsigned(atomItem->getCurrentMeshLod()), viewLevel[2]);
                CHECK_MSG(stockItem->getCurrentMeshLod() != coarsest,
                          "(d) control: the stock item beside it is put back by the view's walk (%u)",
                          unsigned(stockItem->getCurrentMeshLod()));
                atomItem->_setCurrentMeshLod(0);
            }
        }
        e->destroyView(view);
        e->destroyScene(s);
    }
    engine.reset();
    std::printf("%s (%d failure%s)\n", failures || rc ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
    return failures || rc ? 1 : 0;
}
