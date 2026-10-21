// atom.instruments_honest — THE ATOM INSTRUMENTS TELL THE TRUTH (ATOM-8 stage 0; SPECS/ATOM_8_SPEC.md).
//
// Two Atom objects of ONE baked mesh (the 20,000-triangle UV sphere, its cluster DAG through the product's bake), the
// FRONT one large and near, the BACK one small and wholly behind it on the view axis; the occlusion door SHUT
// (Scene::setAtomOcclusionEnabled(false): every object in the frustum is drawn, so the back one is drawn and hidden —
// the hidden share the instruments must see), the camera still, the Atom view on.
//   (a) THE DENSITY VIEW is the cut's DRAWN triangles over the visible pixels: the view's density table at the front
//       object's slot equals the probe's drawnTriangles / pixels for it, and the fixture's premise holds — the cut
//       drew fewer than the base mesh's level 0 (the figure the table held before, which grew as d^2 whatever the
//       cut drew); the back object's entry is 0 (no pixel);
//   (b) THE HIDDEN SHARE: world.atomStatus's drawnHiddenTriangles (AtomDrawStatus) = the back object's drawn
//       triangles, its drawnHiddenOf = both objects' drawn triangles — in the Density mode and in the Triangles
//       mode, and no figure with the view Off;
//   (c) THE VISIBLE CLUSTERS: the probe's distinctClustersVisible is 0 for the back object (whose clusters share
//       their mesh-wide ids with the front one's visible clusters: the bitmap is per INSTANCE) and > 0 for the
//       front one, never more than it drew.
// Frames, never time: the engine's fixed clock.
#include "jahshaka/engine/Engine.h"
#include "jahshaka/engine/EngineInspect.h"
#include "../support/enginetesthelpers.h"
#include "cluster_fixtures.h"

#include "EnginePrivate.h"

#include <OgreImage2.h>
#include <OgreTextureBox.h>
#include <OgreTextureGpu.h>

#include <QCoreApplication>

#include <cstdio>
#include <cstring>
#include <string>

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

/// The density table's float at `slot` (1024 slots a row, float bits in R32_UINT).
static bool tableAt(OgreView *v, unsigned slot, float &out)
{
    Ogre::TextureGpu *tex = detail::atomDensityTable(v);
    if (!tex) return false;
    Ogre::Image2 img;
    img.convertFromTexture(tex, 0u, 0u);
    const Ogre::TextureBox box = img.getData(0u);
    const unsigned x = slot & 1023u, y = slot >> 10u;
    if (x >= tex->getWidth() || y >= tex->getHeight()) return false;
    std::memcpy(&out, static_cast<const uint8_t *>(box.at(0, y, 0)) + x * sizeof(uint32_t), sizeof(float));
    return true;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    std::printf("== atom.instruments_honest: a front sphere hiding a back one, the occlusion shut\n");
    clusterfix::Fixture fx;
    if (!clusterfix::bake(fx, clusterfix::uvSphere(), "uv-sphere-20k") || fx.data.clusters.empty()) {
        std::printf("FAIL: the fixture did not bake\n");
        return 1;
    }
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-atom-instruments-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->submitFrame(FrameInput::delta(1.0f / 60.0f));
    Engine *e = engine.get();
    View *view = e->createOffscreenView("instruments", kW, kH, Colour(0.2f, 0.3f, 0.5f));
    Scene *s = e->createScene("instruments");
    s->setLodBias(1.0f);
    s->setAmbient(Colour(0.10f, 0.11f, 0.13f), Colour(0.05f, 0.05f, 0.05f));
    PbrParams pp;
    pp.albedo = Colour(0.7f, 0.3f, 0.2f);
    pp.roughness = 0.4f;
    const MaterialId mat = s->createPbrMaterial(pp);
    const MeshId mesh = s->createMesh(fx.data);
    const NodeId front = s->createNode(), back = s->createNode();
    s->setNodeTransform(front, Vec3(0.0f, 0.0f, -40.0f), Quat(), Vec3(8.0f, 8.0f, 8.0f));   // r 4 m at 40 m
    s->attachMesh(front, mesh, mat);
    s->setNodeTransform(back, Vec3(0.0f, 0.0f, -60.0f), Quat(), Vec3(3.0f, 3.0f, 3.0f));    // r 1.5 m at 60 m
    s->attachMesh(back, mesh, mat);
    enginetest::addDirectionalLight(s, Vec3(-0.45f, -0.8f, -0.4f), 2.2f);
    s->setAtomOcclusionEnabled(false);
    view->setOffscreenContract(OffscreenContract::StillPicture);
    view->setScene(s);
    view->setCamera(enginetest::testCameraDescLookAt(Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, -40.0f)));
    OgreView *ov = static_cast<OgreView *>(view);
    s->setAtomView(AtomView::Density);
    enginetest::frames(e, 12);   // the chain, the cut, the counts, the hidden read (a few frames late)

    // ---- THE PROBE: one frame's drawn and visible accounts, per object ----
    AtomCutProbe p;
    bool probed = false;
    if (s->armAtomCutProbe()) {
        for (int i = 0; i < 6 && !probed; ++i) {
            enginetest::frames(e, 1);
            probed = s->atomCutProbe(p) && p.valid;
        }
    }
    CHECK_MSG(probed, "the cut probe read one frame back (%s)", p.error.c_str());
    const AtomCutProbeObject *fo = nullptr, *bo = nullptr;
    for (const AtomCutProbeObject &o : p.objects) {
        if (o.node == front) fo = &o;
        if (o.node == back) bo = &o;
    }
    CHECK(fo && bo, "both objects are in the probe (the shut door drew both)");
    if (!fo || !bo) { std::printf("FAILED\n"); return 1; }
    std::printf("   front: slot %u, level 0 %llu, drawn %llu tris / %u clusters, pixels %u, visible clusters %u\n",
                fo->slot, fo->level0Triangles, fo->drawnTriangles, fo->drawnClusters, fo->pixels, fo->visibleClusters);
    std::printf("   back:  slot %u, level 0 %llu, drawn %llu tris / %u clusters, pixels %u, visible clusters %u\n",
                bo->slot, bo->level0Triangles, bo->drawnTriangles, bo->drawnClusters, bo->pixels, bo->visibleClusters);
    CHECK_MSG(fo->pixels > 0u && bo->pixels == 0u && bo->drawnTriangles > 0u,
              "the fixture: the front object shows (%u px), the back one is drawn (%llu tris) and shows nothing (%u px)",
              fo->pixels, bo->drawnTriangles, bo->pixels);
    CHECK_MSG(fo->drawnTriangles > 0u && fo->drawnTriangles < fo->level0Triangles,
              "the premise: the cut drew the front object coarser than level 0 (%llu of %llu)", fo->drawnTriangles,
              fo->level0Triangles);

    // ---- (a) THE DENSITY VIEW: drawn / visible ----
    float tf = -1.0f, tb = -1.0f;
    const bool read = tableAt(ov, fo->slot, tf) && tableAt(ov, bo->slot, tb);
    const float want = fo->pixels ? float(fo->drawnTriangles) / float(fo->pixels) : 0.0f;
    const float level0 = fo->pixels ? float(fo->level0Triangles) / float(fo->pixels) : 0.0f;
    std::printf("   the density table: front %.6f (drawn/visible %.6f, level-0/visible %.6f), back %.6f\n", double(tf),
                double(want), double(level0), double(tb));
    CHECK_MSG(read && tf == want, "(a) the density view at the front object = drawn / visible (%.6f, want %.6f)",
              double(tf), double(want));
    CHECK_MSG(read && tb == 0.0f, "(a) the back object's density entry is 0 (no pixel; %.6f)", double(tb));

    // ---- (b) THE HIDDEN SHARE, in Density then Triangles, none with the view Off ----
    const unsigned long long all = fo->drawnTriangles + bo->drawnTriangles;
    {
        const AtomDrawStatus st = s->atomDrawStatus();
        std::printf("   density mode: hidden %llu of %llu (valid %d)\n", st.drawnHiddenTriangles,
                    st.hiddenDrawnTriangles, int(st.hiddenValid));
        CHECK_MSG(st.hiddenValid && st.drawnHiddenTriangles == bo->drawnTriangles && st.hiddenDrawnTriangles == all,
                  "(b) Density: drawnHiddenTriangles = the back object's drawn %llu (%llu), of every drawn %llu (%llu)",
                  bo->drawnTriangles, st.drawnHiddenTriangles, all, st.hiddenDrawnTriangles);
    }
    s->setAtomView(AtomView::Triangles);
    enginetest::frames(e, 8);
    {
        const AtomDrawStatus st = s->atomDrawStatus();
        CHECK_MSG(st.hiddenValid && st.drawnHiddenTriangles == bo->drawnTriangles && st.hiddenDrawnTriangles == all,
                  "(b) Triangles: drawnHiddenTriangles = the back object's drawn %llu (%llu), of %llu (%llu)",
                  bo->drawnTriangles, st.drawnHiddenTriangles, all, st.hiddenDrawnTriangles);
        const PublishedFrame::AtomHidden ah = s->published().atomHidden;
        CHECK_MSG(ah.valid && ah.hidden == st.drawnHiddenTriangles && ah.drawn == st.hiddenDrawnTriangles,
                  "(b) the published frame carries the same figure for the overlay (%llu of %llu)", ah.hidden, ah.drawn);
    }
    s->setAtomView(AtomView::Off);
    enginetest::frames(e, 2);
    CHECK(!s->atomDrawStatus().hiddenValid, "(b) the view Off: no hidden figure (the count does not run)");

    // ---- (c) THE VISIBLE CLUSTERS, per instance ----
    CHECK_MSG(bo->visibleClusters == 0u, "(c) the back object's distinctClustersVisible is 0 (%u)", bo->visibleClusters);
    CHECK_MSG(fo->visibleClusters > 0u && fo->visibleClusters <= fo->drawnClusters,
              "(c) the front object's distinctClustersVisible is in (0, drawn] (%u of %u)", fo->visibleClusters,
              fo->drawnClusters);

    view->setScene(nullptr);
    e->destroyView(view);
    e->destroyScene(s);
    engine.reset();
    std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
