// THE SCALE FIXTURES (lane D1-SCALE-FIXTURES; SPECS/briefs/D1-SCALE-FIXTURES.md).
//
// The measuring stick for phase E: one WORLD, a set of large ASSETS and (in
// test_scale_library.cpp) one LIBRARY, each big enough to push a wall of
// SPECS/audits/V2_ATOM_PHOTON_STRATEGY_2026-09-25.md §3 (W1-W14), and one
// `scale.*` suite per wall that PRINTS today's number in the `target:` form and
// gates nothing (label `scale-target`, split out by scripts/gate-scope.py). The
// part that closes a wall turns its row into a bar.
//
// THE WORLD IS A DOCUMENT, pushed through the product's SceneMirror, lit through
// the product's own tier tables (services/worldmodes: setPhoton + setMode) — so
// what a suite measures is what the editor would render for the same scene, the
// tier's GI, post chain and ray tier included, not a hand-set GiParams. It is
// BUILT AT TEST TIME (no fixture file in the repo) at the engine boundary, never
// through script verbs (DOCS/traps/GATE_AND_RIG.md: a scripted primitive costs
// ~58 ms; a lattice is built at the engine boundary), and its meshes reach it
// through the PRODUCT'S BAKE (MeshBake::buildFromFile — the import door's own
// entry: parse, LOD chain, cards, SDF, cluster DAG), cached on disk by the bake's
// own fingerprint so a process pays a deserialize, not a bake.
#pragma once

#include "irisgl/irisglfwd.h"
#include "irisgl/core/math/vec.h"
#include "irisgl/mirror/scenemirror.h"
#include "jahshaka/engine/Engine.h"
#include "services/worldmodes.h"

#include <QString>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace scale {

using namespace jahshaka::engine;

/// The fixed clock every scripted path steps on (the engine has no wall clock).
constexpr float kDt = 1.0f / 60.0f;

// ---------------------------------------------------------------------------
// THE BAKE CACHE
// ---------------------------------------------------------------------------

/// Where baked blobs live: $JAH_SCALE_ASSET_CACHE, else the build tree's
/// tests/scale/asset-cache (SCALE_ASSET_CACHE_DIR). Keyed by the bake's own
/// fingerprint, so a producer change (a format bump, a new bake source) misses
/// the cache instead of reading a stale blob.
QString cacheDir();

struct BakeInfo {
    QString name;
    QString blobPath;
    bool    fromCache = false;
    double  bakeMs = 0.0;       ///< MeshBake::buildFromFile, wall clock (0 from the cache)
    double  dagMs = 0.0;        ///< the cluster-DAG stage alone (the bake's own log line)
    double  readMs = 0.0;       ///< deserialize from the cache
    qint64  blobBytes = 0;
    /// THE IMPORT SPLITS A LARGE MESH: assimp's SplitLargeMeshes (in the
    /// TargetRealtime_Quality preset, ImportFlags::Canonical) cuts any mesh above
    /// 1 M triangles / 1 M vertices into pieces, so a "10 M asset" arrives as a
    /// MODEL of `pieces` meshes, each with its own chain and DAG. Every count
    /// below is over the whole model.
    int     pieces = 0;
    size_t  triangles = 0;      ///< level 0, summed over the pieces
    int     levels = 0;         ///< the longest piece's chain, level 0 included
    size_t  coarsestTriangles = 0;   ///< every piece at its coarsest level
    size_t  level7Triangles = 0;     ///< ...at level 7, the GPU path's last (kLevelsPerMesh 8)
    int     cards = 0;
    int     clusters = 0, groups = 0;
    unsigned long long peakRssKb = 0;   ///< the process's VmHWM after the bake
};

/// Bake `sourcePath` through MeshBake::buildFromFile (or read its cached blob)
/// and return the model's FIRST mesh. `info` gets the numbers either way.
iris::MeshPtr bakedMesh(const QString &sourcePath, const QString &name, BakeInfo *info = nullptr,
                        bool forceBake = false);

/// THE LARGE ASSET: enginetest::proceduralShell(triangles) written as a binary
/// PLY and baked through the same door. Cached like every other bake; the PLY
/// is deleted once its blob exists. `triangles` 0 = the bake is never forced.
QList<iris::MeshPtr> shellAsset(size_t triangles, BakeInfo *info = nullptr, bool bakeIfMissing = true);
/// The blob path a shell of `triangles` would be cached under (exists or not).
QString shellBlobPath(size_t triangles);

/// VmHWM / VmRSS of this process, kB (Linux /proc/self/status).
unsigned long long peakRssKb();
unsigned long long rssKb();

// ---------------------------------------------------------------------------
// THE ENGINE + MIRROR ENVIRONMENT
// ---------------------------------------------------------------------------

struct Env {
    /// The rig's X connection + an unmapped 1920x1080 window: the WORLD renders
    /// through a WINDOW view, the editor viewport's own shape — an offscreen view
    /// ignores the post chain (Engine.h setPostFx), and with it the tier's SSR,
    /// the ray tier's reflections and the gather's screen half.
    void *xDisplay = nullptr;
    unsigned long xWindow = 0;
    std::unique_ptr<Engine> engine;
    View *view = nullptr;
    Scene *scene = nullptr;
    iris::ScenePtr doc;
    std::unique_ptr<SceneMirror> mirror;
    iris::CameraNodePtr camera;
    int width = 0, height = 0;
    /// An offscreen-chain Env's DISABLED stand-in view the environment is applied
    /// through (see frame()); null for a window Env.
    View *envView = nullptr;
    /// A measurement's own post flags over the world's description (W7's HZB).
    std::function<void(PostFxDesc &)> fxOverride;
};

/// Boot the engine with ONE view of `w` x `h` (1920x1080 = the rig's law) on the
/// `DISPLAY` the ctest line names, and an empty document.
///
/// THE VIEW IS OFFSCREEN WITH THE VIEWPORT'S POST CHAIN OPTED IN
/// (PostFxDesc::allowOffscreen — the editor screenshot's own door): the same
/// passes the editor viewport runs (the tier's SSR, the ray tier, the gather, the
/// id pass and the decode), without the rig's present. Under Xvfb a window view's
/// present is a 45-76 ms CPU copy a frame (measured here: a still frame of the
/// world 132 ms in a window, of which engine.swap 68) that is the RIG's cost, not
/// the engine's — and it would put every path suite over its five minutes.
/// JAH_SCALE_WINDOW=1 boots a WINDOW view instead (an unmapped 1920x1080 X
/// window: the viewport's own shape), for an A/B of that claim.
bool boot(Env &env, const char *logFile, int w = 1920, int h = 1080);
/// One frame the way the editor's viewport draws one: the document refreshed,
/// mirrored, the environment and the camera applied, one renderOneFrame.
void frame(Env &env, int count = 1);
/// Place the document camera (the view follows at the next frame).
void setCamera(Env &env, const iris::Vec3 &pos, const iris::Vec3 &lookAt);
/// Tear down in the startup order's mirror (mirror, document, engine).
void shutdown(Env &env);

// ---------------------------------------------------------------------------
// THE WORLD
// ---------------------------------------------------------------------------

struct WorldSpec {
    int   instances = 10000;         ///< ~20 baked meshes, jittered grid, three scales
    float spacing = 6.0f;            ///< grid pitch, metres (100 x 100 -> a 600 m square)
    int   lights = 500;              ///< point/spot lamps on a `lightGrid` grid
    float lightGrid = 30.0f;
    float groundSize = 2000.0f;      ///< the default ground's plane, scaled to 2 km
    worldmodes::PhotonTier tier = worldmodes::PhotonTier::High;
    /// 0 = the editor's own shape: every object its own PbrMaterial
    /// (SceneEditService gives each placed primitive one). N > 0: N shared
    /// materials cycled over the instances (scale.decode's bucket arms).
    int   materials = 0;
    /// DISTINCT base-colour textures cycled over the materials (0 = untextured).
    /// The decode's bucket count follows them (HlmsAtom::BucketKey::textures).
    int   textures = 0;
    bool  withShippedModels = true;  ///< the dragon + the physics model
};

struct World {
    WorldSpec spec;
    std::vector<iris::MeshNodePtr> items;
    std::vector<iris::LightNodePtr> lights;
    iris::LightNodePtr sun;
    iris::MeshNodePtr ground;
    std::vector<BakeInfo> meshes;    ///< the ~20 baked meshes the items cycle over
    double bakeOrReadMs = 0.0;       ///< getting the meshes (cache hits = deserialize)
    double documentMs = 0.0;         ///< building the document
    double firstSyncMs = 0.0;        ///< the mirror's adopting sync + the first frame
    int    settleFrames = 0;         ///< frames until GI reported at rest (or the cap)
};

/// Build `spec` into env's document and settle it (the first frame, then frames
/// until `giAtRest` or `settleCap`). Prints one WORLD line with the build times.
bool buildWorld(Env &env, const WorldSpec &spec, World &world, int settleCap = 600);

/// (Re)assign the items' materials: `materials` shared ones cycled (0 = one each,
/// the editor's shape), `textures` distinct base-colour maps cycled over them.
void applyMaterials(World &world, int materials, int textures);

/// The world's centre of population (the grid is centred on the origin).
inline iris::Vec3 worldEye() { return iris::Vec3(0.0f, 1.7f, 0.0f); }

// ---------------------------------------------------------------------------
// MEASURING
// ---------------------------------------------------------------------------

/// Arm the render stats and the frame monitor (Review) — the geometry counters
/// are OFF until something reads renderStats (GATE_AND_RIG.md).
void armMonitor(Env &env);
/// Drain the monitor's records (attributed by FrameRecord::frame, never order).
std::vector<FrameRecord> drain(Env &env);
/// The frame monitor's GPU timing needs a capture's query pool: false when the
/// build or the device has none (every GPU ms then reads negative — never 0).
bool gpuTimed(Env &env);

float median(std::vector<float> v);
double medianD(std::vector<double> v);

/// A `target:` line (ATOM-FARBLAS-1's convention: reported, never failing).
void target(const char *wall, double value, const char *unit, const char *what, const char *bar = "none yet");

}   // namespace scale
