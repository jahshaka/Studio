// THE PROBE CACHE'S INPUTS — ENGINE_CACHE_POLICY_SPEC §2 P1 + P7, §3 (the
// invalidation table).
//
// Reflection probes are a CACHE since P1: captured once, reused every frame,
// and re-captured only when something they can see changes. Before P1 an
// endless sweep re-captured one probe every frame of a still scene, for ever —
// 19 ms a frame on the Showroom — and that sweep was also, silently, the ONLY
// thing that ever brought a reflection round after a material, sky or
// light-colour edit: nothing staled a probe on any of them. Stopping the sweep
// without adding the missing invalidations would have frozen every such edit
// out of the reflections permanently. This suite pins the invalidations.
//
// For each input it asserts, in counters (never milliseconds):
//   * the edit STALES the grid on the frame it lands, with the right reason
//     (giStatus lastStaleReason / staleSerial);
//   * no frame of the catch-up captures more than the budget (P6);
//   * the grid catches up (staleProbes 0), and then the scene IDLES again —
//     zero captures for 20 frames. A cache that never settles is a sweep.
// And, through the PICTURE, the property the endless sweep was masking: a
// chrome cube's mirror pixel FOLLOWS an albedo edit on the wall it reflects.
//
// A still room, never the Showroom (not open-to-open deterministic): a sealed
// box, a red wall behind the camera, a mirror cube in the middle whose centre
// pixel is a direct read of what the probes hold (gi.budget's room).
//
// Mirror-linked like gi.budget: the light-parameter half of P7 is mirror-side
// (the GI signature now hashes a light's colour/intensity/range, not only its
// transform) and the probe half is engine-side; the contract is the pair.
#include <QGuiApplication>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>

#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/shadowmap.h"
#include "irisgl/irisglfwd.h"
#include "irisgl/mirror/scenemirror.h"
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)

static const char *reasonName(GiStaleReason r)
{
    switch (r) {
    case GiStaleReason::None:     return "none";
    case GiStaleReason::Rebuild:  return "rebuild";
    case GiStaleReason::Refresh:  return "refresh";
    case GiStaleReason::Moved:    return "moved";
    case GiStaleReason::Light:    return "light";
    case GiStaleReason::Material: return "material";
    case GiStaleReason::Sky:      return "sky";
    case GiStaleReason::Ambient:  return "ambient";
    case GiStaleReason::Fog:      return "fog";
    case GiStaleReason::Animated: return "animated";
    }
    return "?";
}

static iris::MeshNodePtr slab(const iris::ScenePtr &doc, const char *name,
                              const iris::PbrMaterialPtr &m,
                              const iris::Vec3 &pos, const iris::Vec3 &scale)
{
    auto n = iris::MeshNode::create();
    n->setName(QString::fromLatin1(name));
    n->setMesh(QStringLiteral(JAHSHAKA_SOURCE_DIR "/app/content/primitives/cube.obj"));
    n->setLocalPos(pos);
    n->setLocalScale(scale);
    n->setMaterial(m);
    doc->getRootNode()->addChild(n);
    return n;
}

static iris::PbrMaterialPtr pbr(const QColor &c, float rough = 0.9f, float metal = 0.0f)
{
    auto m = iris::PbrMaterial::create();
    m->setBaseColor(c);
    m->setRoughnessFactor(rough);
    m->setMetallicFactor(metal);
    return m;
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-probe-inputs-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }

    View *view = engine->createOffscreenView("probe_inputs", 128, 128, Colour(0, 0, 0));
    Scene *escene = engine->createScene("probe_inputs");
    view->setScene(escene);

    auto doc = iris::Scene::create();
    doc->giMode = iris::GiMode::VCT_PCC_HYBRID;
    doc->giQuality = iris::GiQuality::MEDIUM;
    doc->giNumBounces = 2;
    doc->giPccGrid = iris::Vec3(2, 1, 2);          // 4 probes
    doc->giUpdateBudget = 1;
    doc->giDynamicProbes = 0;
    doc->ambientColor = QColor(0, 0, 0);
    doc->ambientFromSky = false;
    doc->skyType = iris::SkyType::SINGLE_COLOR;
    doc->skyColor = QColor(0, 0, 0);
    doc->giBoundsMin = iris::Vec3(-4.6f, -0.6f, -4.6f);
    doc->giBoundsMax = iris::Vec3(4.6f, 5.6f, 4.6f);
    const int kProbes = 4, kAllowed = 1;           // budget 1 + dynamic 0

    const QColor white(217, 217, 217);
    slab(doc, "floor",   pbr(white), iris::Vec3(0.0f, -0.2f, 0.0f), iris::Vec3(8.8f, 0.4f, 8.8f));
    slab(doc, "ceiling", pbr(white), iris::Vec3(0.0f, 5.2f, 0.0f),  iris::Vec3(8.8f, 0.4f, 8.8f));
    slab(doc, "-Z wall", pbr(white), iris::Vec3(0.0f, 2.5f, -4.2f), iris::Vec3(8.8f, 5.0f, 0.4f));
    slab(doc, "-X wall", pbr(white), iris::Vec3(-4.2f, 2.5f, 0.0f), iris::Vec3(0.4f, 5.0f, 8.8f));
    slab(doc, "+X wall", pbr(white), iris::Vec3(4.2f, 2.5f, 0.0f),  iris::Vec3(0.4f, 5.0f, 8.8f));
    auto wallMat = pbr(QColor(255, 5, 5));
    slab(doc, "red wall", wallMat, iris::Vec3(0.0f, 2.5f, 4.2f), iris::Vec3(8.8f, 5.0f, 0.4f));
    slab(doc, "mirror", pbr(QColor(255, 255, 255), 0.0f, 1.0f), iris::Vec3(0.0f, 2.0f, 0.0f),
         iris::Vec3(1.6f, 1.6f, 1.6f));

    auto sun = iris::LightNode::create();
    sun->setName("sun");
    sun->lightType = iris::LightType::Directional;
    sun->intensity = 6.0f;
    sun->shadowMap->shadowType = iris::ShadowMapType::None;
    sun->setLocalPos(iris::Vec3(0.0f, 4.0f, -4.0f));
    sun->setLocalRot(iris::Quat::fromEulerAngles(83.0f, 180.0f, 0.0f));
    doc->getRootNode()->addChild(sun);

    SceneMirror mirror(escene);
    mirror.setSource(doc);
    auto cam = iris::CameraNode::create();
    cam->setLocalPos(iris::Vec3(0.0f, 2.0f, 2.4f));
    cam->lookAt(iris::Vec3(0.0f, 2.0f, 0.0f));
    cam->update(0.0f);
    mirror.applyCamera(cam, view);

    // The viewport's per-frame order (EngineSceneViewport): sync, sky, then the
    // world settings. applySky is its own host call, not part of
    // applyEnvironment — without it no sky change would ever reach the engine.
    const auto frame = [&]() {
        doc->refresh();
        mirror.sync();
        mirror.applySky(view);
        mirror.applyEnvironment(view, engine.get());
        engine->renderOneFrame();
    };
    const auto frames = [&](int n) { for (int i = 0; i < n; ++i) frame(); };
    const auto mirrorPixel = [&]() {
        Image img;
        view->readPixels(img);
        return img.at(64, 64);
    };

    // Runs `n` frames; the worst single-frame capture count among them.
    const auto worstOver = [&](int n) {
        int worst = 0;
        for (int f = 0; f < n; ++f) {
            frame();
            worst = std::max(worst, escene->giStatus().probeCapturesLastFrame);
        }
        return worst;
    };

    // One input, the whole contract: stale with `why` on its own frame, spread
    // (never more than the budget per frame), caught up, then idle.
    const auto input = [&](const char *what, GiStaleReason why, const std::function<void()> &edit,
                           int settleFrames) {
        const unsigned long long serial = escene->giStatus().staleSerial;
        const unsigned long long rebuilds = escene->giStatus().rebuilds;
        edit();
        frame();
        const GiStatus st = escene->giStatus();
        std::printf("-- %s: reason=%s serial %llu -> %llu, stale=%d, captured=%d\n", what,
                    reasonName(st.lastStaleReason), serial, st.staleSerial, st.staleProbes,
                    st.probeCapturesLastFrame);
        char msg[256];
        std::snprintf(msg, sizeof msg, "%s: the edit STALES the grid on its own frame (reason %s)",
                      what, reasonName(why));
        CHECK(st.staleSerial > serial && st.lastStaleReason == why, msg);
        const int worst = std::max(st.probeCapturesLastFrame, worstOver(settleFrames));
        std::snprintf(msg, sizeof msg, "%s: no frame of the catch-up captures more than the budget "
                      "(worst %d)", what, worst);
        CHECK(worst <= kAllowed, msg);
        std::snprintf(msg, sizeof msg, "%s: the grid has caught up (no probe stale)", what);
        CHECK(escene->giStatus().staleProbes == 0, msg);
        std::snprintf(msg, sizeof msg, "%s: ...and then the scene IDLES (20 frames, zero captures)", what);
        CHECK(worstOver(20) == 0, msg);
        std::snprintf(msg, sizeof msg, "%s: no from-scratch GI rebuild (the probes were re-captured, "
                      "never re-placed)", what);
        CHECK(escene->giStatus().rebuilds == rebuilds, msg);
    };

    // ---- settle ------------------------------------------------------------
    frames(40);
    {
        const GiStatus st = escene->giStatus();
        CHECK(st.probeCount == kProbes && st.pccBound, "the 2x1x2 probe grid built and bound");
        CHECK(st.staleProbes == 0, "settled: the post-build catch-up is done");
        CHECK(worstOver(20) == 0, "a still room re-captures nothing (the old sweep never idled)");
    }
    const Colour redMirror = mirrorPixel();
    std::printf("   mirror, red wall   r=%.3f g=%.3f b=%.3f\n", redMirror.r, redMirror.g, redMirror.b);
    CHECK(redMirror.r > redMirror.b + 0.3f, "the chrome cube reflects the RED wall (not vacuous)");

    // ---- a material ALBEDO edit --------------------------------------------
    // The wall the mirror reflects turns blue. Before P7 nothing staled a probe
    // on a material edit; the endless sweep was the only thing that ever showed
    // it. It also re-voxelizes ONCE on settle (the wall's bounce changes colour
    // too): VctMaterial caches each datablock's conversion by pointer, so the
    // voxel arm is rebuilt fresh under the probes it keeps.
    const quint64 solvesBeforeAlbedo = mirror.giRefreshCount();
    input("albedo", GiStaleReason::Material,
          [&]() { wallMat->setBaseColor(QColor(5, 5, 255)); }, 40);
    const Colour blueMirror = mirrorPixel();
    std::printf("   mirror, after the albedo edit   r=%.3f g=%.3f b=%.3f\n",
                blueMirror.r, blueMirror.g, blueMirror.b);
    CHECK(blueMirror.b > blueMirror.r + 0.3f,
          "albedo: the mirror pixel FOLLOWS the edit — it now shows the BLUE wall");
    CHECK(mirror.giRefreshCount() - solvesBeforeAlbedo == 1,
          "albedo: the voxel half re-solves exactly once (coalesced, like a moved object)");

    // A ROUGHNESS edit is seen by the probes but not by the voxelizer (which
    // reads albedo/emissive/alpha only): it re-captures and costs no re-solve.
    const quint64 solvesBeforeRough = mirror.giRefreshCount();
    input("roughness", GiStaleReason::Material,
          [&]() { wallMat->setRoughnessFactor(0.5f); }, 12);
    CHECK(mirror.giRefreshCount() == solvesBeforeRough,
          "roughness: probes re-capture, the voxels do not re-solve");

    // An edit that changes NOTHING stales nothing (the mirror pushes materials
    // on change, and the engine compares the parameters itself as well).
    {
        const unsigned long long serial = escene->giStatus().staleSerial;
        wallMat->setRoughnessFactor(0.5f);
        frames(3);
        CHECK(escene->giStatus().staleSerial == serial, "a no-op material write stales nothing");
    }

    // ---- a SKY change --------------------------------------------------------
    input("sky", GiStaleReason::Sky, [&]() {
        doc->skyType = iris::SkyType::GRADIENT;
        doc->gradientTop = QColor(40, 60, 200);
        doc->gradientMid = QColor(120, 120, 160);
        doc->gradientBot = QColor(30, 30, 30);
        doc->gradientOffset = 0.5f;
    }, 12);

    // ---- a LIGHT COLOUR change -----------------------------------------------
    // Engine half: setLight stales the probes. Mirror half: the GI signature
    // now hashes the light's parameters, so the voxels re-inject on the drag
    // cadence and re-solve ONCE on settle — before, a colour edit never reached
    // the voxel bounce at all.
    const quint64 solvesBeforeLight = mirror.giRefreshCount();
    input("light colour", GiStaleReason::Light,
          [&]() { sun->color = QColor(255, 180, 120); }, 40);
    CHECK(mirror.giRefreshCount() - solvesBeforeLight == 1,
          "light colour: exactly ONE settle re-solve (it used to be none)");

    // ---- an AMBIENT change ----------------------------------------------------
    input("ambient", GiStaleReason::Ambient,
          [&]() { doc->ambientColor = QColor(25, 25, 25); }, 12);

    // =======================================================================
    // P10 — THE BINDING RE-ASSERT (Scene::reassertGiBinding)
    // =======================================================================
    // HlmsPbs' GI binding is process-wide: the last scene to BUILD owns it. A
    // second scene building its own hybrid takes it from this one; the host
    // used to take it back by re-pushing GI, i.e. a from-scratch rebuild. The
    // verb re-points the binding and rebuilds nothing.
    std::printf("-- P10: the binding re-assert\n");
    {
        const Colour ref = mirrorPixel();
        Scene *other = engine->createScene("probe_inputs_other");
        const NodeId n = other->createNode();
        const MeshId mesh = other->createMesh(enginetest::unitCubeMesh());
        const MaterialId mat = other->createPbrMaterial(PbrParams());
        CHECK(n && mesh && mat && other->attachMesh(n, mesh, mat), "P10: a second scene with geometry");
        other->setNodeTransform(n, Vec3(0.0f, 1.0f, 0.0f), Quat(), Vec3(2.0f, 2.0f, 2.0f));
        GiParams gi;
        gi.mode = GiMode::VctPccHybrid;
        gi.quality = GiQuality::Low;
        gi.pccProbesX = gi.pccProbesY = gi.pccProbesZ = 1;
        gi.boundsMin = Vec3(-3.0f, -1.0f, -3.0f);
        gi.boundsMax = Vec3(3.0f, 4.0f, 3.0f);
        CHECK(other->setGlobalIllumination(gi), "P10: the second scene builds its own hybrid");
        const GiStatus stolen = escene->giStatus();
        CHECK(!stolen.pccBound && !stolen.vctBound && other->giStatus().pccBound,
              "P10: ...and takes the process-wide binding (last builder wins)");
        const unsigned long long rebuilds = stolen.rebuilds;
        const unsigned long long otherRebuilds = other->giStatus().rebuilds;
        CHECK(escene->reassertGiBinding(), "P10: re-assert re-points the binding");
        const GiStatus back = escene->giStatus();
        CHECK(back.pccBound && back.vctBound, "P10: this scene's probes and voxels are bound again");
        CHECK(!other->giStatus().pccBound && !other->giStatus().vctBound,
              "P10: ...and the other scene's are not");
        CHECK(back.rebuilds == rebuilds && other->giStatus().rebuilds == otherRebuilds,
              "P10: nothing was rebuilt on either side");
        CHECK(!escene->reassertGiBinding(), "P10: a second re-assert is a no-op (already the owner)");
        frames(4);
        const Colour after = mirrorPixel();
        std::printf("   mirror before the takeover r=%.3f g=%.3f b=%.3f, after the re-assert "
                    "r=%.3f g=%.3f b=%.3f\n", ref.r, ref.g, ref.b, after.r, after.g, after.b);
        CHECK(std::fabs(after.r - ref.r) < 0.05f && std::fabs(after.g - ref.g) < 0.05f &&
              std::fabs(after.b - ref.b) < 0.05f,
              "P10: the reflection is this scene's own again, with no rebuild");
        engine->destroyScene(other);
        frames(2);
        CHECK(escene->giStatus().pccBound && escene->giStatus().vctBound,
              "P10: destroying the non-owner leaves this scene's binding alone");
    }

    doc->giMode = iris::GiMode::OFF;
    frame();
    mirror.setSource(iris::ScenePtr());
    engine.reset();
    std::printf("\n%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
