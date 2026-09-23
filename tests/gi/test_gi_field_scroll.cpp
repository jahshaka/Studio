// gi.field_scroll — THE IRRADIANCE FIELD SCROLLS (PHOTON-WRITER-1, P4 items
// FIELD-SCROLL and L11-STEP; B4 design §3 and §4).
//
// WHAT CHANGED. A cascade-0 step used to re-place the field and re-integrate
// ALL of it in the step frame (8,192 probes at High — the dropped frame of a
// headset, LATER_OPTIMISATIONS L11). The field is now a TOROIDAL WINDOW over a
// probe lattice fixed in the world: a probe's atlas tile is its lattice
// coordinate modulo the probe count (jah_field_window.glsl, one definition for
// the generation job, the integration jobs and the pixel's reader). A step moves
// the window by whole probe spacings; the probes that stay inside keep their
// tiles and their values, and only the planes that entered are integrated in the
// step frame.
//
// WHAT THIS SUITE ASSERTS, each a measurement:
//   1. THE KEPT PROBES ARE BYTE-IDENTICAL across the step frame: both atlases
//      (irradiance, depth moments) read back just before and just after it, every
//      tile of a probe that never left the window compared byte for byte.
//   2. THE STEP FRAME INTEGRATES EXACTLY THE ENTERED PLANES: the follow's work
//      (GiStatus::ifdScrollProbes, and the monitor row's units) equals the
//      planes the window's offset says entered — not the field.
//   3. THE MODULO PUTS EVERY PROBE WHERE IT BELONGS: one walk of six steps out
//      and back, taken twice in this process from the same from-scratch build -
//      the field scrolling, and the field re-placed whole on the same lattice at
//      every step (`JAHSHAKA_GI_FIELD_NO_SCROLL`, the behaviour replaced) - and the
//      return pose renders the same picture both ways (the pose BEFORE a walk is
//      not a reference: the chain's hysteretic placement does not come back to
//      where a from-scratch build put it, scroll or no scroll).
//   4. THE COST, in ONE process: the two walks' ifd.follow rows are the step
//      frame after and before - printed as GPU ms (the median of the rows the
//      monitor timed), their ratio and their share of the VR frame (11.1 ms);
//      asserted only as "the scroll is cheaper".
//
// The field is PAUSED (update budget 0) for 1 and 2: no progressive walk runs
// between the two readbacks, so the only work in the step frame is the scroll's.
// Its own binary like every GI suite (the field binds process-wide to HlmsPbs).
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)
#define CHECK_MSG(cond, fmt, ...)                                               \
    do {                                                                        \
        char buf_[512];                                                         \
        std::snprintf(buf_, sizeof(buf_), fmt, __VA_ARGS__);                    \
        CHECK(cond, buf_);                                                      \
    } while (0)

static const unsigned kSize = 128;

static void render(Engine *e, int frames = 1) { for (int i = 0; i < frames; ++i) e->renderOneFrame(); }

static GiParams fieldGi(int budget)
{
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.numBounces = 1;
    gi.ddgi = GiToggle::On;
    gi.updateBudget = budget;
    gi.cascades = true;
    return gi;
}

/// One probe's tile, both atlases, as bytes.
static std::vector<unsigned char> tileOf(const GiFieldAtlas &a, unsigned slot)
{
    std::vector<unsigned char> out;
    const auto copyTile = [&](const std::vector<unsigned char> &bytes, unsigned width,
                              unsigned bordered, unsigned bpp) {
        const unsigned x0 = (slot * bordered) % width;
        const unsigned y0 = ((slot * bordered) / width) * bordered;
        for (unsigned y = 0; y < bordered; ++y) {
            const size_t row = (size_t(y0 + y) * width + x0) * bpp;
            out.insert(out.end(), bytes.begin() + long(row), bytes.begin() + long(row + size_t(bordered) * bpp));
        }
    };
    copyTile(a.irradiance, a.irradWidth, a.irradBordered, a.irradBytesPerTexel);
    copyTile(a.depth, a.depthWidth, a.depthBordered, a.depthBytesPerTexel);
    return out;
}

static float worstDiff(const Image &a, const Image &b)
{
    float worst = 0.0f;
    for (unsigned y = 0; y < a.height; ++y)
        for (unsigned x = 0; x < a.width; ++x) {
            const Colour ca = a.at(x, y), cb = b.at(x, y);
            worst = std::max(worst, std::fabs(ca.r - cb.r) * 255.0f);
            worst = std::max(worst, std::fabs(ca.g - cb.g) * 255.0f);
            worst = std::max(worst, std::fabs(ca.b - cb.b) * 255.0f);
        }
    return worst;
}

int main()
{
    std::printf("== gi.field_scroll: the irradiance field is a toroidal window; a step keeps what "
                "stays and integrates what entered\n");
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-field-scroll-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();

    View *view = e->createOffscreenView("scroll", kSize, kSize, Colour(0, 0, 0));
    Scene *scene = e->createScene("scroll");
    view->setScene(scene);
    scene->setAmbient(Colour(0.30f, 0.30f, 0.30f), Colour(0.30f, 0.30f, 0.30f));
    // A long ground, walls every few metres (real geometry in cascade 0 at every
    // pose of the walk), and a lamp so the field holds a bounce and not only sky.
    const NodeId ground = enginetest::addTestCube(scene, Colour(0.8f, 0.8f, 0.8f), 0.0f, 0.9f);
    enginetest::setNodePosition(scene, ground, Vec3(0.0f, -0.05f, 0.0f));
    enginetest::setNodeScale(scene, ground, Vec3(400.0f, 0.1f, 400.0f));
    for (int i = -4; i < 12; ++i) {
        const NodeId wall = enginetest::addTestCube(scene, Colour(0.9f, 0.1f, 0.1f), 0.0f, 0.9f);
        enginetest::setNodePosition(scene, wall, Vec3(float(i) * 4.0f, 2.0f, -3.0f));
        enginetest::setNodeScale(scene, wall, Vec3(2.5f, 4.0f, 0.2f));
    }
    const NodeId lamp = scene->createNode();
    scene->setNodeTransform(lamp, Vec3(3.0f, 3.0f, 0.0f), Quat(), Vec3(1, 1, 1));
    LightDesc l;
    l.type = LightType::Point;
    l.colour = Colour(1, 1, 1);
    l.intensity = 2.0f;
    l.range = 40.0f;
    scene->setLight(lamp, l);
    const auto camAt = [&](float x) {
        view->setCamera(enginetest::testCameraDescLookAt(Vec3(x, 2.0f, 5.0f), Vec3(x, 1.0f, -3.0f)));
    };
    camAt(0.0f);
    render(e, 4);

    CHECK(scene->setGlobalIllumination(fieldGi(0)), "the chain with the field, paused");
    render(e, 12);
    GiStatus st = scene->giStatus();
    CHECK(st.ifdBound && !st.cascades.empty(), "the field is bound over cascade 0");
    GiFieldAtlas probeA;
    CHECK(scene->giFieldAtlas(probeA) && probeA.available, "the field's atlases read back");
    const unsigned N[3] = { probeA.probes[0], probeA.probes[1], probeA.probes[2] };
    const unsigned total = N[0] * N[1] * N[2];
    std::printf("   field %ux%ux%u = %u probes, window offset %u,%u,%u\n", N[0], N[1], N[2], total,
                probeA.windowOffset[0], probeA.windowOffset[1], probeA.windowOffset[2]);

    // ---- 1 + 2. ONE STEP, READ AROUND ITS FRAME -----------------------------
    // Walk until cascade 0 rebuilds; the field follows on the NEXT frame (V1-RIG:
    // the two halves of a step never share one), so the atlas read on the rebuild
    // frame is "before" and the one read after the next frame is "after".
    engine->setFrameMonitor(MonitorLevel::Review);
    float x = 0.0f;
    unsigned long long c0Rebuilds = st.cascades[0].rebuilds;
    bool stepped = false;
    GiFieldAtlas before, after;
    for (int f = 0; f < 120 && !stepped; ++f) {
        x += 0.1f;
        camAt(x);
        render(e, 1);
        const GiStatus s = scene->giStatus();
        if (s.cascades[0].rebuilds != c0Rebuilds) {
            CHECK(scene->giFieldAtlas(before), "the atlases read back on the rebuild frame");
            const unsigned long long follows = s.ifdFollows;
            render(e, 1);                                  // THE STEP FRAME: the follow
            const GiStatus s2 = scene->giStatus();
            CHECK(s2.ifdFollows == follows + 1u, "the field followed on the very next frame");
            CHECK(scene->giFieldAtlas(after), "...and read back after it");
            stepped = true;
        }
    }
    CHECK(stepped, "the walk crossed a cascade-0 step (the test is not vacuous)");
    if (stepped && before.available && after.available) {
        // The move, from the offsets: slot = (local + offset) mod N, and a move of d
        // spacings moves the offset by d (|d| < N/2 on a cascade-0 step).
        int d[3];
        for (int a = 0; a < 3; ++a) {
            const int n = int(N[a]);
            d[a] = ((int(after.windowOffset[a]) - int(before.windowOffset[a])) % n + n + n / 2) % n - n / 2;
        }
        unsigned kept = 0, keptSame = 0, entered = 0;
        for (unsigned slot = 0; slot < total; ++slot) {
            const unsigned s3[3] = { slot % N[0], (slot / N[0]) % N[1], slot / (N[0] * N[1]) };
            bool stays = true;
            for (int a = 0; a < 3; ++a) {
                // The OLD window-local coordinate of the probe that held this slot,
                // and where it is in the new window: it stayed iff it is inside.
                const int localOld = (int(s3[a]) - int(before.windowOffset[a]) + int(N[a])) % int(N[a]);
                const int localNew = localOld - d[a];
                if (localNew < 0 || localNew >= int(N[a])) stays = false;
            }
            if (!stays) { ++entered; continue; }
            ++kept;
            if (tileOf(before, slot) == tileOf(after, slot)) ++keptSame;
        }
        const GiStatus s = scene->giStatus();
        std::printf("   the step moved the window %d,%d,%d spacings: %u probes kept (%u byte-identical), "
                    "%u entered; the follow integrated %u\n", d[0], d[1], d[2], kept, keptSame,
                    entered, s.ifdScrollProbes);
        CHECK_MSG(keptSame == kept && kept > 0,
                  "THE PROBES THAT STAYED IN THE WINDOW ARE BYTE-IDENTICAL across the step frame "
                  "(%u of %u tiles, both atlases)", keptSame, kept);
        CHECK_MSG(s.ifdScrollProbes == entered && entered < total,
                  "THE STEP FRAME INTEGRATED EXACTLY THE PLANES THAT ENTERED (%u, of a %u-probe "
                  "field)", s.ifdScrollProbes, total);
    }

    // ---- 3 + 4. ONE WALK, TWO WAYS, ONE PROCESS ------------------------------
    // Six cascade-0 steps out, three jumps (a teleport, a headset-shaped jump, back)
    // and the walk home, at the shipped budget, from a
    // from-scratch build at the same pose - once with the field SCROLLING, once
    // with every step RE-PLACING the whole field on the same lattice
    // (`JAHSHAKA_GI_FIELD_NO_SCROLL`, the behaviour the scroll replaced). The chain
    // walks the same path both times, so its placements match; after the walk
    // and its settle both fields hold whole integrations of one chain at one
    // window, and the return pose must render the same picture - which is what
    // says the window's modulo puts every probe where it belongs. (The pose
    // before the walk is NOT the reference: the chain's hysteretic placement does
    // not come back to where a from-scratch build put it, so that picture
    // differs by the chain's own path, scroll or no scroll.) The monitor's
    // ifd.follow rows of the two walks are the step frame before and after.
    struct Arm { Image back; float scrollGpu = -1.0f, scrollCpu = -1.0f; unsigned units = 0;
                 unsigned long long follows = 0, replacements = 0; std::vector<float> gpu;
                 float worstCentreErr = 0.0f; bool fieldBound = true; };
    // A JUMP: the camera lands `p` in ONE frame (a teleport, or a headset re-centred
    // far away), then the scene settles; the field must be where cascade 0 is.
    const auto jumpTo = [&](Arm &arm, const Vec3 &p) {
        view->setCamera(enginetest::testCameraDescLookAt(p, Vec3(p.x, p.y - 1.0f, p.z - 8.0f)));
        render(e, 60);
        const GiStatus js = scene->giStatus();
        arm.fieldBound = arm.fieldBound && js.ifdBound;
        if (!js.cascades.empty()) {
            const float cx = 0.5f * (js.ifdMin.x + js.ifdMax.x) - js.cascades[0].centre.x;
            const float cy = 0.5f * (js.ifdMin.y + js.ifdMax.y) - js.cascades[0].centre.y;
            const float cz = 0.5f * (js.ifdMin.z + js.ifdMax.z) - js.cascades[0].centre.z;
            arm.worstCentreErr = std::max(arm.worstCentreErr, std::sqrt(cx * cx + cy * cy + cz * cz));
        }
    };
    const auto walk = [&](bool noScroll) {
        Arm arm;
        if (noScroll) ::setenv("JAHSHAKA_GI_FIELD_NO_SCROLL", "1", 1);
        GiParams offGi; offGi.mode = GiMode::Off;
        scene->setGlobalIllumination(offGi);
        render(e, 2);
        camAt(0.0f);
        render(e, 2);
        scene->setGlobalIllumination(fieldGi(1));
        render(e, 90);
        std::vector<FrameRecord> drop;
        render(e, 8);
        engine->takeFrameRecords(drop);
        const unsigned long long f0 = scene->giStatus().ifdFollows;
        const unsigned long long r0 = scene->giStatus().ifdReplacements;
        float x2 = 0.0f;
        for (int f = 0; f < 800 && scene->giStatus().ifdFollows < f0 + 6u; ++f) {
            x2 += 0.1f;
            camAt(x2);
            render(e, 1);
        }
        // THE JUMPS (the lead's fix round item 1): three cascade-0 boxes along the
        // walk in one frame, then a headset-shaped jump (far, sideways and up),
        // then back to where the walk stopped - each keeps nothing of the window,
        // so each must RE-PLACE the field onto cascade 0; the walk home scrolls again.
        jumpTo(arm, Vec3(x2 + 30.0f, 2.0f, 5.0f));
        jumpTo(arm, Vec3(x2 - 20.0f, 9.0f, 45.0f));
        jumpTo(arm, Vec3(x2, 2.0f, 5.0f));
        for (int f = 0; f < 800 && x2 > 0.0f; ++f) {
            x2 = std::max(0.0f, x2 - 0.1f);
            camAt(x2);
            render(e, 1);
        }
        render(e, 240);                          // the settle and the progressive walk
        view->readPixels(arm.back);
        arm.follows = scene->giStatus().ifdFollows - f0;
        arm.replacements = scene->giStatus().ifdReplacements - r0;
        std::vector<FrameRecord> recs;
        engine->takeFrameRecords(recs);
        for (const FrameRecord &r : recs)
            for (const CacheWork &w : r.cacheWork) {
                if (w.detail != "ifd.follow") continue;
                // The re-placed walk's every step is the whole field; the scrolled
                // walk's jumps are too, and they are not what its step frame costs.
                if (!noScroll && w.units == total) continue;
                if (w.gpuMs > 0.0f) arm.gpu.push_back(w.gpuMs);
                if (w.gpuMs > arm.scrollGpu) arm.scrollGpu = w.gpuMs;
                arm.units = std::max(arm.units, w.units);
                arm.scrollCpu = std::max(arm.scrollCpu, w.ms);
            }
        if (noScroll) ::unsetenv("JAHSHAKA_GI_FIELD_NO_SCROLL");
        return arm;
    };
    const Arm scrolled = walk(false);
    const Arm replaced = walk(true);
    const float returnDiff = worstDiff(scrolled.back, replaced.back);
    std::printf("   the same walk (%llu / %llu follows): the return pose, scrolled against re-placed, "
                "%.2f/255\n", scrolled.follows, replaced.follows, returnDiff);
    CHECK(scrolled.follows >= 6u && replaced.follows >= 6u,
          "both walks stepped the field out six steps and back");
    std::printf("   the jumps: %llu whole re-placements in the scrolled walk; the field's centre "
                "stood within %.3f m of cascade 0's after each (the lattice snap is under one "
                "spacing)\n", scrolled.replacements, scrolled.worstCentreErr);
    CHECK_MSG(scrolled.replacements >= 3u && scrolled.fieldBound,
              "A JUMP RE-PLACES THE FIELD (a teleport of three cascade-0 boxes, a headset-shaped "
              "jump, the way back: %llu re-placements) and it stays bound",
              scrolled.replacements);
    CHECK_MSG(scrolled.worstCentreErr < 0.4f,
              "...AND THE FIELD FOLLOWS CASCADE 0 THERE (its centre within %.3f m, under one probe "
              "spacing) - it is not left at the place the camera jumped from",
              scrolled.worstCentreErr);
    CHECK_MSG(returnDiff <= 1.0f,
              "THE RETURN POSE RENDERS WHAT A FIELD THAT NEVER SCROLLED RENDERS THERE (%.2f/255): "
              "the window's modulo puts every probe where it belongs", returnDiff);
    // THE MONITOR TIMES A SHARE OF THE ROWS (a GPU timestamp pair is not always
    // back for a between-pass dispatch): the median of those that were, per arm.
    const auto median = [](std::vector<float> v) {
        if (v.empty()) return -1.0f;
        std::sort(v.begin(), v.end());
        return v[v.size() / 2];
    };
    const float scrollMed = median(scrolled.gpu), replacedMed = median(replaced.gpu);
    std::printf("   THE STEP FRAME, one process (unlocked clocks - a ratio): scrolled median %.2f ms "
                "GPU over %zu timed rows, worst %.2f (the largest follow %u probes); re-placed "
                "median %.2f ms GPU over %zu timed rows, worst %.2f (%u probes); ratio %.2f; "
                "against the VR frame of 11.1 ms: %.1f %% -> %.1f %%\n",
                scrollMed, scrolled.gpu.size(), scrolled.scrollGpu, scrolled.units, replacedMed,
                replaced.gpu.size(), replaced.scrollGpu, replaced.units,
                replacedMed > 0.0f ? scrollMed / replacedMed : -1.0f,
                100.0f * replaced.scrollGpu / 11.1f, 100.0f * scrolled.scrollGpu / 11.1f);
    CHECK(replaced.units == total && scrolled.units > 0 && scrolled.units < total,
          "the re-placed walk integrated the whole field per step, the scrolled one a part of it");
    if (scrollMed > 0.0f && replacedMed > 0.0f)
        CHECK_MSG(scrollMed < replacedMed,
                  "A SCROLL COSTS THE STEP FRAME LESS THAN RE-PLACING THE FIELD (median %.2f against "
                  "%.2f ms GPU)", scrollMed, replacedMed);

    GiParams off; off.mode = GiMode::Off;
    scene->setGlobalIllumination(off);
    render(e, 2);
    view->setScene(nullptr);
    e->destroyScene(scene);
    e->destroyView(view);
    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
