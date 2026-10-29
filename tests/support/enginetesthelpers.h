#pragma once
// Test-side stand-ins for the legacy convenience verbs the engine boundary
// dropped (IRISGL_ARCHITECTURE_AUDIT §4.2: addTestCube/addDirectionalLight/
// setNodePosition/setNodeScale/setCameraPosition/lookAt were selftest-era
// surface with no Studio callers). The suites keep their call shapes; the
// helpers speak only the real verbs, so the tests now pin the production path.
//
// Pixel-compat notes:
//  - the cube geometry and PBR params are byte-identical to the deleted
//    Scene::addTestCube (buildCubeV2's 24-vertex unit cube: positions +
//    per-face normals, no uvs — and createPbrMaterial applies exactly the
//    workflow/fog/albedo/metal/rough sequence addTestCube inlined);
//  - testCameraAt reproduces setCameraPosition-without-lookAt (identity
//    orientation, i.e. looking down -Z, default 45° vertical fov);
//  - testCameraLookAt reproduces setCameraPosition+lookAt (+Y up), and
//    testCameraDescLookAt hands back the same CameraDesc unpushed, for a
//    suite that needs to vary one field of it (projection, clips, lens).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "jahshaka/engine/Engine.h"
#include "jahshaka/engine/ProcessExit.h"
#include "jahshaka/engine/EngineInspect.h"   // the engine\'s record of what the fixture pushed (U3 stage 2)
#include "jahshaka/engine/Types.h"

namespace enginetest {

using namespace jahshaka::engine;

/// THE ONE FIXTURE CONFIG (FIXTURE-CONFIG-1): every engine fixture's EngineConfig is
/// built here and nowhere else (source.testing_rules R10). What every fixture shared —
/// the render-system plugins and the Hlms media of the build (JAHSHAKA_TEST_PLUGIN_DIR /
/// JAHSHAKA_TEST_MEDIA_DIR, each suite's compile definitions), every other field at
/// its EngineConfig default — is set here; what a fixture says differently it says
/// through EngineOverrides, ONE shape for all of them:
///
///     auto cfg = enginetest::testEngineConfig("test-x-ogre.log");
///     auto cfg = enginetest::testEngineConfig("test-y-ogre.log",
///                                            enginetest::EngineOverrides().headless());
///
/// A fixture whose SUBJECT is a config field states it here too, never around the
/// helper: test_shader_cache's cache dir (shaderCacheDir), test_warm_up's cache kept
/// OFF (shaderCacheOff), test_engine_headless's refused dir.
struct EngineOverrides {
    /// The NULL render system (EngineConfig::headless).
    EngineOverrides &headless() { mHeadless = true; return *this; }
    /// The host's display connection, for an on-screen View (EngineConfig::display).
    EngineOverrides &display(NativeDisplayHandle d) { mDisplay = d; return *this; }
    /// The process's VR policy enabled, any runtime (EngineConfig::vr.enabled).
    EngineOverrides &vr() { mVr = true; return *this; }
    /// The boot-time log sink (EngineConfig::logSink).
    EngineOverrides &logSink(EngineLogSink sink) { mLogSink = std::move(sink); return *this; }
    /// A media dir other than the build's (EngineConfig::hlmsMediaDir).
    EngineOverrides &hlmsMediaDir(std::string dir) { mHlmsMediaDir = std::move(dir); return *this; }
    /// The fixture's OWN persistent shader cache (EngineConfig::shaderCacheDir).
    EngineOverrides &shaderCacheDir(std::string dir) { mShaderCacheDir = std::move(dir); return *this; }
    /// The persistent shader cache OFF on purpose — whatever the harness hook would set.
    EngineOverrides &shaderCacheOff() { mShaderCacheDir = std::string(); return *this; }
    /// THE REAL DESTROY, kept (URGENT #102): this fixture destroys its engine to test the destroy itself and/or makes
    /// another in the same process (test_engine's teardown_is_clean, test_engine_recreate) — it never takes the
    /// exit-only teardown (takeExitOnlyTeardown below).
    EngineOverrides &realDestroy() { mRealDestroy = true; return *this; }

    bool mHeadless = false;
    NativeDisplayHandle mDisplay = 0;
    bool mVr = false;
    EngineLogSink mLogSink;
    std::optional<std::string> mHlmsMediaDir;
    std::optional<std::string> mShaderCacheDir;   ///< unset = the harness's choice (the hook)
    bool mRealDestroy = false;
};

/// THE HARNESS HOOK (ENGINE-UPGRADE-1 stage 1, BOOT): what every fixture shares and no
/// fixture states — the fixtures' shared WARM shader cache. The row's registration
/// (jah_gpu_row CLASS engine, tests/CMakeLists.txt) runs the fixture through
/// fresh_home.sh --warm, which hard-links the tree's one shared cache into the row's
/// home and promotes it after a green run that grew it, and exports the linked dir as
/// JAHSHAKA_SHADER_CACHE_DIR; this copies it into the config. Unset (a fixture run by
/// hand, a cold row) = no cache, the engine's default. It runs BEFORE a fixture's
/// overrides, so a fixture that states its cache (shaderCacheDir / shaderCacheOff)
/// always wins.
/// THE FIXTURES' CAP: the union of every engine family's shaders outgrows the app's 256 MiB (220 MB /
/// 1,686 shaders on one tree's night), and a save past the cap WIPES the directory — the union would go
/// cold every few days. The harness's cache is held to 1 GiB (shadercache.fixture_warm (archived: archive/tests/shadercache) reads it back).
constexpr unsigned long long kFixtureShaderCacheMaxBytes = 1024ull * 1024ull * 1024ull;

inline void applyHarnessDefaults(EngineConfig &cfg)
{
    if (const char *dir = std::getenv("JAHSHAKA_SHADER_CACHE_DIR")) {
        cfg.shaderCacheDir = dir;
        cfg.shaderCacheMaxBytes = kFixtureShaderCacheMaxBytes;
    }
}

#if defined(JAHSHAKA_TEST_PLUGIN_DIR) && defined(JAHSHAKA_TEST_MEDIA_DIR)
/// The fixture's EngineConfig: the shared defaults, the harness hook, then the
/// fixture's overrides. `logFile` is every fixture's own (each names its log).
/// A FIXTURE WHOSE ENGINE FAILED TO COMPILE A SHADER FAILS (URGENT #83's law for the fixtures, PHOTON-7 10-4): a program
/// that does not compile is logged and survived, and its draws simply never happen - a black reading that a law of
/// "nothing lit here" calls green (gi.fog_sealed passed that way while its colour pass's shader was refused). At the
/// process's exit, whatever its laws said, a fixture whose engine logged a failed compile prints the count and exits 1.
/// THE EXIT-ONLY TEARDOWN, every fixture's (URGENT #102, jahshaka/engine/ProcessExit.h): the process is marked, so
/// the fixture's engine — destroyed at main's end as always — tears down with its caches saved and joined but keeps
/// Root (no vkDestroyDevice, which waits for the NVIDIA driver's background compile: 10-20 s on a fixture's first run
/// after a shader change, measured); then exit()'s handlers run LIFO — every one registered after this (the shader-
/// compile check below among them) first — and this one ends the process with std::_Exit(status), before the
/// driver's and Ogre's finalizers would run against the live device. glibc's on_exit is the one handler that is
/// handed the exit status; without it (macOS) the fixture keeps the real destroy. The driver's per-exe cache of a
/// fixture is still cold on its first run after a shader change and pays that compile once — stored DURING the
/// run (measured), never awaited at the exit. A fixture whose subject is the destroy says realDestroy().
inline void takeExitOnlyTeardown()
{
#if defined(__GLIBC__)
    static const bool once = [] {
        markProcessExit();
        on_exit([](int status, void *) { std::fflush(nullptr); std::_Exit(status); }, nullptr);
        return true;
    }();
    (void)once;
#endif
}

inline void requireEveryShaderCompiled()
{
    static const bool once = [] {
        std::atexit([] {
            const unsigned long long failed = Engine::processShaderCompileErrors();
            if (failed == 0) return;
            std::fprintf(stderr, "FAIL: %llu shader program(s) failed to compile in this fixture (its readings are of "
                                 "draws that never happened) - see its Ogre log\n", failed);
            std::fflush(stderr);
            std::_Exit(1);
        });
        return true;
    }();
    (void)once;
}

inline EngineConfig testEngineConfig(std::string logFile, const EngineOverrides &o = EngineOverrides())
{
    if (!o.mRealDestroy) takeExitOnlyTeardown();   // FIRST: its handler must run after the compile check's
    requireEveryShaderCompiled();
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = std::move(logFile);
    applyHarnessDefaults(cfg);
    if (o.mHeadless) cfg.headless = true;
    if (o.mDisplay) cfg.display = o.mDisplay;
    if (o.mVr) cfg.vr.enabled = true;
    if (o.mLogSink) cfg.logSink = o.mLogSink;
    if (o.mHlmsMediaDir) cfg.hlmsMediaDir = *o.mHlmsMediaDir;
    if (o.mShaderCacheDir) cfg.shaderCacheDir = *o.mShaderCacheDir;
    return cfg;
}
#endif

/// THE TEST DOOR'S ONE FRAME VERB (ENGINE-UPGRADE-U3 stage 2b): every engine suite steps the engine through here —
/// `frames(e, n)` renders n frames, `frames(e, 1, rec)` hands the record over first. `e` is whatever the suite holds
/// (an Engine *, a unique_ptr, an Engine &): a template, so no call site edits its receiver. Its body is today's bytes
/// (submit if given, then renderOneFrame n times); stage 4 swaps it ONCE for the lockstep wait on the render thread.
namespace framesdetail {
template <class E, class = void> struct RendersByDot : std::false_type {};
template <class E>
struct RendersByDot<E, std::void_t<decltype(std::declval<E &>().renderOneFrame())>> : std::true_type {};
}  // namespace framesdetail

template <class E> void frames(E &&e, int n, const FrameInput *rec = nullptr)
{
    if constexpr (framesdetail::RendersByDot<std::remove_reference_t<E>>::value) {
        if (rec) e.submitFrame(*rec);
        for (int i = 0; i < n; ++i) e.renderOneFrame();
    } else {
        if (rec) e->submitFrame(*rec);
        for (int i = 0; i < n; ++i) e->renderOneFrame();
    }
}
/// The same with the record by reference — a suite's temporary record (`frames(e, 1, FrameInput::delta(0.0f))`).
template <class E> void frames(E &&e, int n, const FrameInput &rec) { frames(std::forward<E>(e), n, &rec); }

/// THE SUBSCRIBED STATUSES, READ THE WAY A HOST READS THEM (ENGINE-UPGRADE-U3 stage 3, decision 17): each call
/// renews the counters' subscription for the window after it and answers the last frame's snapshot — counters
/// unmeasured before the span's first frame, exactly the old getters' "the first read arms" contract.
namespace framesdetail {
template <class E> Engine *engineOf(E &e)
{
    if constexpr (RendersByDot<std::remove_reference_t<E>>::value) return &e;
    else return &*e;
}
}  // namespace framesdetail
constexpr unsigned kStatusWindowFrames = 120u;
template <class E> ShadowStatus shadowStatus(E &&e)
{
    Engine *eng = framesdetail::engineOf(e);
    eng->publishShadowStatus(kStatusWindowFrames);
    return eng->published().shadow;
}
template <class E> bool renderStats(E &&e, RenderStats &out)
{
    Engine *eng = framesdetail::engineOf(e);
    eng->publishRenderStats(kStatusWindowFrames);
    out = eng->published().render;
    return true;
}

/// THE VOXEL READ-BACK LANDS A FEW FRAMES LATE (VOXEL-READOUT-1): a cascade's
/// `items`, `attached`, `voxelLevels`, `voxelTriangles`, `voxelRecords` and
/// `voxelOverflow` are a read-back of its gather that nothing waits for. A suite
/// that judges them renders frames until every cascade's reading describes its
/// newest gather (`voxelReadingGather == voxelGathers`). Returns the frames it
/// rendered, -1 if `maxFrames` passed first - which every caller asserts against
/// (`>= 0`): a reading that never landed must say so, not fail later as a stale count.
inline int settleVoxelReadings(Engine *e, Scene *s, int maxFrames = 64)
{
    for (int f = 0;; ++f) {
        const GiStatus st = s->published().gi;
        bool current = true;
        for (const GiStatus::CascadeStatus &c : st.cascades)
            current = current && c.voxelReadingGather == c.voxelGathers;
        if (current) return f;
        if (f >= maxFrames) return -1;
        frames(e, 1);
    }
}

/// The deleted addTestCube's exact geometry (OgreEngine buildCubeV2): a unit
/// cube, 24 vertices, positions + per-face normals, no uvs.
inline MeshData unitCubeMesh()
{
    MeshData d;
    const float h = 0.5f;
    const float fn[6][3] = {{0,0,1},{0,0,-1},{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}};
    const float fv[6][4][3] = {
        {{-h,-h, h},{ h,-h, h},{ h, h, h},{-h, h, h}},
        {{ h,-h,-h},{-h,-h,-h},{-h, h,-h},{ h, h,-h}},
        {{ h,-h, h},{ h,-h,-h},{ h, h,-h},{ h, h, h}},
        {{-h,-h,-h},{-h,-h, h},{-h, h, h},{-h, h,-h}},
        {{-h, h, h},{ h, h, h},{ h, h,-h},{-h, h,-h}},
        {{-h,-h,-h},{ h,-h,-h},{ h,-h, h},{-h,-h, h}} };
    for (int f = 0; f < 6; ++f) {
        for (int v = 0; v < 4; ++v) {
            d.positions.insert(d.positions.end(), { fv[f][v][0], fv[f][v][1], fv[f][v][2] });
            d.normals.insert(d.normals.end(), { fn[f][0], fn[f][1], fn[f][2] });
        }
        const unsigned b = unsigned(f * 4);
        d.indices.insert(d.indices.end(), { b, b + 1, b + 2, b, b + 2, b + 3 });
    }
    return d;
}

/// THE SIX-FACE BOX CARD LIST for a unit cube of half-extent `h`, in MESH space —
/// the shape the bake's generator produces for a convex mesh, written by hand so
/// an engine suite needs no document and no .jmb (gi.card_*, gi.gather_cards (archived: archive/tests/gi)).
inline std::vector<MeshCardDesc> boxCards(float h, float margin = 0.02f)
{
    std::vector<MeshCardDesc> cards;
    for (unsigned a = 0; a < 6u; ++a) {
        MeshCardDesc c;
        c.axis = static_cast<unsigned char>(a);
        c.lodLevel = 0;
        c.origin = Vec3(0, 0, 0);
        c.halfU = h;
        c.halfV = h;
        c.halfDepth = h + margin;
        cards.push_back(c);
    }
    return cards;
}

/// Per-node transform state so position/scale/rotation helpers compose the way
/// the deleted verbs did (each verb overwrote only its component).
struct NodePose { Vec3 pos{0,0,0}; Quat rot; Vec3 scale{1,1,1}; };   // Quat defaults to identity
inline std::map<const Scene *, std::map<NodeId, NodePose>> &poseRegistry()
{
    static std::map<const Scene *, std::map<NodeId, NodePose>> reg;
    return reg;
}

/// addTestCube(albedo, metalness, roughness), rebuilt on the real verbs.
inline NodeId addTestCube(Scene *s, const Colour &albedo, float metalness, float roughness)
{
    const NodeId node = s->createNode();
    if (!node) return 0;
    const MeshId mesh = s->createMesh(unitCubeMesh());
    PbrParams p;
    p.albedo = albedo;
    p.metalness = metalness;
    p.roughness = roughness;
    const MaterialId mat = s->createPbrMaterial(p);
    if (!mesh || !mat || !s->attachMesh(node, mesh, mat)) return 0;
    // Start from a clean pose: the registry is keyed by Scene*, and a destroyed
    // Engine's Scene address can be reused by the next one (test_engine_recreate),
    // which would otherwise leave a stale transform under the same NodeId.
    poseRegistry()[s][node] = NodePose{};
    return node;
}

/// addDirectionalLight(direction, power): a node whose -Y is aimed along
/// `direction`, carrying a directional light (the engine's light convention).
inline NodeId addDirectionalLight(Scene *s, const Vec3 &direction, float power)
{
    const NodeId node = s->createNode();
    if (!node) return 0;
    // Rotate -Y onto `direction`.
    const float len = std::sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
    Vec3 d{ direction.x / len, direction.y / len, direction.z / len };
    const Vec3 from{ 0.f, -1.f, 0.f };
    const Vec3 cross{ from.y * d.z - from.z * d.y, from.z * d.x - from.x * d.z, from.x * d.y - from.y * d.x };
    const float dot = from.x * d.x + from.y * d.y + from.z * d.z;
    Quat q;
    if (dot < -0.999999f) { q = Quat{ 0.f, 0.f, 1.f, 0.f }; }         // 180° about Z
    else {
        const float w = 1.f + dot;
        const float n = std::sqrt(w * w + cross.x * cross.x + cross.y * cross.y + cross.z * cross.z);
        q = Quat{ cross.x / n, cross.y / n, cross.z / n, w / n };      // Quat is (x,y,z,w)
    }
    s->setNodeTransform(node, Vec3{0,0,0}, q, Vec3{1,1,1});
    LightDesc l;
    l.type = LightType::Directional;
    l.colour = Colour(1.f, 1.f, 1.f);
    // setLight applies powerScale = intensity * pi; the deleted verb applied
    // `power` as the scale directly.
    l.intensity = power / 3.14159265358979323846f;
    if (!s->setLight(node, l)) return 0;
    poseRegistry()[s][node] = NodePose{ Vec3{0,0,0}, q, Vec3{1,1,1} };
    return node;
}

inline void setNodePosition(Scene *s, NodeId n, const Vec3 &pos)
{
    NodePose &p = poseRegistry()[s][n];
    p.pos = pos;
    s->setNodeTransform(n, p.pos, p.rot, p.scale);
}

inline void setNodeScale(Scene *s, NodeId n, const Vec3 &scale)
{
    NodePose &p = poseRegistry()[s][n];
    p.scale = scale;
    s->setNodeTransform(n, p.pos, p.rot, p.scale);
}

// ---------------------------------------------------------------------------
// THE DISPLAY ENCODE AND THE SUITES THAT MEASURE LIGHT (SRGB-ENCODE-1). A view
// that presents, or an offscreen view that opted into the chain
// (PostFxDesc::allowOffscreen), writes sRGB-ENCODED bytes; an offscreen view
// that did not is the Plain instrument and writes linear radiance. A suite whose
// bars are LIGHT (a red excess, an occlusion mass, a shadow ratio) reads the
// former through `linearise`, which turns the bytes back into 8-bit linear
// radiance — the space every such bar in this tree was derived in.
inline bool displayEncoded(View *v)
{
    return v && (!v->isOffscreen() || jahshaka::engine::inspect::view(v)->postFx().allowOffscreen);
}
inline void linearise(Image &img)
{
    unsigned char lut[256];
    for (int i = 0; i < 256; ++i) {
        const double c = i / 255.0;
        const double l = c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
        lut[i] = static_cast<unsigned char>(std::lround(l * 255.0));
    }
    for (size_t i = 0; i < img.rgba.size(); ++i)
        if ((i & 3u) != 3u) img.rgba[i] = lut[img.rgba[i]];
}
/// readPixels in LINEAR radiance whatever the view's space (see above).
inline bool readLinear(View *v, Image &img)
{
    if (!v || !v->readPixels(img)) return false;
    if (displayEncoded(v)) linearise(img);
    return true;
}

/// setCameraPosition without lookAt: identity orientation (looking down -Z),
/// engine-default projection — exactly what the deleted View verb left behind.
inline void testCameraAt(View *v, const Vec3 &pos)
{
    CameraDesc c;
    c.position = pos;               // orientation stays identity: looking down -Z
    v->setCamera(c);
}

/// setCameraPosition + lookAt(target), +Y up.
/// The DESCRIPTION testCameraLookAt pushes, so a suite that needs a variant of
/// it (an ORTHOGRAPHIC camera at the same pose, tests/ssr's ortho fixture) can
/// build one without a second copy of the quaternion.
inline CameraDesc testCameraDescLookAt(const Vec3 &pos, const Vec3 &target)
{
    const Vec3 f0{ target.x - pos.x, target.y - pos.y, target.z - pos.z };
    const float fl = std::sqrt(f0.x * f0.x + f0.y * f0.y + f0.z * f0.z);
    const Vec3 f{ f0.x / fl, f0.y / fl, f0.z / fl };          // forward (camera -Z)
    const Vec3 upW{ 0.f, 1.f, 0.f };
    Vec3 r{ f.y * upW.z - f.z * upW.y, f.z * upW.x - f.x * upW.z, f.x * upW.y - f.y * upW.x }; // right = f x up
    const float rl = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z);
    r = Vec3{ r.x / rl, r.y / rl, r.z / rl };
    const Vec3 u{ r.y * f.z - r.z * f.y, r.z * f.x - r.x * f.z, r.x * f.y - r.y * f.x };       // up = r x f
    // Rotation matrix columns: x=r, y=u, z=-f  ->  quaternion.
    const float m00 = r.x, m01 = u.x, m02 = -f.x;
    const float m10 = r.y, m11 = u.y, m12 = -f.y;
    const float m20 = r.z, m21 = u.z, m22 = -f.z;
    const float tr = m00 + m11 + m22;
    Quat q;
    if (tr > 0.f) {
        const float sq = std::sqrt(tr + 1.f) * 2.f;
        q = Quat{ (m21 - m12) / sq, (m02 - m20) / sq, (m10 - m01) / sq, 0.25f * sq };
    } else if (m00 > m11 && m00 > m22) {
        const float sq = std::sqrt(1.f + m00 - m11 - m22) * 2.f;
        q = Quat{ 0.25f * sq, (m01 + m10) / sq, (m02 + m20) / sq, (m21 - m12) / sq };
    } else if (m11 > m22) {
        const float sq = std::sqrt(1.f + m11 - m00 - m22) * 2.f;
        q = Quat{ (m01 + m10) / sq, 0.25f * sq, (m12 + m21) / sq, (m02 - m20) / sq };
    } else {
        const float sq = std::sqrt(1.f + m22 - m00 - m11) * 2.f;
        q = Quat{ (m02 + m20) / sq, (m12 + m21) / sq, 0.25f * sq, (m10 - m01) / sq };
    }
    CameraDesc c;
    c.position = pos;
    c.orientation = q;
    return c;
}

inline void testCameraLookAt(View *v, const Vec3 &pos, const Vec3 &target)
{
    v->setCamera(testCameraDescLookAt(pos, target));
}

// ---------------------------------------------------------------------------
// THE LEAK ROOM — ONE fixture, two suites (GATHER-0 fix round, B1).
//
// A sealed 10 m room with one lamp INSIDE it (pure green, so the red channel
// is 100 % the other lamp) and one OUTSIDE it (red, a metre off the -Z wall).
// Shadows on, the direct term through the wall is exactly zero at every
// thickness, so every red pixel inside the room arrived through global
// illumination and that number IS the leak.
//
// It lived in tests/gi/test_gi_leak_room.cpp and was copied verbatim into
// gi.gather (archived: archive/tests/gi), which is one fixture too many: the two suites compare their
// numbers against each other (the gather's field arm reproduces gi.leak_room (archived: archive/tests/gi)'s
// chain arm to within 0.7 %) and that comparison is only worth anything while
// the rooms are the SAME room. It is here so they cannot drift.
namespace leakroom {

/// One axis-aligned slab of the shell (CARDED on request: its six box cards, PHOTON-7 stage 5's leak arm).
inline NodeId addSlab(Scene *s, const Colour &albedo, const Vec3 &pos, const Vec3 &scale, bool carded = false)
{
    const NodeId node = s->createNode();
    MeshData md = unitCubeMesh();
    if (carded) md.cards = boxCards(0.5f);
    const MeshId mesh = s->createMesh(md);
    PbrParams p;
    p.albedo = albedo;
    p.metalness = 0.0f;
    p.roughness = 0.9f;
    const MaterialId mat = s->createPbrMaterial(p);
    if (!node || !mesh || !mat || !s->attachMesh(node, mesh, mat)) return 0;
    s->setNodeTransform(node, pos, Quat(), scale);
    return node;
}

/// What `build` hands back: the two lamps, and the outside one's description
/// so a caller can darken and relight it (which is how the leak is isolated).
struct Room {
    NodeId inside = 0;
    NodeId outside = 0;
    LightDesc outsideLight;
};

/// The room at wall thickness `T`, its two lamps, and the camera at the pose
/// both suites measure from. The interior (x,z in [-5,5], y in [0,4]) is
/// identical at every thickness — only the barrier's depth in voxels changes.
/// `shiftZ` moves the room and its lamps (not the camera, so not the lattice)
/// along z: at 0 the -Z wall's inner face lies ON a cascade-0 texel plane, so a
/// wall thinner than the cell is split between two texels, not held by one
/// (gi.gather (archived: archive/tests/gi)'s two-sided arm moves it -0.02 m; PHOTON-VOXEL-5).
inline Room build(Scene *s, View *view, float T, float shiftZ = 0.0f, bool carded = false)
{
    s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
    const Colour white(0.8f, 0.8f, 0.8f);
    const float ho = 5.0f + T * 0.5f;
    const float span = 10.0f + 2.0f * T;
    addSlab(s, white, Vec3(0, -T * 0.5f, shiftZ), Vec3(span, T, span), carded);            // floor
    addSlab(s, white, Vec3(0, 4.0f + T * 0.5f, shiftZ), Vec3(span, T, span), carded);      // ceiling
    addSlab(s, white, Vec3(0, 2, -ho + shiftZ), Vec3(span, 4.0f, T), carded);                  // -Z: THE wall
    addSlab(s, white, Vec3(0, 2,  ho + shiftZ), Vec3(span, 4.0f, T), carded);                  // +Z
    addSlab(s, white, Vec3(-ho, 2, shiftZ), Vec3(T, 4.0f, span), carded);                  // -X
    addSlab(s, white, Vec3( ho, 2, shiftZ), Vec3(T, 4.0f, span), carded);                  // +X

    Room r;
    // The inside lamp: PURE GREEN, so the red channel is entirely the outside
    // lamp's and no threshold has to separate them.
    r.inside = s->createNode();
    s->setNodeTransform(r.inside, Vec3(0.0f, 3.0f, 2.5f + shiftZ), Quat(), Vec3(1, 1, 1));
    LightDesc li;
    li.type = LightType::Point;
    li.colour = Colour(0.0f, 1.0f, 0.0f);
    li.intensity = 3.0f;
    li.range = 14.0f;
    li.castShadows = true;
    s->setLight(r.inside, li);

    // The outside lamp: RED, a metre beyond the outer face of the -Z wall.
    r.outside = s->createNode();
    s->setNodeTransform(r.outside, Vec3(0.0f, 2.0f, -(ho + T * 0.5f + 1.0f) + shiftZ), Quat(), Vec3(1, 1, 1));
    r.outsideLight.type = LightType::Point;
    r.outsideLight.colour = Colour(1.0f, 0.0f, 0.0f);
    r.outsideLight.intensity = 25.0f;
    r.outsideLight.range = 12.0f;
    r.outsideLight.castShadows = true;
    s->setLight(r.outside, r.outsideLight);

    // The camera looks at the inner face of the -Z wall from THREE METRES
    // away, off to +X so nothing else is in the shot. Three metres, and not
    // the original spike's six, for one reason: under a chain the irradiance
    // field rides cascade 0 — a 10 m box centred on the camera — and the wall
    // being measured has to be inside it, or the suite grades the ring.
    if (view) testCameraLookAt(view, Vec3(3.6f, 2.0f, -2.0f), Vec3(3.6f, 2.0f, -5.0f));
    return r;
}

/// The lamp on or off, for the two halves of one leak reading.
inline void setOutsideIntensity(Scene *s, Room &r, float intensity)
{
    r.outsideLight.intensity = intensity;
    s->setLight(r.outside, r.outsideLight);
}

/// Mean red and green of the centred block both suites measure — expressed as
/// FRACTIONS of the image so the two may use different resolutions.
inline void meanRG(const Image &img, float &r, float &g)
{
    double sr = 0.0, sg = 0.0; int n = 0;
    const unsigned x0 = img.width * 5u / 16u, x1 = img.width * 11u / 16u;
    const unsigned y0 = img.height * 5u / 16u, y1 = img.height * 11u / 16u;
    for (unsigned y = y0; y < y1; ++y)
        for (unsigned x = x0; x < x1; ++x) { const Colour c = img.at(x, y); sr += c.r; sg += c.g; ++n; }
    r = float(n ? sr / n : 0.0); g = float(n ? sg / n : 0.0);
}
/// The same mean over a DISPLAY-ENCODED readback (a view that opted into the
/// chain: SRGB-ENCODE-1), decoded to linear light first, so the leak is read in
/// the unit its bars were derived in.
inline void meanRGDecoded(const Image &img, float &r, float &g)
{
    const auto lin = [](unsigned char v) {
        const double c = v / 255.0;
        return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    };
    double sr = 0.0, sg = 0.0; int n = 0;
    const unsigned x0 = img.width * 5u / 16u, x1 = img.width * 11u / 16u;
    const unsigned y0 = img.height * 5u / 16u, y1 = img.height * 11u / 16u;
    for (unsigned y = y0; y < y1; ++y)
        for (unsigned x = x0; x < x1; ++x) {
            const size_t i = (size_t(y) * img.width + x) * 4u;
            sr += lin(img.rgba[i]); sg += lin(img.rgba[i + 1]); ++n;
        }
    r = float(n ? sr / n : 0.0); g = float(n ? sg / n : 0.0);
}

}  // namespace leakroom

// ---------------------------------------------------------------------------
// THE ANALYTIC SKY VISIBILITY OF A FIXTURE (PHOTON-READER-1). What a surface
// point with an upward (+Y) normal can see of the sky, cosine-weighted, when the
// only occluders are axis-aligned boxes: the quantity the irradiance field's
// stored escape and the cones' escape weight both estimate. Exact up to the
// quadrature (a stratified NxN cosine-distributed grid over the hemisphere).
// ---------------------------------------------------------------------------
struct AnalyticBox { Vec3 mn, mx; };

inline bool rayHitsBox(const Vec3 &o, const Vec3 &d, const AnalyticBox &b)
{
    float t0 = 0.0f, t1 = 1e30f;
    const float O[3] = { o.x, o.y, o.z }, D[3] = { d.x, d.y, d.z };
    const float A[3] = { b.mn.x, b.mn.y, b.mn.z }, B[3] = { b.mx.x, b.mx.y, b.mx.z };
    for (int a = 0; a < 3; ++a) {
        if (std::fabs(D[a]) < 1e-12f) {
            if (O[a] < A[a] || O[a] > B[a]) return false;
            continue;
        }
        float ta = (A[a] - O[a]) / D[a], tb = (B[a] - O[a]) / D[a];
        if (ta > tb) { const float t = ta; ta = tb; tb = t; }
        t0 = std::max(t0, ta);
        t1 = std::min(t1, tb);
    }
    return t0 <= t1;
}

/// THE DIFFUSE LOBE'S DIRECTIONAL ALBEDO, BY DIRECT INTEGRATION (PHOTON-WRITER-1):
/// HlmsPbs's normalised Disney diffuse (200.BRDFs_piece_ps.any: energyFactor =
/// lerp(1, 1/1.51, r), fd90 = 0.5 r + 2 r (V.H)^2, lightScatter and viewScatter)
/// integrated over a uniform incoming hemisphere, as a fraction of a Lambertian
/// surface's reflection — for a view at cos theta_v = `v`. The suites' reference
/// for what the renderer's jahDiffuseAlbedo (its closed form, REDS-1) must
/// reproduce: computed here from the lobe, never copied from the shader. The
/// quadrature is in cos theta_l (midpoints under the weight 2 cos), where the lobe
/// is a polynomial: within 0.006 % of the integral at n = 48 (the old midpoints in
/// cos^2 converged as n^-1.5 and read the hemispherical mean 0.030 % low at r = 1).
inline double disneyDiffuseAlbedo(double v, double r, int n = 64)
{
    v = std::min(1.0, std::max(0.0, v));
    r = std::min(1.0, std::max(1e-4, r));
    const double V[3] = { std::sqrt(std::max(0.0, 1.0 - v * v)), v, 0.0 };
    const double ef = 1.0 + (1.0 / 1.51 - 1.0) * r;
    double sum = 0.0;
    for (int i = 0; i < n; ++i) {
        const double ct = (double(i) + 0.5) / double(n);    // the cosine's midpoints, weighted 2 cos below
        const double st = std::sqrt(std::max(0.0, 1.0 - ct * ct));
        const double wc = 2.0 * ct;
        for (int j = 0; j < 2 * n; ++j) {
            const double ph = 2.0 * 3.14159265358979323846 * (double(j) + 0.5) / double(2 * n);
            const double L[3] = { st * std::cos(ph), ct, st * std::sin(ph) };
            double H[3] = { L[0] + V[0], L[1] + V[1], L[2] + V[2] };
            const double hl = std::sqrt(H[0] * H[0] + H[1] * H[1] + H[2] * H[2]);
            const double vdh = hl > 0.0 ? std::max(0.0, (V[0] * H[0] + V[1] * H[1] + V[2] * H[2]) / hl) : 0.0;
            const double fd90 = 0.5 * r + 2.0 * vdh * vdh * r;
            const double ls = 1.0 + (fd90 - 1.0) * std::pow(1.0 - ct, 5.0);
            const double vs = 1.0 + (fd90 - 1.0) * std::pow(1.0 - v, 5.0);
            sum += wc * ls * vs * ef;
        }
    }
    return sum / double(n * 2 * n);
}

/// THE METAL'S SPECULAR LOBE, BY DIRECT INTEGRATION (METAL-BOUNCE-1): HlmsPbs's BRDF_Default specular
/// (200.BRDFs_piece_ps.any: GGX D, the height-correlated Smith GGX visibility, Schlick's Fresnel on V.H;
/// alpha = perceptual roughness squared) integrated over the light directions for a view at
/// cos theta_v = `v` - its directional albedo is F0 x A + B, A the (1 - Fc) part and B the Fc part,
/// Fc = (1 - V.H)^5. The suites' reference for what the store's table (PhotonSpecularAlbedo.h) must
/// reproduce: computed here from the lobe, never read from the table. The half vector is drawn by
/// D(h) cos(h) over the full circle, n x 2n stratified.
inline void ggxSpecularAlbedo(double v, double r, double &A, double &B, int n = 256)
{
    v = std::min(1.0, std::max(1e-3, v));
    const double a = std::max(r * r, 1e-3), a2 = a * a;
    const double V[3] = { std::sqrt(std::max(0.0, 1.0 - v * v)), 0.0, v };
    double sa = 0.0, sb = 0.0;
    for (int i = 0; i < n; ++i) {
        const double u = (double(i) + 0.5) / double(n);
        const double c2 = (1.0 - u) / (1.0 + (a2 - 1.0) * u);
        const double ch = std::sqrt(c2), sh = std::sqrt(std::max(0.0, 1.0 - c2));
        for (int j = 0; j < 2 * n; ++j) {
            const double ph = 2.0 * 3.14159265358979323846 * (double(j) + 0.5) / double(2 * n);
            const double H[3] = { sh * std::cos(ph), sh * std::sin(ph), ch };
            const double vh = V[0] * H[0] + V[1] * H[1] + V[2] * H[2];
            const double lz = 2.0 * vh * H[2] - V[2];
            if (!(lz > 0.0) || !(vh > 0.0)) continue;
            const double lv = lz * std::sqrt(v * v * (1.0 - a2) + a2);
            const double ll = v * std::sqrt(lz * lz * (1.0 - a2) + a2);
            // f cos_l / pdf(l), pdf(l) = D(h) cos(h) / (4 V.H): the D cancels
            const double w = 0.5 / (lv + ll) * lz * 4.0 * vh / ch;
            const double fc = std::pow(1.0 - vh, 5.0);
            sa += w * (1.0 - fc);
            sb += w * fc;
        }
    }
    A = sa / double(n * 2 * n);
    B = sb / double(n * 2 * n);
}

/// ...and over a UNIFORM incoming hemisphere: A-bar = 2 x integral of A(mu) mu dmu (B-bar likewise), at
/// `m` cosines - what the bounce re-emits of what a metal gathers.
inline void ggxSpecularAlbedoHemi(double r, double &A, double &B, int m = 64, int n = 128)
{
    A = 0.0;
    B = 0.0;
    for (int k = 0; k < m; ++k) {
        const double mu = (double(k) + 0.5) / double(m);
        double a = 0.0, b = 0.0;
        ggxSpecularAlbedo(mu, r, a, b, n);
        A += 2.0 * a * mu / double(m);
        B += 2.0 * b * mu / double(m);
    }
}

/// THE DIFFUSE LOBE'S HEMISPHERICAL ALBEDO, by direct integration: 2 x integral of
/// disneyDiffuseAlbedo(mu, r) mu dmu - the bounce's jahDiffuseAlbedoHemi.
inline double disneyDiffuseAlbedoHemi(double r, int m = 64, int n = 48)
{
    double h = 0.0;
    for (int k = 0; k < m; ++k) {
        const double mu = (double(k) + 0.5) / double(m);
        h += 2.0 * disneyDiffuseAlbedo(mu, r, n) * mu / double(m);
    }
    return h;
}

inline float cosineSkyVisibilityUp(const Vec3 &p, const std::vector<AnalyticBox> &boxes,
                                   int n = 200)
{
    unsigned open = 0, total = 0;
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j) {
            const float u1 = (float(i) + 0.5f) / float(n), u2 = (float(j) + 0.5f) / float(n);
            const float r = std::sqrt(u1), ph = 6.28318530718f * u2;
            const Vec3 d(r * std::cos(ph), std::sqrt(1.0f - u1), r * std::sin(ph));
            bool hit = false;
            for (const AnalyticBox &b : boxes)
                if (rayHitsBox(p, d, b)) { hit = true; break; }
            open += hit ? 0u : 1u;
            ++total;
        }
    return total ? float(open) / float(total) : 0.0f;
}

/// Where the pixel (px, py) of a square `size` x `size` view from
/// testCameraLookAt(pos, target) at `fovDeg` meets the plane y = planeY.
inline Vec3 groundPointForPixel(const Vec3 &pos, const Vec3 &target, unsigned px, unsigned py,
                                unsigned size, float planeY = 0.0f, float fovDeg = 45.0f)
{
    Vec3 f(target.x - pos.x, target.y - pos.y, target.z - pos.z);
    float l = std::sqrt(f.x * f.x + f.y * f.y + f.z * f.z);
    f = Vec3(f.x / l, f.y / l, f.z / l);
    Vec3 r(-f.z, 0.0f, f.x);                                   // f x (0,1,0)
    l = std::sqrt(r.x * r.x + r.z * r.z);
    r = Vec3(r.x / l, 0.0f, r.z / l);
    const Vec3 u(r.y * f.z - r.z * f.y, r.z * f.x - r.x * f.z, r.x * f.y - r.y * f.x);
    const float t = std::tan(fovDeg * 0.5f * 3.14159265f / 180.0f);
    const float nx = 2.0f * (float(px) + 0.5f) / float(size) - 1.0f;
    const float ny = 1.0f - 2.0f * (float(py) + 0.5f) / float(size);
    const Vec3 d(f.x + (r.x * nx + u.x * ny) * t, f.y + (r.y * nx + u.y * ny) * t,
                 f.z + (r.z * nx + u.z * ny) * t);
    const float s = (planeY - pos.y) / d.y;
    return Vec3(pos.x + d.x * s, planeY, pos.z + d.z * s);
}

/// GPU MILLISECONDS ARE A CAPTURE'S (lane TEST-1). Every GPU time a status
/// readout reports — RayQueryStatus::reflectMs / tlasMs / blasMs / skinMs, the
/// gather's four jobs, SunContactStatus::gpuMs, the cards' relight and traces — is
/// the frame monitor's row for that job (Engine.h: one GPU-timing facility), so it
/// reads -1 outside a capture: by owner decision D3 no query pool exists outside
/// one. A measuring window holds one of these for its life — opened AFTER the first
/// view exists (the device it samples on comes up with it; a capture started earlier is
/// CPU-only for its whole life). Nests: an engine whose monitor is already on (a
/// suite's own capture) is left as it is.
class GpuTimingWindow {
public:
    explicit GpuTimingWindow(Engine *e) : mE(e) {
        if (mE && mE->frameMonitor() == MonitorLevel::Off) {
            mE->setFrameMonitor(MonitorLevel::Review);
            mArmed = true;
        }
    }
    ~GpuTimingWindow() {
        if (!mArmed) return;
        mE->setFrameMonitor(MonitorLevel::Off);
        std::vector<MonitorRecord> tail;   // the capture's records are not this window's subject
        mE->popMonitorRecords(tail);
    }
    GpuTimingWindow(const GpuTimingWindow &) = delete;
    GpuTimingWindow &operator=(const GpuTimingWindow &) = delete;
private:
    Engine *mE = nullptr;
    bool mArmed = false;
};

}  // namespace enginetest
