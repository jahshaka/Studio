// gi.opaque_bindings (PHOTON-7 stage 7, 7-1; SPECS/PHOTON_7_SPEC.md stage 7, L4) - THE OPAQUE PASSES READ ONLY
// SCREEN-SPACE GI.
//
// THE DOOR (jahshaka/engine/PhotonStatus.h photonPassBindings): per colour pass the chain names, every Photon texture
// the pass binds - the world caches by register family (`vctProbes x4` ..., `ifdColour`, `ifdDepth`) and the screen
// products by their one name.
// THE FIXTURE: a floor, two walls, a refractive pane (the refractives pass: its fragments have no screen product
// and keep the world caches, the door's positive control) and a BLENDED pane at alpha 1 (7-5: the blended queues' own
// pass on a gathering chain), a lamp and a sun, one offscreen view with the chain. Every
// tier (Low, Medium, High, Epic: the GiQuality + the World Mode's SSR row; every Auto toggle the tier's) on the RAY
// chain (the rays on: the view gathers, as every view of a ray scene does). (The NO-RAY chain's arm, the scene's rays
// Off, archived by NO-RAY-CUT-1, 2026-10-30: the minimum GPU traces. The LIVE offscreen chain's arm and (5), which held
// its blended pane to the ray chain's, deleted by ROUTE-ROWS-1 stage 0 with the Live contract: one producer, no second
// chain of a ray scene to compare.)
// THE LAWS, per arm:
//   (1) the door saw the opaque pass ("Jahshaka opaque") in the frames read (never vacuous);
//   (2) L4: every record of the opaque passes ("Jahshaka opaque", "Jahshaka atom decode", both hosts) lists ONLY the
//       six screen products (photonScreenProducts(): the diffuse, the rough specular, the cone's hit, the reflection,
//       the sun contact, the area visibility);
//   (3) THE CONTROL: the door can see a world cache - the refractives pass lists one (the voxel volumes or the field);
//   (4) THE BLENDED SURFACES DRAW IN THEIR OWN PASS ("Jahshaka blended"), which reads the world caches the refractives
//       pass reads (it lists one) - a blended fragment has no screen product (7-5 on a gathering chain; 7-6 on every
//       chain: a chain that does not gather has the screen integrate's products, so it is a screen-GI chain too).
// Every record of every arm is printed. Red on the base (the opaque passes bind the 40 voxel volumes and the field's
// two textures at Epic); the ray arms went green at 7-5.
// A fixture (the engine alone, one offscreen view). Counted in frames.
#include "jahshaka/engine/Engine.h"
#include "jahshaka/engine/PhotonStatus.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                                                              \
    do {                                                                                                              \
        if (cond) std::printf("  ok   %s\n", msg);                                                                    \
        else { std::printf("  FAIL %s\n", msg); ++failures; }                                                         \
    } while (0)

namespace {
NodeId box(Scene *s, const Vec3 &at, const Vec3 &size, const PbrParams &p) {
    const NodeId node = s->createNode();
    MeshData d = enginetest::unitCubeMesh();
    d.cards = enginetest::boxCards(0.5f);
    const MeshId mesh = s->createMesh(d);
    const MaterialId mat = s->createPbrMaterial(p);
    if (!node || !mesh || !mat || !s->attachMesh(node, mesh, mat)) return 0;
    enginetest::poseRegistry()[s][node] = enginetest::NodePose{};
    enginetest::setNodePosition(s, node, at);
    enginetest::setNodeScale(s, node, size);
    return node;
}
bool isOpaquePass(const std::string &pass) { return pass == "Jahshaka opaque" || pass == "Jahshaka atom decode"; }
bool isWorldCache(const std::string &name) { return name.rfind("vctProbe", 0) == 0 || name.rfind("ifd", 0) == 0; }
}   // namespace

int main()
{
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-gi-opaque-bindings-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    e->setRayTracing(true);
    int frames = 1;
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));
    const auto run = [&](int n) { enginetest::frames(e, n); frames += n; };
    View *view = e->createOffscreenView("opaque_bindings", 256, 256, Colour(0, 0, 0));
    Scene *scene = e->createScene("opaque_bindings");
    if (!view || !scene) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(scene);
    PbrParams matte;
    matte.albedo = Colour(0.6f, 0.6f, 0.6f);
    matte.roughness = 0.9f;
    box(scene, Vec3(0.0f, -0.05f, 0.0f), Vec3(8.0f, 0.1f, 8.0f), matte);    // the floor
    box(scene, Vec3(0.0f, 2.0f, -4.05f), Vec3(8.0f, 4.0f, 0.1f), matte);   // the back wall
    box(scene, Vec3(-4.05f, 2.0f, 0.0f), Vec3(0.1f, 4.0f, 8.0f), matte);   // the left wall
    PbrParams glass;
    glass.albedo = Colour(0.9f, 0.95f, 1.0f);
    glass.roughness = 0.05f;
    glass.alphaMode = PbrAlphaMode::Refractive;
    glass.alpha = 0.3f;
    box(scene, Vec3(0.8f, 1.0f, 0.5f), Vec3(1.2f, 1.6f, 0.05f), glass);    // the refractive pane
    PbrParams blend;
    blend.albedo = Colour(0.8f, 0.35f, 0.3f);
    blend.roughness = 0.6f;
    blend.alphaMode = PbrAlphaMode::Blend;
    blend.alpha = 1.0f;
    const Vec3 paneAt(-0.8f, 1.1f, -1.2f), paneSize(1.0f, 0.8f, 0.05f);
    box(scene, paneAt, paneSize, blend);                                     // the blended pane
    {
        const NodeId lamp = scene->createNode();
        LightDesc l;
        l.type = LightType::Point;
        l.colour = Colour(1.0f, 0.9f, 0.8f);
        l.intensity = 20.0f;
        l.range = 8.0f;
        scene->setNodeTransform(lamp, Vec3(-1.5f, 2.6f, -1.0f), Quat(), Vec3(1, 1, 1));
        scene->setLight(lamp, l);
    }
    enginetest::addDirectionalLight(scene, Vec3(-0.4f, -0.8f, -0.45f), 3.0f);
    enginetest::testCameraLookAt(view, Vec3(1.5f, 2.2f, 5.0f), Vec3(-0.5f, 1.0f, -1.5f));

    struct Tier { const char *name; GiQuality quality; int ssr; };
    const Tier tiers[] = { { "Low", GiQuality::Low, 0 }, { "Medium", GiQuality::Medium, 0 },
                           { "High", GiQuality::High, 1 }, { "Epic", GiQuality::Epic, 2 } };
    const std::vector<std::string> &allowed = photonScreenProducts();
    const char *const chain = "ray";   // the one chain of a ray scene (the arms' printed name)
    scene->setRayTracing(RayTracingMode::On);
    {
        for (const Tier &t : tiers) {
            PostFxDesc fx;
            fx.allowOffscreen = true;
            fx.ssr = t.ssr;
            fx.refractions = true;
            view->setPostFx(fx);
            GiParams gi;
            gi.mode = GiMode::Vct;
            gi.quality = t.quality;
            gi.ddgi = GiToggle::Auto;   // the tier's (a document's untouched row)
            gi.bounces = true;
            if (!scene->setGlobalIllumination(gi)) { std::printf("FAIL: the GI builds\n"); return 1; }
            e->waitForAsyncShaders();
            run(60);   // the chain rebuilt for the arm and the GI built
            resetPhotonPassBindings();
            run(2);
            const std::vector<PhotonPassBindingRecord> recs = photonPassBindings();
            std::printf("\n  [%s, %s chain] rays resolved %d, %zu pass records:\n", t.name, chain,
                        int(scene->rayTracingResolved()), recs.size());
            bool sawOpaque = false, opaqueScreenOnly = true, refractivesWorld = false, sawBlended = false,
                 blendedWorld = false;
            std::string offenders;
            for (const PhotonPassBindingRecord &r : recs) {
                std::string list;
                for (const std::string &b : r.bound) list += (list.empty() ? "" : ", ") + b;
                std::printf("    %-28s %-8s f%llu: %s\n", r.pass.c_str(), r.host.c_str(), r.frame,
                            list.empty() ? "(nothing)" : list.c_str());
                if (r.pass == "Jahshaka opaque") sawOpaque = true;
                if (isOpaquePass(r.pass))
                    for (const std::string &b : r.bound)
                        if (std::find(allowed.begin(), allowed.end(), b) == allowed.end()) {
                            opaqueScreenOnly = false;
                            if (offenders.find(b) == std::string::npos) offenders += (offenders.empty() ? "" : ", ") + b;
                        }
                if (r.pass == "Jahshaka refractives")
                    for (const std::string &b : r.bound) refractivesWorld = refractivesWorld || isWorldCache(b);
                if (r.pass == "Jahshaka blended") {
                    sawBlended = true;
                    for (const std::string &b : r.bound) blendedWorld = blendedWorld || isWorldCache(b);
                }
            }
            char msg[512];
            std::snprintf(msg, sizeof msg, "[%s, %s] (1) the door saw the opaque pass", t.name, chain);
            CHECK(sawOpaque, msg);
            std::snprintf(msg, sizeof msg, "[%s, %s] (2) L4: the opaque passes bind only screen products (offenders: %s)",
                          t.name, chain, offenders.empty() ? "none" : offenders.c_str());
            CHECK(opaqueScreenOnly, msg);
            std::snprintf(msg, sizeof msg, "[%s, %s] (3) the control: the refractives pass lists a world cache",
                          t.name, chain);
            CHECK(refractivesWorld, msg);
            std::snprintf(msg, sizeof msg,
                          "[%s, %s] (4) the blended surfaces draw in their own pass, which lists a world cache", t.name,
                          chain);
            CHECK(sawBlended && blendedWorld, msg);
        }
    }

    std::printf("\ngi.opaque_bindings: %d frames, %d failure(s)\n", frames, failures);
    return failures ? 1 : 0;
}
