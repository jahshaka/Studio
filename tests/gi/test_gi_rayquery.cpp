// gi.rayquery — THE HARDWARE RAY-QUERY TIER (SPECS/PHOTON_SPEC.md §7 R1).
//
// The tier keeps a ray-traceable copy of the scene: one bottom-level
// acceleration structure per mesh, one top-level structure over the instances,
// built from the SAME Ogre vertex and index buffers the raster draws
// (fork d014b064f+b028638c1+1a81f866a (was 0038/0039/0040)) and recorded into the frame's own command
// buffer. Nothing in the picture consumes it yet — R2 (probe visibility),
// R3 (sun contact) and R5 (reflections) are the consumers, and each reads this
// same structure. So this suite cannot assert a pixel; it asserts GEOMETRY,
// which is the only thing the tier claims to know.
//
// What each case proves, and why it is the case that matters:
//
//   1. THE TIER COMES UP, AND THE PICTURE DOES NOT MOVE. Enabling ray queries
//      changes the Vulkan device (instance 1.2, three feature bits, two buffer
//      usage bits on every device-local pool). The proof that it is inert is
//      that the scene renders the same pixels with the tier holding structures
//      as with it switched off. A byte-identical picture, not a tolerance.
//
//   2. A RAY ANSWERS THE ANALYTIC DISTANCE. The honest proof that a ray tier
//      works is a ray whose hit can be compared with arithmetic: a known point,
//      a known direction, a known box. Not shading, not a probe — geometry.
//      It also proves the BLAS arithmetic (pool address + buffer start +
//      element offset) is right, because a wrong offset reads someone else's
//      triangles and lands somewhere else entirely.
//
//   3. THE TRACED SET IS THE WORLD, NOT THE EDITOR. The S3 spike traced every
//      Item the SceneManager held — gizmo arrows, light icons, the backdrop
//      (audit C-4) — and skinned Items at bind pose (C-5). A helper must not be
//      hittable, and hiding an object must remove it from the structure.
//
//   4. A STILL SCENE COSTS NOTHING. The tier gates on the same movement epoch
//      the shadow caster walk does. If it rebuilt every frame it would be a
//      permanent tax on a 60 Hz editor that nothing is yet consuming.
//
//   5. THE NO-RAYS SWITCH IS A REAL SWITCH. Off, the structures go and the
//      status reads available true / enabled false — this machine rendering the
//      picture a machine WITHOUT ray-tracing hardware renders. That is what
//      lets every later ray-consuming suite assert both pictures on one GPU
//      instead of assuming the fallback.
//
//   6. A MOVER MOVES ITS HIT. The point of a per-frame structure: move the box,
//      and the same ray reports the new distance.
//
//   7. TWO DRAWN SCENES IN ONE FRAME — the ordinary product case (the editor
//      plus a material-preview or thumbnail scene). Each must keep its own
//      structures and its own timings; the tier's frame accounting must not
//      count a per-SCENE call as a frame, or every "wait N frames in flight"
//      guard waits half as long as it claims and a scratch arena is freed
//      while the build that reads it is still queued (round 2, finding 3).
//
//   9. AN ALPHA-TEST FLIP MOVES THE TRACED SET ON A STILL SCENE. Alpha-tested
//      items are kept OUT of the structure, and the material edit that makes an
//      item cut-out changes no transform and no render queue — so without an
//      explicit epoch bump a still scene kept tracing the old set indefinitely
//      (round 3, finding 3).
//
//  10. A PARKED SCENE'S COMPACTION SLOTS ARE NOT STOLEN. A scene that drew,
//      queued compaction size queries and was then hidden before reading them
//      holds those slots; the ring must route around them rather than reset and
//      rewrite them, which would make the reader return another structure's
//      size and compact into a buffer sized for it (round 3, finding 1).
//
//   8. A DESTROYED SCENE TAKES ITS STRUCTURES WITH IT. Without this the tier
//      held every preview scene's BLASes — and the MeshPtrs under them — for
//      the process's life, and a recycled OgreScene address inherited a dead
//      scene's structures (round 2, finding 2).
//
// SKIPPED, NOT FAILED, without ray-query hardware: a device that does not
// advertise VK_KHR_ray_query is a supported platform (every Mac is one), and a
// suite that went red there would be asserting the driver, not the code.
//
// Its own binary like every GI suite: an engine per process (Ogre::Root is a
// singleton).
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

/// CHECK with the measurement in the line, so a failure says what it saw.
#define CHECK_MSG(cond, ...)                                                    \
    do {                                                                        \
        std::printf((cond) ? "ok: " : "FAIL: ");                                 \
        std::printf(__VA_ARGS__);                                                \
        std::printf("\n");                                                       \
        if (!(cond)) ++failures;                                                 \
    } while (0)

static const unsigned kSize = 128;

static void render(Engine *e, int frames = 1)
{
    enginetest::frames(e, frames);
}

/// One ray in the batch layout Scene::traceRays takes: origin.xyz + tMin,
/// direction.xyz + tMax, then the instance mask and three unused words.
static void pushRay(std::vector<float> &rays, const Vec3 &o, const Vec3 &d, float tMin, float tMax,
                    unsigned mask = 0xFFu)
{
    rays.insert(rays.end(), { o.x, o.y, o.z, tMin, d.x, d.y, d.z, tMax });
    // The mask word is read as an unsigned in the shader; it travels through a
    // float array, so it is BIT-COPIED rather than converted (a cast would turn
    // 0xFF into 255.0f and back into 255 by luck, but not every mask is small).
    float bits;
    const unsigned m = mask;
    std::memcpy(&bits, &m, sizeof(bits));
    rays.insert(rays.end(), { bits, 0.0f, 0.0f, 0.0f });
}

struct Hit { float distance; int node; int primitive; bool hit; };

static Hit hitAt(const std::vector<float> &hits, size_t i)
{
    return Hit{ hits[i * 4 + 0], int(hits[i * 4 + 1]), int(hits[i * 4 + 2]),
                hits[i * 4 + 3] > 0.5f };
}

/// A subdivided grid, `n` quads a side, lying in the XZ plane with its top at
/// y = +0.5 — thousands of triangles rather than a cube's twelve. Case 10 needs
/// meshes whose COMPACTED SIZES differ by orders of magnitude: if every mesh in
/// the scene is the same cube, a compaction slot read from the wrong structure
/// returns a numerically identical answer and the defect it is meant to catch
/// passes unnoticed.
static MeshData gridMesh(unsigned n)
{
    MeshData d;
    for (unsigned z = 0; z <= n; ++z)
        for (unsigned x = 0; x <= n; ++x) {
            const float fx = float(x) / float(n) - 0.5f;
            const float fz = float(z) / float(n) - 0.5f;
            d.positions.insert(d.positions.end(), { fx, 0.5f, fz });
            d.normals.insert(d.normals.end(), { 0.0f, 1.0f, 0.0f });
        }
    for (unsigned z = 0; z < n; ++z)
        for (unsigned x = 0; x < n; ++x) {
            const unsigned a = z * (n + 1) + x, b = a + 1, c = a + n + 1, e2 = c + 1;
            d.indices.insert(d.indices.end(), { a, c, b, b, c, e2 });
        }
    return d;
}

static bool imagesEqual(const Image &a, const Image &b)
{
    if (a.width != b.width || a.height != b.height) return false;
    for (unsigned y = 0; y < a.height; ++y)
        for (unsigned x = 0; x < a.width; ++x) {
            const Colour &p = a.at(x, y), &q = b.at(x, y);
            if (p.r != q.r || p.g != q.g || p.b != q.b) return false;
        }
    return true;
}

// =========================================================================================
// engine.ray_update_counts (--update-counts) and engine.ray_update_cost (--update-cost, a measure row)
// THE RAY TIER'S HOST UPDATE IS PATCHED BY WHAT CHANGED (ENGINE-UPGRADE-U5 stage 3, the bars ruled by the
// lead's decision 14.4). On Bistro's walk updateRayQuery was 12.9 % of the render thread: the job's per-mesh
// table re-written whole every frame a BLAS level was built, two scans over every BLAS key with a find each,
// the alpha and hit tables re-derived every frame. Now the tables are patched by the change feed's events.
// The guard COUNTS the work, never times it (RayQueryStatus: updateLookups = every hash or map operation,
// updateRows = every table row written, updateVisits = every element a whole pass visited), with a bar PER
// KIND OF CHANGE, each DERIVED from the code path (headroom 0: a count, not a time):
//   * IDLE (nothing changed): 0 lookups, 0 rows, 0 visits.
//   * k MOVED (transforms only - the instances are written on the device): 0 lookups, 0 rows; the visits are
//     the moved frame's one status pass, <= 1 per structure per frame.
//   * k HIDDEN or SHOWN: <= 1 lookup and 0 rows each - the key event's find (a built key is never missing).
//   * k ADDED, each its own mesh: <= kAddLookups = 23 lookups and 2 rows each -
//       1 the key event's find + 1 `missing` insert + 1 the build's visit of it + 1 the find after the build
//       + 1 `missing` erase + (1 + 8) the mesh table's new entry (its index, its record's kTlasLevels finds,
//       a row) + (1 + 8) the compaction's new address a few frames later (the dirty record's index + its
//       eight finds, a row) = 23; rows 2.
//   * k REMOVED: <= 2 lookups and 1 row each - the key event's find, the old mesh's index erased, and its
//       record ZERO-FILLED (a row).
//   * k RE-LEVELLED (an LOD mesh moved past the ray rule's band: its near copy's level changes): <= 26
//       lookups and 2 rows each - a slot whose keys changed re-takes BOTH its copies' keys, so four key
//       events (the near copy's old and new level, the far copy's out and back: a find each) + the new
//       level's 1 `missing` insert, its build's visit, the find after it and the erase (3) + its record
//       (1 + 8) + the compaction's (1 + 8) = 26; rows 2.
//   * an ALPHA-TESTED slot hidden: the alpha table is re-written whole: <= 1 lookup + the members left rows.
//   * a LAMP added, moved or DELETED: the hit table re-written whole: 0 lookups, its lights + material rows;
//     a deleted lamp's light is GONE from the table the next frame (decision 14.1).
//   * n > 8 CUT-OUT MASKS stale in one frame (their textures re-uploaded): all remade within ceil(n / 8)
//     frames of the first (decision 14.2 - past the budget a stale mask was served as made and never remade).
// The cost row prints the tier's update in ms (the engine.rayquery stage) at N meshes with 0 / 10 / 100
// changes a frame, over the CHANGE frames only (decision 14.3).
static const unsigned long long kAddLookups = 23ull;
static const unsigned long long kRelevelLookups = 26ull;

/// Eight vertical bars across u, the even ones solid, the odd ones clear; `tint` makes each texture its own.
static std::vector<unsigned char> barsTexels(unsigned tint)
{
    const unsigned n = 32;
    std::vector<unsigned char> rgba(size_t(n) * n * 4u);
    for (unsigned y = 0; y < n; ++y)
        for (unsigned x = 0; x < n; ++x) {
            unsigned char *p = &rgba[(size_t(y) * n + x) * 4u];
            const bool solid = ((x * 8u / n) % 2u) == 0u;
            p[0] = (unsigned char)(40u + 13u * tint); p[1] = 120; p[2] = 30; p[3] = solid ? 255 : 0;
        }
    return rgba;
}

/// A sphere with eight levels whose measured bounds double level to level (0.02 m at level 1): the ray
/// rule's level moves with the distance (the footprint grows with it).
static MeshData lodSphere()
{
    MeshData d;
    const float kPi = 3.14159265358979f;
    const int rings = 12, segments = 24;
    for (int r = 0; r <= rings; ++r) {
        const float th = float(r) / float(rings) * kPi;
        for (int sg = 0; sg < segments; ++sg) {
            const float ph = float(sg) / float(segments) * 2.0f * kPi;
            const float x = std::sin(th) * std::cos(ph), y = std::cos(th), z = std::sin(th) * std::sin(ph);
            d.positions.insert(d.positions.end(), { 0.5f * x, 0.5f * y, 0.5f * z });
            d.normals.insert(d.normals.end(), { x, y, z });
        }
    }
    for (int r = 0; r < rings; ++r)
        for (int sg = 0; sg < segments; ++sg) {
            const unsigned a = unsigned(r * segments + sg), b = unsigned(r * segments + (sg + 1) % segments);
            const unsigned c = a + unsigned(segments), e = b + unsigned(segments);
            d.indices.insert(d.indices.end(), { a, b, c, b, e, c });
        }
    for (int l = 1; l < 8; ++l) {
        d.lodIndices.push_back(d.indices);
        d.lodBounds.push_back(0.02f * float(1u << unsigned(l - 1)));
        d.lodErrors.push_back(0.02f * float(1u << unsigned(l - 1)));
    }
    return d;
}

struct Counts {
    unsigned long long lookups = 0, rows = 0, visits = 0;
};

static int updateMain(bool cost)
{
    std::string err;
    const auto cfg = enginetest::testEngineConfig(cost ? "test-gi-rayquery-update-cost-ogre.log"
                                                       : "test-gi-rayquery-update-counts-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->submitFrame(jahshaka::engine::FrameInput::delta(1.0f / 60.0f));
    Engine *e = engine.get();
    View *view = e->createOffscreenView("rq-update", kSize, kSize, Colour(0, 0, 0));
    Scene *scene = e->createScene("rq-update");
    view->setScene(scene);
    scene->setAmbient(Colour(0.3f, 0.3f, 0.35f), Colour(0.2f, 0.2f, 0.25f));
    enginetest::addDirectionalLight(scene, Vec3(-0.4f, -0.8f, -0.45f), 3.0f);
    view->setCamera(enginetest::testCameraDescLookAt(Vec3(0.0f, 20.0f, 40.0f), Vec3(0.0f, 0.0f, 0.0f)));
    render(e, 4);
    if (!e->rayQueryAvailable()) {
        std::printf("SKIP: no ray-query tier on this device\n");
        return 0;
    }
    const unsigned N = cost ? 1393u : 200u;
    // N objects, each its OWN mesh (addTestCube makes one per call): N bottom-level structures.
    std::vector<NodeId> nodes;
    const auto place = [&](NodeId n, unsigned i, float lift) {
        enginetest::setNodePosition(scene, n, Vec3(float(int(i % 40u)) * 1.5f - 30.0f, lift, float(int(i / 40u)) * 1.5f - 30.0f));
        enginetest::setNodeScale(scene, n, Vec3(0.6f, 0.6f, 0.6f));
    };
    for (unsigned i = 0; i < N; ++i) {
        const NodeId n = enginetest::addTestCube(scene, Colour(0.3f + 0.5f * float(i % 7u) / 7.0f, 0.5f, 0.6f), 0.0f, 0.7f);
        place(n, i, 0.3f);
        nodes.push_back(n);
    }
    // THE COUNTS FIXTURE'S OWN OBJECTS: four LOD spheres near the camera (the re-level arm) and twelve
    // cut-out quads, each with its own texture (the alpha arms).
    std::vector<NodeId> spheres, cutouts;
    std::vector<TextureId> cutTex;
    if (!cost) {
        for (unsigned i = 0; i < 4u; ++i) {
            const NodeId n = scene->createNode();
            PbrParams p; p.albedo = Colour(0.6f, 0.6f, 0.6f); p.roughness = 0.6f;
            scene->attachMesh(n, scene->createMesh(lodSphere()), scene->createPbrMaterial(p));
            enginetest::setNodePosition(scene, n, Vec3(-3.0f + 2.0f * float(i), 15.0f, 30.0f));
            spheres.push_back(n);
        }
        for (unsigned i = 0; i < 12u; ++i) {
            const std::vector<unsigned char> texels = barsTexels(i);
            const TextureId t = scene->createTexture(32, 32, texels.data(), true);
            PbrParams p; p.albedo = Colour(1.0f, 1.0f, 1.0f); p.roughness = 0.7f;
            p.alphaMode = PbrAlphaMode::Cutout; p.alphaCutoff = 0.5f;
            const MaterialId m = scene->createPbrMaterial(p);
            scene->setPbrTexture(m, PbrTextureSlot::Albedo, t);
            const NodeId n = enginetest::addTestCube(scene, Colour(1, 1, 1), 0.0f, 0.7f);
            scene->setNodeMaterial(n, m);
            enginetest::setNodePosition(scene, n, Vec3(-12.0f + 2.0f * float(i), 0.5f, 10.0f));
            cutouts.push_back(n);
            cutTex.push_back(t);
        }
    }
    const unsigned structures = N + unsigned(spheres.size() + cutouts.size());
    int frames = 5;
    const auto run = [&](int k) { render(e, k); frames += k; };
    for (int k = 0; k < 300 && scene->published().rayQuery.blasCount < int(structures); ++k) run(1);
    run(8);
    const RayQueryStatus at = scene->published().rayQuery;
    std::printf("   %u objects, %d bottom-level structures, %d traced instances\n", structures, at.blasCount, at.instances);
    CHECK(at.blasCount >= int(structures), "every object's structure is resident");

    if (cost) {
        // THE MEASURE ROW: the tier's update in ms per frame (the monitor's engine.rayquery stage), over the
        // CHANGE frames only (decision 14.3): frame f hides the range f and shows the range f - 1 hid, so every
        // frame of the window changes 2 x k slots (the first hides only); a frame is a change frame when the
        // tier's own counters say it did work, and the record of that frame (by the engine's frame stamp) is
        // what is measured. 0 changes: the idle frames, the baseline.
        e->setFrameMonitor(MonitorLevel::Review);
        std::vector<MonitorRecord> recs;
        for (const unsigned changes : { 0u, 10u, 100u }) {
            recs.clear();
            e->popMonitorRecords(recs);   // (the pop appends: the frames since the last pop alone)
            std::vector<unsigned long long> changeFrames, idleFrames;
            const auto range = [&](int f, bool visible) {
                for (unsigned c = 0; c < changes; ++c) scene->setNodeVisible(nodes[(unsigned(f) * changes + c) % N], visible);
            };
            for (int f = 0; f <= 60; ++f) {
                if (f < 60) range(f, false);
                if (f > 0) range(f - 1, true);
                run(1);
                const PublishedFrame &pf = scene->published();
                const bool worked = pf.rayQuery.updateLookups || pf.rayQuery.updateRows;
                (worked ? changeFrames : idleFrames).push_back(pf.frame);
            }
            run(6);
            recs.clear();
            e->popMonitorRecords(recs);   // (the pop appends: the frames since the last pop alone)
            const std::vector<unsigned long long> &want = changes ? changeFrames : idleFrames;
            std::vector<double> ms;
            for (const MonitorRecord &r : recs) {
                if (std::find(want.begin(), want.end(), r.frame) == want.end()) continue;
                for (const auto &st : r.stages)
                    if (st.name == "engine.rayquery") ms.push_back(st.ms);
            }
            std::sort(ms.begin(), ms.end());
            const double med = ms.empty() ? -1.0 : ms[ms.size() / 2];
            const double p95 = ms.empty() ? -1.0 : ms[std::min(ms.size() - 1u, size_t(double(ms.size()) * 0.95))];
            std::printf("engine.ray_update_cost: N %u, %u changes a frame (%u slot edits): engine.rayquery median %.3f ms, "
                        "p95 %.3f ms, max %.3f ms over %zu %s frames (%zu change, %zu idle in the window)\n",
                        N, changes, 2u * changes, med, p95, ms.empty() ? -1.0 : ms.back(), ms.size(),
                        changes ? "change" : "idle", changeFrames.size(), idleFrames.size());
            CHECK_MSG(changes == 0u || changeFrames.size() >= 59u, "the %u-change window's frames changed (%zu of 61)",
                      changes, changeFrames.size());
            CHECK_MSG(!ms.empty(), "the %u-change window was measured", changes);
        }
        e->setFrameMonitor(MonitorLevel::Off);
        std::printf("\n   frames rendered: %d\n", frames);
        std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
        return failures ? 1 : 0;
    }

    // The counters are the LAST update's; a change is answered in the frame after it is made (the table
    // carries it then), so every frame of a window is summed.
    const auto window = [&](int nFrames, unsigned *peakVisits = nullptr) {
        Counts c;
        for (int f = 0; f < nFrames; ++f) {
            run(1);
            const RayQueryStatus st = scene->published().rayQuery;
            c.lookups += st.updateLookups;
            c.rows += st.updateRows;
            c.visits += st.updateVisits;
            if (peakVisits) *peakVisits = std::max(*peakVisits, unsigned(st.updateVisits));
        }
        return c;
    };
    const auto say = [](const char *what, unsigned k, const Counts &c) {
        std::printf("   %-22s k %2u: %5llu lookups, %4llu rows, %6llu visits\n", what, k, c.lookups, c.rows, c.visits);
    };

    // IDLE: nothing changed.
    {
        const Counts idle = window(4);
        say("idle (4 frames)", 0u, idle);
        CHECK(idle.lookups == 0u && idle.rows == 0u && idle.visits == 0u,
              "AN IDLE FRAME DOES NO HOST WORK: 0 lookups, 0 rows, 0 visits (four frames)");
    }
    const unsigned k = 20u;
    const unsigned blas = unsigned(scene->published().rayQuery.blasCount);
    // MOVED: transforms only.
    {
        for (unsigned c = 0; c < k; ++c) place(nodes[c], c, 1.3f);
        unsigned peak = 0u;
        const Counts moved = window(3, &peak);
        say("moved", k, moved);
        CHECK(moved.lookups == 0u && moved.rows == 0u, "k MOVED objects cost no lookup and no row (their instances are the device's)");
        CHECK_MSG(peak <= blas, "a moved frame's whole passes visit each structure at most once (%u <= %u)", peak, blas);
    }
    // HIDDEN, then SHOWN.
    {
        for (unsigned c = 0; c < k; ++c) scene->setNodeVisible(nodes[k + c], false);
        const Counts hidden = window(3);
        for (unsigned c = 0; c < k; ++c) scene->setNodeVisible(nodes[k + c], true);
        const Counts shown = window(3);
        say("hidden", k, hidden);
        say("shown", k, shown);
        CHECK_MSG(hidden.lookups <= 1ull * k && hidden.rows == 0u && shown.lookups <= 1ull * k && shown.rows == 0u,
                  "k HIDDEN or SHOWN objects cost <= 1 lookup and 0 rows each (%llu / %llu, %llu / %llu)",
                  hidden.lookups, hidden.rows, shown.lookups, shown.rows);
        CHECK(hidden.lookups > 0u, "...and the hides were seen (not vacuous)");
    }
    // ADDED (new meshes), then REMOVED.
    {
        std::vector<NodeId> extra;
        for (unsigned c = 0; c < k; ++c) {
            const NodeId n = enginetest::addTestCube(scene, Colour(0.9f, 0.3f, 0.2f), 0.0f, 0.5f);
            place(n, N + c, 0.3f);
            extra.push_back(n);
        }
        const Counts added = window(6);
        for (NodeId n : extra) scene->removeNode(n);
        const Counts removed = window(4);
        say("added", k, added);
        say("removed", k, removed);
        CHECK_MSG(added.lookups <= kAddLookups * k && added.rows <= 2ull * k && added.lookups > 0u,
                  "k ADDED objects cost <= 23 lookups and <= 2 rows each (%llu, %llu)", added.lookups, added.rows);
        CHECK_MSG(removed.lookups <= 2ull * k && removed.rows <= 1ull * k && removed.rows > 0u,
                  "k REMOVED objects cost <= 2 lookups and 1 row each - the zero-filled record counted (%llu, %llu)",
                  removed.lookups, removed.rows);
    }
    // RE-LEVELLED: the LOD spheres from 25 m to 110 m of the camera, then back.
    {
        const RayQueryStatus before = scene->published().rayQuery;
        for (size_t i = 0; i < spheres.size(); ++i)
            enginetest::setNodePosition(scene, spheres[i], Vec3(-3.0f + 2.0f * float(i), 15.0f, -60.0f));
        const Counts away = window(6);
        const RayQueryStatus after = scene->published().rayQuery;
        say("re-levelled (away)", unsigned(spheres.size()), away);
        CHECK_MSG(after.blasBuilds > before.blasBuilds && away.lookups > 0u,
                  "the spheres' near copies changed level (structures built %llu -> %llu)", before.blasBuilds,
                  after.blasBuilds);
        CHECK_MSG(away.lookups <= kRelevelLookups * spheres.size() && away.rows <= 2ull * spheres.size(),
                  "k RE-LEVELLED objects cost <= 26 lookups and <= 2 rows each (%llu, %llu)", away.lookups, away.rows);
    }
    // AN ALPHA-TESTED SLOT HIDDEN: the alpha table re-written, a row per member left.
    {
        scene->setNodeVisible(cutouts[0], false);
        const Counts a = window(3);
        say("cut-out hidden", 1u, a);
        CHECK_MSG(a.lookups <= 1u && a.rows <= cutouts.size() - 1u && a.rows > 0u,
                  "a hidden cut-out costs <= 1 lookup and the alpha table's rows of the members left (%llu, %llu <= %zu)",
                  a.lookups, a.rows, cutouts.size() - 1u);
        scene->setNodeVisible(cutouts[0], true);
        window(3);
    }
    // A LAMP added, moved, DELETED: the hit table re-written whole; the deleted lamp gone from it.
    {
        const unsigned lightsBefore = unsigned(scene->published().rayQuery.hitLights);
        const NodeId lamp = scene->createNode();
        enginetest::setNodePosition(scene, lamp, Vec3(0.0f, 4.0f, 5.0f));
        LightDesc l;
        l.type = LightType::Point;
        l.intensity = 20.0f;
        l.range = 12.0f;
        scene->setLight(lamp, l);
        const Counts added = window(3);
        const unsigned lightsAdded = unsigned(scene->published().rayQuery.hitLights);
        enginetest::setNodePosition(scene, lamp, Vec3(2.0f, 4.0f, 5.0f));
        const Counts moved = window(3);
        // A LAMP DRAGGED 12 FRAMES (decision 16.2): the hit table written every frame re-makes no ring buffer
        // once its three slots exist (from the drag's 4th frame on, at the latest).
        unsigned long long buffersAt4 = 0ull;
        for (int f = 0; f < 12; ++f) {
            enginetest::setNodePosition(scene, lamp, Vec3(2.0f + 0.1f * float(f + 1), 4.0f, 5.0f));
            run(1);
            if (f == 3) buffersAt4 = scene->published().rayQuery.hitTableBuffers;
        }
        const unsigned long long buffersMade = scene->published().rayQuery.hitTableBuffers - buffersAt4;
        std::printf("   a lamp dragged 12 frames: %llu hit-table buffers made after its 4th frame\n", buffersMade);
        CHECK_MSG(buffersMade == 0ull, "A HIT TABLE WRITTEN EVERY FRAME RE-MAKES NO BUFFER once its ring is full (%llu)",
                  buffersMade);
        scene->removeLight(lamp);
        Counts deleted = window(1);
        const unsigned lightsNext = unsigned(scene->published().rayQuery.hitLights);
        const Counts tail = window(1);
        deleted.lookups += tail.lookups;
        deleted.rows += tail.rows;
        deleted.visits += tail.visits;
        const unsigned rowsPer = lightsAdded + unsigned(scene->published().rayQuery.hitMaterialRows);
        say("lamp added", 1u, added);
        say("lamp moved", 1u, moved);
        say("lamp deleted", 1u, deleted);
        std::printf("   the hit table's lights: %u before, %u with the lamp, %u the frame after it was deleted\n",
                    lightsBefore, lightsAdded, lightsNext);
        CHECK_MSG(lightsAdded == lightsBefore + 1u && lightsNext == lightsBefore,
                  "A DELETED LAMP IS GONE FROM THE HIT TABLE THE NEXT FRAME (%u -> %u -> %u)", lightsBefore, lightsAdded,
                  lightsNext);
        CHECK_MSG(added.lookups == 0u && moved.lookups == 0u && deleted.lookups == 0u && added.rows <= rowsPer &&
                      moved.rows <= rowsPer && deleted.rows <= rowsPer && moved.rows > 0u && deleted.rows > 0u,
                  "a lamp added, moved or deleted re-writes the hit table once: 0 lookups, <= its lights + material rows "
                  "(%llu, %llu, %llu <= %u)", added.rows, moved.rows, deleted.rows, rowsPer);
        scene->removeNode(lamp);
        window(2);
    }
    // n > 8 MASKS STALE IN ONE FRAME: every cut-out's texture re-uploaded at once.
    {
        const unsigned n = unsigned(cutTex.size());
        const unsigned long long made0 = scene->published().rayQuery.alphaMasksMade;
        for (unsigned i = 0; i < n; ++i) {
            const std::vector<unsigned char> texels = barsTexels(i + 20u);
            scene->updateTexture(cutTex[i], 32, 32, texels.data());
        }
        int first = -1, done = -1;
        unsigned long long stale = 0ull;
        for (int f = 0; f < 30 && done < 0; ++f) {
            run(1);
            const RayQueryStatus st = scene->published().rayQuery;
            stale += st.alphaStaleServed;
            if (first < 0 && st.alphaMasksMade > made0) first = f;
            if (st.alphaMasksMade >= made0 + n) done = f;
        }
        const int needed = int((n + 7u) / 8u);
        std::printf("   %u masks stale at once: remade from frame %d to frame %d (%llu stale served on the way)\n", n,
                    first, done, stale);
        CHECK_MSG(done >= 0 && first >= 0 && done - first + 1 <= needed,
                  "n STALE MASKS ARE ALL REMADE within ceil(n / 8) = %d frames of the first (%d)", needed,
                  done >= 0 && first >= 0 ? done - first + 1 : -1);
        CHECK_MSG(stale > 0u, "the frame past the budget served stale masks and said so (%llu)", stale);
    }
    std::printf("\n   frames rendered: %d\n", frames);
    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (argc > 1 && std::string(argv[1]) == "--update-counts") return updateMain(false);
    if (argc > 1 && std::string(argv[1]) == "--update-cost") return updateMain(true);
    std::string err;
    // A LOG PER RUN. Three ctest entries share this binary and this working
    // directory (rays on, rays off, and the sanitised twin); one log name would
    // have them clobbering each other's evidence whenever two run at once.
    const auto cfg = enginetest::testEngineConfig(getenv("JAHSHAKA_NO_RAY_QUERY")
                      ? "test-gi-rayquery-" JAH_RQ_LOG_TAG "-norays-ogre.log"
                      : "test-gi-rayquery-" JAH_RQ_LOG_TAG "-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->submitFrame(jahshaka::engine::FrameInput::delta(1.0f / 60.0f));
    Engine *e = engine.get();

    // THE DEVICE DOES NOT EXIST YET. Ogre creates the VkDevice with the first
    // render target, not with Root (the same startup-order law that makes a
    // render window a prerequisite for registerHlms and createSceneManager), so
    // rayQueryAvailable() is only meaningful once a View has been made — asking
    // before that reports false on hardware that has rays.
    View *view = e->createOffscreenView("rayquery", kSize, kSize, Colour(0, 0, 0));
    Scene *scene = e->createScene("rayquery");
    view->setScene(scene);
    scene->setAmbient(Colour(0.3f, 0.3f, 0.35f), Colour(0.2f, 0.2f, 0.25f));
    enginetest::addDirectionalLight(scene, Vec3(-0.4f, -0.8f, -0.45f), 3.0f);

    // A ground slab and ONE target box with an exactly known geometry, so a ray
    // aimed at it has an answer arithmetic can produce.
    const NodeId ground = enginetest::addTestCube(scene, Colour(0.7f, 0.7f, 0.7f), 0.0f, 0.9f);
    enginetest::setNodePosition(scene, ground, Vec3(0.0f, -0.5f, 0.0f));
    enginetest::setNodeScale(scene, ground, Vec3(40.0f, 1.0f, 40.0f));

    // A UNIT cube scaled to 2 m, centred at (0, 1, 0): its +X face is the plane
    // x = 1, its top y = 2. Every distance below is read off those two numbers.
    // Built through the real verbs rather than the helper, because case 9 needs
    // to EDIT this material and the helper keeps its MaterialId to itself.
    const NodeId box = scene->createNode();
    const MeshId boxMesh = scene->createMesh(enginetest::unitCubeMesh());
    PbrParams boxParams;
    boxParams.albedo = Colour(0.8f, 0.2f, 0.2f);
    boxParams.metalness = 0.0f;
    boxParams.roughness = 0.6f;
    const MaterialId boxMaterial = scene->createPbrMaterial(boxParams);
    CHECK(box && boxMesh && boxMaterial && scene->attachMesh(box, boxMesh, boxMaterial),
          "the target box exists with a material of its own");
    enginetest::setNodePosition(scene, box, Vec3(0.0f, 1.0f, 0.0f));
    enginetest::setNodeScale(scene, box, Vec3(2.0f, 2.0f, 2.0f));

    view->setCamera(enginetest::testCameraDescLookAt(Vec3(5.0f, 3.0f, 6.0f), Vec3(0.0f, 1.0f, 0.0f)));
    render(e, 6);

    // =====================================================================
    // THE NO-RAYS RUN (JAHSHAKA_NO_RAY_QUERY=1, which is what --no-ray-query
    // sets). This is the whole point of the switch: this GPU renders the
    // picture a machine WITHOUT ray-tracing hardware renders, so the fallback
    // every later ray-consuming suite depends on is PROVED on every push
    // instead of assumed.
    //
    // It is a DIFFERENT contract from the traced one, not a subset, so it gets
    // its own assertions and stops here — the cases below would otherwise
    // switch the tier back on at runtime and test nothing the traced run does
    // not already cover.
    //
    // With the switch set the patch does not even ask the driver for the
    // extensions, so `available` is false too: the process is on exactly the
    // device it would have had before the ray tier existed.
    // =====================================================================
    if (!e->rayTracing()) {
        std::printf("\n== the no-rays run (JAHSHAKA_NO_RAY_QUERY) ==\n");
        const RayQueryStatus st = scene->published().rayQuery;
        CHECK(!st.available, "the DEVICE was built without ray queries — the switch reaches it");
        CHECK(!st.enabled, "...and the tier is off");
        CHECK(st.blasCount == 0 && st.instances == 0 && st.triangles == 0,
              "no acceleration structure exists at all");
        CHECK(st.blasBytes == 0 && st.tlasBytes == 0, "...and it costs no memory");
        CHECK(st.tlasBuilds == 0 && st.tlasRefits == 0 && st.blasBuilds == 0,
              "...and no frame ever built one");
        std::vector<float> rays, hits;
        pushRay(rays, Vec3(10.0f, 1.0f, 0.0f), Vec3(-1.0f, 0.0f, 0.0f), 0.001f, 100.0f);
        CHECK(!scene->traceRays(rays, hits),
              "a trace REFUSES rather than answering wrongly");
        CHECK(hits.empty(), "...leaving no answers behind");
        CHECK(!e->rayQueryAvailable(), "the engine agrees the device has none");
        // AND THE SCENE STILL RENDERS. The fallback is the same picture with
        // one term computed differently — never a degraded or missing one.
        Image img;
        CHECK(view->readPixels(img) && img.width == kSize && img.height == kSize,
              "and the scene renders exactly as it always did");
        std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures,
                    failures == 1 ? "" : "s");
        return failures ? 1 : 0;
    }

    if (!e->rayQueryAvailable()) {
        // NOT A FAILURE. macOS, a pre-RTX GPU and a software rasteriser without
        // the extension are all supported platforms; the fallback picture is
        // the product, not a degraded build.
        std::printf("SKIP: this device does not advertise VK_KHR_ray_query — "
                    "the no-rays path is the supported picture here\n");
        return 0;
    }
    std::printf("device advertises hardware ray queries\n");

    // =====================================================================
    // CASE 1 — the tier holds a structure, and the picture did not move
    // =====================================================================
    std::printf("\n== case 1: the tier is up and inert ==\n");
    {
        const RayQueryStatus st = scene->published().rayQuery;
        std::printf("   blas %d, instances %d, triangles %d, blasBytes %llu, tlasBytes %llu\n",
                    st.blasCount, st.instances, st.triangles,
                    (unsigned long long)st.blasBytes, (unsigned long long)st.tlasBytes);
        CHECK(st.available, "the device's answer is available:true");
        CHECK(st.enabled, "...and the tier is enabled");
        CHECK(st.blasCount >= 1, "a bottom-level structure exists");
        CHECK(st.instances == 2, "the traced set is exactly the ground and the box");
        CHECK(st.triangles > 0 && st.blasBytes > 0, "it holds real geometry and real memory");
        CHECK(st.tlasBuilds >= 1, "a top-level structure was built");

        // THE PICTURE. Byte-identical with the tier holding structures and with
        // it switched off — the tier touches no pixel, which is what makes
        // enabling the Vulkan device features safe.
        Image withRays, withoutRays;
        view->readPixels(withRays);
        e->setRayTracing(false);
        render(e, 3);
        view->readPixels(withoutRays);
        CHECK(imagesEqual(withRays, withoutRays),
              "THE PICTURE IS BYTE-IDENTICAL with the tier on and off");
        e->setRayTracing(true);
        render(e, 3);
    }

    // =====================================================================
    // CASE 2 — a ray answers the analytic distance
    // =====================================================================
    std::printf("\n== case 2: a known ray against a known box ==\n");
    {
        std::vector<float> rays, hits;
        // (a) straight down the -X axis from x = 10 at the box's centre height:
        //     the +X face is at x = 1, so the first hit is at 9.
        pushRay(rays, Vec3(10.0f, 1.0f, 0.0f), Vec3(-1.0f, 0.0f, 0.0f), 0.001f, 100.0f);
        // (b) straight down from above the box's centre: its top is y = 2, from
        //     y = 12 that is 10.
        pushRay(rays, Vec3(0.0f, 12.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f), 0.001f, 100.0f);
        // (c) the same downward ray displaced 5 m in +X: it clears the box
        //     (half-extent 1) and lands on the ground, whose top is y = 0, so 12.
        pushRay(rays, Vec3(5.0f, 12.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f), 0.001f, 100.0f);
        // (d) a ray aimed at nothing at all: straight up, above everything.
        pushRay(rays, Vec3(0.0f, 20.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f), 0.001f, 100.0f);
        CHECK(scene->traceRays(rays, hits), "the batch traced");
        CHECK(hits.size() == 4u * 4u, "...and came back with one answer per ray");
        if (hits.size() == 16u) {
            const Hit a = hitAt(hits, 0), b = hitAt(hits, 1), c = hitAt(hits, 2), d = hitAt(hits, 3);
            std::printf("   (a) %.4f  (b) %.4f  (c) %.4f  (d) %.4f\n",
                        a.distance, b.distance, c.distance, d.distance);
            // A millimetre. The geometry is exact and the arithmetic is exact;
            // the only slack is the traversal's own float precision.
            CHECK(a.hit && std::fabs(a.distance - 9.0f) < 0.001f,
                  "(a) -X from x=10 hits the box's +X face at exactly 9 m");
            CHECK(b.hit && std::fabs(b.distance - 10.0f) < 0.001f,
                  "(b) -Y from y=12 hits the box's top at exactly 10 m");
            CHECK(c.hit && std::fabs(c.distance - 12.0f) < 0.001f,
                  "(c) the same ray 5 m aside misses the box and hits the ground at 12 m");
            CHECK(!d.hit && d.distance < 0.0f, "(d) a ray into the sky reports a miss");
            CHECK(a.node == b.node, "(a) and (b) name the SAME object — both hit the box");
            CHECK(c.node != a.node, "...and (c) names a different one — the ground");
        }
    }

    // =====================================================================
    // CASE 3 — the traced set is the world, not the editor
    // =====================================================================
    std::printf("\n== case 3: what is in the structure ==\n");
    {
        // A HELPER: editor furniture carries its own visibility channel instead
        // of the world's, exactly so captures can exclude it. A gizmo arrow in
        // the acceleration structure is the defect audit C-4 found in the spike.
        const NodeId helper = enginetest::addTestCube(scene, Colour(0.1f, 0.9f, 0.1f), 0.0f, 0.5f);
        enginetest::setNodePosition(scene, helper, Vec3(4.0f, 1.0f, 0.0f));
        scene->setNodeHelper(helper, true);
        render(e, 3);
        CHECK(scene->published().rayQuery.instances == 2,
              "a HELPER does not join the traced set (the spike traced gizmos and light icons)");

        std::vector<float> rays, hits;
        // Aimed straight at where the helper stands: it must not be hit, and the
        // ray must fly on to nothing.
        pushRay(rays, Vec3(10.0f, 1.0f, 0.0f), Vec3(-1.0f, 0.0f, 0.0f), 0.001f, 5.5f);
        CHECK(scene->traceRays(rays, hits) && hits.size() == 4u, "a ray aimed at the helper traced");
        if (hits.size() == 4u)
            CHECK(!hitAt(hits, 0).hit, "...and passes straight through it — a helper is not hittable");

        scene->removeNode(helper);
        render(e, 3);

        // HIDING. An object that is not drawn is not traced: the structure
        // follows what the world shows, one walk, one predicate.
        scene->setNodeVisible(box, false);
        render(e, 3);
        CHECK(scene->published().rayQuery.instances == 1, "a HIDDEN object leaves the traced set");
        {
            std::vector<float> r2, h2;
            pushRay(r2, Vec3(0.0f, 12.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f), 0.001f, 100.0f);
            if (scene->traceRays(r2, h2) && h2.size() == 4u)
                CHECK(hitAt(h2, 0).hit && std::fabs(hitAt(h2, 0).distance - 12.0f) < 0.001f,
                      "...and the ray that hit it now reaches the ground at 12 m");
        }
        scene->setNodeVisible(box, true);
        render(e, 3);
        CHECK(scene->published().rayQuery.instances == 2, "showing it again puts it back");
    }

    // =====================================================================
    // CASE 4 — a still scene costs nothing
    // =====================================================================
    std::printf("\n== case 4: at rest ==\n");
    {
        render(e, 5);
        const RayQueryStatus before = scene->published().rayQuery;
        render(e, 30);
        const RayQueryStatus after = scene->published().rayQuery;
        std::printf("   tlas builds %llu -> %llu, refits %llu -> %llu, blas %llu -> %llu "
                    "over 30 still frames\n",
                    (unsigned long long)before.tlasBuilds, (unsigned long long)after.tlasBuilds,
                    (unsigned long long)before.tlasRefits, (unsigned long long)after.tlasRefits,
                    (unsigned long long)before.blasBuilds, (unsigned long long)after.blasBuilds);
        CHECK(after.tlasBuilds == before.tlasBuilds && after.tlasRefits == before.tlasRefits,
              "A STILL SCENE REBUILDS NOTHING for 30 frames");
        CHECK(after.blasBuilds == before.blasBuilds,
              "...and builds no new bottom-level structure either");
    }

    // =====================================================================
    // CASE 5 — a mover moves its hit
    // =====================================================================
    std::printf("\n== case 5: the structure follows the world ==\n");
    {
        const RayQueryStatus before = scene->published().rayQuery;
        // 3 m further from the ray's origin along -X: the +X face moves from
        // x = 1 to x = 4, so the hit at 9 becomes a hit at 6.
        enginetest::setNodePosition(scene, box, Vec3(-3.0f, 1.0f, 0.0f));
        render(e, 3);
        std::vector<float> rays, hits;
        pushRay(rays, Vec3(10.0f, 1.0f, 0.0f), Vec3(-1.0f, 0.0f, 0.0f), 0.001f, 100.0f);
        CHECK(scene->traceRays(rays, hits) && hits.size() == 4u, "the ray traced after the move");
        if (hits.size() == 4u) {
            std::printf("   after moving the box 3 m: %.4f\n", hitAt(hits, 0).distance);
            CHECK(hitAt(hits, 0).hit && std::fabs(hitAt(hits, 0).distance - 12.0f) < 0.001f,
                  "the same ray now hits the box's +X face at 12 m — the structure moved with it");
        }
        const RayQueryStatus after = scene->published().rayQuery;
        CHECK(after.tlasBuilds + after.tlasRefits > before.tlasBuilds + before.tlasRefits,
              "...because a MOVED scene did update the top-level structure");
        CHECK(after.blasBuilds == before.blasBuilds,
              "...without rebuilding a bottom-level one (geometry did not change, only its place)");
        enginetest::setNodePosition(scene, box, Vec3(0.0f, 1.0f, 0.0f));
        render(e, 3);
    }

    // =====================================================================
    // CASE 6 — the no-rays switch
    // =====================================================================
    std::printf("\n== case 6: the no-rays switch ==\n");
    {
        e->setRayTracing(false);
        render(e, 4);
        const RayQueryStatus off = scene->published().rayQuery;
        CHECK(off.available, "the DEVICE still advertises ray queries — that cannot be switched");
        CHECK(!off.enabled, "...but the tier is off");
        CHECK(off.instances == 0 && off.blasCount == 0,
              "every structure is gone — this is the picture a machine without rays renders");
        std::vector<float> rays, hits;
        pushRay(rays, Vec3(10.0f, 1.0f, 0.0f), Vec3(-1.0f, 0.0f, 0.0f), 0.001f, 100.0f);
        CHECK(!scene->traceRays(rays, hits), "and a trace REFUSES rather than answering wrongly");
        CHECK(hits.empty(), "...leaving no answers behind");
        CHECK(!e->rayTracing(), "the engine reports the switch as off");

        e->setRayTracing(true);
        render(e, 5);
        const RayQueryStatus on = scene->published().rayQuery;
        CHECK(on.enabled && on.instances == 2,
              "switching it back on rebuilds the structure from scratch");
        std::vector<float> r2, h2;
        pushRay(r2, Vec3(10.0f, 1.0f, 0.0f), Vec3(-1.0f, 0.0f, 0.0f), 0.001f, 100.0f);
        CHECK(scene->traceRays(r2, h2) && h2.size() == 4u && hitAt(h2, 0).hit &&
                  std::fabs(hitAt(h2, 0).distance - 9.0f) < 0.001f,
              "...and the same ray gives the same 9 m it gave before it was switched off");
    }

    // =====================================================================
    // CASE 7 — two drawn scenes in one frame
    // =====================================================================
    std::printf("\n== case 7: a second scene ==\n");
    {
        // A preview-sized offscreen view and its own scene, exactly like the
        // material preview and the thumbnail renderer: both are drawn every
        // frame the editor is, and both reach the tier through the same call.
        View *pv = e->createOffscreenView("rayquery-preview", 64, 64, Colour(0, 0, 0));
        Scene *ps = e->createScene("rayquery-preview");
        CHECK(pv && ps, "a second view and scene exist");
        if (pv && ps) {
            pv->setScene(ps);
            ps->setAmbient(Colour(0.3f, 0.3f, 0.3f), Colour(0.2f, 0.2f, 0.2f));
            const NodeId a = enginetest::addTestCube(ps, Colour(0.2f, 0.4f, 0.9f), 0.0f, 0.5f);
            enginetest::setNodePosition(ps, a, Vec3(0.0f, 0.0f, 0.0f));
            pv->setCamera(enginetest::testCameraDescLookAt(Vec3(0, 1, 4), Vec3(0, 0, 0)));
            render(e, 6);

            const RayQueryStatus editorSt = scene->published().rayQuery;
            const RayQueryStatus previewSt = ps->published().rayQuery;
            std::printf("   editor: %d instances / %d blas   preview: %d instances / %d blas\n",
                        editorSt.instances, editorSt.blasCount,
                        previewSt.instances, previewSt.blasCount);
            CHECK(previewSt.enabled && previewSt.instances == 1,
                  "the second scene has its OWN structure, with its own one instance");
            CHECK(editorSt.instances == 2,
                  "...and the editor scene's is untouched by it");

            // AND BOTH STILL GO QUIET. If the frame accounting counted calls
            // rather than frames, the guards below would be the thing that
            // broke first — and silently.
            render(e, 5);
            const RayQueryStatus e0 = scene->published().rayQuery, p0 = ps->published().rayQuery;
            render(e, 30);
            const RayQueryStatus e1 = scene->published().rayQuery, p1 = ps->published().rayQuery;
            CHECK(e1.tlasBuilds == e0.tlasBuilds && p1.tlasBuilds == p0.tlasBuilds,
                  "TWO still scenes rebuild nothing over 30 frames");

            // A ray into each still answers for the right world.
            std::vector<float> r, h;
            pushRay(r, Vec3(10.0f, 1.0f, 0.0f), Vec3(-1.0f, 0.0f, 0.0f), 0.001f, 100.0f);
            const bool traced = scene->traceRays(r, h) && h.size() == 4u;
            CHECK(traced, "the editor scene still traces with a second scene alive");
            CHECK(traced && hitAt(h, 0).hit && std::fabs(hitAt(h, 0).distance - 9.0f) < 0.001f,
                  "...and its ray still answers 9 m");

            // =================================================================
            // CASE 8 — destroying it takes the structures with it
            // =================================================================
            std::printf("\n== case 8: a destroyed scene ==\n");
            const int blasBefore = scene->published().rayQuery.blasCount;
            pv->setScene(nullptr);
            e->destroyScene(ps);
            ps = nullptr;
            render(e, 6);
            CHECK(scene->published().rayQuery.blasCount == blasBefore,
                  "destroying the second scene leaves the editor's structures alone");
            // The editor scene still traces correctly afterwards — the honest
            // proof that nothing of the dead scene's state leaked into it.
            std::vector<float> r2, h2;
            pushRay(r2, Vec3(10.0f, 1.0f, 0.0f), Vec3(-1.0f, 0.0f, 0.0f), 0.001f, 100.0f);
            CHECK(scene->traceRays(r2, h2) && h2.size() == 4u && hitAt(h2, 0).hit &&
                      std::fabs(hitAt(h2, 0).distance - 9.0f) < 0.001f,
                  "...and the editor scene still answers 9 m");
            if (pv) e->destroyView(pv);
            render(e, 3);
        }
    }

    // =====================================================================
    // CASE 9 — an alpha-test flip on a STILL scene keeps the item traced
    // =====================================================================
    std::printf("\n== case 9: an alpha-test flip ==\n");
    {
        render(e, 6);
        const int before = scene->published().rayQuery.instances;
        // The box's own material turns cut-out. Nothing moves, no render queue
        // changes, no node is touched — only the datablock's alpha test, in
        // place. SINCE REFLECT-MOVERS-2 A CUT-OUT STAYS IN THE STRUCTURE: its
        // instance turns FORCE_NO_OPAQUE and every ray query tests its candidates
        // against its mask (jah_rq_alpha.glsl; gi.rt_alpha_tested holds the holes).
        // With no albedo map the raster's test reads the background diffuse's
        // alpha (opaque here): every texel is kept, so the ray still stops.
        PbrParams cut;
        cut.albedo = Colour(0.8f, 0.2f, 0.2f);
        cut.metalness = 0.0f;
        cut.roughness = 0.6f;
        cut.alphaMode = PbrAlphaMode::Cutout;
        cut.alphaCutoff = 0.5f;
        CHECK(scene->setPbrMaterial(boxMaterial, cut), "the box's material turns cut-out");
        render(e, 2);
        const int after = scene->published().rayQuery.instances;
        std::printf("   instances %d -> %d after the alpha-test flip\n", before, after);
        CHECK(after == before, "a CUT-OUT item stays in the traced set (its candidates are tested, not dropped)");
        {
            std::vector<float> r, h;
            pushRay(r, Vec3(10.0f, 1.0f, 0.0f), Vec3(-1.0f, 0.0f, 0.0f), 0.001f, 100.0f);
            CHECK(scene->traceRays(r, h) && h.size() == 4u && hitAt(h, 0).hit &&
                      std::fabs(hitAt(h, 0).distance - 9.0f) < 0.001f,
                  "...and a ray at it stops at 9 m (no mask: the constant answer, solid)");
        }

        PbrParams solid = cut;
        solid.alphaMode = PbrAlphaMode::Opaque;
        CHECK(scene->setPbrMaterial(boxMaterial, solid), "...and back to opaque");
        render(e, 2);
        CHECK(scene->published().rayQuery.instances == before, "...which keeps it, again with nothing moving");
        std::vector<float> r, h;
        pushRay(r, Vec3(10.0f, 1.0f, 0.0f), Vec3(-1.0f, 0.0f, 0.0f), 0.001f, 100.0f);
        CHECK(scene->traceRays(r, h) && h.size() == 4u && hitAt(h, 0).hit &&
                  std::fabs(hitAt(h, 0).distance - 9.0f) < 0.001f,
              "...and the ray finds it at 9 m");
    }

    // =====================================================================
    // CASE 10 — a parked scene's compaction slots survive the editor's churn
    // =====================================================================
    std::printf("\n== case 10: a parked scene ==\n");
    {
        // A scene that draws once — queueing compaction size queries for its
        // meshes — and is then parked before those answers are read. Its slots
        // are owed and must not be handed to anyone else.
        View *pv = e->createOffscreenView("rayquery-parked", 64, 64, Colour(0, 0, 0));
        Scene *ps = e->createScene("rayquery-parked");
        CHECK(pv && ps, "a parked view and scene exist");
        if (pv && ps) {
            pv->setScene(ps);
            ps->setAmbient(Colour(0.3f, 0.3f, 0.3f), Colour(0.2f, 0.2f, 0.2f));
            PbrParams heavy;
            heavy.albedo = Colour(0.3f, 0.6f, 0.9f);
            heavy.metalness = 0.0f;
            heavy.roughness = 0.5f;
            const MaterialId heavyMat = ps->createPbrMaterial(heavy);
            for (int i = 0; i < 6; ++i) {
                // 24x24 .. 34x34 quads: 1,152 to 2,312 triangles each, against
                // the editor's twelve-triangle cubes. A compaction answer taken
                // from the wrong structure is then wrong by orders of magnitude,
                // which is what makes the copy land in an undersized buffer
                // instead of quietly working.
                const NodeId c = ps->createNode();
                const MeshId m = ps->createMesh(gridMesh(24u + unsigned(i) * 2u));
                ps->attachMesh(c, m, heavyMat);
                enginetest::setNodePosition(ps, c, Vec3(float(i) * 2.0f, 0.0f, 0.0f));
            }
            pv->setCamera(enginetest::testCameraDescLookAt(Vec3(0, 3, 10), Vec3(0, 0, 0)));
            render(e, 1);                       // build its BLASes, queue the size queries
            const RayQueryStatus parked = ps->published().rayQuery;
            std::printf("   parked scene holds %d blas\n", parked.blasCount);
            pv->setScene(nullptr);              // PARKED: it never draws again

            // Now churn the editor scene hard enough to want many compaction
            // slots. Every new mesh is a new bottom-level structure.
            for (int i = 0; i < 60; ++i) {
                const NodeId c = enginetest::addTestCube(scene, Colour(0.5f, 0.5f, 0.5f), 0.0f, 0.8f);
                enginetest::setNodePosition(scene, c, Vec3(100.0f + float(i), 0.0f, 0.0f));
                enginetest::setNodeScale(scene, c, Vec3(0.5f, 0.5f, 0.5f));
                if ((i % 10) == 9) render(e, 2);
            }
            render(e, 12);

            // The editor scene must still be CORRECT — that is what a stolen
            // slot would break, by compacting one structure into a buffer
            // sized for another.
            std::vector<float> r, h;
            pushRay(r, Vec3(10.0f, 1.0f, 0.0f), Vec3(-1.0f, 0.0f, 0.0f), 0.001f, 100.0f);
            const bool ok = scene->traceRays(r, h) && h.size() == 4u;
            CHECK(ok, "the editor scene still traces after 60 new meshes beside a parked scene");
            CHECK(ok && hitAt(h, 0).hit && std::fabs(hitAt(h, 0).distance - 9.0f) < 0.001f,
                  "...and still answers 9 m");
            std::printf("   editor now holds %d blas / %d instances\n",
                        scene->published().rayQuery.blasCount, scene->published().rayQuery.instances);

            // UN-PARK IT. This is the half that makes the case bite: the parked
            // scene now runs runCompaction and READS the size queries it queued
            // before it was hidden. If the ring handed those slots to the
            // editor's churn, it reads another structure's compacted size and
            // copies into a buffer sized for it — an undersized COMPACT copy,
            // which is a device loss or silently wrong geometry.
            pv->setScene(ps);
            render(e, 12);
            const RayQueryStatus back = ps->published().rayQuery;
            std::printf("   un-parked: %d blas / %d instances / %d tris, %llu blasBytes, enabled=%d\n",
                        back.blasCount, back.instances, back.triangles,
                        (unsigned long long)back.blasBytes, int(back.enabled));
            CHECK(back.enabled && back.instances == 6,
                  "the UN-PARKED scene reads its own compaction answers and still holds its set");
            // AND THEY ARE ITS OWN ANSWERS. This is the assertion that actually
            // catches a stolen slot, and it is a SIZE one because on this driver
            // the corruption is silent: an 86 KB structure compacted into a
            // 1,152-byte buffer neither crashes nor renders visibly wrong, it
            // just records an impossible size. A compacted bottom-level
            // structure runs about 25.7 bytes per triangle on Ada (NVIDIA's
            // published figure; measured here at 22.9), and a scene that read
            // another structure's answers reports far less — measured at 13.9
            // with the previous cursor-and-count ring, which handed this scene's
            // slots to the editor's churn. Eighteen sits between them with room
            // on both sides and means something physically: no real compaction
            // fits a triangle into eighteen bytes.
            CHECK_MSG(back.triangles > 0 &&
                          back.blasBytes >= (unsigned long long)(back.triangles) * 18ull,
                  "...and those answers are its OWN: %llu bytes for %d triangles (%.1f B/tri)",
                  (unsigned long long)back.blasBytes, back.triangles,
                  back.triangles ? double(back.blasBytes) / double(back.triangles) : 0.0);
            {
                std::vector<float> pr, ph;
                // Straight down onto the first grid, whose surface is y = 0.5.
                pushRay(pr, Vec3(0.0f, 10.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f), 0.001f, 100.0f);
                CHECK(ps->traceRays(pr, ph) && ph.size() == 4u && hitAt(ph, 0).hit &&
                          std::fabs(hitAt(ph, 0).distance - 9.5f) < 0.01f,
                      "...and its geometry is intact after compaction (hit at 9.5 m)");
            }
            // The editor scene is still right too.
            {
                std::vector<float> er, eh;
                pushRay(er, Vec3(10.0f, 1.0f, 0.0f), Vec3(-1.0f, 0.0f, 0.0f), 0.001f, 100.0f);
                CHECK(scene->traceRays(er, eh) && eh.size() == 4u && hitAt(eh, 0).hit &&
                          std::fabs(hitAt(eh, 0).distance - 9.0f) < 0.001f,
                      "...and the editor scene is unharmed");
            }
            pv->setScene(nullptr);

            // Destroying the parked scene hands its slots back, and everything
            // keeps working.
            e->destroyScene(ps);
            ps = nullptr;
            render(e, 6);
            std::vector<float> r2, h2;
            pushRay(r2, Vec3(10.0f, 1.0f, 0.0f), Vec3(-1.0f, 0.0f, 0.0f), 0.001f, 100.0f);
            CHECK(scene->traceRays(r2, h2) && h2.size() == 4u && hitAt(h2, 0).hit &&
                      std::fabs(hitAt(h2, 0).distance - 9.0f) < 0.001f,
                  "...and after the parked scene is destroyed");
            if (pv) e->destroyView(pv);
            render(e, 3);
        }
    }

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
