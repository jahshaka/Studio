// gi.bounce_residual — BOUNCES TO A RESIDUAL (PHOTON-5 stage 5; SPECS/audits/PHOTON_AUDIT_2026-10-21.md F5).
//
// The voxel chain's multi-bounce is a Jacobi series (PhotonVoxelLighting::update: new = direct + rho G(total)),
// which contracts by rho f per pass. The tier table's bounces column was a COUNT (1/1/1/3), so an enclosed room —
// f near 1 — kept a tier's fixed share of its series and its far corners read darker at High than at Epic.
//
// THE FIXTURE: a sealed 8 x 4 x 8 m room (walls, floor and ceiling at albedo 0.5), lit ONLY by the sun through one
// window in its +X wall; the far corner (x -4, z +4) never sees the sun. Its wall's HDR radiance near the corner
// is the indirect. Arms in one process, one scene, High: GiParams::numBounces 1 (High's minimum), 4, and 9 - the
// REFERENCE, a minimum of eight passes on every cascade - and 1 again (the measurement repeats). High must read
// within 5 % of the reference; at the base it read 49 % short (the clamp held 9 at 4 passes there).
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK_MSG(cond, ...)                                                    \
    do {                                                                        \
        std::printf((cond) ? "ok: " : "FAIL: ");                                 \
        std::printf(__VA_ARGS__);                                                \
        std::printf("\n");                                                       \
        if (!(cond)) ++failures;                                                 \
    } while (0)

static void addBox(Scene *s, const Vec3 &lo, const Vec3 &hi)
{
    const NodeId n = enginetest::addTestCube(s, Colour(0.5f, 0.5f, 0.5f), 0.0f, 0.95f);
    enginetest::setNodePosition(s, n, Vec3(0.5f * (lo.x + hi.x), 0.5f * (lo.y + hi.y), 0.5f * (lo.z + hi.z)));
    enginetest::setNodeScale(s, n, Vec3(hi.x - lo.x, hi.y - lo.y, hi.z - lo.z));
}

/// The room: inner x, z in [-4, 4], y in [0, 4]; 0.3 m shell; a 2 x 1.5 m window in the +X wall.
static void buildRoom(Scene *s)
{
    const float T = 0.3f;
    addBox(s, Vec3(-4 - T, -T, -4 - T), Vec3(4 + T, 0, 4 + T));        // floor
    addBox(s, Vec3(-4 - T, 4, -4 - T), Vec3(4 + T, 4 + T, 4 + T));      // ceiling
    addBox(s, Vec3(-4 - T, 0, -4 - T), Vec3(-4, 4, 4 + T));             // -X wall
    addBox(s, Vec3(-4, 0, -4 - T), Vec3(4, 4, -4));                     // -Z wall
    addBox(s, Vec3(-4, 0, 4), Vec3(4, 4, 4 + T));                       // +Z wall
    // +X wall around the window z in [-1, 1], y in [1.5, 3]
    addBox(s, Vec3(4, 0, -4), Vec3(4 + T, 4, -1));
    addBox(s, Vec3(4, 0, 1), Vec3(4 + T, 4, 4));
    addBox(s, Vec3(4, 0, -1), Vec3(4 + T, 1.5f, 1));
    addBox(s, Vec3(4, 3, -1), Vec3(4 + T, 4, 1));
}

static double cornerRadiance(Engine *e, Scene *s, View *view, int bounces, GiStatus &st)
{
    GiParams gi;
    gi.mode = GiMode::VctPccHybrid;
    gi.quality = GiQuality::High;
    gi.numBounces = bounces;
    s->setGlobalIllumination(gi);
    // SETTLED IN FRAMES: until the GI is at rest (the chain built, the series measured and answered, the
    // field's passes done) - the field re-converges over its previous arm's data - then the gather's history.
    enginetest::frames(e, 8);
    int f = 0;
    for (; f < 900 && !s->published().gi.giAtRest; ++f) enginetest::frames(e, 1);
    enginetest::frames(e, 60);
    std::printf("   (numBounces %d at rest after %d frames)\n", bounces, f + 8);
    ImageF img;
    double sum = 0.0;
    int n = 0;
    if (view->readPixelsHdr(img)) {
        const int cx = int(img.width / 2), cy = int(img.height / 2);
        for (int y = cy - 6; y <= cy + 6; ++y)
            for (int x = cx - 6; x <= cx + 6; ++x) {
                const Colour c = img.at(unsigned(x), unsigned(y));
                sum += 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b;
                ++n;
            }
    }
    st = s->published().gi;
    return n ? sum / n : -1.0;
}

int main()
{
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-gi-bounce-residual-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    View *view = e->createOffscreenView("bounces", 192, 192, Colour(0, 0, 0));
    PostFxDesc fx;
    fx.hdrReadback = true;
    view->setPostFx(fx);
    view->setShadows(true);
    Scene *s = e->createScene("room");
    view->setScene(s);
    s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
    buildRoom(s);
    enginetest::addDirectionalLight(s, Vec3(-1.0f, -0.6f, 0.0f), 5.0f);
    // From inside, near the window, looking at the far corner's wall (x -4, z +4) a metre up.
    enginetest::testCameraLookAt(view, Vec3(2.0f, 2.0f, -2.0f), Vec3(-4.0f, 1.0f, 3.4f));

    // THE ARMS: the tiers' own requests (1 = High's minimum, 4 the old cap) and the REFERENCE, 9 - a
    // minimum of eight passes on every cascade (kMaxBouncePasses), the series as far as the engine runs it.
    const int arms[4] = { 1, 4, 9, 1 };
    double L[4];
    for (int k = 0; k < 4; ++k) {
        GiStatus st;
        L[k] = cornerRadiance(e, s, view, arms[k], st);
        std::printf("   numBounces %d: far-corner radiance %.5f  (cascades %zu)\n", arms[k], L[k], st.cascades.size());
    }
    // THE PASS COUNTS THE SERIES CHOSE, per cascade (the engine's log line on every change).
    {
        FILE *f = std::fopen(cfg.logFile.c_str(), "r");
        char line[512];
        int shown = 0;
        while (f && std::fgets(line, sizeof line, f))
            if (std::strstr(line, "bounce series") && shown++ < 24) std::printf("   %s", line);
        if (f) std::fclose(f);
    }
    const double ref = L[2];
    // MEASURED AT THE BASE (the tier's fixed count, the clamp at 4): numBounces 1 read 0.00496, 4 read 0.00966
    // and 9 the same 0.00966 (the clamp) - High 49 % short of the series' limit (~0.00978, extrapolated).
    std::printf("   High (numBounces 1) is %.1f %% from the eight-pass reference (%.5f)\n",
                100.0 * std::fabs(1.0 - L[0] / ref), ref);
    CHECK_MSG(L[0] > 0.0 && ref > 0.0, "the far corner reads an indirect (%.5f)", L[0]);
    CHECK_MSG(std::fabs(1.0 - L[0] / ref) <= 0.05,
              "BOUNCES TO A RESIDUAL: High's far corner within 5 %% of the eight-pass reference (%.1f %%)",
              100.0 * std::fabs(1.0 - L[0] / ref));
    CHECK_MSG(std::fabs(1.0 - L[3] / L[0]) <= 0.01,
              "...and the same request twice reads the same corner (%.5f vs %.5f)", L[3], L[0]);

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
