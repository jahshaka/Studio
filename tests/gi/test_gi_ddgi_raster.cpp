// gi.ddgi_raster — THE RASTER-FED IRRADIANCE FIELD (GI_UNIFIED_SPEC.md P3 "A2",
// rayon2 S3): probes fed by six 32x32 scene renders each instead of by cone
// tracing the voxel volume, which is the one way a SKINNED, ANIMATED rig can
// bounce light — a voxelizer bakes a rig at one pose and the voxel field then
// lights the room with a statue of it for ever.
//
// WHAT THE SUITE PROVES, case by case:
//   1. ARMS: `ddgiSource: raster` binds, giStatus reports it, the source is
//      refused nowhere on this box (the compositor is staged, patch 0023 is in
//      the media).
//   2. NEVER DARK: at a PAUSED budget the raster field has captured nothing
//      yet, and its atlases are the voxel field's converged answer — the frame
//      is byte-identical to the voxel-fed one (black ambient, so the pass
//      buffer's raster block changes no pixel either).
//   3. BUDGET: probes per frame is the dial's fold (kIfdRasterProbesPerBudget
//      per unit, clamped to the field), and the field converges in exactly
//      ceil(probes / perFrame) frames — the same determinism discipline as
//      gi.ddgi (fixed delta, an exact frame count, a control rebuild).
//   4. THE RIG: a two-bone skinned arm (a red box, hinged at the floor) swings
//      from vertical to horizontal under a fixed-dt clip. The floor patch its
//      horizontal pose lies over must go redder under the RASTER field once it
//      re-converges at that pose, and must NOT under the voxel field (which
//      only ever saw the bind pose). The control: the same pose, rebuilt, is
//      pixel-identical.
//   5. MASK: a bright emissive cube flagged as an editor HELPER, hung right
//      over the floor probe, must not reach the probes (helpers carry
//      kHelperBit instead of kVisibleBit and the capture pass masks 0x1); the
//      same cube as real geometry must (the control that the mask is what is
//      doing the excluding, not distance).
//   6. SKY: with a blue 1x1 equirect sky bound, the raster probes carry it
//      (RQ 0 is inside the capture range): an upward-facing floor goes bluer
//      than under no sky, AND the ambient proxy is inert while the sky is
//      bound (ddgiAmbient 1 vs 0 identical) — the double-count guard.
//
// Its own binary like every GI suite (process-wide HlmsPbs binding); fixed
// frame delta; no wall clock anywhere.
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

static void render(Engine *e, int frames = 4) { for (int i = 0; i < frames; ++i) e->renderOneFrame(); }
static float lum(const Colour &c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }
static void show(const char *what, const Colour &c)
{
    std::printf("   %-40s r=%.4f g=%.4f b=%.4f  (lum %.4f)\n", what, c.r, c.g, c.b, lum(c));
}
static bool sameImage(const Image &a, const Image &b, unsigned *outX = nullptr, unsigned *outY = nullptr,
                      float *outDelta = nullptr)
{
    if (a.width != b.width || a.height != b.height) return false;
    float worst = 0.0f; unsigned wx = 0, wy = 0;
    for (unsigned y = 0; y < a.height; ++y)
        for (unsigned x = 0; x < a.width; ++x) {
            const Colour ca = a.at(x, y), cb = b.at(x, y);
            const float d = std::max(std::max(std::fabs(ca.r - cb.r), std::fabs(ca.g - cb.g)),
                                     std::fabs(ca.b - cb.b));
            if (d > worst) { worst = d; wx = x; wy = y; }
        }
    if (outX) *outX = wx; if (outY) *outY = wy; if (outDelta) *outDelta = worst;
    return worst == 0.0f;
}

/// JAHSHAKA_TEST_DUMP=<dir>: writes a frame as PPM so a probe pixel can be
/// pinned against what actually rendered (the build record's job; never on
/// in ctest).
static void dumpFrame(const Image &img, const char *name)
{
    const char *dir = std::getenv("JAHSHAKA_TEST_DUMP");
    if (!dir) return;
    const std::string path = std::string(dir) + "/" + name + ".ppm";
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%u %u\n255\n", img.width, img.height);
    for (unsigned y = 0; y < img.height; ++y)
        for (unsigned x = 0; x < img.width; ++x) {
            const size_t i = (size_t(y) * img.width + x) * 4;
            std::fwrite(&img.rgba[i], 1, 3, f);
        }
    std::fclose(f);
}

static NodeId addSlab(Scene *s, const Colour &albedo, const Vec3 &pos, const Vec3 &scale)
{
    const NodeId n = enginetest::addTestCube(s, albedo, 0.0f, 0.9f);
    enginetest::setNodePosition(s, n, pos);
    enginetest::setNodeScale(s, n, scale);
    return n;
}

static Quat rotZ(float degrees)
{
    const float half = degrees * 3.14159265f / 360.0f;
    return Quat(0.0f, 0.0f, std::sin(half), std::cos(half));
}

// THE ARM: a red box 0.6 x 3 x 0.6 (y in [0, 3] in its own space), skinned to
// a two-bone rig — root at its base, tip 1.5 up — lower half to the root,
// upper half to the tip, HINGED 1.5 ABOVE THE FLOOR (the node sits at y 1.5).
// Its clip swings the ROOT about Z: 0 = vertical (a column from 1.5 to 4.5),
// 1 s = horizontal (hanging along -X at height 1.5, its underside facing the
// floor), 2 s = back. Absolute times are pushed per frame (the engine never
// advances a clip on its own), so a pose is a number, and the same number is
// the same pose for ever.
//
// WHY THE HINGE IS RAISED (measured at the first gate runs, rayon2 S3): an arm
// swung down ONTO the floor is seen by exactly one probe row, 0.5 above it, at
// a grazing angle, and a 0.6-wide box at that angle is averaged away by the
// 6x6-texel irradiance octahedron a probe stores — the SAME numbers came out
// for a plain unskinned box posed by its node (the env-gated control below),
// so it was the probe field's angular resolution, not the capture. Raised, the
// horizontal arm's underside faces every probe beneath it.
struct Arm { NodeId node = 0; NodeId grey = 0; };
static Arm addArm(Scene *s)
{
    Arm arm;
    MeshData d = enginetest::unitCubeMesh();
    const size_t n = d.vertexCount();
    d.blendIndices.assign(n * 4, 0);
    d.blendWeights.assign(n * 4, 0.0f);
    for (size_t i = 0; i < n; ++i) {
        float &x = d.positions[i * 3 + 0], &y = d.positions[i * 3 + 1], &z = d.positions[i * 3 + 2];
        x *= 0.6f; z *= 0.6f; y = y * 3.0f + 1.5f;          // 0..3
        d.blendIndices[i * 4] = (unsigned char)(y < 1.5f ? 0 : 1);
        d.blendWeights[i * 4] = 1.0f;
    }
    d.dynamic = true;
    SkeletonDesc rig;
    rig.id = "gi_raster_arm";
    BoneDesc root; root.name = "root"; root.parent = -1; root.bindPosition = Vec3(0, 0, 0);
    BoneDesc tip;  tip.name = "tip";   tip.parent = 0;   tip.bindPosition = Vec3(0, 1.5f, 0);
    rig.bones = { root, tip };
    arm.node = s->createNode();
    if (std::getenv("JAHSHAKA_TEST_UNSKINNED")) {
        // EXPERIMENT (rayon2 S3 gate): the same arm as plain geometry, posed by
        // rotating its node — tells skinned-draw defects from placement ones.
        d.blendIndices.clear(); d.blendWeights.clear(); d.dynamic = false;
        const MeshId m2 = s->createMesh(d);
        PbrParams p2; p2.albedo = Colour(1.0f, 0.05f, 0.05f); p2.roughness = 0.9f; p2.emissive = Colour(3.0f, 0.15f, 0.15f);
        const MaterialId mat2 = s->createPbrMaterial(p2);
        if (!m2 || !mat2 || !s->attachMesh(arm.node, m2, mat2)) { arm.node = 0; }
        s->setNodeTransform(arm.node, Vec3(0.0f, 1.5f, 0.0f), Quat(), Vec3(1, 1, 1));
        return arm;
    }
    const MeshId mesh = s->createMesh(d);
    // Red AND emissive: a lit matte arm's one bounce onto the floor beside it
    // is below 1/255 (measured: the voxel field's red there is VCT's known
    // over-bright leak, the raster field's is 0.000), so the arm carries its
    // own light — a glowing rig is the honest way to make "the probes see the
    // pose" a number, and emissive geometry reaches the probes only by being
    // RENDERED into them.
    PbrParams p; p.albedo = Colour(1.0f, 0.05f, 0.05f); p.metalness = 0.0f; p.roughness = 0.9f;
    p.emissive = Colour(3.0f, 0.15f, 0.15f);
    const MaterialId mat = s->createPbrMaterial(p);
    if (!mesh || !mat || !s->attachSkinnedMesh(arm.node, mesh, mat, rig)) { arm.node = 0; return arm; }
    ClipDesc clip;
    clip.id = "gi_raster_swing"; clip.name = "Swing"; clip.length = 2.0f;
    BoneTrack t; t.bone = 0;
    BoneKey k0; k0.time = 0.0f; k0.rotation = rotZ(0.0f);
    BoneKey k1; k1.time = 1.0f; k1.rotation = rotZ(90.0f);
    BoneKey k2; k2.time = 2.0f; k2.rotation = rotZ(0.0f);
    t.keys = { k0, k1, k2 };
    clip.tracks = { t };
    if (!s->attachClips(arm.node, &clip, 1)) { arm.node = 0; return arm; }
    s->setNodeTransform(arm.node, Vec3(0.0f, 1.5f, 0.0f), Quat(), Vec3(1, 1, 1));
    // THE GREY TWIN: the same mesh and rig on a second node with a plain grey
    // material, hinged 2.5 behind the red arm and swinging in parallel. The red
    // arm's faces are emissive-saturated (they clip at 1.0), so "the rig
    // RECEIVES field diffuse" is read on this one: a skinned surface whose
    // value moves with the field's intensity is lit BY the field.
    arm.grey = s->createNode();
    PbrParams pg; pg.albedo = Colour(0.6f, 0.6f, 0.6f); pg.metalness = 0.0f; pg.roughness = 0.9f;
    const MaterialId matg = s->createPbrMaterial(pg);
    if (!matg || !s->attachSkinnedMesh(arm.grey, mesh, matg, rig) || !s->attachClips(arm.grey, &clip, 1)) {
        arm.node = 0; return arm;
    }
    s->setNodeTransform(arm.grey, Vec3(0.0f, 1.5f, -2.5f), Quat(), Vec3(1, 1, 1));
    return arm;
}
static void pose(Scene *s, const Arm &arm, float seconds)
{
    if (std::getenv("JAHSHAKA_TEST_UNSKINNED")) {
        const float deg = seconds <= 1.0f ? 90.0f * seconds : 90.0f * (2.0f - seconds);
        s->setNodeTransform(arm.node, Vec3(0.0f, 1.5f, 0.0f), rotZ(deg), Vec3(1, 1, 1));
        return;
    }
    ClipState st; st.name = "Swing"; st.enabled = true; st.time = seconds; st.looping = true;
    s->setClipStates(arm.node, &st, 1);
    if (arm.grey) s->setClipStates(arm.grey, &st, 1);
}

/// The room every case uses: a white floor, a directional light from above,
/// the arm at the origin, and a black ambient (so every number below is
/// bounce or sky, never the proxy — case 6 turns ambient on for itself).
struct Room {
    View *view = nullptr; Scene *scene = nullptr; Arm arm; NodeId light = 0;
};
static Room buildRoom(Engine *e, const char *name)
{
    Room r;
    r.view = e->createOffscreenView(name, 128, 128, Colour(0, 0, 0));
    r.scene = e->createScene(name);
    r.view->setScene(r.scene);
    r.scene->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
    // Mid-grey floor under a modest light: a clipped floor cannot move, and
    // every number below is a difference (the first run of this suite read
    // 1.0 everywhere at albedo 0.9 / intensity 1.5).
    addSlab(r.scene, Colour(0.5f, 0.5f, 0.5f), Vec3(0.0f, -0.05f, 0.0f), Vec3(14.0f, 0.1f, 14.0f));
    r.arm = addArm(r.scene);
    // The light comes in at 45 degrees from the CAMERA's side (+Z): the arm's
    // front face is lit and bounces onto the floor in front of it. Straight
    // down would light only its top face, which bounces upward, away from the
    // floor — the first gate run measured exactly zero red that way.
    r.light = enginetest::addDirectionalLight(r.scene, Vec3(0.0f, -0.7071f, -0.7071f), 0.8f);
    // Looking down at the floor from +Z, the arm's horizontal pose lies along
    // -X: the probe sits under where the arm's tip ends up.
    enginetest::testCameraLookAt(r.view, Vec3(0.0f, 5.0f, 5.0f), Vec3(-1.0f, 0.0f, 0.0f));
    return r;
}
static GiParams base()
{
    GiParams gi;
    gi.mode = GiMode::Vct; gi.quality = GiQuality::Medium; gi.numBounces = 2;
    gi.boundsMin = Vec3(-6.0f, -1.0f, -6.0f); gi.boundsMax = Vec3(6.0f, 5.0f, 6.0f);
    gi.ddgi = GiToggle::On; gi.ddgiAmbient = 0.0f;
    // Budget 64: one budget unit buys ONE raster probe per frame (measured
    // ~3.4 ms each, OgreGi.cpp kIfdRasterProbesPerBudget), so 64 keeps a
    // full re-converge at 128 frames and the suite under a minute.
    gi.updateBudget = 64;
    return gi;
}
// The floor BESIDE the arm's horizontal pose: the arm lies ON the floor (root
// at the origin, swung 90 degrees about Z, along -X), so the floor under it is
// hidden and its red bounce lands on the floor next to it. PINNED against the
// rendered 128x128 frame at the first gate run (rayon2 S3 build record): the
// horizontal arm spans x 20..85, y 50..70; (50,76) is open floor just in front
// of its middle, and open floor at the vertical pose too (the column stands at
// x ~ 85).
static const unsigned kProbeX = 50, kProbeY = 76;
// The grey twin's front face in the horizontal pose (pinned from the frame).
static const unsigned kGreyX = 60, kGreyY = 15;   // the band x 34..94, y 11..18 in the dumped frame
static const unsigned kFarX = 100, kFarY = 84;        // floor well clear of the arm

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-ddgi-raster-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();

    Room r = buildRoom(e, "ddgiraster");
    Scene *s = r.scene;
    CHECK(r.arm.node != 0, "the two-bone skinned arm attaches with its swing clip");
    if (!r.arm.node) std::printf("   %s\n", e->lastError().c_str());
    Image img;

    // ================================================================ 1 + 2
    std::printf("\n== 1/2: the raster source arms, and is never dark ==\n");
    pose(s, r.arm, 0.0f);
    GiParams voxel = base();
    CHECK(s->setGlobalIllumination(voxel), "voxel-fed field builds");
    render(e, 6);
    Image voxelFrame; r.view->readPixels(voxelFrame);
    dumpFrame(voxelFrame, "raster_voxel_vertical");
    GiStatus st = s->giStatus();
    CHECK(st.ifdBound && st.ifdConverged && st.ifdSource == GiSource::Voxel,
          "voxel field bound, converged, reported as voxel");
    show("floor probe, voxel field", voxelFrame.at(kProbeX, kProbeY));

    GiParams paused = base();
    paused.ddgiSource = GiSource::Raster;
    paused.updateBudget = 0;
    CHECK(s->setGlobalIllumination(paused), "raster source at a paused budget builds");
    render(e, 6);
    st = s->giStatus();
    CHECK(st.ifdBound, "the field is bound");
    CHECK(st.ifdSource == GiSource::Raster, "giStatus reports the RASTER source");
    CHECK(!st.ifdConverged && st.ifdProbesPerFrame == 0,
          "paused: nothing captured yet, probes/frame 0 (the raster field never converges inline)");
    Image pausedFrame; r.view->readPixels(pausedFrame);
    {
        unsigned wx, wy; float d;
        const bool same = sameImage(voxelFrame, pausedFrame, &wx, &wy, &d);
        std::printf("   NEVER DARK: raster-at-paused vs voxel frame, worst delta %.5f at (%u,%u)\n", d, wx, wy);
        CHECK(same, "NEVER DARK: a raster field that has captured nothing renders the voxel "
                    "field's answer byte for byte (its atlases ARE the voxel answer)");
    }

    // ================================================================ 3
    std::printf("\n== 3: the budget fold and an exact convergence ==\n");
    GiParams raster = base();
    raster.ddgiSource = GiSource::Raster;
    CHECK(s->setGlobalIllumination(raster), "raster source at budget 64 builds");
    st = s->giStatus();
    const int probes = st.ifdProbes, perFrame = st.ifdProbesPerFrame;
    std::printf("   %d probes, %d per frame at budget 64\n", probes, perFrame);
    CHECK(perFrame == std::min(64, probes), "budget 64 buys 64 raster probes per frame (one per budget unit)");
    const int framesToConverge = perFrame ? (probes + perFrame - 1) / perFrame : 0;
    for (int f = 0; f < framesToConverge - 1; ++f) e->renderOneFrame();
    CHECK(!s->giStatus().ifdConverged, "one frame short of ceil(probes / perFrame): not converged");
    e->renderOneFrame();
    CHECK(s->giStatus().ifdConverged, "exactly ceil(probes / perFrame) frames: converged");
    render(e, 2);
    Image rasterV; r.view->readPixels(rasterV);
    show("floor probe, raster field (arm vertical)", rasterV.at(kProbeX, kProbeY));
    CHECK(lum(rasterV.at(kFarX, kFarY)) > 0.05f, "the frame is lit (the raster field is not black)");

    // ================================================================ 4
    std::printf("\n== 4: the rig — bounce follows the pose under raster, not under voxel ==\n");
    // Swing to horizontal. The clip time moving bumps the rig epoch; a converged
    // raster field re-arms on it and re-captures under the budget.
    pose(s, r.arm, 1.0f);
    e->renderOneFrame();
    st = s->giStatus();
    CHECK(!st.ifdConverged, "posing the rig RE-ARMS the converged raster field");
    for (int f = 0; f < framesToConverge + 2; ++f) e->renderOneFrame();
    CHECK(s->giStatus().ifdConverged, "and it re-converges under the budget");
    Image rasterH; r.view->readPixels(rasterH);
    dumpFrame(rasterV, "raster_vertical");
    dumpFrame(rasterH, "raster_horizontal");
    show("floor probe, raster field (arm horizontal)", rasterH.at(kProbeX, kProbeY));
    std::printf("   floor row y=76, red chroma x=20..80 step 10, raster vertical / horizontal:\n     ");
    for (unsigned x = 20; x <= 80; x += 10) {
        const Colour a = rasterV.at(x, 76), b = rasterH.at(x, 76);
        std::printf("x%u: %.3f/%.3f  ", x, a.r - 0.5f * (a.g + a.b), b.r - 0.5f * (b.g + b.b));
    }
    std::printf("\n");
    const Colour pv = rasterV.at(kProbeX, kProbeY), ph = rasterH.at(kProbeX, kProbeY);
    const float redV = pv.r - 0.5f * (pv.g + pv.b), redH = ph.r - 0.5f * (ph.g + ph.b);
    std::printf("   red bounce on the floor under the arm: vertical %.4f -> horizontal %.4f\n", redV, redH);
    // The pixel is in the arm's shadow zone too, so assert on the CHROMA (red
    // bounce) rather than brightness: the arm lying over it bounces red down.
    CHECK(redH > redV + 0.01f, "RASTER: the floor under the horizontal arm receives its red bounce");
    // 4b. THE RIG RECEIVES FIELD DIFFUSE: the GREY twin's front face (pinned
    //     at kGreyX/kGreyY in the horizontal pose from the dumped frame)
    //     changes when the field's diffuse is switched off (ddgiIntensity 0
    //     keeps the field bound and contributing nothing — the A/B the
    //     intensity knob exists for), so the skinned mesh is lit BY the field,
    //     not just captured into it.
    {
        GiParams noField = raster; noField.ddgiIntensity = 0.0f;
        CHECK(s->setGlobalIllumination(noField), "raster field bound at intensity 0 (arm horizontal)");
        for (int f = 0; f < framesToConverge + 2; ++f) e->renderOneFrame();
        Image dark; r.view->readPixels(dark);
        const Colour armLit = rasterH.at(kGreyX, kGreyY), armDark = dark.at(kGreyX, kGreyY);
        show("grey arm front face, field intensity 1", armLit);
        show("grey arm front face, field intensity 0", armDark);
        std::printf("   grey arm row y=%u, lum x=10..90 step 10, intensity 1 / 0:\n     ", kGreyY);
        for (unsigned x = 10; x <= 90; x += 10)
            std::printf("x%u: %.3f/%.3f  ", x, lum(rasterH.at(x, kGreyY)), lum(dark.at(x, kGreyY)));
        std::printf("\n");
        CHECK(std::fabs(lum(armLit) - lum(armDark)) * 255.0f >= 1.0f,
              "THE RIG RECEIVES FIELD DIFFUSE: the skinned arm's own face moves with the field's intensity");
    }
    // Control: the same pose rebuilt is the same picture.
    CHECK(s->setGlobalIllumination(raster), "rebuild at the horizontal pose");
    for (int f = 0; f < framesToConverge + 2; ++f) e->renderOneFrame();
    Image rasterH2; r.view->readPixels(rasterH2);
    {
        unsigned wx, wy; float d;
        sameImage(rasterH, rasterH2, &wx, &wy, &d);
        std::printf("   control: rebuild at the same pose, worst delta %.5f (%.2f/255)\n", d, d * 255.0f);
        CHECK(d * 255.0f <= 1.0f, "CONTROL: a rebuild at the same pose is stable to 1/255");
    }
    // The voxel field cannot follow: it baked the bind pose.
    GiParams voxelH = base();
    CHECK(s->setGlobalIllumination(voxelH), "voxel-fed field at the horizontal pose");
    render(e, 6);
    Image voxelH1; r.view->readPixels(voxelH1);
    pose(s, r.arm, 0.0f);
    render(e, 6);
    Image voxelV1; r.view->readPixels(voxelV1);
    dumpFrame(voxelH1, "voxel_horizontal");
    const Colour qv = voxelV1.at(kProbeX, kProbeY), qh = voxelH1.at(kProbeX, kProbeY);
    const float vRedV = qv.r - 0.5f * (qv.g + qv.b), vRedH = qh.r - 0.5f * (qh.g + qh.b);
    std::printf("   voxel field, same pixel: vertical %.4f, horizontal %.4f (the voxels never saw the pose)\n",
                vRedV, vRedH);
    // Not a fence on the direct-light shadow (which DOES move) — a statement
    // about the FIELD: the raster field's pose gain must beat the voxel one's.
    CHECK((redH - redV) > (vRedH - vRedV) + 0.005f,
          "the raster field's pose-driven red bounce exceeds the voxel field's (which is static)");

    // ================================================================ 5
    std::printf("\n== 5: the capture mask — helpers out, geometry in ==\n");
    pose(s, r.arm, 0.0f);
    CHECK(s->setGlobalIllumination(raster), "raster field, arm vertical, for the mask case");
    for (int f = 0; f < framesToConverge + 2; ++f) e->renderOneFrame();
    Image noCube; r.view->readPixels(noCube);
    // A bright emissive slab hung 1.5 above the probe: as a HELPER it must be
    // invisible to the probes; as geometry it must light the floor.
    const NodeId lamp = s->createNode();
    {
        const MeshId mesh = s->createMesh(enginetest::unitCubeMesh());
        PbrParams p; p.albedo = Colour(1, 1, 1); p.emissive = Colour(8.0f, 8.0f, 8.0f); p.roughness = 1.0f;
        const MaterialId mat = s->createPbrMaterial(p);
        CHECK(mesh && mat && s->attachMesh(lamp, mesh, mat), "the emissive slab attaches");
        s->setNodeTransform(lamp, Vec3(-2.2f, 1.5f, 0.0f), Quat(), Vec3(2.0f, 0.1f, 2.0f));
    }
    s->setNodeHelper(lamp, true);
    CHECK(s->setGlobalIllumination(raster), "rebuild with the slab as a HELPER");
    for (int f = 0; f < framesToConverge + 2; ++f) e->renderOneFrame();
    Image helperCube; r.view->readPixels(helperCube);
    s->setNodeHelper(lamp, false);
    CHECK(s->setGlobalIllumination(raster), "rebuild with the slab as GEOMETRY");
    for (int f = 0; f < framesToConverge + 2; ++f) e->renderOneFrame();
    Image realCube; r.view->readPixels(realCube);
    // Compare the floor probe only: the slab itself is drawn by the main view
    // in both cases (helpers are visible in the viewport), so whole-frame
    // identity is not the claim — the PROBES' view of it is.
    const float lumNo = lum(noCube.at(kProbeX, kProbeY)), lumHelper = lum(helperCube.at(kProbeX, kProbeY)),
                lumReal = lum(realCube.at(kProbeX, kProbeY));
    std::printf("   floor probe: no slab %.4f | helper slab %.4f | geometry slab %.4f\n", lumNo, lumHelper, lumReal);
    CHECK(std::fabs(lumHelper - lumNo) * 255.0f <= 1.0f,
          "MASK: a helper (kHelperBit, no kVisibleBit) over the probe changes the raster field by nothing");
    CHECK(lumReal > lumNo + 0.02f,
          "CONTROL: the same slab as geometry lights the floor through the probes");
    s->setNodeHelper(lamp, true);   // out of the way for case 6 (helpers are not captured)

    // ================================================================ 6
    std::printf("\n== 6: the sky goes into the probes; the ambient proxy stands down ==\n");
    const unsigned char bluePx[4] = { 20, 60, 255, 255 };
    const TextureId skyTex = s->createTexture(1, 1, bluePx, true);
    CHECK(skyTex != 0, "a 1x1 blue sky texture");
    SkyDesc blueSky; blueSky.mode = SkyMode::Equirectangular; blueSky.equirect = skyTex;
    CHECK(s->setSky(blueSky), "the blue equirect sky binds");
    // Ambient ON now: with a sky bound the proxy must be inert (the probes
    // carry the sky), so ddgiAmbient 1 and 0 must render identically.
    s->setAmbient(Colour(0.4f, 0.4f, 0.44f), Colour(0.1f, 0.1f, 0.12f));
    GiParams skyOn = raster; skyOn.ddgiAmbient = 1.0f;
    CHECK(s->setGlobalIllumination(skyOn), "raster field under the sky, ambient proxy 1");
    for (int f = 0; f < framesToConverge + 2; ++f) e->renderOneFrame();
    Image sky1; r.view->readPixels(sky1);
    GiParams skyOff = raster; skyOff.ddgiAmbient = 0.0f;
    CHECK(s->setGlobalIllumination(skyOff), "raster field under the sky, ambient proxy 0");
    for (int f = 0; f < framesToConverge + 2; ++f) e->renderOneFrame();
    Image sky0; r.view->readPixels(sky0);
    {
        unsigned wx, wy; float d;
        sameImage(sky1, sky0, &wx, &wy, &d);
        std::printf("   sky bound: ambient proxy 1 vs 0, worst delta %.5f (%.2f/255)\n", d, d * 255.0f);
        CHECK(d * 255.0f <= 1.0f, "with a sky bound the ambient proxy is INERT on a raster field (no double count)");
    }
    const Colour under = sky0.at(kFarX, kFarY), before = noCube.at(kFarX, kFarY);
    show("open floor, no sky (raster)", before);
    show("open floor, blue sky (raster)", under);
    CHECK(under.b - under.r > (before.b - before.r) + 0.02f,
          "SKY: the raster probes carry the sky — the open floor goes bluer under a blue sky");

    r.view->setScene(nullptr);
    e->destroyScene(s);
    e->destroyView(r.view);

    // ================================================================ 7
    std::printf("\n== 7: the raster escape calibration (kIfdRasterEscapeScale) ==\n");
    // gi.ddgi_ambient's open scene, rebuilt fresh: an ambient PAIR set before
    // anything else, a big floor, one wall, NO lights, no sky. The open floor's
    // ambient through the raster field's depth proxy must match the voxel
    // field's (S1 calibrated the voxel path to 100% of the cone reference);
    // this prints the ratio the constant is pinned from and asserts it within
    // 10% — the same tolerance gi.ddgi_ambient uses. (The rig scene above is
    // not reused: re-issuing an ambient pair after a sky has been bound and
    // removed left its floor black on both fields — recorded in the build
    // record as a state-order finding, not chased here.)
    {
        View *cv = e->createOffscreenView("ddgiraster_cal", 128, 128, Colour(0, 0, 0));
        Scene *cs = e->createScene("ddgiraster_cal");
        cv->setScene(cs);
        cs->setAmbient(Colour(0.40f, 0.40f, 0.44f), Colour(0.10f, 0.10f, 0.12f));
        addSlab(cs, Colour(0.9f, 0.9f, 0.9f), Vec3(0.0f, -0.05f, 0.0f), Vec3(16.0f, 0.1f, 16.0f));
        addSlab(cs, Colour(0.9f, 0.9f, 0.9f), Vec3(0.0f, 2.0f, -2.0f), Vec3(12.0f, 4.0f, 0.4f));
        enginetest::testCameraLookAt(cv, Vec3(0.0f, 3.0f, 7.0f), Vec3(0.0f, 0.0f, -1.0f));
        const unsigned ox = 64, oy = 118;     // gi.ddgi_ambient's open-floor probe
        GiParams calV;
        calV.mode = GiMode::Vct; calV.quality = GiQuality::Medium; calV.numBounces = 2;
        calV.boundsMin = Vec3(-9.0f, -1.5f, -9.0f); calV.boundsMax = Vec3(9.0f, 7.5f, 9.0f);
        calV.ddgi = GiToggle::On; calV.ddgiAmbient = 1.0f; calV.updateBudget = 64;
        CHECK(cs->setGlobalIllumination(calV), "calibration: voxel field, ambient proxy on");
        render(e, 6);
        Image calVoxel; cv->readPixels(calVoxel);
        GiParams calR = calV; calR.ddgiSource = GiSource::Raster;
        CHECK(cs->setGlobalIllumination(calR), "calibration: raster field, ambient proxy on");
        const GiStatus cst = cs->giStatus();
        const int cFrames = cst.ifdProbesPerFrame ? (cst.ifdProbes + cst.ifdProbesPerFrame - 1) / cst.ifdProbesPerFrame : 0;
        for (int f = 0; f < cFrames + 2; ++f) e->renderOneFrame();
        CHECK(cs->giStatus().ifdSource == GiSource::Raster && cs->giStatus().ifdConverged,
              "calibration: the raster field converged");
        Image calRaster; cv->readPixels(calRaster);
        const Colour pv = calVoxel.at(ox, oy), pr = calRaster.at(ox, oy);
        show("open floor, voxel field + proxy", pv);
        show("open floor, raster field + proxy", pr);
        const float calRatio = lum(pv) > 1e-4f ? lum(pr) / lum(pv) : 0.0f;
        std::printf("   CALIBRATION: raster/voxel open-floor ambient = %.3f (kIfdRasterEscapeScale pins this to ~1)\n",
                    calRatio);
        CHECK(lum(pv) > 0.05f, "the voxel reference is lit by its ambient (the calibration is not vacuous)");
        CHECK(calRatio > 0.90f && calRatio < 1.10f,
              "CALIBRATION: the raster field's ambient proxy lands within 10% of the voxel field's");
        cv->setScene(nullptr);
        e->destroyScene(cs);
        e->destroyView(cv);
    }

    engine.reset();
    std::printf(failures ? "\n%d FAILURES\n" : "\nall ok\n", failures);
    return failures ? 1 : 0;
}
