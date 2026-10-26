// gi.opaque_bindings (PHOTON-7 stage 7, 7-1; SPECS/PHOTON_7_SPEC.md stage 7, L4) - THE OPAQUE PASSES READ ONLY
// SCREEN-SPACE GI.
//
// THE DOOR (jahshaka/engine/PhotonStatus.h photonPassBindings): per colour pass the chain names, every Photon texture
// the pass binds - the world caches by register family (`vctProbes x4` ..., `ifdColour`, `ifdDepth`) and the screen
// products by their one name.
// THE FIXTURE: a floor, two walls, a refractive pane (the refractives pass: its fragments have no screen product
// and keep the world caches, the door's positive control) and a BLENDED pane at alpha 1 (7-5: the blended queues' own
// pass on a gathering chain), a lamp and a sun, one offscreen view with the chain. Every
// tier (Low, Medium, High, Epic: the GiQuality + the World Mode's SSR row; every Auto toggle the tier's) on THREE
// chains: a RAY chain (the rays on, a still picture: the view gathers like a window), a NO-RAY chain (the scene's rays
// Off) and a LIVE offscreen chain (the rays on, the Live contract: a live preview takes no gather).
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
// two textures at Epic); the ray arms went green at 7-5, the no-ray and live arms at 7-6.
// AND ONCE, AT HIGH (7-5):
//   (5) THE TRANSLUCENT ROUTE IS ONE ON EVERY CHAIN: the blended pane's pixels (alpha 1: its own shading alone, the
//       picture behind it weighed by zero) on the ray chain against the no-ray chain and the live ray chain - on every
//       one its own pass after the opaque one (7-6; before it the two drew it inside the opaque pass with the
//       prepass's properties withdrawn) - each at the GI's rest: the same function of the same world caches, lights and shadow maps, stored once into the
//       RGBA16F target, so within ONE fp16 ulp of the larger value per channel (a gathering chain used to light it with
//       the environment alone: PHOTON-GATHER-1d's withdrawal). The live arm read +29 % until the gather was recorded
//       only for a view whose chain gathers (7-5: a live view's opaque pass bound the gather beside the world caches).
// A fixture (the engine alone, one offscreen view). Counted in frames.
#include "jahshaka/engine/Engine.h"
#include "jahshaka/engine/PhotonStatus.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
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
    View *view = e->createOffscreenView("opaque_bindings", 256, 256, Colour(0, 0, 0),
                                        OffscreenContract::StillPicture);   // (each chain below sets its own)
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
    struct Chain { const char *name; bool rays; OffscreenContract contract; };
    const Chain chains[] = { { "ray", true, OffscreenContract::StillPicture },
                             { "no-ray", false, OffscreenContract::StillPicture },
                             { "live", true, OffscreenContract::Live } };
    for (const Chain &ch : chains) {
        scene->setRayTracing(ch.rays ? RayTracingMode::On : RayTracingMode::Off);
        view->setOffscreenContract(ch.contract);   // (the setter's own use: one view switching purpose)
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
            std::printf("\n  [%s, %s chain] rays resolved %d, %zu pass records:\n", t.name, ch.name,
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
            std::snprintf(msg, sizeof msg, "[%s, %s] (1) the door saw the opaque pass", t.name, ch.name);
            CHECK(sawOpaque, msg);
            std::snprintf(msg, sizeof msg, "[%s, %s] (2) L4: the opaque passes bind only screen products (offenders: %s)",
                          t.name, ch.name, offenders.empty() ? "none" : offenders.c_str());
            CHECK(opaqueScreenOnly, msg);
            std::snprintf(msg, sizeof msg, "[%s, %s] (3) the control: the refractives pass lists a world cache",
                          t.name, ch.name);
            CHECK(refractivesWorld, msg);
            std::snprintf(msg, sizeof msg,
                          "[%s, %s] (4) the blended surfaces draw in their own pass, which lists a world cache", t.name,
                          ch.name);
            CHECK(sawBlended && blendedWorld, msg);
        }
    }

    // ---- (5) THE TRANSLUCENT ROUTE IS ONE ON EVERY CHAIN (7-5), at High ----
    {
        PostFxDesc fx;
        fx.allowOffscreen = true;
        fx.ssr = 1;
        fx.refractions = true;
        fx.hdrReadback = true;
        view->setPostFx(fx);
        scene->setRayTracing(RayTracingMode::On);
        GiParams gi;
        gi.mode = GiMode::Vct;
        gi.quality = GiQuality::High;
        gi.ddgi = GiToggle::Auto;
        gi.bounces = true;
        if (!scene->setGlobalIllumination(gi)) { std::printf("FAIL: the GI builds\n"); return 1; }
        const Vec3 eye(1.5f, 2.2f, 5.0f), at(-0.5f, 1.0f, -1.5f);
        // the pane's front face, its left 70 % x its middle 70 % (the refractive pane covers the rest from this eye)
        const auto paneFace = [&](int i, int k) {
            return Vec3(paneAt.x - 0.35f * paneSize.x + 0.35f * paneSize.x * (float(i) / 6.0f),
                        paneAt.y - 0.35f * paneSize.y + 0.70f * paneSize.y * (float(k) / 6.0f),
                        paneAt.z + 0.5f * paneSize.z + 1e-3f);
        };
        const auto projectTo = [&](const Vec3 &pt, unsigned w, unsigned h, int &ix, int &iy) {
            double f[3] = { at.x - eye.x, at.y - eye.y, at.z - eye.z };
            const double fl = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
            for (double &c : f) c /= fl;
            double r[3] = { -f[2], 0.0, f[0] };   // f x (0, 1, 0)
            const double rl = std::sqrt(r[0] * r[0] + r[2] * r[2]);
            r[0] /= rl; r[2] /= rl;
            const double u[3] = { r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0] };
            const double d[3] = { pt.x - eye.x, pt.y - eye.y, pt.z - eye.z };
            const double zf = d[0] * f[0] + d[1] * f[1] + d[2] * f[2];
            const double tanY = std::tan(22.5 * 3.14159265358979 / 180.0), tanX = tanY * double(w) / double(h);
            const double xn = (d[0] * r[0] + d[1] * r[1] + d[2] * r[2]) / (zf * tanX);
            const double yn = (d[0] * u[0] + d[1] * u[1] + d[2] * u[2]) / (zf * tanY);
            ix = int(std::floor((xn + 1.0) * 0.5 * double(w)));
            iy = int(std::floor((1.0 - yn) * 0.5 * double(h)));
            return zf > 0.0 && ix >= 0 && iy >= 0 && ix < int(w) && iy < int(h);
        };
        const auto settledPane = [&](OffscreenContract contract, std::vector<float> &out, int &restAt) {
            view->setOffscreenContract(contract);
            e->waitForAsyncShaders();
            int f = 0, quiet = 0;
            for (; f < 900 && quiet < 30; ++f) {
                run(1);
                quiet = scene->published().gi.giAtRest ? quiet + 1 : 0;
            }
            restAt = quiet >= 30 ? f - quiet : -1;
            ImageF pic;
            out.clear();
            if (!view->readPixelsHdr(pic)) return false;
            for (int i = 0; i <= 6; ++i)
                for (int k = 0; k <= 6; ++k) {
                    int ix, iy;
                    if (!projectTo(paneFace(i, k), pic.width, pic.height, ix, iy)) return false;
                    const auto px = pic.at(unsigned(ix), unsigned(iy));
                    out.push_back(px.r);
                    out.push_back(px.g);
                    out.push_back(px.b);
                }
            return true;
        };
        std::vector<float> rayPane, livePane, liveRayPane;
        int restRay = -1, restLive = -1, restLiveRay = -1;
        const bool okRay = settledPane(OffscreenContract::StillPicture, rayPane, restRay);
        const GatherStatus gRay = scene->published().gi.gather;
        const bool okLiveRay = settledPane(OffscreenContract::Live, liveRayPane, restLiveRay);   // the live ray chain
        scene->setRayTracing(RayTracingMode::Off);   // the no-ray chain: the same still picture, no rays
        const bool okLive = settledPane(OffscreenContract::StillPicture, livePane, restLive);
        const GatherStatus gLive = scene->published().gi.gather;
        // ONE fp16 ulp of the larger value: 2^(floor(log2 v) - 10) for a normal half
        const auto ulpOf = [](double v) {
            v = std::max(std::fabs(v), 6.103515625e-05);
            return std::ldexp(1.0, int(std::floor(std::log2(v))) - 10);
        };
        double worstUlps = 0.0, meanRay = 0.0, meanLive = 0.0, meanLiveRay = 0.0;
        size_t past = 0;
        const size_t n = std::min(std::min(rayPane.size(), livePane.size()), liveRayPane.size());
        for (size_t i = 0; i < n; ++i) {
            const double a = rayPane[i];
            for (const double b : { double(livePane[i]), double(liveRayPane[i]) }) {
                const double ulps = std::fabs(a - b) / ulpOf(std::max(a, b));
                worstUlps = std::max(worstUlps, ulps);
                if (ulps > 1.0) ++past;
            }
            meanRay += a;
            meanLive += livePane[i];
            meanLiveRay += liveRayPane[i];
        }
        if (n) { meanRay /= double(n); meanLive /= double(n); meanLiveRay /= double(n); }
        std::printf("\n   (5) the blended pane (alpha 1), %zu channel samples: the ray chain (gather %s, at rest after %d "
                    "frames) mean %.5f, the live ray chain (at rest after %d) mean %.5f, the no-ray chain (gather %s, at "
                    "rest after %d) mean %.5f; worst %.2f fp16 ulps, %zu past one\n",
                    n, gRay.on ? "on" : "OFF", restRay, meanRay, restLiveRay, meanLiveRay, gLive.on ? "on" : "off",
                    restLive, meanLive, worstUlps, past);
        CHECK(okRay && okLive && okLiveRay && gRay.on && n >= 3u * 40u && meanRay > 0.0 && past == 0u,
              "(5) THE TRANSLUCENT ROUTE IS ONE ON EVERY CHAIN: the blended pane on the gathering chain within one fp16 "
              "ulp of the live ray chain's and the no-ray chain's");
    }
    std::printf("\ngi.opaque_bindings: %d frames, %d failure(s)\n", frames, failures);
    return failures ? 1 : 0;
}
