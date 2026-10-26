// gi.sun_contact_acne — THE SUN CONTACT NEVER SHADOWS A SURFACE BY ITSELF, AND ITS LIFT IS NO SMALLER THAN THE LAW
// (PHOTON-7 stage 6, READ_A W1, READ_B F3 + F5).
//
// THE LAW: the contact job traces one ray per pixel from the pixel's DRAWN surface toward the sun and folds
// min( map, ray ) into the direct sun; on a sunlit surface with nothing between it and the sun the ray must find
// nothing, so the picture with the job ON equals the picture with it OFF there, pixel for pixel. The pixel shows the
// drawn LOD (a stock item's level) while the ray traces each mesh's LEVEL 0 (ONE RAY LEVEL PER MESH): on a CONVEX
// surface the drawn facets lie INSIDE level 0, so the origin leaves along the GEOMETRIC normal by the ONE ORIGIN LAW
// (jah_ray_origin.glsl, Types.h rayOriginDrawnFootprints): the float bound + kLevelDriftOverBound x the tolerance x
// one footprint at the point's Euclidean distance (over 1 - the view's band; this offscreen view has none).
// WHY THE DRIFT RATIO, AND WHAT THIS ROW PLACES BETWEEN THE TWO (READ_B F5): the LOD rule spends the bake's STORED
// error, which bounds each step's deviation from its INPUT, and a level's drift from level 0 may reach 2x it (the
// bake's ship law, atom2.one_step_drift_bound). So the fixture's level is AUTHORED that way: level 1's facets lie
// inside the sphere by their measured sag D while the level table stores D / kFixtureDrift (1.8, inside the law) -
// the strategy draws it wherever the stored bound is under the tolerance - and the camera stands where HALF the law's
// lift (what the tolerance alone gives: the pre-F3 lift) is under D and the whole law is over it. THREE PICTURES with
// the job on: the law (green: nothing darker), HALF its drawn term (rays.originCut 0.5) and NONE of it (0) - both RED:
// the facets' middles start their rays inside level 0.
// THE FIXTURE: a matte UV sphere (radius 1 m) whose level 1 (16 x 8 of level 0's 64 x 32) the STOCK strategy draws (the
// scene's visibility buffer off: it draws a DAG-less mesh at level 0), a sun 35 degrees up behind the camera's
// shoulder, a floor far below, no GI. The lit hemisphere's pixels: each picture vs the job off. HALF the law's term
// red is also the proof that level 1 is what the view drew: level 0 is the traced surface, which a lift of
// centimetres clears.
// Frames: 1 + 4 x 16 = 65.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, ...)                                                        \
    do {                                                                        \
        if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } \
        else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++failures; } \
    } while (0)

static const unsigned kW = 640, kH = 360;
static const float kPi = 3.14159265358979f;
static const int kSeg0 = 64, kRing0 = 32, kStep1 = 4;
/// The fixture level's drift from level 0 over its STORED bound - inside the bake's ship law (kLevelDriftOverBound).
static constexpr float kFixtureDrift = 1.8f;
static_assert(kFixtureDrift < kLevelDriftOverBound, "the fixture's level must obey the bake's drift law");

/// THE SPHERE: level 0 and level 1 (every 4th vertex each way) over one vertex buffer; level 1's measured sag D, the
/// largest distance of its triangles from the sphere (sampled), returned - its table stores D / kFixtureDrift.
static MeshData sphereMesh(float &bound1)
{
    MeshData d;
    for (int r = 0; r <= kRing0; ++r) {
        const float ph = kPi * float(r) / float(kRing0);
        for (int s = 0; s <= kSeg0; ++s) {
            const float th = 2.0f * kPi * float(s) / float(kSeg0);
            const float x = std::sin(ph) * std::cos(th), y = std::cos(ph), z = std::sin(ph) * std::sin(th);
            d.positions.insert(d.positions.end(), { x, y, z });
            d.normals.insert(d.normals.end(), { x, y, z });
            d.uvs.insert(d.uvs.end(), { float(s) / float(kSeg0), float(r) / float(kRing0) });
        }
    }
    const auto vtx = [](int r, int s) { return unsigned(r * (kSeg0 + 1) + s); };
    // counter-clockwise seen from outside
    const auto quads = [&](std::vector<unsigned> &idx, int step) {
        for (int r = 0; r < kRing0; r += step)
            for (int s = 0; s < kSeg0; s += step) {
                const unsigned a = vtx(r, s), b = vtx(r, s + step), c = vtx(r + step, s), e = vtx(r + step, s + step);
                idx.insert(idx.end(), { a, b, e, a, e, c });
            }
    };
    quads(d.indices, 1);
    std::vector<unsigned> level1;
    quads(level1, kStep1);
    bound1 = 0.0f;
    const auto P = [&](unsigned i) { return Vec3(d.positions[3 * i], d.positions[3 * i + 1], d.positions[3 * i + 2]); };
    for (size_t t = 0; t + 2 < level1.size(); t += 3) {
        const Vec3 A = P(level1[t]), B = P(level1[t + 1]), C = P(level1[t + 2]);
        for (int i = 0; i <= 8; ++i)
            for (int j = 0; i + j <= 8; ++j) {
                const float u = float(i) / 8.0f, v = float(j) / 8.0f, w = 1.0f - u - v;
                const Vec3 q(A.x * w + B.x * u + C.x * v, A.y * w + B.y * u + C.y * v, A.z * w + B.z * u + C.z * v);
                bound1 = std::max(bound1, 1.0f - std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z));
            }
    }
    d.lodIndices.push_back(level1);
    // THE STORED BOUND, as a bake that drifted kFixtureDrift x would store it (the head of this file)
    d.lodBounds.push_back(bound1 / kFixtureDrift);
    d.lodErrors.push_back(bound1 / kFixtureDrift);
    return d;
}

/// THE LIT HEMISPHERE: the pixels of the sphere's disk whose surface faces the sun, a margin inside the terminator and
/// the silhouette (the mean over a pixel there mixes the sun's terminator, which no lift changes).
struct Disk { float cx, cy, r; };

int main()
{
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-sun-contact-acne-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));
    View *view = e->createOffscreenView("sunacne", kW, kH, Colour(0.45f, 0.55f, 0.70f), jahshaka::engine::OffscreenContract::Live);
    Scene *s = e->createScene("sunacne");
    if (!view || !s) { std::printf("FAIL: view/scene: %s\n", e->lastError().c_str()); return 1; }
    view->setScene(s);
    if (!e->rayQueryAvailable() || !e->rayTracing()) {
        std::printf("SKIP: no ray-query device here - the contact job is a ray job\n");
        return 77;
    }
    view->setShadows(true);
    PostFxDesc fx;
    fx.allowOffscreen = true;
    view->setPostFx(fx);
    s->setPixelTolerance(1.0f);
    // THROUGH OGRE'S OWN LOD (the stock strategy, OgreMesh.cpp): the visibility buffer draws a mesh with no cluster DAG
    // at its LEVEL 0 (the cut picks per cluster, and this mesh has none - measured: the same 0 / 0 / 866 px with level 1
    // made unaffordable), so the level this row is about is drawn only by the stock route - the route every item Atom
    // does not take draws through (a blended, skinned, custom-piece or planar item).
    s->setAtomDrawEnabled(false);
    s->setAmbient(Colour(0.10f, 0.11f, 0.12f), Colour(0.08f, 0.08f, 0.08f));
    // the sun 35 degrees up, from behind the camera's right shoulder (the camera looks down -z)
    const float el = 35.0f * kPi / 180.0f, az = 25.0f * kPi / 180.0f;
    const Vec3 toSun(std::cos(el) * std::sin(az), std::sin(el), std::cos(el) * std::cos(az));
    {
        const NodeId sun = enginetest::addDirectionalLight(s, Vec3(-toSun.x, -toSun.y, -toSun.z), 6.0f);
        LightDesc l;
        l.type = LightType::Directional;
        l.colour = Colour(1.f, 1.f, 1.f);
        l.intensity = 6.0f / kPi;
        l.castShadows = true;
        CHECK(sun && s->setLight(sun, l), "the sun casts shadows");
    }
    float bound1 = 0.0f;
    const NodeId ball = s->createNode();
    PbrParams p;
    p.albedo = Colour(0.7f, 0.7f, 0.7f);
    p.roughness = 1.0f;
    const MaterialId mat = s->createPbrMaterial(p);
    const MeshId mesh = s->createMesh(sphereMesh(bound1));
    CHECK(ball && mat && mesh && s->attachMesh(ball, mesh, mat), "the two-level sphere exists (level 1's bound %.4f m)",
          bound1);
    const NodeId floor = enginetest::addTestCube(s, Colour(0.5f, 0.5f, 0.5f), 0.0f, 1.0f);
    enginetest::setNodeScale(s, floor, Vec3(200.0f, 0.2f, 200.0f));
    enginetest::setNodePosition(s, floor, Vec3(0.0f, -1.6f, 0.0f));
    // THE DISTANCE: the sphere's nearest point where HALF the law's lift (one footprint of 2 tan(22.5) / H a metre at
    // the tolerance 1) is D / 1.4 - so half the law clears no facet middle deeper than 0.71-0.77 D over the lit
    // hemisphere, the law clears 1.43 D at least, and the stored bound D / 1.8 is 0.78 of the tolerance there (the
    // strategy draws level 1: it judges the object at its bounding sphere's nearest distance)
    const float footPerMetre = 2.0f * std::tan(22.5f * kPi / 180.0f) / float(kH);
    const float dist = 1.0f + bound1 / (1.4f * footPerMetre);
    const Vec3 cam(0.0f, 0.0f, dist);
    enginetest::testCameraLookAt(view, cam, Vec3(0.0f, 0.0f, 0.0f));
    const float allowed = footPerMetre * (dist - 1.0f);
    std::printf("   the camera %.2f m from the sphere's centre: level 1's sag D %.4f m, stored %.4f m = %.2f of the "
                "tolerance at the nearest point; the law's lift there %.4f m (%.2f D), half of it %.4f m (%.2f D)\n",
                dist, bound1, bound1 / kFixtureDrift, bound1 / kFixtureDrift / allowed,
                rayOriginDrawnFootprints(1.0f, 0.0f) * allowed, rayOriginDrawnFootprints(1.0f, 0.0f) * allowed / bound1,
                0.5f * rayOriginDrawnFootprints(1.0f, 0.0f) * allowed,
                0.5f * rayOriginDrawnFootprints(1.0f, 0.0f) * allowed / bound1);

    const auto shoot = [&](bool contact, Image &img) {
        SunContactDesc sc;
        sc.enabled = contact;
        sc.resolution = SunContactResolution::Full;
        s->setSunContact(sc);
        enginetest::frames(e, 16);
        return view->readPixels(img);
    };
    Image off, on, half, noCut;
    CHECK(shoot(false, off), "the picture with the contact job off");
    CHECK(shoot(true, on), "the picture with the contact job on");
    const SunContactStatus st = s->published().sunContact;
    CHECK(st.on && st.running, "the contact job ran (on %d, running %d%s%s)", int(st.on), int(st.running),
          st.reason.empty() ? "" : ": ", st.reason.c_str());
    bool armed = false;
    e->request([&](Engine &en) { armed = en.setArm("rays.originCut", 0.5); });
    CHECK(armed, "the first control's arm (rays.originCut 0.5)");
    CHECK(shoot(true, half), "the picture with the contact job on, its origin's drawn term halved");
    armed = false;
    e->request([&](Engine &en) { armed = en.setArm("rays.originCut", 0.0); });
    CHECK(armed, "the second control's arm (rays.originCut 0)");
    CHECK(shoot(true, noCut), "the picture with the contact job on, its origin's drawn term off");
    e->request([&](Engine &en) { en.setArm("rays.originCut", 1.0); });

    // THE LIT DISK: the sphere's projected disk, its pixels whose surface faces the sun by more than 0.2
    const float tanHalf = std::tan(22.5f * kPi / 180.0f);
    const float pxPerRad = 0.5f * float(kH) / tanHalf;
    const float rDisk = pxPerRad * std::asin(1.0f / dist) * 0.92f;
    int lit = 0, acneOn = 0, acneHalf = 0, acneNoCut = 0;
    const auto luma = [](const Image &img, unsigned x, unsigned y) {
        const size_t i = (size_t(y) * img.width + x) * 4u;
        return 0.2126f * img.rgba[i] + 0.7152f * img.rgba[i + 1] + 0.0722f * img.rgba[i + 2];
    };
    for (unsigned y = 0; y < kH; ++y)
        for (unsigned x = 0; x < kW; ++x) {
            const float dx = (float(x) + 0.5f - 0.5f * float(kW)), dy = (0.5f * float(kH) - float(y) - 0.5f);
            if (dx * dx + dy * dy > rDisk * rDisk) continue;
            // the sphere's surface point under the pixel (the ray from the camera)
            const float vx = dx / pxPerRad, vy = dy / pxPerRad;
            const float l = std::sqrt(vx * vx + vy * vy + 1.0f);
            const Vec3 dir(vx / l, vy / l, -1.0f / l);
            const float b = cam.x * dir.x + cam.y * dir.y + cam.z * dir.z;
            const float c = cam.x * cam.x + cam.y * cam.y + cam.z * cam.z - 1.0f;
            const float disc = b * b - c;
            if (disc <= 0.0f) continue;
            const float t = -b - std::sqrt(disc);
            const Vec3 n(cam.x + dir.x * t, cam.y + dir.y * t, cam.z + dir.z * t);
            if (n.x * toSun.x + n.y * toSun.y + n.z * toSun.z < 0.2f) continue;
            ++lit;
            const float a = luma(off, x, y);
            if (luma(on, x, y) < a - 1.0f) ++acneOn;
            if (luma(half, x, y) < a - 1.0f) ++acneHalf;
            if (luma(noCut, x, y) < a - 1.0f) ++acneNoCut;
        }
    std::printf("   the lit hemisphere: %d px; darker by more than a code with the job on %d, with its drawn term"
                " halved %d, with it off %d\n", lit, acneOn, acneHalf, acneNoCut);
    CHECK(lit >= 500, "the lit hemisphere is in view (%d px)", lit);
    CHECK(acneOn == 0, "the contact job shadows no lit pixel of the convex sphere by itself (%d)", acneOn);
    CHECK(acneHalf > 0, "THE FIRST CONTROL is red: HALF the law's drawn term starts the facets' middles' rays inside "
          "level 0 (%d px)", acneHalf);
    CHECK(acneNoCut > 0, "THE SECOND CONTROL is red: the float bound alone starts the rays inside level 0 (%d px)",
          acneNoCut);

    std::printf("%s\n", failures ? "gi.sun_contact_acne: FAIL" : "gi.sun_contact_acne: PASS");
    return failures ? 1 : 0;
}
