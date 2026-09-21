// gi.surface_cache_spike — SURFACE-CACHE-0, the phase-0 DESIGN SPIKE's numbers.
//
// WHAT THIS SUITE IS FOR. `SPECS/SURFACE_CACHE_ASSESSMENT.md` §8 records three
// numbers as unmeasured, and §7 phase 0 is the lane that takes them. This suite
// is where they are taken and PRINTED — it is a measurement, not a gate, and
// its only assertions are the ones that would make the numbers meaningless if
// they failed (the cards captured something; the A/B really switched).
//
//   1. THE CAPTURE COST. One mesh instance's six axis-aligned cards, captured
//      through a JahshakaPcc-shaped compositor workspace into resident
//      Type2DArray atlases, at 128 and 256 texels a card, for a 2 m crate and
//      for a wall-sized panel. CPU wall milliseconds per round and per card.
//   2. THE CARD READ. The same fixture `gi.rt_reflect` uses — a mirror wall, an
//      emissive cube BEHIND the camera — with the reflection ray job reading
//      the cube's cards at its hits instead of the cascade's voxels. The
//      dispatch's own GPU milliseconds either way.
//   3. THE PICTURE. Both arms IN ONE PROCESS at one pose (the paired-arm law),
//      the pixel delta between them, and the images written beside the log.
//
// BOTH ARMS IN ONE PROCESS AND AT ONE POSE is the whole discipline here: a GPU
// number taken in a second run of the same binary is a number about the clock
// state of the machine (CLAUDE.md, PHOTON-E2 item 0), and a picture compared
// across processes is a picture compared across two GI settles.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)
#define CHECK_MSG(cond, ...)                                                    \
    do {                                                                        \
        std::printf((cond) ? "ok: " : "FAIL: ");                                \
        std::printf(__VA_ARGS__);                                               \
        std::printf("\n");                                                      \
        if (!(cond)) ++failures;                                                \
    } while (0)

static const unsigned kSize = 192;

static void render(Engine *e, int n) { for (int i = 0; i < n; ++i) e->renderOneFrame(); }

/// THE HELPERS' CUBE CARRIES NO UVs, and a card with no pattern on it measures
/// only a silhouette — so this suite builds its own: the same six quads, with
/// each face's own 0..1 coordinates, so a texture lands on every face once.
static MeshData texturedCubeMesh()
{
    MeshData d = enginetest::unitCubeMesh();
    for (int f = 0; f < 6; ++f) {
        const float uv[4][2] = { { 0.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 0.0f }, { 0.0f, 0.0f } };
        for (int v = 0; v < 4; ++v) d.uvs.insert(d.uvs.end(), { uv[v][0], uv[v][1] });
    }
    return d;
}

static void writePpm(const Image &img, const std::string &path)
{
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%u %u\n255\n", img.width, img.height);
    for (unsigned y = 0; y < img.height; ++y)
        for (unsigned x = 0; x < img.width; ++x) {
            const Colour c = img.at(x, y);
            const unsigned char rgb[3] = {
                (unsigned char)(c.r < 0 ? 0 : (c.r > 1 ? 255 : c.r * 255.0f + 0.5f)),
                (unsigned char)(c.g < 0 ? 0 : (c.g > 1 ? 255 : c.g * 255.0f + 0.5f)),
                (unsigned char)(c.b < 0 ? 0 : (c.b > 1 ? 255 : c.b * 255.0f + 0.5f)) };
            std::fwrite(rgb, 1, 3, f);
        }
    std::fclose(f);
    std::printf("    wrote %s\n", path.c_str());
}

/// How red the WALL REGION is — `gi.rt_reflect`'s own measure, so the two
/// suites' numbers can be read against each other.
static float redExcess(const Image &img)
{
    double sum = 0.0;
    unsigned n = 0;
    for (unsigned y = img.height / 4; y < img.height * 3 / 4; ++y)
        for (unsigned x = img.width / 4; x < img.width * 3 / 4; ++x) {
            const Colour &c = img.at(x, y);
            sum += double(c.r) - double(std::max(c.g, c.b));
            ++n;
        }
    return n ? float(sum / double(n)) : 0.0f;
}

/// THE ARM'S PICTURE, AVERAGED OVER N FRAMES — and averaging is not a
/// convenience here, it is the only honest instrument. One ray per pixel per
/// frame is a stochastic estimator and the chain's incremental settle keeps
/// injecting, so two readings of the SAME arm, taken apart, differ by hundreds
/// of pixels (measured on this fixture: ~800 of 36,864 at up to 203/255). A
/// frame count is not a settle (CLAUDE.md, cameras.exposure / PHOTON-M3) and a
/// still-frame test cannot pass on a picture that never stops moving — so the
/// arm is the MEAN of N consecutive frames, and the suite prints the same
/// statistic taken twice on the SAME arm beside the A/B, as its own noise
/// floor. A difference smaller than that floor is not a difference.
static void readAveraged(Engine *e, View *v, unsigned frames, std::vector<float> &out,
                         unsigned &w, unsigned &h)
{
    Image img;
    out.clear();
    w = h = 0;
    for (unsigned i = 0; i < frames; ++i) {
        render(e, 1);
        if (!v->readPixels(img)) return;
        if (out.empty()) {
            w = img.width; h = img.height;
            out.assign(size_t(w) * h * 3u, 0.0f);
        }
        for (unsigned y = 0; y < h; ++y)
            for (unsigned x = 0; x < w; ++x) {
                const Colour c = img.at(x, y);
                float *o = &out[(size_t(y) * w + x) * 3u];
                o[0] += c.r; o[1] += c.g; o[2] += c.b;
            }
    }
    for (float &f : out) f /= float(frames);
}

/// The mean of the frames as an Image, for the sheet.
static Image toImage(const std::vector<float> &a, unsigned w, unsigned h)
{
    Image img;
    img.width = w; img.height = h;
    img.rgba.assign(size_t(w) * h * 4u, 0u);
    const auto q = [](float v) {
        return (unsigned char)(std::min(std::max(v, 0.0f), 1.0f) * 255.0f + 0.5f);
    };
    for (size_t i = 0, n = size_t(w) * h; i < n; ++i) {
        img.rgba[i * 4 + 0] = q(a[i * 3 + 0]);
        img.rgba[i * 4 + 1] = q(a[i * 3 + 1]);
        img.rgba[i * 4 + 2] = q(a[i * 3 + 2]);
        img.rgba[i * 4 + 3] = 255u;
    }
    return img;
}

static void meanDelta(const std::vector<float> &a, const std::vector<float> &b, unsigned w,
                      unsigned h, unsigned &moved, float &worst, double &mean)
{
    moved = 0; worst = 0.0f; mean = 0.0;
    if (a.size() != b.size() || a.empty()) return;
    double sum = 0.0;
    for (size_t i = 0, n = size_t(w) * h; i < n; ++i) {
        float d = 0.0f;
        for (int k = 0; k < 3; ++k) d = std::max(d, std::abs(a[i * 3 + k] - b[i * 3 + k]));
        d *= 255.0f;
        if (d >= 1.0f) { ++moved; sum += double(d); }
        worst = std::max(worst, d);
    }
    mean = moved ? sum / double(moved) : 0.0;
}

static void pixelDelta(const Image &a, const Image &b, unsigned &moved, unsigned &total,
                       float &worst, double &mean)
{
    moved = 0; total = 0; worst = 0.0f; mean = 0.0;
    if (a.width != b.width || a.height != b.height) return;
    double sum = 0.0;
    for (unsigned y = 0; y < a.height; ++y)
        for (unsigned x = 0; x < a.width; ++x) {
            const Colour &p = a.at(x, y), &q = b.at(x, y);
            const float d = std::max(std::max(std::abs(p.r - q.r), std::abs(p.g - q.g)),
                                     std::abs(p.b - q.b)) * 255.0f;
            ++total;
            if (d >= 0.5f) { ++moved; sum += double(d); }
            worst = std::max(worst, d);
        }
    mean = moved ? sum / double(moved) : 0.0;
}

// ---------------------------------------------------------------------------
int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-surface-cache-spike-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();

    View *view = e->createOffscreenView("cardspike", kSize, kSize, Colour(0, 0, 0));
    Scene *s = e->createScene("cardspike");
    if (!view || !s) { std::printf("FAIL: view/scene: %s\n", e->lastError().c_str()); return 1; }
    view->setScene(s);

    const bool haveRays = e->rayQueryAvailable() && e->rayTracing();
    s->setAmbient(Colour(0.20f, 0.20f, 0.20f), Colour(0.15f, 0.15f, 0.15f));

    // ---- gi.rt_reflect's fixture, unchanged ---------------------------------
    const NodeId wall = s->createNode();
    PbrParams wallParams;
    wallParams.albedo = Colour(1.0f, 1.0f, 1.0f);
    wallParams.metalness = 1.0f;
    wallParams.roughness = 0.0f;
    const MaterialId wallMat = s->createPbrMaterial(wallParams);
    const MeshId wallMesh = s->createMesh(enginetest::unitCubeMesh());
    CHECK(wall && wallMat && wallMesh && s->attachMesh(wall, wallMesh, wallMat),
          "the mirror wall exists");
    enginetest::setNodeScale(s, wall, Vec3(14.0f, 9.0f, 0.3f));
    enginetest::setNodePosition(s, wall, Vec3(0.0f, 2.0f, 5.0f));

    const NodeId cube = s->createNode();
    {
        PbrParams p;
        p.albedo = Colour(0.05f, 0.05f, 0.05f);
        p.emissive = Colour(4.0f, 0.0f, 0.0f);
        p.roughness = 0.6f;
        const MaterialId mat = s->createPbrMaterial(p);
        const MeshId mesh = s->createMesh(texturedCubeMesh());
        CHECK(cube && mat && mesh && s->attachMesh(cube, mesh, mat),
              "the emissive cube exists (behind the camera)");
        // AND IT CARRIES A PATTERN, which is the whole point of the picture
        // arm: a uniform emitter reads the same through a card and through a
        // voxel, so the only thing a uniform fixture could measure is the
        // silhouette. An 8x8 checker at 64 texels is 12.5 cm of pattern on a
        // 3 m cube — four times the finest DIRECTIONAL voxel texel of cascade 0
        // at this table (16 cm), and a tenth of cascade 1's. The card's texel
        // here is 1.2 cm.
        std::vector<unsigned char> tex(64u * 64u * 4u);
        for (unsigned y = 0; y < 64u; ++y)
            for (unsigned x = 0; x < 64u; ++x) {
                const bool on = (((x / 8u) + (y / 8u)) & 1u) == 0u;
                unsigned char *o = &tex[(size_t(y) * 64u + x) * 4u];
                o[0] = on ? 255u : 10u; o[1] = 0u; o[2] = 0u; o[3] = 255u;
            }
        const TextureId emissiveMap = s->createTexture(64u, 64u, tex.data(), true, false);
        CHECK(emissiveMap && s->setPbrTexture(mat, PbrTextureSlot::Emissive, emissiveMap),
              "the cube wears a checkered emissive map");
    }
    enginetest::setNodeScale(s, cube, Vec3(3.0f, 3.0f, 3.0f));
    enginetest::setNodePosition(s, cube, Vec3(0.0f, 2.0f, -11.0f));

    // A 2 m CRATE, the assessment's own example, off to one side and out of the
    // shot: it exists to be captured, not to be seen.
    const NodeId crate = s->createNode();
    {
        PbrParams p;
        p.albedo = Colour(0.6f, 0.45f, 0.25f);
        p.roughness = 0.7f;
        const MaterialId mat = s->createPbrMaterial(p);
        const MeshId mesh = s->createMesh(enginetest::unitCubeMesh());
        CHECK(crate && mat && mesh && s->attachMesh(crate, mesh, mat), "the 2 m crate exists");
    }
    enginetest::setNodeScale(s, crate, Vec3(2.0f, 2.0f, 2.0f));
    enginetest::setNodePosition(s, crate, Vec3(-9.0f, 1.0f, -6.0f));

    enginetest::addDirectionalLight(s, Vec3(-0.3f, -1.0f, 0.4f), 2.0f);

    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.numBounces = 1;
    gi.testBoundsMin = Vec3(-14.0f, -2.0f, -14.0f);
    gi.testBoundsMax = Vec3(14.0f, 10.0f, 7.0f);
    CHECK(s->setGlobalIllumination(gi), "the voxel arm builds over the whole fixture");

    enginetest::testCameraLookAt(view, Vec3(0.0f, 2.0f, -6.0f), Vec3(0.0f, 2.0f, 5.0f));
    PostFxDesc fx;
    fx.allowOffscreen = true;
    fx.ssr = 2;
    view->setPostFx(fx);
    render(e, 24);

    // ---- 1. THE CAPTURE COST ------------------------------------------------
    std::printf("\n== 1. THE CAPTURE COST (six axis cards, one workspace update a round)\n");
    struct Arm { const char *what; NodeId node; unsigned size; };
    const Arm arms[] = { { "2 m crate", crate, 128u }, { "2 m crate", crate, 256u },
                         { "9 m panel (the mirror wall)", wall, 128u },
                         { "9 m panel (the mirror wall)", wall, 256u } };
    for (const Arm &a : arms) {
        SurfaceCardSpikeDesc d;
        d.action = SurfaceCardSpikeAction::Build;
        d.node = a.node;
        d.cardSize = a.size;
        SurfaceCardSpikeResult r;
        if (!s->surfaceCardSpike(d, r)) {
            CHECK_MSG(false, "card build (%s at %u): %s", a.what, a.size, r.error.c_str());
            continue;
        }
        // TWO MEASURED BATCHES after the build's own warm-up round, so a first
        // round that compiled the capture permutation cannot be the number.
        SurfaceCardSpikeDesc c;
        c.action = SurfaceCardSpikeAction::Capture;
        c.rounds = 4u;
        SurfaceCardSpikeResult warm, meas;
        s->surfaceCardSpike(c, warm);
        // THE GPU HALF, through the engine's OWN instrument. The monitor
        // records every compositor pass's timestamp pair (ogre-patch 0027) and
        // the capture workspace is in its listener walk, so the card passes
        // arrive as `FramePass` rows named by their profiling id — no timing
        // code of this lane's own, and the same mechanism every other pass in
        // the engine is measured with.
        e->setFrameMonitor(MonitorLevel::Review);
        // A FRAME FIRST, and it is load-bearing: the monitor attaches its
        // listener to every live workspace at the head of a FRAME, and this
        // spike drives its workspace between frames — with no frame in between,
        // the capture passes are recorded by nobody and the GPU column reads
        // "not measured" while the build has timestamps.
        render(e, 2);
        std::vector<FrameRecord> recs;
        e->takeFrameRecords(recs);
        c.rounds = 32u;
        s->surfaceCardSpike(c, meas);
        // ...AND THE SAME CAPTURE AGAIN, INSIDE THE FRAME. The out-of-frame
        // rounds above are the only way to time ONE capture's CPU cost on its
        // own; this arm is what a shipped capture does — the workspace runs as
        // part of renderOneFrame — and it is the only arm the monitor's GPU
        // timestamps can cover, because the monitor attaches its listeners at a
        // frame's head.
        {
            SurfaceCardSpikeDesc arm;
            arm.action = SurfaceCardSpikeAction::ArmInFrame;
            SurfaceCardSpikeResult ar;
            s->surfaceCardSpike(arm, ar);
            render(e, 24);
            arm.action = SurfaceCardSpikeAction::DisarmInFrame;
            s->surfaceCardSpike(arm, ar);
        }
        render(e, 8);                       // the timestamps come back frames later
        recs.clear();
        e->takeFrameRecords(recs);
        double gpuSum = 0.0; unsigned gpuN = 0; double cpuSum = 0.0;
        {
            const MonitorStatus ms = e->monitorStatus();
            std::printf("    [monitor] frames %llu, listeners %u, gpuCompiled %d supported %d "
                        "active %d truncated %u %s\n",
                        (unsigned long long)ms.framesRecorded, ms.attachedListeners,
                        int(ms.gpuCompiled), int(ms.gpuSupported), int(ms.gpuActive),
                        ms.gpuSamplesTruncated, ms.gpuReason.c_str());
            unsigned rows = 0;
            for (const FrameRecord &fr : recs) rows += unsigned(fr.passes.size());
            std::printf("    [monitor] %zu frame records, %u pass rows\n", recs.size(), rows);
        }
        for (const FrameRecord &fr : recs)
            for (const FramePass &fp : fr.passes)
                if (fp.pass == "Jahshaka card capture") {
                    if (fp.gpuMs >= 0.0f) { gpuSum += double(fp.gpuMs); ++gpuN; }
                    cpuSum += double(fp.cpuMs);
                }
        e->setFrameMonitor(MonitorLevel::Off);
        std::printf("    %-28s %3u^2  CPU %7.3f ms / round  %6.3f ms / card  "
                    "(+ %5.3f ms the scratch graph walk, once a frame in a real capture)  "
                    "tris %5u  VRAM %6.2f MB  texel %.4f m\n",
                    a.what, a.size, double(meas.cpuMsPerRound), double(meas.cpuMsPerCard),
                    double(meas.graphMsPerRound), meas.triangles,
                    double(meas.vramBytes) / (1024.0 * 1024.0), double(meas.texelWorld));
        if (cpuSum > 0.0)
            std::printf("    %-28s %3u^2  CPU in-frame %6.4f ms / card over the monitor's own "
                        "pass rows\n", a.what, a.size, cpuSum / double(gpuN ? gpuN : 1u));
        if (gpuN)
            std::printf("    %-28s %3u^2  GPU %7.4f ms / card over %u timed card passes "
                        "(%.4f ms / round)\n",
                        a.what, a.size, gpuSum / double(gpuN), gpuN,
                        6.0 * gpuSum / double(gpuN));
        else
            std::printf("    %-28s %3u^2  GPU not measured (no JAH_GPU_TIMESTAMPS, or no "
                        "results back yet)\n", a.what, a.size);
        CHECK_MSG(meas.cpuMsPerRound > 0.0f, "a capture round for the %s at %u^2 was timed",
                  a.what, a.size);
    }

    // ---- 2 + 3. THE CARD READ, AND THE PICTURE ------------------------------
    std::printf("\n== 2/3. THE CARD READ AT A REFLECTION HIT (both arms, one process, one pose)\n");
    if (!haveRays) {
        std::printf("ok: no ray queries on this machine — the card READ is about the ray tier "
                    "and skips cleanly (the capture numbers above stand)\n");
        SurfaceCardSpikeDesc dd; dd.action = SurfaceCardSpikeAction::Destroy;
        SurfaceCardSpikeResult rr; s->surfaceCardSpike(dd, rr);
        std::printf("%s\n", failures ? "FAILED" : "PASSED");
        return failures ? 1 : 0;
    }

    // The CUBE is what the mirror shows, so the cube is what gets the cards.
    {
        SurfaceCardSpikeDesc d;
        d.action = SurfaceCardSpikeAction::Build;
        d.node = cube;
        d.cardSize = 256u;
        SurfaceCardSpikeResult r;
        CHECK_MSG(s->surfaceCardSpike(d, r), "the emissive cube's cards are captured at 256^2 "
                  "(slot %u, texel %.4f m, VRAM %.2f MB) %s",
                  r.itemSlot, double(r.texelWorld), double(r.vramBytes) / (1024.0 * 1024.0),
                  r.error.c_str());
        SurfaceCardSpikeDesc dump;
        dump.action = SurfaceCardSpikeAction::Dump;
        dump.dumpPath = "surface-card";
        SurfaceCardSpikeResult dr;
        s->surfaceCardSpike(dump, dr);
    }

    const unsigned kAvg = 48u;   // frames averaged into one arm's picture
    unsigned w = 0, h = 0;
    const auto readArm = [&](bool cards, const char *what, std::vector<float> &out) {
        SurfaceCardSpikeDesc d;
        d.action = cards ? SurfaceCardSpikeAction::ReadOn : SurfaceCardSpikeAction::ReadOff;
        SurfaceCardSpikeResult r;
        s->surfaceCardSpike(d, r);
        render(e, 48);                       // let the switch reach the mean
        readAveraged(e, view, kAvg, out, w, h);
        const Image img = toImage(out, w, h);
        const RayQueryStatus rq = s->rayQueryStatus();
        std::printf("    %-24s red excess %.4f   reflect dispatch %.4f ms (%d rays)\n", what,
                    double(redExcess(img)), double(rq.reflectMs), rq.reflectRays);
        return rq.reflectMs;
    };

    // THREE POSES, BOTH ARMS, ALL IN ONE PROCESS — the sheet the owner reads,
    // and the paired-arm law's own shape: every pair is taken minutes apart in
    // the same process with the same clocks, never across two runs.
    struct Pose { const char *name; Vec3 eye, at; };
    const Pose poses[3] = {
        { "head-on",   Vec3(0.0f, 2.0f, -6.0f),  Vec3(0.0f, 2.0f, 5.0f) },
        { "oblique",   Vec3(-4.5f, 2.0f, -5.0f), Vec3(0.5f, 2.0f, 5.0f) },
        { "close-low", Vec3(0.8f, 0.8f, -2.0f),  Vec3(0.0f, 2.2f, 5.0f) },
    };
    float voxMsSum = 0.0f, cardMsSum = 0.0f;
    unsigned poseN = 0;
    double bestMean = 0.0, floorMean = 0.0;
    unsigned bestMoved = 0, floorMoved = 0;
    for (const Pose &po : poses) {
        std::printf("  -- pose '%s'\n", po.name);
        enginetest::testCameraLookAt(view, po.eye, po.at);
        std::vector<float> vox1, card1;
        voxMsSum += readArm(false, "voxels (today)", vox1);
        cardMsSum += readArm(true, "cards", card1);
        ++poseN;
        unsigned moved = 0; float worst = 0.0f; double mean = 0.0;
        meanDelta(vox1, card1, w, h, moved, worst, mean);
        std::printf("    cards vs voxels: %u of %u pixels move (%.1f %%), mean %.2f/255 over the "
                    "moved, worst %.0f/255\n",
                    moved, w * h, 100.0 * double(moved) / double(w * h ? w * h : 1u), mean,
                    double(worst));
        writePpm(toImage(vox1, w, h), std::string("surface-cache-") + po.name + "-voxels.ppm");
        writePpm(toImage(card1, w, h), std::string("surface-cache-") + po.name + "-cards.ppm");
        if (mean > bestMean) { bestMean = mean; bestMoved = moved; }
        // THE NOISE FLOOR, measured at the FIRST pose under the same conditions
        // as the signal: one ray per pixel per frame is stochastic and the
        // chain's settle keeps injecting, so two readings of the SAME arm
        // differ. A difference smaller than this floor is not a difference.
        if (poseN == 1u) {
            std::vector<float> vox2;
            readArm(false, "voxels again (the floor)", vox2);
            float nworst = 0.0f;
            meanDelta(vox1, vox2, w, h, floorMoved, nworst, floorMean);
            std::printf("    THE NOISE FLOOR (voxels vs voxels): %u pixels, mean %.2f/255, "
                        "worst %.0f\n", floorMoved, floorMean, double(nworst));
        }
    }
    const float voxMs = voxMsSum / float(poseN), cardMs = cardMsSum / float(poseN);
    std::printf("\n    THE RAY COST over %u poses: voxels %.4f ms, cards %.4f ms "
                "(delta %+.4f ms, %+.1f %%)\n",
                poseN, double(voxMs), double(cardMs), double(cardMs - voxMs),
                voxMs > 0.0f ? 100.0 * double(cardMs - voxMs) / double(voxMs) : 0.0);
    // THE SIGNAL IS THE SIZE OF THE CHANGE, NOT THE COUNT OF PIXELS THAT MOVED:
    // the stochastic tail moves a similar NUMBER of pixels either way, by a
    // fraction of a code.
    CHECK_MSG(bestMean > 4.0 * floorMean,
              "the card read changes the reflection well past the noise floor "
              "(mean %.2f/255 over %u pixels, against a floor of %.2f over %u)",
              bestMean, bestMoved, floorMean, floorMoved);

    // ---- THE TEARDOWN ORDER, which is a real assert and not hygiene --------
    //
    // A card set holds a scratch SceneManager whose one Item's SubItems LINK
    // this scene's datablocks, and `OgreScene::destroy()`'s material loop
    // destroys every one of them — so a scene torn down with a LIVE card set
    // used to take `~HlmsDatablock`'s assert on a datablock that still has
    // linked renderables (OgreHlmsDatablock.cpp:205) and leak the scratch
    // manager, the five atlases and the capture workspace to Root::shutdown.
    // The set is therefore deleted first inside destroy(), and this case is the
    // guard: a card set is built and DELIBERATELY NOT destroyed, then the whole
    // scene goes.
    //
    // HOW A REGRESSION SHOWS, measured by standing the fix down and re-running:
    // NOT as a failed assertion line here. The scene's own destroy() survives —
    // it leaves the scratch Item pointing at freed datablocks — and the process
    // SEGVs during the ENGINE's teardown, after this file has already printed
    // PASSED (exit 139, core dumped). ctest reds on the exit code, which is the
    // whole point of the case: without it a regression would surface as a
    // crash in whichever suite next happened to close a scene holding one.
    {
        SurfaceCardSpikeDesc d;
        d.action = SurfaceCardSpikeAction::Build;
        d.node = crate;
        d.cardSize = 128u;
        SurfaceCardSpikeResult r;
        CHECK(s->surfaceCardSpike(d, r), "a card set is rebuilt and left alive on purpose");
    }
    view->setScene(nullptr);
    e->destroyScene(s);
    s = nullptr;
    CHECK(true, "the scene is destroyed with a LIVE card set (a regression reds on the "
          "process's EXIT CODE, not on this line — see the note above)");

    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
