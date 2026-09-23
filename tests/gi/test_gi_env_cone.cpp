// gi.env_cone — THE ENVIRONMENT'S CONE LOOKUP, MEASURED (PHOTON-ENV-1).
//
// Every escape in the renderer reads the sky through ONE function,
// jahEnvCone( dir, tan(half-angle) ) (src/rayquery/include/jah_environment.glsl):
// ONE fetch of the GGX-prefiltered sky chain at the mip whose lobe matches the
// cone. A GGX lobe is not a box, so this is an approximation — the mapping
// picks the lobe whose mean of (1 - cos) about its axis is 0.6 of the uniform
// cone's (the factor measured HERE: 1.0 is the band-1-exact match, but a GGX
// lobe's heavy tail then reaches the horizon glow from a zenith cone; the
// derivation and the sweep are in jah_environment.glsl). This suite measures
// the lookup's error on the SHIPPED
// sky (the default scene's analytic atmosphere) against the thing the lookup
// stands in for: the mean of the cube's finest mip over 64 directions spread
// uniformly over the cone's solid angle, computed in the same job
// (Engine::environmentCones).
//
// THREE APERTURES, the ones the renderer asks for: the six-cone diffuse set's
// (tan 0.577), the irradiance field's probe ray (tan(2 pi / 144), 144 rays per
// probe), and a 0.1 rad specular cone. BAR: the mean relative luminance error
// over 26 directions below 5 % at each, at two sun heights (noon-ish and a low
// sun, whose horizon glow is the sky's sharpest feature).
#include "jahshaka/engine/Engine.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)

static double lum(const float c[3]) { return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]; }

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-env-cone-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();
    View *view = e->createOffscreenView("envcone", 64, 64, Colour(0, 0, 0));
    Scene *scene = e->createScene("envcone");
    view->setScene(scene);

    // 26 directions: the six axes, the twelve edge diagonals, the eight corners.
    std::vector<Vec3> dirs;
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y)
            for (int z = -1; z <= 1; ++z) {
                if (!x && !y && !z) continue;
                const float l = std::sqrt(float(x * x + y * y + z * z));
                dirs.push_back(Vec3(x / l, y / l, z / l));
            }

    struct Aperture { const char *name; float tan; };
    const Aperture apertures[] = {
        { "six-cone diffuse (tan 0.577)", 0.577f },
        { "field probe ray (tan 2pi/144)", std::tan(6.28318531f / 144.0f) },
        { "specular 0.1 rad", std::tan(0.1f) },
        { "four-cone diffuse (tan 0.983), printed", 0.98269f },
    };
    const float kBar = 0.05f;

    for (float elevDeg : { 35.0f, 5.0f }) {
        SkyDesc sky;
        sky.mode = SkyMode::Atmosphere;
        sky.atmosphere.hasSun = true;
        const float rad = elevDeg * 3.14159265f / 180.0f;
        sky.atmosphere.sunDir[0] = std::cos(rad);
        sky.atmosphere.sunDir[1] = std::sin(rad);
        sky.atmosphere.sunDir[2] = 0.0f;
        CHECK(scene->setSky(sky), "the analytic sky binds");
        // The capture runs inside a frame and the convolution lands at the top
        // of the next: wait for the cube in FRAMES.
        std::vector<EnvironmentConeAnswer> probe;
        const std::vector<EnvironmentConeQuery> one = { EnvironmentConeQuery{ Vec3(0, 1, 0), 0.577f } };
        bool ready = false;
        for (int f = 0; f < 30 && !ready; ++f) {
            e->renderOneFrame();
            if (f >= 4) ready = e->environmentCones(scene, one, probe);
        }
        CHECK(ready, "the environment cube exists (the sky's capture and convolution landed)");
        if (!ready) { std::printf("   %s\n", e->takeLastError().c_str()); continue; }
        std::printf("\n== sun elevation %.0f deg ==\n", elevDeg);
        for (const Aperture &a : apertures) {
            std::vector<EnvironmentConeQuery> q;
            for (const Vec3 &d : dirs) q.push_back(EnvironmentConeQuery{ d, a.tan });
            std::vector<EnvironmentConeAnswer> ans;
            const bool ok = e->environmentCones(scene, q, ans);
            CHECK(ok, "the cone harness ran");
            if (!ok) { std::printf("   %s\n", e->takeLastError().c_str()); continue; }
            double sumRel = 0.0, worst = 0.0;
            size_t counted = 0, worstIdx = 0;
            for (size_t i = 0; i < ans.size(); ++i) {
                const double ref = lum(ans[i].reference);
                if (ref <= 1e-6) continue;      // a black direction has no relative error
                const double rel = std::fabs(lum(ans[i].lookup) - ref) / ref;
                sumRel += rel;
                ++counted;
                if (rel > worst) { worst = rel; worstIdx = i; }
            }
            const double mean = counted ? sumRel / double(counted) : 1.0;
            std::printf("   %-40s lod %.2f  mean |err| %.2f %%  worst %.2f %% (dir %.2f %.2f %.2f: "
                        "lookup %.4f ref %.4f)  over %zu dirs\n",
                        a.name, double(ans[0].lod), 100.0 * mean, 100.0 * worst,
                        double(dirs[worstIdx].x), double(dirs[worstIdx].y), double(dirs[worstIdx].z),
                        lum(ans[worstIdx].lookup), lum(ans[worstIdx].reference), counted);
            if (std::string(a.name).find("printed") != std::string::npos) continue;
            char msg[160];
            std::snprintf(msg, sizeof msg,
                          "the cone lookup is within %.0f %% of the cone integral (%s, sun %.0f deg)",
                          100.0 * kBar, a.name, elevDeg);
            CHECK(counted == dirs.size() && mean < kBar, msg);
        }
    }

    view->setScene(nullptr);
    e->destroyScene(scene);
    e->destroyView(view);
    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
