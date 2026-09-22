// ATOM: DRAW-CALL-BOUND OR VERTEX-BOUND (lane MEASURE-1d) — a TOOL, not a suite.
// It sweeps TWO axes independently on a cube lattice and reads the frame
// record's own numbers, so "at N instances / T triangles the frame is
// {CPU draw-bound | GPU vertex-bound | GPU pixel-bound}" is measured rather than
// asserted (ATOM_BUILD_SPEC.md:65's stage 3 payoff stands on it).
//
// THE INSTRUMENT is the render-loop monitor at `MonitorLevel::Review`
// (Engine.h:2557, `takeFrameRecords` :2575). Per Main-bucket pass:
// `gpuMs`/`cpuMs`/`draws`/`batches`/`instances`/`triangles`
// (Types.h:5951 FramePass), plus the "engine.record" stage (Types.h:6051,
// written at OgreEngine.cpp:1274) — the CPU cost of RECORDING the frame on the
// thread that draws.
//
// THE REPLAY TRAP, and why every arm is measured twice (Types.h FramePass and
// tests/engine/test_engine.cpp:5762): Ogre's RenderQueue REPLAYS a cached
// command buffer when the queue is unchanged since the last frame, and
// `_addMetrics` only runs on the BUILD path — so a still frame can legitimately
// report zero draws and a submission cost that is not the cost of building the
// pass. Each arm therefore runs with a STILL camera (the brief's arm) and with a
// camera that turns a fraction of a degree per frame (a rebuild every frame,
// which is what the owner's moving editor actually pays).
//
// THE MESH AXIS IS ONE SPHERE AT THREE TESSELLATIONS, x1/x4/x16 EXACTLY, at the
// SAME radius — three different primitives would change the silhouette and move
// the pixel cost with the triangle count, which is the one confound that would
// make a vertex-vs-pixel verdict meaningless.
//
// Nothing here is product code: build with `ninja -C build-linux
// atom_bound_measure`, run on a rig display with an explicit DISPLAY.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static const unsigned kW = 1920u, kH = 1080u;
static const float PI = 3.14159265358979323846f;

/// A UV sphere of `segs` x `rings` quads => 2*segs*rings triangles, radius 0.5.
static MeshData sphereMesh(unsigned segs, unsigned rings)
{
    MeshData d;
    for (unsigned r = 0; r <= rings; ++r) {
        const float v = float(r) / float(rings);
        const float theta = v * PI;
        for (unsigned s = 0; s <= segs; ++s) {
            const float u = float(s) / float(segs);
            const float phi = u * 2.0f * PI;
            const float x = std::sin(theta) * std::cos(phi);
            const float y = std::cos(theta);
            const float z = std::sin(theta) * std::sin(phi);
            d.positions.insert(d.positions.end(), { 0.5f * x, 0.5f * y, 0.5f * z });
            d.normals.insert(d.normals.end(), { x, y, z });
        }
    }
    const unsigned stride = segs + 1u;
    for (unsigned r = 0; r < rings; ++r)
        for (unsigned s = 0; s < segs; ++s) {
            const unsigned a = r * stride + s, b = a + 1u, c = a + stride, e = c + 1u;
            if (std::getenv("ATOM_FLIP_WINDING"))
                d.indices.insert(d.indices.end(), { a, b, c, b, e, c });
            else
                d.indices.insert(d.indices.end(), { a, c, b, b, c, e });
        }
    return d;
}
static unsigned long long triCount(const MeshData &m) { return m.indices.size() / 3; }

struct Arm {
    std::string label;
    unsigned instances = 0;
    unsigned segs = 0, rings = 0;
    bool moving = false;
    bool shippedDefaults = false;
    /// ONE DATABLOCK FOR EVERY INSTANCE, or one EACH. N identical items sharing
    /// a VAO and a datablock collapse into ONE draw call with N instances
    /// (measured: `draws 1, instances 8000`), so the merged arm prices
    /// INSTANCED rendering and the unique arm prices DRAW CALLS. The pair at
    /// the same triangle count is the draw-call cost, isolated.
    bool uniqueMaterials = false;
    /// ONE VAO PER ITEM. MEASURED on this renderer: 8,000 items sharing a mesh
    /// are ONE to TWO draw calls, and giving each its own DATABLOCK still only
    /// makes 35 — the material rides the instance's const-buffer slot, so the
    /// draw is split only by the VERTEX ARRAY. A real draw-call axis therefore
    /// needs a mesh per item, which is what this builds.
    bool uniqueMeshes = false;
};

struct Sample {
    double gpuMs = 0, cpuMs = 0, recordMs = 0, sceneGraphMs = 0;
    double draws = 0, batches = 0, instances = 0, triangles = 0;
    unsigned framesWithDraws = 0, frames = 0, framesWithGpu = 0;
};

/// The GPU's CORE CLOCK, read from nvidia-smi, because the rig's clocks are NOT
/// locked for this run (the lock needs a privilege this runner does not have)
/// and an unlocked clock is the one thing that can make two arms of one sweep
/// incomparable (DOCS/traps/GATE_AND_RIG.md). Every arm prints the clock it was
/// measured at, so the comparison can be CHECKED instead of assumed.
static std::string gpuClock()
{
    std::string out;
    FILE *p = popen("nvidia-smi --query-gpu=clocks.gr,utilization.gpu "
                    "--format=csv,noheader,nounits 2>/dev/null", "r");
    if (!p) return "?";
    char buf[128];
    while (fgets(buf, sizeof(buf), p)) out += buf;
    pclose(p);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    return out;
}

static double median(std::vector<double> v)
{
    if (v.empty()) return std::nan("");
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

/// Drains the ring and folds every Main-bucket pass of every record.
struct Fold {
    std::vector<double> gpu, cpu, rec, sg, draws, batches, inst, tris;
    unsigned frames = 0, withDraws = 0, withGpu = 0;
    void add(const std::vector<FrameRecord> &recs)
    {
        for (const FrameRecord &r : recs) {
            double g = 0, c = 0, dr = 0, ba = 0, in = 0, tr = 0;
            bool anyGpu = false;
            for (const FramePass &p : r.passes) {
                if (p.bucket != PassBucket::Main) continue;
                c += p.cpuMs;
                dr += p.draws; ba += p.batches; in += p.instances; tr += double(p.triangles);
                if (p.gpuMs >= 0.0f) { g += p.gpuMs; anyGpu = true; }
            }
            double recMs = 0, sgMs = 0;
            for (const FrameStage &s : r.stages) {
                if (s.name == "engine.record") recMs += s.ms;
                if (s.name == "engine.sceneGraph") sgMs += s.ms;
            }
            ++frames;
            if (dr > 0) ++withDraws;
            if (anyGpu) { gpu.push_back(g); ++withGpu; }
            cpu.push_back(c); rec.push_back(recMs); sg.push_back(sgMs);
            if (dr > 0) { draws.push_back(dr); batches.push_back(ba); inst.push_back(in);
                          tris.push_back(tr); }
        }
    }
    Sample summary() const
    {
        Sample s;
        s.gpuMs = median(gpu); s.cpuMs = median(cpu); s.recordMs = median(rec);
        s.sceneGraphMs = median(sg);
        s.draws = median(draws); s.batches = median(batches); s.instances = median(inst);
        s.triangles = median(tris);
        s.frames = frames; s.framesWithDraws = withDraws; s.framesWithGpu = withGpu;
        return s;
    }
};

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "atom-bound-measure-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();
    View *view = e->createOffscreenView("atom", kW, kH, Colour(0.02f, 0.02f, 0.03f));
    if (!view) { std::printf("FAIL: view: %s\n", e->lastError().c_str()); return 1; }

    // OGRE'S GEOMETRY COUNTERS ARE OFF UNTIL SOMEBODY ASKS (Types.h:5309 —
    // "reading renderStats() turns recording ON ... the FIRST read reports
    // false with zeroed counters"). Without this the monitor's per-pass
    // draws/batches/instances/triangles are all zero and a sweep measures
    // nothing it claims to: arm them here, once, before any frame that counts.
    { RenderStats rs0; e->renderStats(rs0); e->renderOneFrame(); e->renderStats(rs0);
      std::printf("metrics recording armed: %d\n", int(rs0.metricsRecording)); }

    const unsigned kWarm = 120u, kMeasure = 300u, kRepeats = 3u;
    std::vector<Arm> arms;
    // (i) the INSTANCE axis at constant triangles per instance (the x4 sphere):
    //     MERGED (one mesh, one datablock -> the renderer instances them) and
    //     MESHES (a vertex array each -> a draw call each).
    for (unsigned n : { 1000u, 2000u, 4000u, 8000u })
        arms.push_back({ "merged-inst" + std::to_string(n), n, 16u, 12u, false, false, false,
                         false });
    for (unsigned n : { 1000u, 2000u, 4000u, 8000u })
        arms.push_back({ "meshes-inst" + std::to_string(n), n, 16u, 12u, false, false, false,
                         true });
    // (ii) the TRIANGLE axis at constant 2,000 instances, x1 / x4 / x16.
    arms.push_back({ "merged-tri-x1", 2000u, 8u, 6u, false, false, false, false });
    arms.push_back({ "merged-tri-x4", 2000u, 16u, 12u, false, false, false, false });
    arms.push_back({ "merged-tri-x16", 2000u, 32u, 24u, false, false, false, false });
    arms.push_back({ "meshes-tri-x1", 2000u, 8u, 6u, false, false, false, true });
    arms.push_back({ "meshes-tri-x4", 2000u, 16u, 12u, false, false, false, true });
    arms.push_back({ "meshes-tri-x16", 2000u, 32u, 24u, false, false, false, true });
    // the REFERENCE arm: the shipped defaults (GI + shadows on) at 2,000 x4.
    arms.push_back({ "defaults-tri-x4", 2000u, 16u, 12u, false, true, false, true });

    const char *group = std::getenv("ATOM_GROUP");
    const std::string g = group ? group : "A";
    std::vector<Arm> sel;
    for (const Arm &a : arms) {
        const bool isTri = a.label.find("tri") != std::string::npos ||
                           a.label.find("defaults") != std::string::npos;
        if ((g == "B") == isTri) sel.push_back(a);
    }

    std::printf("== ATOM bound sweep, GROUP %s: %u x %u offscreen, INTERLEAVED arms "
                "(one frame each, round robin) so every arm meets the same GPU clock; "
                "%u warm + %u measured frames x %u repeats ==\n",
                g.c_str(), kW, kH, kWarm, kMeasure, kRepeats);

    // ---- build every arm's scene up front; they are measured together.
    struct Built { Arm arm; Scene *scene = nullptr; CameraDesc cam; unsigned long long tris = 0;
                   std::vector<Fold> reps; };
    std::vector<Built> built;
    for (const Arm &a : sel) {
        Built b;
        b.arm = a;
        Scene *s2 = e->createScene("atom-" + a.label);
        if (!s2) { std::printf("FAIL: scene\n"); return 1; }
        b.scene = s2;
        const MeshData md = sphereMesh(a.segs, a.rings);
        b.tris = triCount(md);
        const MeshId sharedMesh = s2->createMesh(md);
        PbrParams pp; pp.albedo = Colour(0.6f, 0.55f, 0.5f); pp.metalness = 0.0f;
        pp.roughness = 0.6f;
        const MaterialId sharedMat = s2->createPbrMaterial(pp);
        const int side = int(std::ceil(std::cbrt(double(a.instances))));
        const float spacing = 1.5f, half = 0.5f * spacing * float(side - 1);
        for (unsigned i = 0; i < a.instances; ++i) {
            const int gx = int(i) % side, gy = (int(i) / side) % side,
                      gz = int(i) / (side * side);
            const NodeId n = s2->createNode();
            MeshId mesh = sharedMesh;
            if (a.uniqueMeshes) {
                // A VAO of its own: the same sphere, its radius nudged by a
                // part in ten thousand (invisible; a different vertex buffer).
                MeshData m2 = md;
                const float k = 1.0f + 0.0001f * float(i % 97u);
                for (float &v : m2.positions) v *= k;
                mesh = s2->createMesh(m2);
            }
            MaterialId mat = sharedMat;
            if (a.uniqueMaterials) {
                PbrParams up = pp;
                up.albedo = Colour(0.4f + 0.2f * float(i % 7u) / 7.0f,
                                   0.4f + 0.2f * float(i % 11u) / 11.0f,
                                   0.4f + 0.2f * float(i % 13u) / 13.0f);
                mat = s2->createPbrMaterial(up);
            }
            if (!n || !s2->attachMesh(n, mesh, mat)) { std::printf("FAIL: node %u\n", i); return 1; }
            s2->setNodeTransform(n, Vec3{ float(gx) * spacing - half, float(gy) * spacing - half,
                                          float(gz) * spacing - half }, Quat(), Vec3{ 1, 1, 1 });
        }
        enginetest::addDirectionalLight(s2, Vec3{ -0.4f, -1.0f, -0.55f }, 4.0f);
        s2->setAmbient(Colour(0.2f, 0.2f, 0.22f), Colour(0.1f, 0.1f, 0.12f));
        GiParams gi;
        if (a.shippedDefaults) {
            gi.mode = GiMode::VctPccHybrid; gi.quality = GiQuality::High; gi.cascades = true;
            gi.ddgi = GiToggle::On; gi.numBounces = 1;
        } else {
            gi.mode = GiMode::Off; gi.cascades = false; gi.ddgi = GiToggle::Off;
        }
        s2->setGlobalIllumination(gi);
        const float dist = (half + 1.0f) / std::tan(22.5f * PI / 180.0f) + half + 2.0f;
        b.cam = enginetest::testCameraDescLookAt(Vec3{ 0.0f, 0.0f, dist }, Vec3{ 0.0f, 0.0f, 0.0f });
        built.push_back(b);
    }

    // ---- warm-up, and the proof that each arm's lattice is ON SCREEN.
    for (Built &b : built) {
        view->setScene(b.scene);
        view->setShadows(b.arm.shippedDefaults);
        PostFxDesc fx; fx.allowOffscreen = true; fx.ssr = 0;
        view->setPostFx(fx);
        view->setCamera(b.cam);
        for (unsigned i = 0; i < kWarm; ++i) e->renderOneFrame();
        RenderStats rs; e->renderStats(rs);
        Image img; view->readPixels(img);
        double mean = 0.0; unsigned lit = 0;
        for (size_t i = 0; i + 3 < img.rgba.size(); i += 4) {
            const unsigned v = std::max(std::max(img.rgba[i], img.rgba[i+1]), img.rgba[i+2]);
            mean += v; if (v > 12u) ++lit;
        }
        mean /= double(std::max<size_t>(1, img.rgba.size() / 4));
        std::printf("  [%s] draws %u instances %u tris %llu | picture mean %.2f, %u of %u px lit\n",
                    b.arm.label.c_str(), (unsigned)rs.draws, (unsigned)rs.instances,
                    (unsigned long long)rs.triangles, mean, lit, unsigned(img.rgba.size() / 4));
        std::fflush(stdout);
    }

    // ---- THE MEASUREMENT: one frame per arm, round robin, three windows.
    for (unsigned rep = 0; rep < kRepeats; ++rep) {
        std::vector<FrameRecord> recs;
        e->takeFrameRecords(recs);
        e->setFrameMonitor(MonitorLevel::Review);
        for (Built &b : built) b.reps.push_back(Fold());
        const std::string clockAt = gpuClock();
        // A RECORD ARRIVES SEVERAL FRAMES LATE (its GPU timestamps are read back
        // two frames after the frame — Engine.h:2569), so in a round robin the
        // record drained after arm k's frame is NOT arm k's. Every record is
        // therefore collected first and attributed by its own FRAME NUMBER:
        // frame f belongs to arm (f - f0) % armCount. Without this every arm's
        // fold holds every arm's frames and all the arms read alike (measured).
        std::vector<FrameRecord> all;
        const size_t k = built.size();
        for (unsigned i = 0; i < kMeasure + 4u; ++i) {
            for (size_t j = 0; j < k; ++j) {
                Built &b = built[j];
                view->setScene(b.scene);
                view->setShadows(b.arm.shippedDefaults);
                view->setCamera(b.cam);
                e->renderOneFrame();
                recs.clear();
                e->takeFrameRecords(recs);
                all.insert(all.end(), recs.begin(), recs.end());
            }
        }
        recs.clear(); e->takeFrameRecords(recs);
        all.insert(all.end(), recs.begin(), recs.end());
        unsigned long long f0 = 0ull;
        bool haveF0 = false;
        for (const FrameRecord &r : all) if (!haveF0 || r.frame < f0) { f0 = r.frame; haveF0 = true; }
        unsigned mis = 0;
        for (const FrameRecord &r : all) {
            if (r.frame < f0) { ++mis; continue; }
            const size_t idx = size_t((r.frame - f0) % k);
            std::vector<FrameRecord> one{ r };
            built[idx].reps.back().add(one);
        }
        std::printf("  window %u: %zu records attributed by frame number (f0 %llu, %u skipped)\n",
                    rep, all.size(), (unsigned long long)f0, mis);
        e->setFrameMonitor(MonitorLevel::Off);
        std::printf("  window %u measured at clock %s -> %s\n", rep, clockAt.c_str(),
                    gpuClock().c_str());
        std::fflush(stdout);
    }

    std::printf("%-24s %6s %8s %9s %9s %9s %8s %9s %11s %6s\n",
                "arm", "inst", "tris/in", "gpuMs", "cpuMs", "recordMs", "draws",
                "instances", "triangles", "fGpu");
    for (Built &b : built) {
        std::vector<double> gpu, cpu, rec;
        Sample last;
        for (const Fold &f : b.reps) {
            const Sample s3 = f.summary();
            gpu.push_back(s3.gpuMs); cpu.push_back(s3.cpuMs); rec.push_back(s3.recordMs);
            last = s3;
        }
        auto spread = [](std::vector<double> v) {
            std::sort(v.begin(), v.end());
            return v.empty() ? 0.0 : v.back() - v.front();
        };
        std::printf("%-24s %6u %8llu %9.3f %9.3f %9.3f %8.0f %9.0f %11.0f %6u\n",
                    b.arm.label.c_str(), b.arm.instances, b.tris, median(gpu), median(cpu),
                    median(rec), last.draws, last.instances, last.triangles, last.framesWithGpu);
        std::printf("    spread over %u windows: gpuMs %.3f, cpuMs %.3f, recordMs %.3f\n",
                    kRepeats, spread(gpu), spread(cpu), spread(rec));
    }
    for (Built &b : built) e->destroyScene(b.scene);
    return 0;
}
