// gi.glass_escape (PHOTON-7 10-4, the R4 carry, the lead's design (D); L9: THE RENDERER FOLLOWS THE RAY) — A BLENDED PANE'S
// REFLECTION OF WHAT LIES BEYOND THE VOXEL CHAIN IS WHAT IS THERE, NEVER THE SKY. A black world (no sky, no light) holds
// one emissive wall (radiance 1, albedo 0) 120 m from the camera - past the outermost cascade's 60 m half-size, inside the
// near copies' length - and a 1 x 1 m metal mirror pane (albedo 1, metalness 1, roughness 0.05) 3 m in front of the camera,
// turned 45 degrees so the camera's view of it reflects onto the wall. The voxel rays of the pane's lobe read the chain
// to its last shell and ESCAPE (nothing of the wall is in it): before (D) a blended pane lit that share by the environment
// (black here) and showed nothing of the wall; the ray tier's escape record now traces it on the TLAS from where the
// chain's read ended (rq_glass_escape.comp) and adds the wall's light into the pixel.
//
// THE LAW (the ray tier at High; the HDR readback - the linear scene radiance -, over the pane's pixels, 3 px in): the
// REFERENCE R is the same pane OPAQUE, whose reflection the ray tier's traced reflection answers (rq_reflect.comp: the
// TLAS from the surface); for each blended mode at alpha 1 - Blend (Fade: the source factor alpha), Glass (Transparent:
// the source factor one), Refractive (the refractives' pass and list) - the pane's mean equals R's within the reference's
// noise (its two reads one frame apart) plus ONE CODE (the width of the sRGB code R's level falls in). The escape's counts
// are printed (RayQueryStatus::escape*): records traced, what answered them, none dropped, no sample none. ITS RED: the
// test door rq.glassEscapeOff (no record: the open share lit by the environment, as before (D)) - the same arms, each
// printed against R and checked to miss it by more than a code.
// THE RECORD'S WEIGHT (the second part): the wall hidden, a uniform sky (the Sky Light's band 0: radiance 0.5 every way);
// every blend at alpha 0.5 - Blend, Glass, Refractive, Additive - lit with the record (the escape's miss reads the sky)
// equals the same arm lit by the fragment's own environment term (the door), within its noise plus one code: the record's
// weight is what the pixel would have taken of the share (BRDF_EnvMap's albedo, the eye leg's media, the blend's alpha).
// THE BRIGHT SKY (the one read's F11, a law): a sky of 2000 - the record's picture equals the door's (a miss is the
// fragment's own environment term, never clamped), and a sky of 10^6 saturates the pixel's fixed-point sum, counted.
// THE HIDDEN PANE (a law): the pane behind an opaque box - the depth test runs before an appending fragment, so a hidden
// pane appends no record: the picture equals the door's within one code, no record counted.
// THE NAMED LIMIT, MEASURED (not a law: the review list's stacked glass): a second, plain glass pane (alpha 0.5, black)
// between the camera and the mirror pane at Glass alpha 0.5 - the deferred add lands ON TOP of the front pane, so the
// mirror's traced light is not passed by the front pane's 1 - alpha: printed is the two-pane mean against the exact
// (1 - 0.5) x the one-pane mean, and the over-count against the front pane's alpha x the traced light.
// A fixture (the engine alone, one small offscreen view). Counted in frames.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
using namespace jahshaka::engine;

namespace {
int g_fail = 0;
void check(bool ok, const char *what)
{
    std::printf("%s %s\n", ok ? "  ok  " : "  FAIL", what);
    if (!ok) ++g_fail;
}
NodeId box(Scene *s, const Vec3 &at, const Vec3 &size, MaterialId mat, const Quat &rot = Quat())
{
    const NodeId node = s->createNode();
    MeshData d = enginetest::unitCubeMesh();
    d.cards = enginetest::boxCards(0.5f);
    const MeshId mesh = s->createMesh(d);
    if (!node || !mesh || !mat || !s->attachMesh(node, mesh, mat)) return 0;
    enginetest::NodePose pose;
    pose.pos = at;
    pose.rot = rot;
    pose.scale = size;
    enginetest::poseRegistry()[s][node] = pose;
    s->setNodeTransform(node, at, rot, size);
    return node;
}
// ONE CODE AT A LEVEL: the width of the display's 8-bit sRGB code linear radiance L falls in
double srgbEncode(double L) { return L <= 0.0031308 ? 12.92 * L : 1.055 * std::pow(L, 1.0 / 2.4) - 0.055; }
double srgbDecode(double v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); }
double oneCodeAt(double L)
{
    const double code = std::min(std::floor(srgbEncode(std::max(L, 0.0)) * 255.0), 254.0);
    return srgbDecode((code + 1.0) / 255.0) - srgbDecode(code / 255.0);
}
}   // namespace

int main()
{
    std::string err;
    auto engine = Engine::create(enginetest::testEngineConfig("test-gi-glass-escape-ogre.log"), err);
    if (!engine) { std::printf("FAIL: engine: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    e->setRayTracing(true);
    int frames = 1;
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));
    const auto run = [&](int n) { enginetest::frames(e, n); frames += n; };
    View *view = e->createOffscreenView("glass_escape", 256, 256, Colour(0, 0, 0), OffscreenContract::StillPicture);
    Scene *scene = e->createScene("glass_escape");
    if (!view || !scene) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(scene);
    {
        PostFxDesc fx;
        fx.allowOffscreen = true;
        fx.ssr = 1;
        fx.hdrReadback = true;
        fx.refractions = true;   // (the refractives' pass and its list)
        view->setPostFx(fx);
    }
    // THE WALL: emissive 1, albedo 0, 120 m out along +x (the outermost cascade's half-size is 60 m at High), 300 m square
    const float wallX = 120.0f;
    PbrParams wallP;
    wallP.albedo = Colour(0.0f, 0.0f, 0.0f);
    wallP.roughness = 1.0f;
    wallP.emissive = Colour(1.0f, 1.0f, 1.0f);
    const MaterialId wallMat = scene->createPbrMaterial(wallP);
    const NodeId wall = box(scene, Vec3(wallX + 1.0f, 1.5f, -3.0f), Vec3(2.0f, 300.0f, 300.0f), wallMat);
    if (!wall) {
        std::printf("FAIL: the wall\n");
        return 1;
    }
    // THE MIRROR PANE, 3 m in front of the camera, turned 45 degrees about y: its front normal (1, 0, 1)/sqrt 2 sends the
    // camera's -z view onto +x
    PbrParams paneP;
    paneP.albedo = Colour(1.0f, 1.0f, 1.0f);
    paneP.metalness = 1.0f;
    paneP.roughness = 0.05f;
    const MaterialId paneMat = scene->createPbrMaterial(paneP);
    // (a quarter turn halved: the quaternion of 45 degrees about y turns the pane's +z face onto (1, 0, 1)/sqrt 2)
    const float paneZ = -3.0f, paneH = 0.5f;
    const NodeId pane = box(scene, Vec3(0.0f, 1.5f, paneZ), Vec3(2.0f * paneH, 2.0f * paneH, 0.02f), paneMat,
                            Quat(0.0f, std::sin(0.125f * 3.14159265f), 0.0f, std::cos(0.125f * 3.14159265f)));
    if (!pane) { std::printf("FAIL: the pane\n"); return 1; }
    // THE FRONT PANE of the limit's arm (hidden until then): plain glass, black, alpha 0.5, 1.5 m in front of the camera
    PbrParams frontP;
    frontP.albedo = Colour(0.0f, 0.0f, 0.0f);
    frontP.roughness = 0.5f;
    frontP.alphaMode = PbrAlphaMode::Glass;
    frontP.alpha = 0.5f;
    const MaterialId frontMat = scene->createPbrMaterial(frontP);
    const NodeId front = box(scene, Vec3(0.0f, 1.5f, -1.5f), Vec3(1.2f, 1.2f, 0.02f), frontMat);
    if (!front) { std::printf("FAIL: the front pane\n"); return 1; }
    scene->setNodeVisible(front, false);
    // THE OCCLUDER of the depth arm (hidden until then): an opaque black matte box between the camera and the pane
    PbrParams occP;
    occP.albedo = Colour(0.0f, 0.0f, 0.0f);
    occP.roughness = 1.0f;
    const MaterialId occMat = scene->createPbrMaterial(occP);
    const NodeId occluder = box(scene, Vec3(0.0f, 1.5f, -2.0f), Vec3(1.2f, 1.2f, 0.05f), occMat);
    if (!occluder) { std::printf("FAIL: the occluder\n"); return 1; }
    scene->setNodeVisible(occluder, false);
    float sh[27] = { 0 };   // no sky
    scene->setAmbientSh(sh);
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.ddgi = GiToggle::On;
    gi.cards = true;
    gi.bounces = true;
    gi.updateBudget = 0;
    if (!scene->setGlobalIllumination(gi)) { std::printf("FAIL: the GI builds\n"); return 1; }
    const Vec3 eye(0.0f, 1.5f, 0.0f), at(0.0f, 1.5f, paneZ);
    enginetest::testCameraLookAt(view, eye, at);
    e->waitForAsyncShaders();

    // THE REST (gi.glass_pass's): the chain owes nothing, the cache relit nothing, for 30 frames in a row
    const auto settle = [&]() {
        unsigned long long relights = ~0ull, admitted = ~0ull;
        int quiet = 0, f = 0;
        for (; f < 900 && quiet < 30; ++f) {
            run(1);
            const GiStatus g = scene->published().gi;
            const bool still = !g.chainOwed && !g.feedbackOwed && !g.ifdWorking && g.cards.built &&
                               g.cards.relights == relights && g.cards.admitted == admitted && g.cards.queueLength == 0u;
            relights = g.cards.relights;
            admitted = g.cards.admitted;
            quiet = still ? quiet + 1 : 0;
        }
        return quiet >= 30 ? f - quiet : -1;
    };
    // the pane's pixels: its projected outline (the 45-degree turn narrows it in x), a 0.3 m central square, 3 px in
    const auto rect = [&](unsigned W, unsigned H, int &x0, int &x1, int &y0, int &y1) {
        const double t = std::tan(0.5 * 45.0 * 3.14159265358979 / 180.0), d = -paneZ;
        const double half = 0.15 / (d * t) * 0.5;   // 0.3 m square, a fraction of the picture
        x0 = int(std::ceil((0.5 - half) * W)) + 3;
        x1 = int(std::floor((0.5 + half) * W)) - 3;
        y0 = int(std::ceil((0.5 - half) * H)) + 3;
        y1 = int(std::floor((0.5 + half) * H)) - 3;
    };
    struct Pic { bool ok = false; std::vector<double> v; double mean = 0.0; };
    const auto read = [&]() {
        Pic p;
        ImageF img;
        if (!view->readPixelsHdr(img) || img.width < 64u || img.height < 64u) return p;
        int x0, x1, y0, y1;
        rect(img.width, img.height, x0, x1, y0, y1);
        double sum = 0.0;
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                const Colour c = img.at(unsigned(x), unsigned(y));
                p.v.push_back((c.r + c.g + c.b) / 3.0);
                sum += p.v.back();
            }
        p.ok = !p.v.empty();
        p.mean = p.ok ? sum / double(p.v.size()) : 0.0;
        return p;
    };
    const auto counts = [&](const char *arm) {
        const RayQueryStatus rq = scene->published().rayQuery;
        std::printf("      %s escapes: %llu records (capacity %llu, dropped %llu); traced %llu - card %llu, cascades %llu, "
                    "direct alone %llu, far copy %llu, environment %llu, back face %llu, no sample %llu\n", arm,
                    rq.escapeRecords, rq.escapeCapacity, rq.escapeDropped, rq.escapeTraced, rq.escapeCard,
                    rq.escapeVoxel, rq.escapeDirect, rq.escapeFar, rq.escapeSky, rq.escapeBack, rq.escapeNoSample);
        return rq;
    };

    // (0) THE REFERENCE: the pane opaque, its reflection the traced reflection's
    const int restRef = settle();
    const Pic ref = read();
    run(1);
    const Pic ref2 = read();
    const double noise = std::fabs(ref2.mean - ref.mean);
    const double code = oneCodeAt(ref.mean);
    std::printf("== (0) OPAQUE (the reference): at rest after %d frames, %zu pixels, mean %.6f; one frame later it moved "
                "%.6f; one code at its level %.6f; the wall at %.0f m (the outermost cascade's half-size 60 m)\n", restRef,
                ref.v.size(), ref.mean, noise, code, double(wallX));
    check(restRef >= 0 && ref.ok && ref.mean > 0.5, "(0) the opaque mirror reflects the emissive wall (the traced reflection)");

    struct Arm { PbrAlphaMode m; const char *name; };
    const Arm arms[3] = { { PbrAlphaMode::Blend, "BLEND (Fade)" },
                          { PbrAlphaMode::Glass, "GLASS (Transparent)" },
                          { PbrAlphaMode::Refractive, "REFRACTIVE (the refractives' list)" } };
    for (const Arm &a : arms) {
        PbrParams p = paneP;
        p.alphaMode = a.m;
        p.alpha = 1.0f;
        if (!scene->setPbrMaterial(paneMat, p)) { check(false, "the arm's material"); continue; }
        e->waitForAsyncShaders();
        const int rest = settle();
        const Pic pic = read();
        const RayQueryStatus rq = counts(a.name);
        const double d = pic.mean - ref.mean;
        std::printf("   %s at alpha 1: at rest after %d frames; the pane's mean %.6f against the reference's %.6f (%+.6f = "
                    "%+.2f codes)\n", a.name, rest, pic.mean, ref.mean, d, code > 0.0 ? d / code : -1.0);
        char what[320];
        std::snprintf(what, sizeof(what), "%s: the blended pane's reflection of the wall beyond the chain equals the opaque "
                      "pane's (within its noise plus one code), every record traced and none dropped", a.name);
        check(rest >= 0 && pic.ok && std::fabs(d) <= noise + code && rq.escapeTraced > 0ull && rq.escapeDropped == 0ull &&
                  rq.escapeNoSample == 0ull,
              what);
    }

    // THE NAMED LIMIT, MEASURED: the stacked panes
    {
        PbrParams p = paneP;
        p.alphaMode = PbrAlphaMode::Glass;
        p.alpha = 0.5f;
        scene->setPbrMaterial(paneMat, p);
        e->waitForAsyncShaders();
        settle();
        const Pic one = read();
        scene->setNodeVisible(front, true);
        e->waitForAsyncShaders();
        settle();
        const Pic two = read();
        counts("THE STACKED PANES");
        const double exact = 0.5 * one.mean, over = two.mean - exact;
        std::printf("   THE LIMIT (stacked glass, the review list): the mirror pane at Glass 0.5 alone %.6f; behind a black "
                    "Glass 0.5 pane %.6f against the exact (1 - 0.5) x %.6f = %.6f: over by %+.6f (%.2f codes) - the front "
                    "pane's alpha x the traced light is %.6f at most\n", one.mean, two.mean, one.mean, exact, over,
                    oneCodeAt(exact) > 0.0 ? over / oneCodeAt(exact) : -1.0, 0.5 * one.mean);
    }
    // THE DEPTH ARM: the pane (Glass, alpha 1) hidden behind an opaque box - a fragment the depth test rejects appends no
    // record and lights nothing: the picture with the escapes equals the picture without them (the door)
    {
        PbrParams p = paneP;
        p.alphaMode = PbrAlphaMode::Glass;
        p.alpha = 1.0f;
        scene->setPbrMaterial(paneMat, p);
        scene->setNodeVisible(front, false);
        scene->setNodeVisible(occluder, true);
        Pic pic[2];
        RayQueryStatus rq[2];
        for (int door = 0; door < 2; ++door) {
            e->setArm("rq.glassEscapeOff", door ? 1.0 : 0.0);
            e->waitForAsyncShaders();
            settle();
            pic[door] = read();
            rq[door] = counts(door ? "THE HIDDEN PANE (the door)" : "THE HIDDEN PANE");
        }
        e->setArm("rq.glassEscapeOff", 0.0);
        scene->setNodeVisible(occluder, false);
        const double d = pic[0].mean - pic[1].mean, c = oneCodeAt(std::max(pic[1].mean, 0.0));
        std::printf("   THE HIDDEN PANE: behind an opaque box, the picture %.6f against the door's %.6f (%+.2f codes); %llu "
                    "records\n", pic[0].mean, pic[1].mean, c > 0.0 ? d / c : -1.0, rq[0].escapeRecords);
        check(pic[0].ok && pic[1].ok && std::fabs(d) <= c && rq[0].escapeRecords == 0ull,
              "the hidden pane: a fragment behind an opaque surface appends no record and lights nothing");
    }

    // THE RED (the door): the same blended arms with no record
    e->setArm("rq.glassEscapeOff", 1.0);
    for (const Arm &a : arms) {
        PbrParams p = paneP;
        p.alphaMode = a.m;
        p.alpha = 1.0f;
        scene->setPbrMaterial(paneMat, p);
        e->waitForAsyncShaders();
        settle();
        const Pic pic = read();
        const double d = pic.mean - ref.mean;
        std::printf("   THE RED (rq.glassEscapeOff) %s at alpha 1: the pane's mean %.6f against the reference's %.6f (%+.2f "
                    "codes)\n", a.name, pic.mean, ref.mean, code > 0.0 ? d / code : -1.0);
        char what[256];
        std::snprintf(what, sizeof(what), "the red: %s untraced misses the wall (more than a code off)", a.name);
        check(pic.ok && std::fabs(d) > noise + code, what);
    }
    e->setArm("rq.glassEscapeOff", 0.0);

    // THE RECORD'S WEIGHT: the wall hidden, a uniform sky; each blend at alpha 0.5, the record against the door
    scene->setNodeVisible(front, false);
    scene->setNodeVisible(wall, false);
    {
        float skySh[27] = { 0 };
        skySh[0] = skySh[1] = skySh[2] = 0.5f;   // band 0: radiance 0.5 in every direction
        scene->setAmbientSh(skySh);
    }
    const Arm halves[4] = { { PbrAlphaMode::Blend, "BLEND (Fade)" },
                            { PbrAlphaMode::Glass, "GLASS (Transparent)" },
                            { PbrAlphaMode::Refractive, "REFRACTIVE" },
                            { PbrAlphaMode::Additive, "ADDITIVE" } };
    for (const Arm &a : halves) {
        PbrParams p = paneP;
        p.alphaMode = a.m;
        p.alpha = 0.5f;
        scene->setPbrMaterial(paneMat, p);
        Pic pic[2];
        double moved[2] = { 0.0, 0.0 };
        for (int door = 1; door >= 0; --door) {
            e->setArm("rq.glassEscapeOff", door ? 1.0 : 0.0);
            e->waitForAsyncShaders();
            settle();
            pic[door] = read();
            run(1);
            moved[door] = std::fabs(read().mean - pic[door].mean);
        }
        e->setArm("rq.glassEscapeOff", 0.0);
        const RayQueryStatus rq = counts(a.name);
        const double d = pic[0].mean - pic[1].mean, c = oneCodeAt(pic[1].mean), n = std::max(moved[0], moved[1]);
        std::printf("   THE WEIGHT, %s at alpha 0.5 under the sky: the record %.6f against the fragment's environment "
                    "term %.6f (%+.6f = %+.2f codes; one code %.6f, the noise %.6f)\n", a.name, pic[0].mean, pic[1].mean,
                    d, c > 0.0 ? d / c : -1.0, c, n);
        char what[256];
        std::snprintf(what, sizeof(what), "%s: the record's weight is the pixel's own take of the open share (the "
                      "record's picture within one code of the environment term's)", a.name);
        check(pic[0].ok && pic[1].ok && pic[1].mean > 0.0 && std::fabs(d) <= n + c && rq.escapeTraced > 0ull, what);
    }
    // THE BRIGHT SKY (the one read's F11): a miss is the environment the fragment itself takes, unbounded as its own term
    // is - the escape used to clamp it at the reflections' radiance bound (1024), so a sun glint in glass read differently
    // with the escape. A sky of radiance 2000 (band 0), Glass at alpha 0.5: the record's picture equals the door's (the
    // fragment's own environment term) within its noise plus one code plus the record's weight as halves (2^-10 of the
    // level: an 11-bit mantissa); no add saturated - the sky past the bound (1024) the old clamp cut it to, whatever the
    // pixel's weight. Then a sky of 10^6: the pixel's sum (its weight 0.127 x 10^6) passes the fixed point's top (65536 a
    // channel - past the colour target's half-float range too) and SATURATES, counted, never wrapping.
    {
        PbrParams p = paneP;
        p.alphaMode = PbrAlphaMode::Glass;
        p.alpha = 0.5f;
        scene->setPbrMaterial(paneMat, p);
        const auto sky = [&](float L) {
            float skySh[27] = { 0 };
            skySh[0] = skySh[1] = skySh[2] = L;
            scene->setAmbientSh(skySh);
        };
        sky(2000.0f);
        Pic pic[2];
        double moved[2] = { 0.0, 0.0 };
        RayQueryStatus rq;
        for (int door = 1; door >= 0; --door) {
            e->setArm("rq.glassEscapeOff", door ? 1.0 : 0.0);
            e->waitForAsyncShaders();
            settle();
            pic[door] = read();
            if (!door) rq = counts("THE BRIGHT SKY");
            run(1);
            moved[door] = std::fabs(read().mean - pic[door].mean);
        }
        const double d = pic[0].mean - pic[1].mean, c = oneCodeAt(pic[1].mean) + pic[1].mean / 1024.0;
        const double n = std::max(moved[0], moved[1]);
        std::printf("   THE BRIGHT SKY (2000), GLASS at alpha 0.5: the record %.6f against the fragment's environment term "
                    "%.6f (%+.6f; the bar %.6f, the noise %.6f); %llu adds saturated\n", pic[0].mean, pic[1].mean, d, c, n,
                    rq.escapeSaturated);
        check(pic[0].ok && pic[1].ok && pic[1].mean > 0.0 && std::fabs(d) <= n + c && rq.escapeTraced > 0ull &&
                  rq.escapeSaturated == 0ull,
              "THE BRIGHT SKY: a miss reads the fragment's own environment term, unbounded (F11)");
        sky(1.0e6f);
        settle();
        const RayQueryStatus top = counts("THE SKY PAST THE TOP");
        std::printf("   THE SKY PAST THE TOP (1e6): %llu adds saturated\n", top.escapeSaturated);
        check(top.escapeTraced > 0ull && top.escapeSaturated > 0ull,
              "THE SKY PAST THE TOP: a pixel's sum past its fixed point's top saturates, counted (F11)");
        sky(0.5f);
    }
    std::printf("\n%s (%d failures, %d frames)\n", g_fail ? "FAIL" : "PASS", g_fail, frames);
    return g_fail ? 1 : 0;
}
