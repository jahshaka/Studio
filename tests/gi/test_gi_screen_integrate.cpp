// gi.screen_integrate (PHOTON-7 stage 7, 7-6; re-pointed at physics in 10-3, R12) - THE NO-RAY ROUTE'S PRODUCER MOVED,
// THE PHYSICS DID NOT.
//
// On a chain that does not gather, the opaque passes read the screen integrate's products (the diffuse, the cone's
// specular and its hit) in place of the voxel cascades (L4 on every chain). THE A/B, in one process: the shipped route
// against the voxel route the pixel ran before 7-6 (the measuring door gi.screenIntegrateOff), each at the GI's rest,
// the scene radiance read in float (hdrReadback), the voxel route's OWN NOISE beside them (its picture again with the
// camera one millimetre aside) - and both against THE HIGH-RAY REFERENCE (R12; the pre-7-6 picture was the reference
// before): the same arrangement, its own scene, at the ray tier, its screen gather's rest mean over 64 frames of its
// jittered 64-ray probes (gather.restFrames: 4,096 TLAS rays a probe) and the cards' gather at 128 rays (gi.truthRays)
// - every hit's answer an exact estimator of its ray (gi.card_truth). With the GI off the two tiers' pictures are
// identical in every region (measured, 0.00 %): the sun, its shadows and the sky agree, the differences are the GI's.
// THE FIXTURE: a floor, a back wall and three boxes (two coloured, one white), all matte with NO SPECULAR LOBE (kS = 0:
// the reference traces its reflections and the no-ray route takes the cone's - the laws are the diffuse bounce's), a sun
// and a sky (SH), High, a still picture with the chain (selftest B2's class), in TWO ARRANGEMENTS, each its own scene:
//   AXIS-ALIGNED (B2's own case): every diffuse cone of its surfaces is a 45-degree cone, a TIE of the march's stepping
//     axis (jahVoxelAxis) - DECIDED (jahVoxelAxis's tolerance), so the two routes agree here too.
//   TURNED 21 degrees about a generic axis: no surface on a voxel axis, no tie - the producer away from the seam.
// THE BAR IS THE STORE'S QUANTUM q (the lead's ruling, R12): one half-float step at the reference's value.
// THE LAWS, per arrangement:
//   (1) the door: the shipped route's opaque pass reads the producer's three products and no voxel volume, the voxel
//       route's reads the cascades and not the producer;
//   (2) THE PRODUCER MOVED, THE PHYSICS DID NOT: every region's mean (the floor, the wall, the boxes' fronts and their
//       sides toward the middle), per channel, no further from the reference than the voxel route's, by q and that
//       route's own 1 mm noise (measured: the two routes within 0.01-0.08 % of each other);
//   (2') THE NO-RAY ROUTE'S DIFFUSE IS THE PHYSICS: every region within q of the reference - DECLARED RED (the lead's
//       ruling, 2026-10-27), owned by 10-4's R7 (the one-cell rule), re-measured at its merge; what it leaves is the
//       no-ray tier's measured limit. Measured (the worst channel): axis-aligned 1.3-6.7 % (the fronts ~4 % bright,
//       the sides ~6 % dark); turned 2.0-51.4 % - the magenta side 0.116 against 0.219, darker than the GI-off
//       picture (0.170), the blue side 20 %, the wall 8-10 %; over the whole picture 4.9 % mean (axis-aligned) and
//       9.3 % (turned) from the reference;
//   (3) OVER THE PICTURE: the producer's mean relative distance to the reference and its pixels over 5 % no more than
//       the voxel route's, by that route's own 1 mm noise.
// A fixture (the engine alone, one offscreen view). Counted in frames.
#include "jahshaka/engine/Engine.h"
#include "jahshaka/engine/PhotonStatus.h"
#include "../support/enginetesthelpers.h"
#include "../support/lawline.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
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
/// THE TURN: `deg` degrees about normalize(0.35, 1, 0.2), about the origin (the arrangement's centre on the floor)
struct Turn {
    Quat q;
    explicit Turn(float deg) {
        const float ax = 0.35f, ay = 1.0f, az = 0.2f, n = std::sqrt(ax * ax + ay * ay + az * az);
        const float h = 0.5f * deg * 3.14159265f / 180.0f, sh = std::sin(h) / n;
        q = Quat(ax * sh, ay * sh, az * sh, std::cos(h));
    }
    Vec3 operator()(const Vec3 &v) const {
        // v' = v + 2 w (u x v) + 2 u x (u x v), u = the quaternion's vector part
        const Vec3 u(q.x, q.y, q.z);
        const auto cross = [](const Vec3 &a, const Vec3 &b) {
            return Vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
        };
        const Vec3 t = cross(u, v);
        const Vec3 t2 = cross(u, t);
        return Vec3(v.x + 2.0f * (q.w * t.x + t2.x), v.y + 2.0f * (q.w * t.y + t2.y), v.z + 2.0f * (q.w * t.z + t2.z));
    }
};
NodeId box(Scene *s, const Turn &turn, const Vec3 &at, const Vec3 &size, const PbrParams &p) {
    const NodeId node = s->createNode();
    MeshData d = enginetest::unitCubeMesh();
    d.cards = enginetest::boxCards(0.5f);
    const MeshId mesh = s->createMesh(d);
    const MaterialId mat = s->createPbrMaterial(p);
    if (!node || !mesh || !mat || !s->attachMesh(node, mesh, mat)) return 0;
    enginetest::poseRegistry()[s][node] = enginetest::NodePose{ turn(at), turn.q, size };
    s->setNodeTransform(node, turn(at), turn.q, size);
    return node;
}
PbrParams matte(float r, float g, float b, float rough = 0.8f) {
    PbrParams p;
    p.albedo = Colour(r, g, b);
    p.roughness = rough;
    // NO SPECULAR LOBE (kS = 0): the reference traces its reflections and the no-ray route takes the cone's - the law is
    // the diffuse bounce's, so the specular routes stay out of it
    p.workflow = PbrParams::Workflow::Specular;
    p.ior = 1.0f;
    p.specularColour = Colour(0.0f, 0.0f, 0.0f);
    return p;
}
double quantumOf(double R) { return R > 0.0 ? std::ldexp(1.0, int(std::floor(std::log2(R))) - 10) : 0.0; }
struct Region { const char *name; unsigned x0, y0, x1, y1; };
}   // namespace

int main()
{
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-gi-screen-integrate-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    e->setRayTracing(true);   // (the reference arm traces; the two routes' scenes are the no-ray tier: setRayTracing Off)
    int frames = 1;
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));
    const auto run = [&](int n) { enginetest::frames(e, n); frames += n; };
    View *view = e->createOffscreenView("screen_integrate", 256, 256, Colour(0, 0, 0), OffscreenContract::StillPicture);
    if (!view) { std::printf("FAIL: view\n"); return 1; }
    {
        PostFxDesc fx;
        fx.allowOffscreen = true;
        fx.ssr = 1;
        fx.hdrReadback = true;
        view->setPostFx(fx);
    }

    // the regions of each arrangement's 256 x 256 picture: the boxes stand left, centre and right
    const Region alignedRegions[] = { { "floor", 80, 220, 176, 250 },        { "wall left", 10, 20, 40, 40 },
                                      { "wall centre", 110, 30, 146, 60 },    { "blue front", 20, 90, 50, 140 },
                                      { "blue side", 57, 90, 61, 150 },       { "white front", 115, 90, 141, 140 },
                                      { "magenta side", 193, 90, 197, 150 },  { "magenta front", 206, 90, 236, 140 } };
    const Region turnedRegions[] = { { "floor", 80, 220, 176, 250 },        { "wall left", 10, 20, 40, 70 },
                                     { "wall centre", 110, 30, 146, 60 },    { "blue front", 10, 100, 45, 170 },
                                     { "blue side", 54, 100, 62, 160 },      { "white front", 115, 90, 141, 140 },
                                     { "magenta side", 191, 90, 198, 160 },  { "magenta front", 205, 80, 240, 160 } };
    struct Arrangement { const char *name; float deg; const Region *regions; size_t nRegions; };
    const Arrangement arrangements[] = { { "axis-aligned", 0.0f, alignedRegions, 8u },
                                         { "turned 21 degrees", 21.0f, turnedRegions, 8u } };

    for (const Arrangement &arr : arrangements) {
        std::printf("\n-- %s --\n", arr.name);
        const Turn turn(arr.deg);
        const Vec3 eye = turn(Vec3(0.0f, 1.6f, 4.0f)), at = turn(Vec3(0.0f, 1.0f, -2.0f));
        const auto build = [&](bool rays) {
            Scene *scene = e->createScene(rays ? "screen_integrate_reference" : "screen_integrate");
            if (!scene) return scene;
            view->setScene(scene);
            scene->setRayTracing(rays ? RayTracingMode::On : RayTracingMode::Off);
            box(scene, turn, Vec3(0.0f, -0.05f, 0.0f), Vec3(10.0f, 0.1f, 10.0f), matte(0.7f, 0.7f, 0.7f));   // the floor
            box(scene, turn, Vec3(0.0f, 2.0f, -3.0f), Vec3(10.0f, 4.0f, 0.2f), matte(0.6f, 0.6f, 0.6f));     // the wall
            box(scene, turn, Vec3(-1.6f, 1.0f, -1.5f), Vec3(0.8f, 2.0f, 0.8f), matte(0.2f, 0.3f, 0.8f));     // blue
            box(scene, turn, Vec3(0.0f, 1.0f, -1.5f), Vec3(0.8f, 2.0f, 0.8f), matte(0.8f, 0.8f, 0.8f));      // white
            box(scene, turn, Vec3(1.6f, 1.0f, -1.5f), Vec3(0.8f, 2.0f, 0.8f), matte(0.8f, 0.2f, 0.7f));      // magenta
            enginetest::addDirectionalLight(scene, Vec3(-0.3f, -0.8f, -0.5f), 3.0f);
            {
                float sh[27] = { 0 };
                sh[0] = 0.25f; sh[1] = 0.3f; sh[2] = 0.4f;   // a dim blue sky
                scene->setAmbientSh(sh);
            }
            enginetest::testCameraLookAt(view, eye, at);
            GiParams gi;
            gi.mode = GiMode::Vct;
            gi.quality = GiQuality::High;
            gi.bounces = true;
            gi.cards = rays;
            if (!scene->setGlobalIllumination(gi)) return static_cast<Scene *>(nullptr);
            e->waitForAsyncShaders();
            return scene;
        };
        // THE REFERENCE FIRST (its own scene): the ray tier, its screen gather's rest mean over 64 frames of its jittered
        // 64-ray probes (4096 rays a probe through the TLAS) and the cards' gather at 128 rays (gi.truthRays) - the
        // hemisphere's high-ray limit, every hit's answer an exact estimator of its ray (gi.card_truth)
        ImageF ref;
        int restR = -1;
        {
            e->setArm("gather.restFrames", 64.0);
            e->setArm("gi.truthRays", 128.0);
            Scene *rs = build(true);
            if (!rs) { std::printf("FAIL: the reference's GI builds\n"); return 1; }
            run(10);
            int f = 0, quiet = 0;
            for (; f < 900 && quiet < 30; ++f) {
                run(1);
                quiet = rs->published().gi.giAtRest ? quiet + 1 : 0;
            }
            restR = quiet >= 30 && view->readPixelsHdr(ref) ? f - quiet : -1;
            e->setArm("gather.restFrames", 0.0);
            e->setArm("gi.truthRays", 0.0);
            view->setScene(nullptr);
            e->destroyScene(rs);
            run(2);
        }
        Scene *scene = build(false);
        if (!scene) { std::printf("FAIL: the GI builds\n"); return 1; }

        const auto settled = [&](bool voxelRoute, ImageF &pic, std::vector<PhotonPassBindingRecord> &recs) {
            e->setArm("gi.screenIntegrateOff", voxelRoute ? 1.0 : 0.0);
            e->waitForAsyncShaders();
            run(10);
            int f = 0, quiet = 0;
            for (; f < 900 && quiet < 30; ++f) {
                run(1);
                quiet = scene->published().gi.giAtRest ? quiet + 1 : 0;
            }
            resetPhotonPassBindings();
            run(2);
            recs = photonPassBindings();
            return view->readPixelsHdr(pic) ? (quiet >= 30 ? f - quiet : -1) : -2;
        };
        ImageF a, b, a2;
        std::vector<PhotonPassBindingRecord> ra, rb, r2;
        const int restA = settled(true, a, ra);
        const int restB = settled(false, b, rb);
        // THE CONE'S OWN NOISE: the voxel route again, the camera one millimetre aside (a turned step: the turn is
        // about the origin)
        const Vec3 step = turn(Vec3(0.001f, 0.0f, 0.0f));
        enginetest::testCameraLookAt(view, Vec3(eye.x + step.x, eye.y + step.y, eye.z + step.z),
                                     Vec3(at.x + step.x, at.y + step.y, at.z + step.z));
        const int restA2 = settled(true, a2, r2);
        const auto binds = [](const std::vector<PhotonPassBindingRecord> &recs, const char *name) {
            for (const PhotonPassBindingRecord &r : recs)
                if (r.pass == "Jahshaka opaque")
                    for (const std::string &t : r.bound)
                        if (t.compare(0, std::strlen(name), name) == 0) return true;
            return false;
        };
        std::printf("   the reference at rest after %d frames; the voxel route after %d (again, 1 mm aside: %d), the "
                    "screen integrate after %d\n", restR, restA, restA2, restB);
        char msg[384];
        std::snprintf(msg, sizeof msg,
                      "[%s] (1) the door: the shipped route's opaque pass reads the producer's three products and no "
                      "voxel volume, the voxel route's the cascades", arr.name);
        CHECK(restA >= 0 && restB >= 0 && restA2 >= 0 && binds(rb, "jahProbeIrradiance") && binds(rb, "jahRoughSpec") &&
                  binds(rb, "jahConeHit") && !binds(rb, "vctProbes") && binds(ra, "vctProbes") &&
                  !binds(ra, "jahConeHit"),
              msg);
        bool within = a.width == b.width && a.width > 0 && ref.width == a.width && restR >= 0, physical = within;
        for (size_t ri = 0; ri < arr.nRegions; ++ri) {
            const Region &r = arr.regions[ri];
            double sa[3] = { 0, 0, 0 }, sb[3] = { 0, 0, 0 }, sr[3] = { 0, 0, 0 }, s2[3] = { 0, 0, 0 };
            unsigned n = 0;
            for (unsigned y = r.y0; y < r.y1 && y < a.height; ++y)
                for (unsigned x = r.x0; x < r.x1 && x < a.width; ++x) {
                    const auto pa = a.at(x, y), pb = b.at(x, y), pr = ref.at(x, y), p2 = a2.at(x, y);
                    sa[0] += pa.r; sa[1] += pa.g; sa[2] += pa.b;
                    sb[0] += pb.r; sb[1] += pb.g; sb[2] += pb.b;
                    sr[0] += pr.r; sr[1] += pr.g; sr[2] += pr.b;
                    s2[0] += p2.r; s2[1] += p2.g; s2[2] += p2.b;
                    ++n;
                }
            double worst = 0.0, eV = 0.0, eS = 0.0, eN = 0.0, physQ = 0.0;
            for (int c = 0; c < 3; ++c) {
                sa[c] /= std::max(n, 1u);
                sb[c] /= std::max(n, 1u);
                sr[c] /= std::max(n, 1u);
                s2[c] /= std::max(n, 1u);
                // (2) the producer no further from the reference than the route it replaced, by q and that route's
                // own 1 mm noise; (2') the route itself within q of the reference
                const double q = quantumOf(sr[c]);
                within = within && std::fabs(sb[c] - sr[c]) <= std::fabs(sa[c] - sr[c]) + q + std::fabs(s2[c] - sa[c]);
                physical = physical && std::fabs(sb[c] - sr[c]) <= q;
                physQ = std::max(physQ, q > 0.0 ? std::fabs(sb[c] - sr[c]) / q : -1.0);
                worst = std::max(worst, std::fabs(sb[c] - sa[c]) / std::max(sa[c], 1e-6));
                eV = std::max(eV, std::fabs(sa[c] - sr[c]) / std::max(sr[c], 1e-6));
                eS = std::max(eS, std::fabs(sb[c] - sr[c]) / std::max(sr[c], 1e-6));
                eN = std::max(eN, std::fabs(s2[c] - sa[c]) / std::max(sa[c], 1e-6));
            }
            std::printf("   %-14s reference %.4f %.4f %.4f | voxel route %.4f %.4f %.4f (%+.2f %%) | screen integrate "
                        "%.4f %.4f %.4f (%.2f %%) - S vs V %.2f %%, V 1 mm %.3f %%\n", r.name, sr[0], sr[1], sr[2],
                        sa[0], sa[1], sa[2], 100.0 * eV, sb[0], sb[1], sb[2], 100.0 * eS, 100.0 * worst, 100.0 * eN);
            within = within && n > 0u;
            {   // THE LAW LINES (HARNESS-3 (b); tests/support/lawline.h): (2')'s worst channel in q; the producer's and the
                // route's distance from the reference, and the route's own 1 mm noise, in %
                const std::string at = enginetest::lawName(std::string(arr.name) + "." + r.name);
                enginetest::lawLine((at + ".screen_vs_ref").c_str(), physQ, "q", "<=", 1.0);
                enginetest::lawLine((at + ".screen_vs_ref_pct").c_str(), 100.0 * eS, "%");
                enginetest::lawLine((at + ".voxel_vs_ref_pct").c_str(), 100.0 * eV, "%");
                enginetest::lawLine((at + ".voxel_1mm_pct").c_str(), 100.0 * eN, "%");
            }
        }
        std::snprintf(msg, sizeof msg,
                      "[%s] (2) THE PRODUCER MOVED, THE PHYSICS DID NOT: every region's mean, per channel, no further from "
                      "the high-ray reference than the voxel route's, by q and that route's own 1 mm noise", arr.name);
        CHECK(within, msg);
        // (DECLARED RED - the lead's ruling, 2026-10-27: owned by 10-4's R7 one-cell rule, re-measured at its merge)
        std::snprintf(msg, sizeof msg,
                      "[%s] (2') THE NO-RAY ROUTE'S DIFFUSE IS THE PHYSICS: every region's mean, per channel, within q of "
                      "the high-ray reference", arr.name);
        CHECK(physical, msg);
        {
            // over every lit pixel: the relative difference of the brightest channel
            struct Spread { double mean = 0.0; unsigned over5 = 0, n = 0; };
            const auto spread = [&](const ImageF &x, const ImageF &base) {
                Spread sp;
                for (unsigned y = 0; y < base.height && y < x.height; ++y)
                    for (unsigned xx = 0; xx < base.width && xx < x.width; ++xx) {
                        const auto pa = base.at(xx, y), px = x.at(xx, y);
                        const double ref = std::max({ double(pa.r), double(pa.g), double(pa.b) });
                        if (!(ref > 1e-3)) continue;
                        const double d = std::max({ std::fabs(double(px.r) - pa.r), std::fabs(double(px.g) - pa.g),
                                                    std::fabs(double(px.b) - pa.b) }) / ref;
                        sp.mean += d;
                        sp.over5 += d > 0.05 ? 1u : 0u;
                        ++sp.n;
                    }
                sp.mean /= std::max(sp.n, 1u);
                return sp;
            };
            const Spread sS = spread(b, ref), sV = spread(a, ref), s2 = spread(a2, a);
            std::printf("   over %u lit pixels, against the reference: the screen integrate %.4f %% mean, %u over 5 %%; the "
                        "voxel route %.4f %% mean, %u over 5 %%; the voxel route against itself 1 mm aside %.4f %% mean, "
                        "%u over 5 %%\n", sS.n, 100.0 * sS.mean, sS.over5, 100.0 * sV.mean, sV.over5, 100.0 * s2.mean,
                        s2.over5);
            std::snprintf(msg, sizeof msg,
                          "[%s] (3) OVER THE PICTURE: the producer no further from the reference than the voxel route, by "
                          "that route's own 1 mm noise (the mean and the pixels over 5 %%)", arr.name);
            const std::string at = enginetest::lawName(arr.name);
            enginetest::lawLine((at + ".picture_mean_pct").c_str(), 100.0 * sS.mean, "%", "<=", 100.0 * (sV.mean + s2.mean));
            enginetest::lawLine((at + ".picture_over5").c_str(), sS.over5, "px", "<=", double(sV.over5 + s2.over5));
            CHECK(sS.n > 1000u && sS.mean <= sV.mean + s2.mean && sS.over5 <= sV.over5 + s2.over5, msg);
        }
        e->setArm("gi.screenIntegrateOff", 0.0);
        view->setScene(nullptr);
        e->destroyScene(scene);
        run(2);
    }
    std::printf("\ngi.screen_integrate: %d frames, %d failure(s)\n", frames, failures);
    return failures ? 1 : 0;
}
