// atom.light_depth_term — U5's PER-MAP LIGHT-DEPTH TERM, PAIRED ARMS IN ONE PROCESS (ATOM-4 stage 2; the archived
// scale.shadow_cut's paired-arm measurement revived as a fixture of its own: archive/tests/scale).
//
// A sun over a ground, a roof slab and a field of Atom boxes under and beside it (the roof hides part of the field
// from the sun, so the light's own depth has something to reject); one offscreen view with shadows, the camera still.
//   THE LAW (shadow.caster_cull's): with the receiver test off (Scene::setShadowReceiverCull(false)), the sun maps
//     drawn with the light-depth path (shadow.lightDepth 1: each sun map's pyramid, the late cut against it) are
//     texel-equal to the maps without it (shadow.lightDepth 0: the frustum-only maps) — and the path rejects
//     something (casterOccluded > 0), so the equality is not vacuous.
//   THE NUMBERS (printed, no timing bar — PROCESS.md: a timing is never a bar in a guard):
//     * the texels and bytes of each map's pyramid, from the arithmetic over the maps' own rectangles
//       (Scene::shadowMapDigests): the full chain from the map's resolution (U5) and the chain from HALF of it (ATOM-4
//       stage 2a: mip 0 the farthest of each 2 x 2; an odd-sized map keeps the full resolution);
//     * U5's TERM: the view's shadow-node GPU ms (PassBucket::ShadowView), shadow.lightDepth 1 less 0, per sun map,
//       medians over two alternated rounds of each arm (ABAB) with the receiver test on (the shipped path);
//     * the pyramid builds' own GPU ms per map (the monitor's `atom.caster.pyramid` row, under the measuring arm
//       shadow.casterPbsProbe — a window of its own, never inside the paired arms).
// Frames, never time: the engine's fixed clock.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
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
        std::fflush(stdout);                                                    \
    } while (0)
#define CHECK_MSG(cond, fmt, ...)                                               \
    do {                                                                        \
        char buf_[640];                                                         \
        std::snprintf(buf_, sizeof(buf_), fmt, __VA_ARGS__);                    \
        CHECK(cond, buf_);                                                      \
    } while (0)

static const unsigned kW = 1280, kH = 720;
static Engine *gE = nullptr;

static NodeId box(Scene *s, MeshId cube, MaterialId m, const Vec3 &c, const Vec3 &size)
{
    const NodeId n = s->createNode();
    s->setNodeTransform(n, c, Quat(), size);
    s->attachMesh(n, cube, m);
    return n;
}

static double median(std::vector<double> v)
{
    if (v.empty()) return -1.0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

/// The view's shadow-node GPU ms per frame (every pass of the node: its scene passes and the caster cut's).
static void shadowMs(std::vector<double> &out)
{
    std::vector<MonitorRecord> recs;
    gE->popMonitorRecords(recs);
    for (const MonitorRecord &r : recs) {
        double gpu = 0.0;
        bool ok = true;
        unsigned n = 0;
        for (const FramePass &p : r.passes) {
            if (p.bucket != PassBucket::ShadowView || p.orphaned) continue;
            ++n;
            if (p.gpuMs < 0.0f) ok = false;
            else gpu += p.gpuMs;
        }
        if (n && ok) out.push_back(gpu);
    }
}

/// The full chain's texels from a w x h level 0 (the reducer's rule: each level half the last, at least 1).
static unsigned long long chainTexels(unsigned w, unsigned h)
{
    unsigned long long t = 0ull;
    for (;;) {
        t += (unsigned long long)w * h;
        if (w == 1u && h == 1u) break;
        w = std::max(1u, w >> 1u);
        h = std::max(1u, h >> 1u);
    }
    return t;
}

static bool sameMaps(const std::vector<ShadowMapDigest> &a, const std::vector<ShadowMapDigest> &b, std::string &why)
{
    if (a.size() != b.size() || a.empty()) { why = "different map sets"; return false; }
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].kind != b[i].kind || a[i].map != b[i].map || a[i].width != b[i].width ||
            a[i].height != b[i].height || a[i].digest != b[i].digest) {
            char buf[200];
            std::snprintf(buf, sizeof(buf), "map %u: %016llx vs %016llx (covered %u vs %u)", a[i].map, a[i].digest,
                          b[i].digest, a[i].covered, b[i].covered);
            why = buf;
            return false;
        }
    return true;
}

int main()
{
    std::printf("== atom.light_depth_term: the sun maps' light-depth path, its law and its per-map term\n");
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-atom-light-depth-term-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->submitFrame(jahshaka::engine::FrameInput::delta(1.0f / 60.0f));
    gE = engine.get();
    View *view = gE->createOffscreenView("light-depth-term", kW, kH, Colour(0, 0, 0));
    Scene *s = gE->createScene("light-depth-term");
    s->setLodBias(1.0f);
    s->setAmbient(Colour(0.10f, 0.11f, 0.13f), Colour(0.05f, 0.05f, 0.05f));
    PbrParams gp;
    gp.albedo = Colour(0.4f, 0.4f, 0.38f);
    gp.roughness = 0.8f;
    const MaterialId gm = s->createPbrMaterial(gp);
    PbrParams op;
    op.albedo = Colour(0.7f, 0.3f, 0.2f);
    op.roughness = 0.5f;
    const MaterialId om = s->createPbrMaterial(op);
    const MeshId cube = s->createMesh(enginetest::unitCubeMesh());
    box(s, cube, gm, Vec3(0.0f, -0.25f, -20.0f), Vec3(80.0f, 0.5f, 80.0f));     // the ground
    box(s, cube, gm, Vec3(1.0f, 6.0f, -15.0f), Vec3(26.0f, 0.3f, 38.0f));       // the roof
    for (int zi = 0; zi < 7; ++zi)
        for (int xi = -2; xi <= 2; ++xi)
            box(s, cube, om, Vec3(2.6f * float(xi) + 0.4f * float(zi % 2), 1.0f, -4.0f - 11.0f * float(zi)),
                Vec3(1.6f, 1.6f, 1.6f));
    enginetest::addDirectionalLight(s, Vec3(-0.45f, -0.8f, -0.4f), 2.2f);
    view->setOffscreenContract(OffscreenContract::StillPicture);
    view->setScene(s);
    view->setShadows(true);
    {
        PostFxDesc fx;
        fx.allowOffscreen = true;
        fx.ssr = 0;
        view->setPostFx(fx);
    }
    view->setCamera(enginetest::testCameraDescLookAt(Vec3(0.0f, 2.2f, 4.0f), Vec3(0.0f, 0.8f, -30.0f)));
    enginetest::frames(gE, 40);

    // ---- THE LAW: the maps with and without the light's depth, the receiver test off ----------------------------
    s->setShadowReceiverCull(false);
    std::vector<ShadowMapDigest> on, off;
    gE->setArm("shadow.lightDepth", 1.0);
    enginetest::frames(gE, 12);
    const AtomDrawStatus st = s->atomDrawStatus();
    const bool readOn = s->shadowMapDigests(on);
    gE->setArm("shadow.lightDepth", 0.0);
    enginetest::frames(gE, 12);
    const bool readOff = s->shadowMapDigests(off);
    gE->setArm("shadow.lightDepth", 1.0);
    s->setShadowReceiverCull(true);
    std::string why;
    CHECK_MSG(readOn && readOff && on.size() >= 3u, "the view's sun maps read back (%zu / %zu)", on.size(), off.size());
    CHECK_MSG(st.casterValid && st.casterOccluded > 0u,
              "the light's depth rejects casters the roof hides from the sun (casterOccluded %u, late drew %u, "
              "neither %u)", st.casterOccluded, st.casterLateInstances, st.casterLateOccluded);
    CHECK_MSG(sameMaps(on, off, why), "the sun maps with the light's depth are texel-equal to the frustum-only maps "
              "(%s)", why.empty() ? "every texel" : why.c_str());

    // ---- THE PYRAMIDS' TEXELS AND BYTES, from the maps' rectangles ---------------------------------------------
    unsigned long long fullT = 0ull, halfT = 0ull;
    unsigned sunMaps = 0u;
    for (const ShadowMapDigest &d : on) {
        if (d.kind != 0u || !d.width || !d.height) continue;
        ++sunMaps;
        const bool even = (d.width % 2u) == 0u && (d.height % 2u) == 0u;
        const unsigned long long f = chainTexels(d.width, d.height);
        const unsigned long long h = even ? chainTexels(d.width / 2u, d.height / 2u) : f;
        fullT += f;
        halfT += h;
        std::printf("   map %u %ux%u: pyramid texels full-resolution %llu (%.2f MiB) -> half %llu (%.2f MiB), "
                    "level 0 written %u -> %u\n",
                    d.map, d.width, d.height, f, double(f) * 4.0 / 1048576.0, h, double(h) * 4.0 / 1048576.0,
                    d.width * d.height, even ? d.width * d.height / 4u : d.width * d.height);
    }
    std::printf("   the scene's sun pyramids: %llu texels (%.2f MiB) at full resolution -> %llu (%.2f MiB) at half\n",
                fullT, double(fullT) * 4.0 / 1048576.0, halfT, double(halfT) * 4.0 / 1048576.0);

    // ---- U5's TERM: the paired arms, ABAB, the receiver test on ------------------------------------------------
    std::vector<double> a1, a0;
    {
        enginetest::GpuTimingWindow timing(gE);
        std::vector<MonitorRecord> drop;
        for (int round = 0; round < 2; ++round)
            for (int arm = 1; arm >= 0; --arm) {
                gE->setArm("shadow.lightDepth", double(arm));
                enginetest::frames(gE, 10);
                gE->popMonitorRecords(drop);
                enginetest::frames(gE, 40);
                shadowMs(arm ? a1 : a0);
            }
        gE->setArm("shadow.lightDepth", 1.0);
    }
    const double m1 = median(a1), m0 = median(a0), maps = double(std::max(1u, sunMaps));
    std::printf("target: atom.light_depth_term the view's shadow node GPU ms, shadow.lightDepth 1 %.4f / 0 %.4f "
                "(%zu / %zu frames): U5's term %+.4f ms a sun map over %u maps (no bar)\n",
                m1, m0, a1.size(), a0.size(), (m1 - m0) / maps, sunMaps);
    CHECK_MSG(a1.size() >= 60u && a0.size() >= 60u, "both arms timed (%zu / %zu frames)", a1.size(), a0.size());

    // ---- THE PYRAMID BUILDS' OWN ms (the measuring arm, a window of its own) -----------------------------------
    {
        enginetest::GpuTimingWindow timing(gE);
        gE->setArm("shadow.casterPbsProbe", 1.0);
        enginetest::frames(gE, 6);
        std::vector<MonitorRecord> recs;
        gE->popMonitorRecords(recs);
        enginetest::frames(gE, 20);
        recs.clear();
        gE->popMonitorRecords(recs);
        gE->setArm("shadow.casterPbsProbe", 0.0);
        std::vector<double> build;
        for (const MonitorRecord &r : recs) {
            double sum = 0.0;
            unsigned n = 0;
            for (const CacheWork &w : r.cacheWork)
                if (w.detail == "atom.caster.pyramid" && w.gpuMs >= 0.0f) { sum += w.gpuMs; ++n; }
            if (n) build.push_back(sum / double(n));
        }
        std::printf("target: atom.light_depth_term one sun map's pyramid build %.4f GPU ms (median over %zu frames; "
                    "no bar)\n", median(build), build.size());
    }

    gE->destroyView(view);
    gE->destroyScene(s);
    engine.reset();
    std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
