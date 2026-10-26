// gi.screen_integrate (PHOTON-7 stage 7, 7-6) - THE NO-RAY ROUTE'S PRODUCER MOVED, THE PHYSICS DID NOT.
//
// On a chain that does not gather, the opaque passes read the screen integrate's products (the diffuse, the cone's
// specular and its hit) in place of the voxel cascades (L4 on every chain). THE A/B, in one process on one scene: the
// shipped route against the voxel route the pixel ran before 7-6 (the measuring door gi.screenIntegrateOff), each at
// the GI's rest, the scene radiance read in float (hdrReadback), and the voxel route's OWN NOISE beside them: its
// picture again with the camera one millimetre aside.
// THE FIXTURE: a floor, a back wall and three boxes (two coloured, one white), a sun and a sky (SH), High, rays off, a
// still picture with the chain (selftest B2's class), in TWO ARRANGEMENTS, each its own scene:
//   AXIS-ALIGNED (B2's own case): every diffuse cone of its surfaces is a 45-degree cone, a TIE of the march's stepping
//     axis (jahVoxelAxis) - which the pixel's frame, built through a float rotation, used to break by its rounding per
//     pixel (two answers up to ~15 % apart in the voxels' share: the sides read 5-7.6 % off) and the producer's exact
//     frame always one way. The tie is DECIDED now (jahVoxelAxis's tolerance), so the two routes agree here too.
//   TURNED 21 degrees about a generic axis: no surface on a voxel axis, no tie - the producer away from the seam.
// THE LAWS, per arrangement:
//   (1) the door: the shipped route's opaque pass reads the producer's three products and no voxel volume, the voxel
//       route's reads the cascades and not the producer;
//   (2) THE PICTURE: every region's mean radiance (the floor, the wall, the boxes' fronts and their sides toward the
//       middle) within 1 % of the voxel route's, per channel;
//   (3) WITHIN THE CONE'S OWN NOISE: over the whole picture, the mean relative difference and the pixels more than 5 %
//       apart no more than the voxel route's own under a one-millimetre camera move.
// A fixture (the engine alone, one offscreen view). Counted in frames.
#include "jahshaka/engine/Engine.h"
#include "jahshaka/engine/PhotonStatus.h"
#include "../support/enginetesthelpers.h"

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
    return p;
}
struct Region { const char *name; unsigned x0, y0, x1, y1; };
}   // namespace

int main()
{
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-gi-screen-integrate-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    e->setRayTracing(false);
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
        Scene *scene = e->createScene("screen_integrate");
        if (!scene) { std::printf("FAIL: scene\n"); return 1; }
        view->setScene(scene);
        scene->setRayTracing(RayTracingMode::Off);
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
        const Vec3 eye = turn(Vec3(0.0f, 1.6f, 4.0f)), at = turn(Vec3(0.0f, 1.0f, -2.0f));
        enginetest::testCameraLookAt(view, eye, at);
        GiParams gi;
        gi.mode = GiMode::Vct;
        gi.quality = GiQuality::High;
        gi.bounces = true;
        if (!scene->setGlobalIllumination(gi)) { std::printf("FAIL: the GI builds\n"); return 1; }
        e->waitForAsyncShaders();

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
        std::printf("   the voxel route at rest after %d frames (again, 1 mm aside: %d), the screen integrate after %d\n",
                    restA, restA2, restB);
        char msg[384];
        std::snprintf(msg, sizeof msg,
                      "[%s] (1) the door: the shipped route's opaque pass reads the producer's three products and no "
                      "voxel volume, the voxel route's the cascades", arr.name);
        CHECK(restA >= 0 && restB >= 0 && restA2 >= 0 && binds(rb, "jahProbeIrradiance") && binds(rb, "jahRoughSpec") &&
                  binds(rb, "jahConeHit") && !binds(rb, "vctProbes") && binds(ra, "vctProbes") &&
                  !binds(ra, "jahConeHit"),
              msg);
        bool within = a.width == b.width && a.width > 0;
        for (size_t ri = 0; ri < arr.nRegions; ++ri) {
            const Region &r = arr.regions[ri];
            double sa[3] = { 0, 0, 0 }, sb[3] = { 0, 0, 0 };
            unsigned n = 0;
            for (unsigned y = r.y0; y < r.y1 && y < a.height; ++y)
                for (unsigned x = r.x0; x < r.x1 && x < a.width; ++x) {
                    const auto pa = a.at(x, y), pb = b.at(x, y);
                    sa[0] += pa.r; sa[1] += pa.g; sa[2] += pa.b;
                    sb[0] += pb.r; sb[1] += pb.g; sb[2] += pb.b;
                    ++n;
                }
            double worst = 0.0;
            for (int c = 0; c < 3; ++c) {
                sa[c] /= std::max(n, 1u);
                sb[c] /= std::max(n, 1u);
                worst = std::max(worst, std::fabs(sb[c] - sa[c]) / std::max(sa[c], 1e-6));
            }
            std::printf("   %-14s voxel route %.4f %.4f %.4f, screen integrate %.4f %.4f %.4f - worst %.2f %%\n", r.name,
                        sa[0], sa[1], sa[2], sb[0], sb[1], sb[2], 100.0 * worst);
            within = within && n > 0u && worst <= 0.01;
        }
        std::snprintf(msg, sizeof msg,
                      "[%s] (2) THE PRODUCER MOVED, THE PHYSICS DID NOT: every region within 1 %% of the voxel route's",
                      arr.name);
        CHECK(within, msg);
        {
            // over every lit pixel: the relative difference of the brightest channel
            struct Spread { double mean = 0.0; unsigned over5 = 0, n = 0; };
            const auto spread = [&](const ImageF &x) {
                Spread sp;
                for (unsigned y = 0; y < a.height && y < x.height; ++y)
                    for (unsigned xx = 0; xx < a.width && xx < x.width; ++xx) {
                        const auto pa = a.at(xx, y), px = x.at(xx, y);
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
            const Spread sb = spread(b), s2 = spread(a2);
            std::printf("   over %u lit pixels: the screen integrate %.4f %% mean, %u over 5 %%; the voxel route 1 mm "
                        "aside %.4f %% mean, %u over 5 %%\n", sb.n, 100.0 * sb.mean, sb.over5, 100.0 * s2.mean, s2.over5);
            std::snprintf(msg, sizeof msg,
                          "[%s] (3) WITHIN THE CONE'S OWN NOISE: the producer no further from the voxel route than the "
                          "voxel route from itself one millimetre aside", arr.name);
            CHECK(sb.n > 1000u && sb.mean <= s2.mean && sb.over5 <= s2.over5, msg);
        }
        e->setArm("gi.screenIntegrateOff", 0.0);
        view->setScene(nullptr);
        e->destroyScene(scene);
        run(2);
    }
    std::printf("\ngi.screen_integrate: %d frames, %d failure(s)\n", frames, failures);
    return failures ? 1 : 0;
}
