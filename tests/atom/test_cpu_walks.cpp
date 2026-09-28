// ATOM-CPU-WALKS-1 — the per-frame CPU walks moved onto the change feed and the device.
//
// `--tlas-compute` (engine.tlas_compute): THE TOP-LEVEL INSTANCES ARE WRITTEN ON THE
// DEVICE (the instance job rq_tlas_write.comp, one thread per GPU scene slot, two instances a
// slot at fixed places, an untraced slot's pair inactive). The array is read back
// (OgreScene::readTlasInstances) and its ACTIVE entries, in order, are held BYTE FOR
// BYTE against a REFERENCE WRITER kept in this suite — the product's CPU writer,
// deleted once this suite proved the two equal — run over the same mirror and the
// same bottom-level structures, with the far copies' hand-over width, the counts and
// the per-slot rows.
// WHAT THE EQUALITY PROVES, AND WHAT IT DOES NOT: the BLAS and skin-structure
// ADDRESSES the reference writes come from the tier's own inputs (TlasReadback's
// structures and skins — the same host words the job reads), so on the reference
// field they prove only that the job reproduces those words. The INDEPENDENT arms
// are the ones the reference derives itself from the mirror and the scene: the mask
// (the flags word), the custom index (the slot), the transform (the world rows), the
// per-slot row, the near level (the scene's own ray-level array, not the table's
// ids.w — the in-place patch's proof) and which slots are active at all. Over:
//   (a) a mixed set — static casters, movers, non-casters, a hidden item and a
//       cut-out (out of the traced set) and a blended material (traced: every BLAS
//       is opaque and only an alpha test leaves the set), LOD chains whose near
//       level the ray rule moves with the camera and whose far copy is the coarsest,
//       scaled instances (the far width grows with the scale), two rigged columns
//       (their own skinned structures, posed);
//   (b) the same set after removals (the swap-remove renumbers slots), additions,
//       a mover moved, a cast-shadow and a visibility toggle, a mobility change;
//   (c) an in-place edit that takes an item OUT of the traced set without anything
//       moving (a material turned cut-out): the set follows in ONE frame (the old
//       writer re-read the set only when the movement epoch moved);
//   (d) the lattice: 20 x 20 x 20 cubes, one material each, one of them moved.
//
// `--words-still` (atom.words_still): THE WORDS WALK IS CHANGE-DRIVEN. The split's
// word set and the ray tier's traced set are kept by the GPU scene's change feed and an
// in-place material edit reaches them through the PBS change log (ScenePbs), so:
//   (a) a STILL frame visits no slot (the split's feed, the ray tier's feed) and
//       re-hashes nothing (the PBS log's drain handles no note);
//   (b) a mover frame visits the mover's slot and nothing else;
//   (c) an in-place blend edit on a material N items wear re-routes all N in ONE
//       frame (the table's Atom bit clears), the screen draws still equal the
//       buckets, and the edit back restores both;
//   (d) a same-slot texture swap moves the material's bucket (a new decode draw),
//       with nothing else edited.
// FRAMES, NEVER TIME.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include "EnginePrivate.h"
#include "GpuScene.h"

#include <OgreMesh2.h>
#include <OgreSubMesh2.h>
#include <Vao/OgreVertexArrayObject.h>

#include <algorithm>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace jahshaka::engine;
using jahshaka::engine::detail::OgreScene;
using jahshaka::engine::detail::TlasReadback;

static int failures = 0;
#define CHECK_MSG(cond, ...)                                                    \
    do {                                                                        \
        std::printf((cond) ? "ok: " : "FAIL: ");                                 \
        std::printf(__VA_ARGS__);                                                \
        std::printf("\n");                                                       \
        if (!(cond)) ++failures;                                                 \
    } while (0)

static void render(Engine *e, int n) { for (int i = 0; i < n; ++i) e->renderOneFrame(); }

static std::unique_ptr<Engine> boot(const char *log)
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = log;
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return nullptr; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    return engine;
}

/// The view FIRST, then its scene (Ogre's startup order: a render target must exist
/// before the first scene manager).
static View *rayView(Engine *e, Scene *&s, const char *name)
{
    View *view = e->createOffscreenView(name, 640, 360, Colour(0, 0, 0));
    view->setOffscreenContract(OffscreenContract::StillPicture);
    s = e->createScene(name);
    view->setScene(s);
    PostFxDesc fx;
    fx.allowOffscreen = true;
    fx.ssr = 2;   // the traced reflection: the ray tier runs for this scene
    view->setPostFx(fx);
    s->setAmbient(Colour(0.2f, 0.2f, 0.22f), Colour(0.1f, 0.1f, 0.1f));
    enginetest::addDirectionalLight(s, Vec3(-0.3f, -1.0f, 0.25f), 3.0f);
    return view;
}

// ---------------------------------------------------------------------------
// FIXTURES (gi.far_blas' LOD cubes, gi.skin_rays' column rig)
static std::vector<unsigned> withoutPlusX(const MeshData &d)
{
    std::vector<unsigned> out;
    for (size_t t = 0; t + 2 < d.indices.size(); t += 3) {
        bool plusX = true;
        for (int k = 0; k < 3; ++k)
            if (d.positions[size_t(d.indices[t + size_t(k)]) * 3u] < 0.49f) plusX = false;
        if (plusX) continue;
        out.insert(out.end(), { d.indices[t], d.indices[t + 1], d.indices[t + 2] });
    }
    return out;
}
/// Three levels: level 1 the cube minus +X at a tiny bound, level 2 the whole cube
/// at half a metre (the coarsest: the far copy, and the far width's bound).
static MeshData splitCube()
{
    MeshData d = enginetest::unitCubeMesh();
    d.lodIndices.push_back(withoutPlusX(d));
    d.lodBounds.push_back(1e-7f);
    d.lodIndices.push_back(d.indices);
    d.lodBounds.push_back(0.5f);
    return d;
}
/// Two levels, level 1's bound `bound` (large keeps the near copy at level 0 up
/// close; the camera walking away moves it).
static MeshData chainedCube(float bound)
{
    MeshData d = enginetest::unitCubeMesh();
    d.lodIndices.push_back(withoutPlusX(d));
    d.lodBounds.push_back(bound);
    return d;
}

static void addBox(MeshData &d, float x0, float x1, float y0, float y1, float z0, float z1, unsigned char bone)
{
    auto quad = [&](const float p[4][3], const float n[3]) {
        const unsigned base = unsigned(d.positions.size() / 3u);
        for (int k = 0; k < 4; ++k) {
            d.positions.insert(d.positions.end(), { p[k][0], p[k][1], p[k][2] });
            d.normals.insert(d.normals.end(), { n[0], n[1], n[2] });
            d.uvs.insert(d.uvs.end(), { float(k & 1), float(k >> 1) });
            d.blendIndices.insert(d.blendIndices.end(), { bone, 0, 0, 0 });
            d.blendWeights.insert(d.blendWeights.end(), { 1.0f, 0.0f, 0.0f, 0.0f });
        }
        d.indices.insert(d.indices.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
    };
    const float fz[4][3] = { { x0, y0, z1 }, { x1, y0, z1 }, { x1, y1, z1 }, { x0, y1, z1 } };
    const float bz[4][3] = { { x1, y0, z0 }, { x0, y0, z0 }, { x0, y1, z0 }, { x1, y1, z0 } };
    const float px[4][3] = { { x1, y0, z1 }, { x1, y0, z0 }, { x1, y1, z0 }, { x1, y1, z1 } };
    const float nx[4][3] = { { x0, y0, z0 }, { x0, y0, z1 }, { x0, y1, z1 }, { x0, y1, z0 } };
    const float py[4][3] = { { x0, y1, z1 }, { x1, y1, z1 }, { x1, y1, z0 }, { x0, y1, z0 } };
    const float ny[4][3] = { { x0, y0, z0 }, { x1, y0, z0 }, { x1, y0, z1 }, { x0, y0, z1 } };
    const float nFz[3] = { 0, 0, 1 }, nBz[3] = { 0, 0, -1 }, nPx[3] = { 1, 0, 0 }, nNx[3] = { -1, 0, 0 },
                nPy[3] = { 0, 1, 0 }, nNy[3] = { 0, -1, 0 };
    quad(fz, nFz); quad(bz, nBz); quad(px, nPx); quad(nx, nNx); quad(py, nPy); quad(ny, nNy);
}
static MeshData columnMesh()
{
    MeshData d;
    addBox(d, -0.2f, 0.2f, 0.0f, 1.0f, -0.2f, 0.2f, 0);
    addBox(d, -0.2f, 0.2f, 1.0f, 2.0f, -0.2f, 0.2f, 1);
    return d;
}
static SkeletonDesc columnRig()
{
    SkeletonDesc rig;
    rig.id = "atom.cpu_walks column rig v1";
    BoneDesc root;
    root.name = "root";
    rig.bones.push_back(root);
    BoneDesc tip;
    tip.name = "tip";
    tip.parent = 0;
    tip.bindPosition = Vec3(0.0f, 1.0f, 0.0f);
    rig.bones.push_back(tip);
    return rig;
}
static ClipDesc bendClip(const SkeletonDesc &rig)
{
    ClipDesc c;
    c.id = rig.id + " / bend";
    c.name = "Bend";
    c.length = 1.0f;
    BoneTrack t;
    t.bone = 1;
    for (int i = 0; i <= 8; ++i) {
        BoneKey k;
        k.time = float(i) / 8.0f;
        k.position = Vec3(0.0f, 1.0f, 0.0f);
        const float a = -1.5707963f * k.time;
        k.rotation = Quat(0.0f, 0.0f, std::sin(0.5f * a), std::cos(0.5f * a));
        t.keys.push_back(k);
    }
    c.tracks.push_back(t);
    return c;
}
static bool pose(Scene *s, NodeId n, float t)
{
    ClipState st;
    st.name = "Bend";
    st.enabled = true;
    st.time = t;
    st.weight = 1.0f;
    st.looping = false;
    return s->setClipStates(n, &st, 1);
}

static NodeId place(Scene *s, MeshId mesh, MaterialId mat, const Vec3 &pos, const Vec3 &scale = Vec3(1, 1, 1))
{
    const NodeId n = s->createNode();
    if (!n || !s->attachMesh(n, mesh, mat)) return 0;
    s->setNodeTransform(n, pos, Quat(), scale);
    return n;
}

/// VkAccelerationStructureInstanceKHR's 64 bytes, as the reference writes them.
struct RefInstance {
    float transform[12];
    uint32_t indexMask;    ///< instanceCustomIndex : 24 | mask : 8
    uint32_t sbtFlags;     ///< shader binding table offset : 24 | flags : 8
    uint64_t reference;
};
static_assert(sizeof(RefInstance) == 64, "VkAccelerationStructureInstanceKHR is 64 bytes");

/// THE REFERENCE WRITER — the CPU instance writer the product deleted, kept HERE:
/// every traced slot of the GPU scene's MIRROR, in slot order, a near copy over the
/// RAY RULE's level (read from the scene's own CPU array, not the table: the table's
/// ids.w is what the device reads, so this is also the patch's proof) and a far copy
/// over the coarsest level; a rigged slot through its ready skin structure or not at
/// all; the far copies' width the coarsest bound times the largest axis scale.
static void referenceInstances(OgreScene *os, const TlasReadback &rb, std::vector<RefInstance> &out,
                               std::vector<uint32_t> &rows, float &overlap, unsigned &far)
{
    namespace d = jahshaka::engine::detail;
    const d::GpuScene &gs = os->gpuScene();
    out.clear();
    overlap = 0.0f;
    far = 0u;
    rows.assign(rb.slots, d::GpuScene::kNoGeomRow);
    auto address = [&](const void *mesh, uint32_t level) {
        for (const TlasReadback::Structure &st : rb.structures)
            if (st.mesh == mesh && st.level == level) return st.address;
        return uint64_t(0);
    };
    auto push = [&](const float *world, uint32_t slot, uint32_t mask, uint64_t ref) {
        RefInstance r{};
        std::memcpy(r.transform, world, sizeof(r.transform));
        r.indexMask = (slot & 0xFFFFFFu) | ((mask & 0xFFu) << 24u);
        r.sbtFlags = 1u << 24u;   // VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR
        r.reference = ref;
        out.push_back(r);
    };
    for (uint32_t i = 0; i < rb.slots && i < gs.slotCount(); ++i) {
        const d::GpuInstance &e = gs.entry(i);
        uint32_t flags = 0u, meshIndex = 0u;
        std::memcpy(&flags, &e.boundsMax[3], sizeof(flags));
        std::memcpy(&meshIndex, &e.boundsMin[3], sizeof(meshIndex));
        if (!(flags & d::kGpuRayTraced)) continue;
        const Ogre::MeshPtr &mesh = gs.meshAt(meshIndex);
        if (!mesh) continue;
        uint32_t mask = kRayMaskNear;
        mask |= (flags & d::kGpuCaster) ? kRayMaskCaster : 0u;
        mask |= (flags & d::kGpuMover) ? kRayMaskMover : kRayMaskStill;
        if ((flags & d::kGpuMover) && (flags & d::kGpuCaster)) mask |= kRayMaskMoverCaster;
        if (!(flags & d::kGpuMover) && (flags & d::kGpuCaster)) mask |= kRayMaskStillCaster;
        if (flags & d::kGpuSkinned) {
            const TlasReadback::Skin *sk = nullptr;
            for (const TlasReadback::Skin &k : rb.skins)
                if (k.node == e.ids[0]) sk = &k;
            if (!sk) continue;
            push(e.world, i, mask, sk->address);
            push(e.world, i, kRayMaskFar, sk->address);
            rows[i] = sk->row;
            ++far;
            continue;
        }
        const size_t vaos = mesh->getNumSubMeshes() ? mesh->getSubMesh(0)->mVao[Ogre::VpNormal].size() : 0u;
        const uint32_t coarsest = vaos > 1u ? uint32_t(vaos - 1u) : 0u;
        const uint32_t nearLevel = std::min(os->rayLevelOf(i), coarsest);
        push(e.world, i, mask, address(mesh.get(), nearLevel));
        push(e.world, i, kRayMaskFar, address(mesh.get(), coarsest));
        ++far;
        if (nearLevel < d::GpuScene::kLevelsPerMesh) rows[i] = d::GpuScene::geomRowIndex(meshIndex, nearLevel, 0u);
        if (coarsest > 0u) {
            const std::vector<float> *b = os->lodBoundsFor(mesh.get());
            const float bound = (b && !b->empty()) ? b->back() : 0.0f;
            if (bound > 0.0f) {
                const float grown = bound * worldMaxAxisScale(e.world);
                if (std::isfinite(grown) && grown > overlap) overlap = grown;
            }
        }
    }
}

/// One verification: every active device instance equal to the reference's.
static void verify(Scene *s, const char *what, unsigned expectTraced = ~0u)
{
    auto *os = static_cast<OgreScene *>(s);
    TlasReadback rb;
    std::string err;
    const bool ok = os->readTlasInstances(rb, err);
    std::vector<RefInstance> dev;
    for (size_t at = 0; at + sizeof(RefInstance) <= rb.instances.size(); at += sizeof(RefInstance)) {
        RefInstance r;
        std::memcpy(&r, rb.instances.data() + at, sizeof(r));
        if (r.reference) dev.push_back(r);
    }
    std::vector<RefInstance> ref;
    std::vector<uint32_t> rows;
    float overlap = 0.0f;
    unsigned far = 0u;
    referenceInstances(os, rb, ref, rows, overlap, far);
    unsigned missing = 0u, mismatched = 0u;
    int first = -1;
    for (const RefInstance &r : ref)
        if (!r.reference) ++missing;
    for (size_t i = 0; i < std::min(dev.size(), ref.size()); ++i)
        if (std::memcmp(&dev[i], &ref[i], sizeof(RefInstance)) != 0) {
            if (first < 0) first = int(i);
            ++mismatched;
        }
    const RayQueryStatus rq = s->rayQueryStatus();
    std::printf("  [%s] slots %u, device active %zu, reference %zu, mismatched %u (first %d), overlap %.6f / %.6f, "
                "missing BLAS %u, traced %d, far %d, skinned %d\n",
                what, rb.slots, dev.size(), ref.size(), mismatched, first, rb.farOverlap, overlap, missing,
                rq.instances, rq.farInstances, rq.skinnedInstances);
    CHECK_MSG(ok, "%s: the instance array read back (%s)", what, err.c_str());
    CHECK_MSG(!ref.empty() && dev.size() == ref.size() && mismatched == 0 && missing == 0,
              "%s: the device's %zu active instances equal the reference writer's %zu byte for byte", what,
              dev.size(), ref.size());
    CHECK_MSG(rb.instanceCount == ref.size() && rb.farInstanceCount == far && rb.rows == rows,
              "%s: the counts (%u / %u) and the per-slot rows agree", what, rb.instanceCount, rb.farInstanceCount);
    CHECK_MSG(rb.farOverlap == overlap, "%s: the far copies' hand-over width is the reference's (%.6f)", what,
              overlap);
    if (expectTraced != ~0u)
        CHECK_MSG(unsigned(rq.instances) == expectTraced, "%s: %u traced objects (expected %u)", what,
                  unsigned(rq.instances), expectTraced);
}

// ---------------------------------------------------------------------------
static int tlasMain()
{
    std::printf("== engine.tlas_compute: the device-written instance array against the CPU writer, byte for byte\n");
    auto engine = boot("test-tlas-compute-ogre.log");
    if (!engine) return 1;
    Engine *e = engine.get();
    Scene *s = nullptr;
    View *view = rayView(e, s, "tlas");
    enginetest::testCameraLookAt(view, Vec3(0.0f, 4.0f, 14.0f), Vec3(0.0f, 0.5f, 0.0f));

    PbrParams p;
    p.albedo = Colour(0.6f, 0.55f, 0.5f);
    const MaterialId plain = s->createPbrMaterial(p);
    PbrParams cut = p;
    cut.alphaMode = PbrAlphaMode::Cutout;
    const MaterialId cutout = s->createPbrMaterial(cut);
    PbrParams glass = p;
    glass.alphaMode = PbrAlphaMode::Blend;
    glass.alpha = 0.5f;
    const MaterialId blended = s->createPbrMaterial(glass);
    const MaterialId editable = s->createPbrMaterial(p);

    const MeshId cube = s->createMesh(enginetest::unitCubeMesh());
    const MeshId split = s->createMesh(splitCube());
    const MeshId chainNear = s->createMesh(chainedCube(0.02f));
    const MeshId chainFar = s->createMesh(chainedCube(0.3f));
    std::vector<NodeId> nodes;
    const NodeId floor = place(s, cube, plain, Vec3(0, -0.5f, 0), Vec3(40, 1, 40));
    nodes.push_back(floor);
    for (int i = 0; i < 20; ++i)
        nodes.push_back(place(s, cube, plain, Vec3(-9.0f + float(i), 0.5f, -3.0f)));
    std::vector<NodeId> movers;
    for (int i = 0; i < 5; ++i) {
        const NodeId n = s->createNode();
        s->setNodeMovable(n, true);
        s->attachMesh(n, cube, plain);
        s->setNodeTransform(n, Vec3(-4.0f + 2.0f * float(i), 0.5f, 1.0f), Quat(), Vec3(1, 1, 1));
        movers.push_back(n);
        nodes.push_back(n);
    }
    for (int i = 0; i < 3; ++i) {
        const NodeId n = place(s, cube, plain, Vec3(-2.0f + 2.0f * float(i), 0.5f, 3.0f));
        s->setNodeCastShadow(n, false);
        nodes.push_back(n);
    }
    const NodeId hidden = place(s, cube, plain, Vec3(6, 0.5f, 3));
    s->setNodeVisible(hidden, false);
    const NodeId cutNode = place(s, cube, cutout, Vec3(7, 0.5f, 3));
    const NodeId glassNode = place(s, cube, blended, Vec3(8, 0.5f, 3));
    const NodeId editNode = place(s, cube, editable, Vec3(9, 0.5f, 3));
    (void)cutNode; (void)glassNode;
    // LOD chains near and far, some SCALED (the far copy's width grows with scale).
    std::vector<NodeId> lods;
    for (int i = 0; i < 6; ++i) {
        const float sc = 1.0f + 0.5f * float(i % 3);
        lods.push_back(place(s, i % 2 ? split : chainNear, plain, Vec3(-6.0f + 2.5f * float(i), 0.5f * sc, -8.0f),
                             Vec3(sc, sc, sc)));
        lods.push_back(place(s, chainFar, plain, Vec3(-6.0f + 2.5f * float(i), 0.5f, -60.0f - 20.0f * float(i))));
    }
    // TWO RIGGED COLUMNS, posed.
    const SkeletonDesc rig = columnRig();
    const MeshId colMesh = s->createMesh(columnMesh());
    const ClipDesc clip = bendClip(rig);
    std::vector<NodeId> chars;
    for (int i = 0; i < 2; ++i) {
        const NodeId n = s->createNode();
        const bool attached = s->attachSkinnedMesh(n, colMesh, plain, rig) && s->attachClips(n, &clip, 1);
        CHECK_MSG(attached, "rigged column %d attached", i);
        enginetest::setNodePosition(s, n, Vec3(3.0f + 1.5f * float(i), 0.0f, 5.0f));
        pose(s, n, 0.5f + 0.5f * float(i));
        chars.push_back(n);
    }
    render(e, 20);
    const RayQueryStatus rq0 = s->rayQueryStatus();
    CHECK_MSG(rq0.enabled && rq0.tlasJobDispatches > 0, "the ray tier is live and wrote its instances on the device "
              "(%llu dispatches)", (unsigned long long)rq0.tlasJobDispatches);
    // Traced: floor + 20 + 5 movers + 3 non-casters + the edit target + 12 LOD + 2 rigged
    // (the hidden, the cut-out and the blended one are out... the blended one is
    // TRACED: only a cut-out leaves, the opaque BLAS carries glass — see the writer).
    // How many slots the RAY RULE put above level 0 — the camera walk below must move it.
    auto coarseSlots = [&](Scene *sc) {
        auto *os = static_cast<OgreScene *>(sc);
        unsigned n = 0u;
        for (uint32_t i = 0; i < os->gpuScene().slotCount(); ++i) n += os->rayLevelOf(i) > 0u ? 1u : 0u;
        return n;
    };
    verify(s, "(a) the mixed set");
    const unsigned tracedA = unsigned(s->rayQueryStatus().instances);
    const unsigned coarseA = coarseSlots(s);

    // (a2) THE CAMERA WALKS: the ray rule moves the near levels (patched in place).
    enginetest::testCameraLookAt(view, Vec3(0.0f, 30.0f, 160.0f), Vec3(0.0f, 0.5f, -40.0f));
    render(e, 3);
    verify(s, "(a2) the camera far away: new near levels");
    const unsigned coarseFar = coarseSlots(s);
    enginetest::testCameraLookAt(view, Vec3(0.0f, 2.0f, -5.0f), Vec3(0.0f, 0.5f, -8.0f));
    render(e, 3);
    verify(s, "(a3) the camera close to the chains");
    const unsigned coarseNear = coarseSlots(s);
    CHECK_MSG(coarseFar > coarseNear, "(a2/a3) the camera walk moved the ray rule's levels (slots above level 0: "
              "%u at the start, %u far away, %u close)", coarseA, coarseFar, coarseNear);

    // (b) REMOVALS (swap-remove), ADDITIONS, a mover, toggles.
    s->removeNode(nodes[3]);
    s->removeNode(nodes[10]);
    s->removeNode(lods[4]);
    for (int i = 0; i < 4; ++i) place(s, split, plain, Vec3(10.0f + float(i), 0.5f, -2.0f), Vec3(2, 2, 2));
    s->setNodeTransform(movers[2], Vec3(0.0f, 1.5f, 2.0f), Quat(0, 0.3826834f, 0, 0.9238795f), Vec3(1, 2, 1));
    s->setNodeCastShadow(nodes[5], false);
    s->setNodeVisible(nodes[6], false);
    s->setNodeMovable(nodes[7], true);
    pose(s, chars[0], 1.0f);
    render(e, 3);
    verify(s, "(b) removals, additions, a mover, toggles, a pose");
    s->removeNode(chars[1]);
    render(e, 3);
    verify(s, "(b2) a rigged column removed");

    // (c) AN IN-PLACE EDIT, NOTHING MOVES: the edit target turns cut-out and leaves the
    // traced set in one frame, then comes back.
    const unsigned before = unsigned(s->rayQueryStatus().instances);
    s->setPbrMaterial(editable, cut);
    render(e, 1);
    verify(s, "(c) an in-place cut-out edit", before - 1u);
    s->setPbrMaterial(editable, p);
    render(e, 1);
    verify(s, "(c2) the edit back", before);
    (void)editNode; (void)tracedA; (void)floor; (void)hidden;

    // (d) THE LATTICE, in a scene of its own: 8,000 cubes, one material each.
    view->setEnabled(false);
    Scene *ls = nullptr;
    View *lview = rayView(e, ls, "lattice");
    const MeshId lcube = ls->createMesh(enginetest::unitCubeMesh());
    NodeId one = 0;
    for (int y = 0; y < 20; ++y)
        for (int z = 0; z < 20; ++z)
            for (int x = 0; x < 20; ++x) {
                PbrParams lp;
                lp.albedo = Colour(0.2f + 0.03f * float(x), 0.2f + 0.03f * float(y), 0.2f + 0.03f * float(z));
                const NodeId n = place(ls, lcube, ls->createPbrMaterial(lp),
                                       Vec3(-20.0f + 2.0f * float(x), 1.0f + 2.0f * float(y), -10.0f - 2.0f * float(z)),
                                       Vec3(0.5f, 0.5f, 0.5f));
                if (x == 10 && y == 3 && z == 5) one = n;
            }
    enginetest::testCameraLookAt(lview, Vec3(0.0f, 20.0f, 30.0f), Vec3(0.0f, 20.0f, -30.0f));
    render(e, 10);
    verify(ls, "(d) the lattice", 8000u);
    ls->setNodeTransform(one, Vec3(0.0f, 50.0f, 0.0f), Quat(), Vec3(3, 3, 3));
    render(e, 2);
    verify(ls, "(d2) the lattice, one cube moved", 8000u);

    std::printf("%s: engine.tlas_compute (%d failure(s))\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}

// ---------------------------------------------------------------------------
static int wordsMain()
{
    std::printf("== atom.words_still: the split's words and the ray tier's set follow the change feed; "
                "a still frame visits nothing\n");
    auto engine = boot("test-words-still-ogre.log");
    if (!engine) return 1;
    Engine *e = engine.get();
    Scene *s = nullptr;
    View *view = rayView(e, s, "words");
    enginetest::testCameraLookAt(view, Vec3(0.0f, 6.0f, 16.0f), Vec3(0.0f, 0.5f, 0.0f));

    // 200 items over 50 materials (4 each), two textures cycled over them.
    std::vector<unsigned char> texA(64 * 64 * 4), texB(64 * 64 * 4);
    for (size_t i = 0; i < texA.size(); i += 4) {
        texA[i] = 200; texA[i + 1] = 120; texA[i + 2] = 60; texA[i + 3] = 255;
        texB[i] = 60; texB[i + 1] = 140; texB[i + 2] = 210; texB[i + 3] = 255;
    }
    const TextureId tA = s->createTexture(64, 64, texA.data(), true, false);
    const TextureId tB = s->createTexture(64, 64, texB.data(), true, false);
    const TextureId tC = s->createTexture(64, 64, texB.data(), true, false);
    std::vector<MaterialId> mats;
    std::vector<PbrParams> params;
    for (int m = 0; m < 50; ++m) {
        PbrParams p;
        p.albedo = Colour(0.3f + 0.01f * float(m), 0.5f, 0.6f);
        p.roughness = 0.3f + 0.01f * float(m);
        mats.push_back(s->createPbrMaterial(p));
        params.push_back(p);
        s->setPbrTexture(mats.back(), PbrTextureSlot::Albedo, m % 2 ? tB : tA);
    }
    const MeshId cube = s->createMesh(enginetest::unitCubeMesh());
    std::vector<NodeId> items;
    for (int i = 0; i < 200; ++i)
        items.push_back(place(s, cube, mats[size_t(i % 50)],
                              Vec3(-10.0f + float(i % 20), 0.5f, -float(i / 20) * 1.5f), Vec3(0.6f, 0.6f, 0.6f)));
    place(s, cube, mats[0], Vec3(0, -0.5f, 0), Vec3(40, 1, 40));
    render(e, 60);

    auto slotOf = [&](NodeId n) {
        const GpuSceneStatus st = s->gpuSceneStatus();
        for (unsigned i = 0; i < st.slotCount; ++i) {
            GpuSceneEntry en;
            if (s->gpuSceneEntry(i, en) && en.nodeId == n) return int(i);
        }
        return -1;
    };
    auto atomBit = [&](NodeId n) {
        GpuSceneEntry en;
        const int slot = slotOf(n);
        return slot >= 0 && s->gpuSceneEntry(unsigned(slot), en) && (en.flags & detail::kGpuAtom) != 0u;
    };

    const AtomDrawStatus a0 = s->atomDrawStatus();
    const RayQueryStatus r0 = s->rayQueryStatus();
    std::printf("  settled: atom %u pbs %u materials %u buckets %u screenDraws %u | visits split %llu rays %llu "
                "| pbs notes %llu datablocks %llu\n",
                a0.atomItems, a0.pbsItems, a0.materials, a0.buckets, a0.screenDraws,
                (unsigned long long)a0.wordSlotVisits, (unsigned long long)r0.feedSlotVisits,
                (unsigned long long)a0.pbsNotes, (unsigned long long)a0.pbsDatablocks);
    CHECK_MSG(a0.on && a0.atomItems >= 200u, "the split is live and the items are Atom (%u)", a0.atomItems);
    CHECK_MSG(r0.enabled, "the ray tier is live");

    // (a) STILL FRAMES.
    render(e, 60);
    const AtomDrawStatus a1 = s->atomDrawStatus();
    const RayQueryStatus r1 = s->rayQueryStatus();
    CHECK_MSG(a1.wordSlotVisits == a0.wordSlotVisits && r1.feedSlotVisits == r0.feedSlotVisits,
              "(a) 60 still frames visit 0 slots (split +%llu, ray tier +%llu)",
              (unsigned long long)(a1.wordSlotVisits - a0.wordSlotVisits),
              (unsigned long long)(r1.feedSlotVisits - r0.feedSlotVisits));
    CHECK_MSG(a1.pbsNotes == a0.pbsNotes, "(a) ...and re-hash nothing (PBS log notes +%llu)",
              (unsigned long long)(a1.pbsNotes - a0.pbsNotes));

    // (b) A MOVER FRAME: one item moved.
    enginetest::setNodePosition(s, items[17], Vec3(3.0f, 2.0f, 4.0f));
    render(e, 1);
    const AtomDrawStatus a2 = s->atomDrawStatus();
    const RayQueryStatus r2 = s->rayQueryStatus();
    CHECK_MSG(a2.wordSlotVisits - a1.wordSlotVisits >= 1u && a2.wordSlotVisits - a1.wordSlotVisits <= 2u &&
                  r2.feedSlotVisits - r1.feedSlotVisits >= 1u && r2.feedSlotVisits - r1.feedSlotVisits <= 2u,
              "(b) a mover frame visits the mover's slot alone (split +%llu, ray tier +%llu)",
              (unsigned long long)(a2.wordSlotVisits - a1.wordSlotVisits),
              (unsigned long long)(r2.feedSlotVisits - r1.feedSlotVisits));
    render(e, 4);

    // (c) AN IN-PLACE BLEND EDIT on material 7 (worn by items 7, 57, 107, 157).
    PbrParams blend = params[7];
    blend.alphaMode = PbrAlphaMode::Blend;
    blend.alpha = 0.5f;
    s->setPbrMaterial(mats[7], blend);
    render(e, 1);
    const bool gone = !atomBit(items[7]) && !atomBit(items[57]) && !atomBit(items[107]) && !atomBit(items[157]);
    const AtomDrawStatus a3 = s->atomDrawStatus();
    CHECK_MSG(gone, "(c) one frame after the blend edit, the table routes all four wearers off Atom");
    CHECK_MSG(a3.blended == a0.blended + 4u && a3.screenDraws == a3.buckets,
              "(c) ...counted blended (%u), and the screen draws follow the buckets (%u == %u)", a3.blended,
              a3.screenDraws, a3.buckets);
    s->setPbrMaterial(mats[7], params[7]);
    render(e, 1);
    const bool back = atomBit(items[7]) && atomBit(items[57]) && atomBit(items[107]) && atomBit(items[157]);
    const AtomDrawStatus a4 = s->atomDrawStatus();
    CHECK_MSG(back && a4.blended == a0.blended && a4.screenDraws == a4.buckets,
              "(c) the edit back restores the route in one frame (screen draws %u, buckets %u)", a4.screenDraws,
              a4.buckets);

    // (d) A SAME-SLOT TEXTURE SWAP: material 8 moves from texture A to a texture of its
    // own — a new bucket, a new decode draw.
    render(e, 4);
    const AtomDrawStatus a5 = s->atomDrawStatus();
    s->setPbrTexture(mats[8], PbrTextureSlot::Albedo, tC);
    render(e, 2);
    const AtomDrawStatus a6 = s->atomDrawStatus();
    CHECK_MSG(a6.buckets == a5.buckets + 1u && a6.screenDraws == a6.buckets,
              "(d) a same-slot texture swap moves the material's bucket (buckets %u -> %u, screen draws %u)",
              a5.buckets, a6.buckets, a6.screenDraws);

    // (a) AGAIN, after all of it: still is free.
    render(e, 30);
    const AtomDrawStatus a7 = s->atomDrawStatus();
    const RayQueryStatus r7 = s->rayQueryStatus();
    render(e, 60);
    const AtomDrawStatus a8 = s->atomDrawStatus();
    const RayQueryStatus r8 = s->rayQueryStatus();
    CHECK_MSG(a8.wordSlotVisits == a7.wordSlotVisits && r8.feedSlotVisits == r7.feedSlotVisits &&
                  a8.pbsNotes == a7.pbsNotes,
              "(a) still again after the edits: 0 slots visited, nothing re-hashed");

    std::printf("%s: atom.words_still (%d failure(s))\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}

int main(int argc, char **argv)
{
    const std::string mode = argc > 1 ? argv[1] : "";
    if (mode == "--tlas-compute") return tlasMain();
    if (mode == "--words-still") return wordsMain();
    std::printf("usage: test_cpu_walks --tlas-compute | --words-still\n");
    return 2;
}
