// DDGI SPIKE (GI_UNIFIED_SPEC.md §4 P0) — THROWAWAY. Not a ctest case: the
// findings, not a pass/fail, are the deliverable.
//
// Question: does the pin's Ogre::IrradianceField (DDGI, VCT-fed) run correctly
// on Vulkan at 52d1a7aaf + our patch stack? Nobody has ever run this class
// outside two upstream samples.
//
// Scene: gi.modes' room, closed enough for a DDGI probe grid to mean something
// — a white floor, a bright red wall, a blue wall on the left, a ceiling, and a
// near-horizontal directional light that lights the red wall and only grazes
// the floor. The floor probe reads the red bounce; the shadowed-corner probe
// reads what DDGI is supposed to be BETTER at than VCT (smooth, leak-resistant
// diffuse in an unlit corner).
//
// Sub-experiments are selected by argv[1] so each runs in its own process (the
// process-wide HlmsPbs binding rule the gi suites already live by):
//   ab          pixel A/B VCT-only vs VCT+IFD, + convergence curve + costs
//   ppf <n>     one probesPerFrame value, conforming or not (trap 4a)
//   ambient     sky-SH ambient double-count probe (trap 4b)
//   teardown    lifecycle/rebind ordering
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static void render(Engine *e, int frames = 3)
{
    for (int i = 0; i < frames; ++i) e->renderOneFrame();
}

static void show(const char *what, const Colour &c)
{
    std::printf("   %-42s r=%.4f g=%.4f b=%.4f\n", what, c.r, c.g, c.b);
}

static void writePpm(const Image &img, const std::string &path)
{
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%u %u\n255\n", img.width, img.height);
    for (size_t i = 0; i < size_t(img.width) * img.height; ++i)
        std::fwrite(&img.rgba[i * 4], 1, 3, f);
    std::fclose(f);
    std::printf("   wrote %s\n", path.c_str());
}

// The room. Returns the light node so callers can move it.
struct Room {
    NodeId floor = 0, redWall = 0, blueWall = 0, ceiling = 0, light = 0, box = 0;
};

static Room buildRoom(Scene *s)
{
    Room r;
    r.floor = enginetest::addTestCube(s, Colour(1.0f, 1.0f, 1.0f), 0.0f, 0.9f);
    enginetest::setNodePosition(s, r.floor, Vec3(0.0f, -0.05f, 0.0f));
    enginetest::setNodeScale(s, r.floor, Vec3(14.0f, 0.1f, 14.0f));

    r.redWall = enginetest::addTestCube(s, Colour(1.0f, 0.05f, 0.05f), 0.0f, 0.9f);
    enginetest::setNodePosition(s, r.redWall, Vec3(0.0f, 3.0f, -3.0f));
    enginetest::setNodeScale(s, r.redWall, Vec3(12.0f, 6.0f, 0.9f));

    // Left wall, strongly blue, in shadow: the DDGI "smooth diffuse in a dark
    // corner" claim is measured against it.
    r.blueWall = enginetest::addTestCube(s, Colour(0.05f, 0.05f, 1.0f), 0.0f, 0.9f);
    enginetest::setNodePosition(s, r.blueWall, Vec3(-5.5f, 3.0f, 1.0f));
    enginetest::setNodeScale(s, r.blueWall, Vec3(0.9f, 6.0f, 9.0f));

    // Ceiling: closes the room so probes have visibility to resolve.
    r.ceiling = enginetest::addTestCube(s, Colour(0.8f, 0.8f, 0.8f), 0.0f, 0.9f);
    enginetest::setNodePosition(s, r.ceiling, Vec3(0.0f, 6.2f, 0.0f));
    enginetest::setNodeScale(s, r.ceiling, Vec3(14.0f, 0.2f, 14.0f));

    // A neutral box standing on the floor near the blue wall: its shadowed
    // side is the classic DDGI probe.
    r.box = enginetest::addTestCube(s, Colour(0.75f, 0.75f, 0.75f), 0.0f, 0.85f);
    enginetest::setNodePosition(s, r.box, Vec3(-2.5f, 0.9f, 1.0f));
    enginetest::setNodeScale(s, r.box, Vec3(1.8f, 1.8f, 1.8f));

    r.light = s->createNode();
    const float half = 40.0f * 3.14159265f / 180.0f;   // 80 degrees about X
    const Quat atWall(std::sin(half), 0.0f, 0.0f, std::cos(half));
    s->setNodeTransform(r.light, Vec3(0, 6, 6), atWall, Vec3(1, 1, 1));
    LightDesc light;
    light.type = LightType::Directional;
    light.colour = Colour(1, 1, 1);
    light.intensity = 2.0f;
    light.castShadows = false;
    s->setLight(r.light, light);
    return r;
}

static GiParams vctParams()
{
    GiParams vct;
    vct.mode = GiMode::Vct;
    vct.quality = GiQuality::Medium;    // 64^3
    vct.numBounces = 2;
    vct.boundsMin = Vec3(-9.0f, -1.5f, -9.0f);
    vct.boundsMax = Vec3(9.0f, 7.5f, 9.0f);
    return vct;
}

static void dumpStatus(const DdgiSpikeStatus &st)
{
    std::printf("   IFD: built=%d bound=%d probes=%d processed=%d converged=%d\n",
                st.built, st.bound, st.totalProbes, st.probesProcessed, st.converged);
    std::printf("   IFD: raysPerIrradPixel=%d threadsPerGroup=%d\n",
                st.numRaysPerIrradiancePixel, st.threadsPerGroup);
    std::printf("   IFD: irradiance %ux%u = %.2f MB   depth %ux%u = %.2f MB   total %.2f MB\n",
                st.irradW, st.irradH, st.irradBytes / 1048576.0,
                st.depthW, st.depthH, st.depthBytes / 1048576.0,
                (st.irradBytes + st.depthBytes) / 1048576.0);
    std::printf("   IFD: field origin (%.2f %.2f %.2f) size (%.2f %.2f %.2f)\n",
                st.fieldOrigin.x, st.fieldOrigin.y, st.fieldOrigin.z,
                st.fieldSize.x, st.fieldSize.y, st.fieldSize.z);
}

// ---------------------------------------------------------------------------

static int runAb(Engine *engine, View *view, Scene *s, const Room &room, int ppf,
                 const std::string &tag)
{
    Image img;
    // Probes. 128x128 view: the floor patch the red bounce lands on, the
    // shadowed side of the box, and the dark floor corner by the blue wall.
    const unsigned floorX = 64, floorY = 96;
    const unsigned cornerX = 20, cornerY = 100;
    const unsigned boxX = 34, boxY = 78;

    engine->setFixedFrameDelta(1.0f / 60.0f);

    render(engine, 4);
    view->readPixels(img);
    const Colour noGiFloor = img.at(floorX, floorY);
    const Colour noGiCorner = img.at(cornerX, cornerY);
    const Colour noGiBox = img.at(boxX, boxY);
    std::printf("-- no GI --\n");
    show("floor", noGiFloor); show("dark corner", noGiCorner); show("box shadow side", noGiBox);
    writePpm(img, tag + "-0-nogi.ppm");

    // ---- VCT only ----------------------------------------------------------
    if (!s->setGlobalIllumination(vctParams())) {
        std::printf("FAIL: VCT enable: %s\n", engine->lastError().c_str());
        return 1;
    }
    render(engine, 4);
    view->readPixels(img);
    const Colour vctFloor = img.at(floorX, floorY);
    const Colour vctCorner = img.at(cornerX, cornerY);
    const Colour vctBox = img.at(boxX, boxY);
    std::printf("-- VCT only --\n");
    show("floor", vctFloor); show("dark corner", vctCorner); show("box shadow side", vctBox);
    writePpm(img, tag + "-1-vct.ppm");

    // ---- build the irradiance field ---------------------------------------
    DdgiSpikeParams dp;   // upstream defaults: 32x8x32, depth 12, irrad 6, 1 ray
    auto t0 = std::chrono::steady_clock::now();
    if (!s->ddgiSpikeBuild(dp)) {
        std::printf("FAIL: ddgiSpikeBuild: %s\n", engine->lastError().c_str());
        return 1;
    }
    auto t1 = std::chrono::steady_clock::now();
    std::printf("-- IrradianceField built in %.1f ms --\n",
                std::chrono::duration<double, std::milli>(t1 - t0).count());
    dumpStatus(s->ddgiSpikeStatus());

    // Converge WITHOUT binding first: isolates update() cost from shading cost.
    const DdgiSpikeStatus st0 = s->ddgiSpikeStatus();
    const int expectedFrames = (st0.totalProbes + ppf - 1) / ppf;
    std::printf("-- converging at probesPerFrame=%d, expect %d frames --\n", ppf, expectedFrames);
    std::vector<double> updateMs;
    int frames = 0;
    while (!s->ddgiSpikeStatus().converged && frames < expectedFrames + 8) {
        auto a = std::chrono::steady_clock::now();
        s->ddgiSpikeUpdate(ppf);
        engine->renderOneFrame();          // the compute workspace runs inside
        auto b = std::chrono::steady_clock::now();
        updateMs.push_back(std::chrono::duration<double, std::milli>(b - a).count());
        ++frames;
    }
    double sum = 0, worst = 0;
    for (double m : updateMs) { sum += m; if (m > worst) worst = m; }
    std::printf("   converged after %d frames (expected %d); update+frame mean %.2f ms, worst %.2f ms\n",
                frames, expectedFrames, updateMs.empty() ? 0.0 : sum / updateMs.size(), worst);

    // Steady-state: update() on a converged field must return immediately.
    {
        auto a = std::chrono::steady_clock::now();
        for (int i = 0; i < 10; ++i) { s->ddgiSpikeUpdate(ppf); engine->renderOneFrame(); }
        auto b = std::chrono::steady_clock::now();
        std::printf("   converged steady state: %.2f ms per frame (10 frames, IFD unbound)\n",
                    std::chrono::duration<double, std::milli>(b - a).count() / 10.0);
    }

    // Unbound field must not have changed the image at all.
    view->readPixels(img);
    const Colour unboundFloor = img.at(floorX, floorY);
    std::printf("-- field converged, still UNBOUND --\n");
    show("floor", unboundFloor);
    std::printf("   %s\n", std::fabs(unboundFloor.r - vctFloor.r) < 0.004f
                ? "ok: an unbound field changes nothing"
                : "NOTE: unbound field changed the image (unexpected)");

    // ---- bind ---------------------------------------------------------------
    s->ddgiSpikeBind(true);
    render(engine, 4);
    view->readPixels(img);
    const Colour ifdFloor = img.at(floorX, floorY);
    const Colour ifdCorner = img.at(cornerX, cornerY);
    const Colour ifdBox = img.at(boxX, boxY);
    std::printf("-- VCT + IFD (bound) --\n");
    show("floor", ifdFloor); show("dark corner", ifdCorner); show("box shadow side", ifdBox);
    dumpStatus(s->ddgiSpikeStatus());
    writePpm(img, tag + "-2-vct-ifd.ppm");

    std::printf("-- deltas (IFD - VCT) --\n");
    std::printf("   floor       dr=%+.4f dg=%+.4f db=%+.4f\n",
                ifdFloor.r - vctFloor.r, ifdFloor.g - vctFloor.g, ifdFloor.b - vctFloor.b);
    std::printf("   dark corner dr=%+.4f dg=%+.4f db=%+.4f\n",
                ifdCorner.r - vctCorner.r, ifdCorner.g - vctCorner.g, ifdCorner.b - vctCorner.b);
    std::printf("   box shadow  dr=%+.4f dg=%+.4f db=%+.4f\n",
                ifdBox.r - vctBox.r, ifdBox.g - vctBox.g, ifdBox.b - vctBox.b);

    // Whole-frame difference so "the IFD contribution is visible" is not one
    // cherry-picked pixel.
    {
        Image vctImg;
        s->ddgiSpikeBind(false);
        render(engine, 3);
        view->readPixels(vctImg);
        s->ddgiSpikeBind(true);
        render(engine, 3);
        view->readPixels(img);
        long diffPixels = 0; long sumAbs = 0; int maxAbs = 0;
        for (size_t i = 0; i < img.rgba.size(); i += 4) {
            int d = 0;
            for (int c = 0; c < 3; ++c)
                d = std::max(d, std::abs(int(img.rgba[i + c]) - int(vctImg.rgba[i + c])));
            if (d > 1) ++diffPixels;
            sumAbs += d;
            if (d > maxAbs) maxAbs = d;
        }
        const long total = long(img.width) * img.height;
        std::printf("-- whole-frame IFD contribution --\n");
        std::printf("   %ld/%ld pixels differ by >1/255 (%.1f%%), mean |delta| %.2f, max %d\n",
                    diffPixels, total, 100.0 * diffPixels / total, double(sumAbs) / total, maxAbs);
        writePpm(vctImg, tag + "-3-vct-again.ppm");
    }

    // ---- rebind A/B determinism -------------------------------------------
    {
        Image a, b;
        s->ddgiSpikeBind(true); render(engine, 3); view->readPixels(a);
        s->ddgiSpikeBind(false); render(engine, 3);
        s->ddgiSpikeBind(true); render(engine, 3); view->readPixels(b);
        std::printf("   rebind reproducibility: %s\n",
                    a.rgba == b.rgba ? "byte-identical" : "DIFFERS");
    }

    // ---- reset() re-arms ----------------------------------------------------
    {
        s->ddgiSpikeReset();
        const DdgiSpikeStatus st = s->ddgiSpikeStatus();
        std::printf("   after reset(): processed=%d converged=%d\n",
                    st.probesProcessed, st.converged);
        int f = 0;
        while (!s->ddgiSpikeStatus().converged && f < expectedFrames + 8) {
            s->ddgiSpikeUpdate(ppf); engine->renderOneFrame(); ++f;
        }
        view->readPixels(img);
        const Colour reFloor = img.at(floorX, floorY);
        std::printf("   re-converged in %d frames; floor r=%.4f (was %.4f) -> %s\n",
                    f, reFloor.r, ifdFloor.r,
                    std::fabs(reFloor.r - ifdFloor.r) < 0.006f ? "same" : "DIFFERS");
    }

    // ---- cost of a bound, converged field on the shading side --------------
    {
        s->ddgiSpikeBind(false);
        render(engine, 5);
        auto a = std::chrono::steady_clock::now();
        for (int i = 0; i < 60; ++i) engine->renderOneFrame();
        auto b = std::chrono::steady_clock::now();
        const double vctOnly = std::chrono::duration<double, std::milli>(b - a).count() / 60.0;
        s->ddgiSpikeBind(true);
        render(engine, 5);
        a = std::chrono::steady_clock::now();
        for (int i = 0; i < 60; ++i) engine->renderOneFrame();
        b = std::chrono::steady_clock::now();
        const double withIfd = std::chrono::duration<double, std::milli>(b - a).count() / 60.0;
        std::printf("-- shading cost, converged (128x128 offscreen, Debug) --\n");
        std::printf("   VCT only %.3f ms/frame; VCT+IFD bound %.3f ms/frame (delta %+.3f)\n",
                    vctOnly, withIfd, withIfd - vctOnly);
    }

    // ---- probesPerFrame cost sweep (conforming values only) ----------------
    std::printf("-- update() cost by probesPerFrame (conforming multiples) --\n");
    for (int v : { 8, 32, 64, 200, 512, 1024, 2048 }) {
        s->ddgiSpikeReset();
        // Warm one call, then time 8.
        s->ddgiSpikeUpdate(v); engine->renderOneFrame();
        auto a = std::chrono::steady_clock::now();
        for (int i = 0; i < 8; ++i) { s->ddgiSpikeUpdate(v); engine->renderOneFrame(); }
        auto b = std::chrono::steady_clock::now();
        const double ms = std::chrono::duration<double, std::milli>(b - a).count() / 8.0;
        std::printf("   ppf=%-5d %6.3f ms/frame  (%d frames to converge => %.1f ms total)\n",
                    v, ms, (8192 + v - 1) / v, ms * ((8192 + v - 1) / v));
    }

    // Leave converged + bound for the caller.
    s->ddgiSpikeReset();
    while (!s->ddgiSpikeStatus().converged) { s->ddgiSpikeUpdate(ppf); engine->renderOneFrame(); }
    s->ddgiSpikeBind(true);
    render(engine, 3);
    view->readPixels(img);
    writePpm(img, tag + "-4-final.ppm");
    return 0;
}

// ---------------------------------------------------------------------------

static int runPpf(Engine *engine, View *view, Scene *s, int ppf, const std::string &tag)
{
    Image img;
    engine->setFixedFrameDelta(1.0f / 60.0f);
    if (!s->setGlobalIllumination(vctParams())) {
        std::printf("FAIL: VCT enable: %s\n", engine->lastError().c_str());
        return 1;
    }
    render(engine, 4);
    DdgiSpikeParams dp;
    if (!s->ddgiSpikeBuild(dp)) {
        std::printf("FAIL: ddgiSpikeBuild: %s\n", engine->lastError().c_str());
        return 1;
    }
    const DdgiSpikeStatus st = s->ddgiSpikeStatus();
    dumpStatus(st);
    const long numRays = long(ppf) * dp.depthRes * dp.depthRes * dp.raysPerPixel;
    std::printf("-- probesPerFrame=%d: numRays=%ld, threadsPerGroup=%d, numRays%%tpg=%ld => %s --\n",
                ppf, numRays, st.threadsPerGroup, numRays % st.threadsPerGroup,
                (numRays % st.threadsPerGroup) ? "NON-CONFORMING (assert would fire in Debug Ogre)"
                                               : "conforming");
    const int expected = (st.totalProbes + ppf - 1) / ppf;
    int f = 0;
    for (; f < expected; ++f) { s->ddgiSpikeUpdate(ppf); engine->renderOneFrame(); }
    s->ddgiSpikeBind(true);
    render(engine, 4);
    view->readPixels(img);
    writePpm(img, tag + "-ppf" + std::to_string(ppf) + ".ppm");
    const Colour floorC = img.at(64, 96);
    const Colour cornerC = img.at(20, 100);
    show("floor", floorC);
    show("dark corner", cornerC);
    // Look for obviously-broken output: NaN-ish black/white blocks show up as
    // extreme channel spreads across the frame.
    long black = 0, saturated = 0;
    for (size_t i = 0; i < img.rgba.size(); i += 4) {
        const int r = img.rgba[i], g = img.rgba[i + 1], b = img.rgba[i + 2];
        if (r == 0 && g == 0 && b == 0) ++black;
        if (r == 255 && g == 255 && b == 255) ++saturated;
    }
    std::printf("   frame census: %ld pure-black px, %ld pure-white px of %ld\n",
                black, saturated, long(img.width) * img.height);
    return 0;
}

// ---------------------------------------------------------------------------
// The SENSITIVE probe: the set of pixels that receive NO direct light at all
// (the box faces pointing away from the directional light are black with GI
// off). Their colour is 100% indirect, so it measures the IFD's magnitude,
// colour and any dispatch damage without 8-bit quantization hiding it behind a
// bright direct term.
struct PureMask {
    std::vector<size_t> idx;      // byte offsets into rgba
};

static PureMask darkMask(const Image &noGi)
{
    PureMask m;
    for (size_t i = 0; i < noGi.rgba.size(); i += 4)
        if (noGi.rgba[i] <= 2 && noGi.rgba[i + 1] <= 2 && noGi.rgba[i + 2] <= 2)
            m.idx.push_back(i);
    return m;
}

static void maskMean(const Image &img, const PureMask &m, double out[3])
{
    double s[3] = {0, 0, 0};
    for (size_t i : m.idx) for (int c = 0; c < 3; ++c) s[c] += img.rgba[i + c];
    for (int c = 0; c < 3; ++c) out[c] = m.idx.empty() ? 0.0 : s[c] / m.idx.size();
}

static int runPure(Engine *engine, View *view, Scene *s, const Room &room,
                   const std::string &tag)
{
    Image img;
    engine->setFixedFrameDelta(1.0f / 60.0f);
    render(engine, 4);
    view->readPixels(img);
    const PureMask mask = darkMask(img);
    std::printf("-- pure-indirect mask: %zu of %ld pixels are pure black with GI off --\n",
                mask.idx.size(), long(img.width) * img.height);
    if (mask.idx.size() < 50) {
        std::printf("FAIL: mask too small to measure\n");
        return 1;
    }
    double v[3];
    maskMean(img, mask, v);
    std::printf("   no GI                mean(0-255) r=%.3f g=%.3f b=%.3f\n", v[0], v[1], v[2]);

    if (!s->setGlobalIllumination(vctParams())) return 1;
    render(engine, 4);
    view->readPixels(img);
    maskMean(img, mask, v);
    std::printf("   VCT only             mean(0-255) r=%.3f g=%.3f b=%.3f\n", v[0], v[1], v[2]);

    DdgiSpikeParams dp;
    if (!s->ddgiSpikeBuild(dp)) { std::printf("FAIL build: %s\n", engine->lastError().c_str()); return 1; }

    // Sweep probesPerFrame over the SENSITIVE mask. Any dispatch leftover shows
    // up as a systematic dimming here.
    std::printf("-- probesPerFrame sweep, measured on the pure-indirect mask --\n");
    std::printf("   %-6s %-6s %-14s %s\n", "ppf", "lefto", "mean rgb", "frame sha (first 16)");
    for (int v2 : { 200, 512, 8192, 1, 2, 3, 5, 7, 9, 100, 101, 1000 }) {
        // FRESH field per entry (reset() does not clear the atlases).
        s->ddgiSpikeBind(false);
        if (!s->ddgiSpikeBuild(dp)) { std::printf("   build failed\n"); continue; }
        const int total = s->ddgiSpikeStatus().totalProbes;
        const int expected = (total + v2 - 1) / v2;
        try {
            for (int f = 0; f < expected; ++f) { s->ddgiSpikeUpdate(v2); engine->renderOneFrame(); }
        } catch (std::exception &e) {
            std::printf("   %-6d THREW: %s\n", v2, e.what()); std::fflush(stdout); continue;
        }
        s->ddgiSpikeBind(true);
        render(engine, 3);
        view->readPixels(img);
        maskMean(img, mask, v);
        const long numRays = long(v2) * dp.depthRes * dp.depthRes * dp.raysPerPixel;
        const int tpg = s->ddgiSpikeStatus().threadsPerGroup;
        // Cheap frame fingerprint (FNV-1a over the whole frame).
        unsigned long long h = 1469598103934665603ULL;
        for (unsigned char b : img.rgba) { h ^= b; h *= 1099511628211ULL; }
        std::printf("   %-6d %-6ld r=%.3f g=%.3f b=%.3f  %016llx\n",
                    v2, numRays % tpg, v[0], v[1], v[2], h);
    }

    // Response: the indirect term must track the driving light.
    std::printf("-- response of the IFD-only indirect to the driving light --\n");
    for (float intensity : { 1.0f, 2.0f, 4.0f }) {
        LightDesc l;
        l.type = LightType::Directional;
        l.colour = Colour(1, 1, 1);
        l.intensity = intensity;
        l.castShadows = false;
        s->setLight(room.light, l);
        s->refreshGlobalIllumination();       // re-voxelize + re-inject
        s->ddgiSpikeBuild(dp);                // the arm was rebuilt under us
        while (!s->ddgiSpikeStatus().converged) { s->ddgiSpikeUpdate(512); engine->renderOneFrame(); }
        s->ddgiSpikeBind(true);
        render(engine, 4);
        view->readPixels(img);
        maskMean(img, mask, v);
        std::printf("   light intensity %.1f  mean r=%.3f g=%.3f b=%.3f\n", intensity, v[0], v[1], v[2]);
    }
    // ...and the red wall out of the room kills the red bounce.
    enginetest::setNodePosition(s, room.redWall, Vec3(0.0f, 3.0f, 40.0f));
    s->refreshGlobalIllumination();
    s->ddgiSpikeBuild(dp);
    while (!s->ddgiSpikeStatus().converged) { s->ddgiSpikeUpdate(512); engine->renderOneFrame(); }
    s->ddgiSpikeBind(true);
    render(engine, 4);
    view->readPixels(img);
    maskMean(img, mask, v);
    std::printf("   red wall removed      mean r=%.3f g=%.3f b=%.3f\n", v[0], v[1], v[2]);
    writePpm(img, tag + "-pure-nowall.ppm");
    return 0;
}

// ---------------------------------------------------------------------------
// Trap 4a, decisive version. At upstream's default resolutions the leftover is
// at most 127 of ppf*144 rays, which the 8-bit frame can swallow. depth 8 /
// irrad 4 gives numRaysPerIrradiancePixel = 4 -> threadsPerGroup = 128 with
// only 64 rays PER PROBE, so:
//   ppf even  -> numRays % 128 == 0            (conforming)
//   ppf odd   -> 64 rays leftover, i.e. a WHOLE probe of every batch
//   ppf == 1  -> numWorkGroups == 0            (nothing dispatched at all)
// If a whole missing probe per batch still renders identically, the "leftover"
// reading of OgreIrradianceField.cpp:737 is wrong.
static int runLeftover(Engine *engine, View *view, Scene *s, const std::string &tag)
{
    Image img;
    engine->setFixedFrameDelta(1.0f / 60.0f);
    render(engine, 4);
    view->readPixels(img);
    const PureMask mask = darkMask(img);
    std::printf("-- pure-indirect mask: %zu px --\n", mask.idx.size());
    if (!s->setGlobalIllumination(vctParams())) return 1;
    render(engine, 4);

    DdgiSpikeParams dp;
    dp.depthRes = 8; dp.irradRes = 4; dp.raysPerPixel = 1;
    if (!s->ddgiSpikeBuild(dp)) { std::printf("FAIL build: %s\n", engine->lastError().c_str()); return 1; }
    dumpStatus(s->ddgiSpikeStatus());
    const int tpg = s->ddgiSpikeStatus().threadsPerGroup;
    const int total = s->ddgiSpikeStatus().totalProbes;
    std::printf("   rays per probe = %d; threadsPerGroup = %d\n",
                dp.depthRes * dp.depthRes * dp.raysPerPixel, tpg);
    std::printf("   %-6s %-9s %-9s %-24s %s\n", "ppf", "numRays", "leftover", "mean rgb", "frame fnv");
    for (int v2 : { 2, 4, 8, 128, 256, 1, 3, 5, 7, 129, 257 }) {
        const long numRays = long(v2) * dp.depthRes * dp.depthRes * dp.raysPerPixel;
        std::printf("   %-6d %-9ld %-9ld ", v2, numRays, numRays % tpg);
        std::fflush(stdout);
        try {
            // FRESH field per entry: reset() does NOT clear the atlases, so a
            // sweep that only resets inherits the PREVIOUS entry's correct
            // texels and cannot see a dispatch leftover at all (measured).
            s->ddgiSpikeBind(false);
            if (!s->ddgiSpikeBuild(dp)) { std::printf("build failed\n"); continue; }
            const int expected = (total + v2 - 1) / v2;
            for (int f = 0; f < expected; ++f) { s->ddgiSpikeUpdate(v2); engine->renderOneFrame(); }
            s->ddgiSpikeBind(true);
            render(engine, 3);
        } catch (std::exception &e) {
            std::printf("THREW: %s\n", e.what());
            std::fflush(stdout);
            continue;
        }
        view->readPixels(img);
        double v[3]; maskMean(img, mask, v);
        unsigned long long h = 1469598103934665603ULL;
        for (unsigned char b : img.rgba) { h ^= b; h *= 1099511628211ULL; }
        std::printf("r=%6.3f g=%6.3f b=%6.3f   %016llx\n", v[0], v[1], v[2], h);
        std::fflush(stdout);
        if (v2 == 1) writePpm(img, tag + "-leftover-ppf1.ppm");
        if (v2 == 2) writePpm(img, tag + "-leftover-ppf2.ppm");
        if (v2 == 3) writePpm(img, tag + "-leftover-ppf3.ppm");
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Trap 4a, single-shot: ONE process, ONE field ever created, ONE probesPerFrame.
// Both in-process sweeps above turned out to be contaminated — reset() does not
// clear the atlases, AND a destroy+create of identically-sized textures gets the
// SAME VRAM back with the old contents (the engine's known allocation-recycling
// behaviour). Only a fresh process can show what an under-dispatched generation
// pass really leaves behind.
static int runOne(Engine *engine, View *view, Scene *s, int ppf, const std::string &tag)
{
    Image img;
    engine->setFixedFrameDelta(1.0f / 60.0f);
    render(engine, 4);
    view->readPixels(img);
    const PureMask mask = darkMask(img);
    if (!s->setGlobalIllumination(vctParams())) return 1;
    render(engine, 4);
    DdgiSpikeParams dp;
    dp.depthRes = 8; dp.irradRes = 4; dp.raysPerPixel = 1;   // 64 rays/probe, tpg 128
    if (!s->ddgiSpikeBuild(dp)) { std::printf("FAIL build\n"); return 1; }
    const DdgiSpikeStatus st = s->ddgiSpikeStatus();
    const long numRays = long(ppf) * dp.depthRes * dp.depthRes * dp.raysPerPixel;
    std::printf("ONE ppf=%d rays/probe=%d tpg=%d numRays=%ld leftover=%ld workGroups=%ld\n",
                ppf, dp.depthRes * dp.depthRes * dp.raysPerPixel, st.threadsPerGroup,
                numRays, numRays % st.threadsPerGroup, numRays / st.threadsPerGroup);
    const int expected = (st.totalProbes + ppf - 1) / ppf;
    try {
        // CONTROL: stop at half convergence and measure. If a half-converged
        // field renders the same as a fully converged one, the whole "missing
        // probes" reasoning is unmeasurable in this scene and the leftover
        // result below says nothing.
        for (int f = 0; f < expected / 2; ++f) { s->ddgiSpikeUpdate(ppf); engine->renderOneFrame(); }
        s->ddgiSpikeBind(true);
        render(engine, 3);
        view->readPixels(img);
        { double hv[3]; maskMean(img, mask, hv);
          unsigned long long hh = 1469598103934665603ULL;
          for (unsigned char b : img.rgba) { hh ^= b; hh *= 1099511628211ULL; }
          std::printf("ONE HALF   ppf=%d mean r=%.3f g=%.3f b=%.3f fnv=%016llx\n",
                      ppf, hv[0], hv[1], hv[2], hh); }
        s->ddgiSpikeBind(false);
        for (int f = expected / 2; f < expected; ++f) { s->ddgiSpikeUpdate(ppf); engine->renderOneFrame(); }
    } catch (std::exception &e) { std::printf("ONE THREW: %s\n", e.what()); return 0; }
    s->ddgiSpikeBind(true);
    render(engine, 3);
    view->readPixels(img);
    double v[3]; maskMean(img, mask, v);
    unsigned long long h = 1469598103934665603ULL;
    for (unsigned char b : img.rgba) { h ^= b; h *= 1099511628211ULL; }
    std::printf("ONE RESULT ppf=%d mask=%zu mean r=%.3f g=%.3f b=%.3f fnv=%016llx\n",
                ppf, mask.idx.size(), v[0], v[1], v[2], h);
    writePpm(img, tag + "-one" + std::to_string(ppf) + ".ppm");
    return 0;
}

// ---------------------------------------------------------------------------
// Trap 4b: sky-IBL diffuse double-count.
static int runAmbient(Engine *engine, View *view, Scene *s, const std::string &tag)
{
    Image img;
    engine->setFixedFrameDelta(1.0f / 60.0f);
    const unsigned floorX = 64, floorY = 96, cornerX = 20, cornerY = 100;

    // A strongly coloured ambient so any double-count is unmistakable: green
    // upper hemisphere, dim green lower. (The engine's sky-SH path pushes the
    // same ambient pair; setAmbient is the direct handle on it.)
    auto readAll = [&](const char *what) {
        render(engine, 4);
        view->readPixels(img);
        std::printf("-- %s --\n", what);
        show("floor", img.at(floorX, floorY));
        show("dark corner", img.at(cornerX, cornerY));
        return img.at(cornerX, cornerY);
    };

    s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
    const Colour a0 = readAll("ambient OFF, no GI");

    s->setAmbient(Colour(0.0f, 0.35f, 0.0f), Colour(0.0f, 0.10f, 0.0f));
    const Colour a1 = readAll("ambient ON (green), no GI");

    if (!s->setGlobalIllumination(vctParams())) {
        std::printf("FAIL: VCT: %s\n", engine->lastError().c_str());
        return 1;
    }
    const Colour a2 = readAll("ambient ON + VCT");

    DdgiSpikeParams dp;
    if (!s->ddgiSpikeBuild(dp)) {
        std::printf("FAIL: build: %s\n", engine->lastError().c_str());
        return 1;
    }
    while (!s->ddgiSpikeStatus().converged) { s->ddgiSpikeUpdate(512); engine->renderOneFrame(); }
    s->ddgiSpikeBind(true);
    const Colour a3 = readAll("ambient ON + VCT + IFD bound");

    s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
    const Colour a4 = readAll("ambient OFF + VCT + IFD bound");
    writePpm(img, tag + "-ambient-off-ifd.ppm");

    std::printf("-- ambient interaction (dark-corner green channel) --\n");
    std::printf("   no ambient/no GI      g=%.4f\n", a0.g);
    std::printf("   +ambient              g=%.4f  (ambient adds %+.4f)\n", a1.g, a1.g - a0.g);
    std::printf("   +ambient +VCT         g=%.4f\n", a2.g);
    std::printf("   +ambient +VCT +IFD    g=%.4f\n", a3.g);
    std::printf("   no ambient +VCT +IFD  g=%.4f  (IFD-only green)\n", a4.g);
    std::printf("   double-count test: (ambient+VCT+IFD) - (VCT+IFD) = %+.4f "
                "vs ambient's standalone contribution %+.4f\n",
                a3.g - a4.g, a1.g - a0.g);
    return 0;
}

// ---------------------------------------------------------------------------
// Trap 6: lifecycle/rebind ordering.
static int runTeardown(Engine *engine, View *view, Scene *s)
{
    Image img;
    engine->setFixedFrameDelta(1.0f / 60.0f);
    if (!s->setGlobalIllumination(vctParams())) return 1;
    render(engine, 3);

    DdgiSpikeParams dp;
    std::printf("-- build + bind + converge --\n");
    if (!s->ddgiSpikeBuild(dp)) { std::printf("FAIL build: %s\n", engine->lastError().c_str()); return 1; }
    while (!s->ddgiSpikeStatus().converged) { s->ddgiSpikeUpdate(512); engine->renderOneFrame(); }
    s->ddgiSpikeBind(true);
    render(engine, 3);
    std::printf("   ok\n");

    std::printf("-- A: GI off with the field still BOUND (teardownVct must unbind+destroy first) --\n");
    GiParams off;
    if (!s->setGlobalIllumination(off)) { std::printf("FAIL gi off\n"); return 1; }
    render(engine, 3);
    view->readPixels(img);
    std::printf("   survived; ddgiSpikeStatus().built=%d\n", s->ddgiSpikeStatus().built);

    std::printf("-- B: rebuild VCT, rebuild field, then rebuild VCT UNDER the field --\n");
    if (!s->setGlobalIllumination(vctParams())) return 1;
    render(engine, 3);
    if (!s->ddgiSpikeBuild(dp)) { std::printf("FAIL build2: %s\n", engine->lastError().c_str()); return 1; }
    while (!s->ddgiSpikeStatus().converged) { s->ddgiSpikeUpdate(512); engine->renderOneFrame(); }
    s->ddgiSpikeBind(true);
    render(engine, 3);
    // refreshGlobalIllumination tears the VCT arm down and rebuilds it, which
    // must take the field with it (a field holding a dangling VctLighting* is
    // the whole hazard).
    s->refreshGlobalIllumination();
    render(engine, 3);
    std::printf("   after refreshGlobalIllumination: built=%d bound=%d\n",
                s->ddgiSpikeStatus().built, s->ddgiSpikeStatus().bound);

    std::printf("-- C: rebuild the field over the refreshed VCT arm --\n");
    if (!s->ddgiSpikeBuild(dp)) { std::printf("FAIL build3: %s\n", engine->lastError().c_str()); return 1; }
    while (!s->ddgiSpikeStatus().converged) { s->ddgiSpikeUpdate(512); engine->renderOneFrame(); }
    s->ddgiSpikeBind(true);
    render(engine, 3);
    std::printf("   ok; leaving it live for engine destruction\n");
    return 0;
}

// ---------------------------------------------------------------------------

int main(int argc, char **argv)
{
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    const std::string what = argc > 1 ? argv[1] : "ab";
    const int arg2 = argc > 2 ? std::atoi(argv[2]) : 200;
    const std::string tag = argc > 3 ? argv[3] : ("ddgi-" + what);

    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-ddgi-spike-" + what + ".log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }

    View *view = engine->createOffscreenView("ddgi", 128, 128, Colour(0, 0, 0));
    Scene *s = engine->createScene("ddgi");
    view->setScene(s);
    s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
    const Room room = buildRoom(s);
    enginetest::testCameraLookAt(view, Vec3(0.0f, 4.0f, 6.0f), Vec3(0.0f, 0.0f, -0.5f));

    int rc = 0;
    if (what == "ab")            rc = runAb(engine.get(), view, s, room, arg2, tag);
    else if (what == "ppf")      rc = runPpf(engine.get(), view, s, arg2, tag);
    else if (what == "pure")     rc = runPure(engine.get(), view, s, room, tag);
    else if (what == "leftover") rc = runLeftover(engine.get(), view, s, tag);
    else if (what == "one")      rc = runOne(engine.get(), view, s, arg2, tag);
    else if (what == "ambient")  rc = runAmbient(engine.get(), view, s, tag);
    else if (what == "teardown") rc = runTeardown(engine.get(), view, s);
    else { std::printf("unknown experiment '%s'\n", what.c_str()); rc = 2; }

    std::printf("-- destroying engine (field alive: %d) --\n", s->ddgiSpikeStatus().built);
    engine.reset();
    std::printf("-- clean shutdown --\n");
    return rc;
}
