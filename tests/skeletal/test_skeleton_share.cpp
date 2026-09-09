// SKELETON SHARING and its LIFETIME RULES (AVATAR_RIG_PERF_SPEC §3.2/§3.3).
//
// THIS SUITE IS THE SPIKE the spec asked for before P1b was estimated (§8: "the
// `detachFromParent -> setParentNode(nullptr)` trap and the parentless instance
// after `stopUsing...` are the two claims that most deserve a spike"), kept as a
// permanent gate instead of thrown away. Both claims are TRUE in the pinned
// engine, read out of its sources and exercised here:
//
//   1. MovableObject::_notifyAttached ends with
//      `mSkeletonInstance->setParentNode(parent)` (OgreMovableObject.cpp:155).
//      On a SLAVE that instance belongs to the MASTER, so detaching a slave's
//      Item — which our detachItem does on every material or mesh swap — sets
//      the shared instance's parent node to NULL: the whole character renders at
//      the origin, silently, from the next frame.
//   2. stopUsingSkeletonInstanceFromMaster creates the fresh instance and never
//      parents it (OgreItem.cpp:266-280). An Item left attached with an
//      unparented instance is the same failure with one fewer step.
//   3. (found while writing this) ClipRec caches a float* into the instance's
//      weight arrays and an index into its animation list, so replacing a node's
//      instance — in EITHER direction — dangles every clip record it has. The
//      engine drops a node's clips on both transitions.
//
// LINKS THE ENGINE ONLY, no document and no Qt, so it can also be built against
// the sanitised backend (skeletal.share_asan): the traps above are
// use-after-free / dangling-pointer defects and ASan is the instrument that
// names them. Runs on the NULL render system: no display, no GPU.
//
// WHAT IT CANNOT PROVE HEADLESS: the POSE consequences (a follower's matrices
// are the master's) need a rendered frame — Ogre resolves bone transforms inside
// the threaded scene update, which only runs when a view renders. That half is
// the rendered arm of the rig-perf gate; everything structural is here.

#include "jahshaka/engine/Engine.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int gFailures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++gFailures; } } while (0)

namespace {

/// A quad weighted entirely to blend index 0 — or to 0 and 1 for a two-bone
/// piece, so a piece can carry a SUBSET of a rig and a remap has something to
/// translate.
MeshData quad(int bones)
{
    MeshData m;
    m.positions = { 0,0,0,  1,0,0,  0,1,0,  1,1,0 };
    m.normals   = { 0,0,1,  0,0,1,  0,0,1,  0,0,1 };
    m.uvs       = { 0,0,   1,0,   0,1,   1,1 };
    m.indices   = { 0, 1, 2, 2, 1, 3 };
    m.blendIndices.assign(16, 0);
    m.blendWeights.assign(16, 0.0f);
    for (int v = 0; v < 4; ++v) {
        m.blendIndices[size_t(v * 4)] = (unsigned char)(bones > 1 ? v % bones : 0);
        m.blendWeights[size_t(v * 4)] = 1.0f;
    }
    return m;
}

/// A three-bone chain: root -> mid -> tip, each a unit up from its parent.
SkeletonDesc chainRig(const std::string &id)
{
    SkeletonDesc rig;
    rig.id = id;
    BoneDesc root; root.name = "root"; root.parent = -1;
    BoneDesc mid;  mid.name = "mid";   mid.parent = 0;  mid.bindPosition = Vec3(0, 1, 0);
    BoneDesc tip;  tip.name = "tip";   tip.parent = 1;  tip.bindPosition = Vec3(0, 1, 0);
    rig.bones = { root, mid, tip };
    return rig;
}

ClipDesc oneClip(const std::string &id, int bone)
{
    ClipDesc c;
    c.id = id;
    c.name = id;
    c.length = 1.0f;
    BoneTrack t;
    t.bone = bone;
    BoneKey a; a.time = 0.0f; a.position = Vec3(0, 1, 0);
    BoneKey b; b.time = 1.0f; b.position = Vec3(0, 1, 0); b.rotation = Quat(0, 0, 0.3827f, 0.9239f);
    t.keys = { a, b };
    c.tracks = { t };
    return c;
}

}  // namespace

int main()
{
    EngineConfig cfg;
    cfg.headless = true;                 // NULL render system: no display, no GPU
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test_skeleton_share-ogre.log";
    std::string err;
    auto engine = Engine::create(cfg, err);
    CHECK(engine != nullptr, "headless engine");
    if (!engine) { std::printf("    %s\n", err.c_str()); return 1; }

    // A headless host never creates a View, and createScene needs the Hlms —
    // which documentGraphScene() is what registers (tests/engine/
    // test_engine_headless.cpp says the same thing at its own createScene).
    engine->documentGraphScene();
    Scene *scene = engine->createScene("share");
    CHECK(scene != nullptr, "scene");
    if (!scene) return 1;
    PbrParams pbr;
    const MaterialId mat = scene->createPbrMaterial(pbr);
    const SkeletonDesc rig = chainRig("share/rig");
    const SkeletonDesc other = chainRig("share/other-rig");

    // Two pieces of ONE character: a three-bone body and a one-bone prop, both
    // rigged to the same rig — the precondition Ogre enforces by throwing.
    const MeshId bodyMesh = scene->createMesh(quad(3));
    const MeshId hairMesh = scene->createMesh(quad(1));
    const NodeId body = scene->createNode();
    const NodeId hair = scene->createNode();
    CHECK(scene->attachSkinnedMesh(body, bodyMesh, mat, rig), "body rigged");
    // The hair piece carries ONE bone of the rig — `mid` — through a remap.
    const unsigned short hairMap[] = { 1 };
    CHECK(scene->attachSkinnedMesh(hair, hairMesh, mat, rig, hairMap, 1), "hair rigged, remapped");
    CHECK(scene->streamedBoneCount(hair) == 1 && scene->streamedBoneCount(body) == 3,
          "each piece streams its own bones");

    // ---- the share -------------------------------------------------------
    CHECK(scene->shareSkeleton(hair, body), "hair shares the body's skeleton");
    CHECK(scene->sharesSkeleton(hair) && !scene->sharesSkeleton(body),
          "the FOLLOWER shares; the master does not (Ogre's refcount would say both)");
    {
        const RigStats rs = scene->rigStats();
        CHECK(rs.rigged == 2 && rs.instances == 1 && rs.shared == 1,
              "two rigged nodes, ONE SkeletonInstance");
    }
    CHECK(scene->shareSkeleton(hair, body), "sharing again is idempotent");

    // ---- refusals --------------------------------------------------------
    CHECK(!scene->shareSkeleton(body, body), "a node cannot share with itself");
    {
        const NodeId third = scene->createNode();
        const MeshId m = scene->createMesh(quad(1));
        CHECK(scene->attachSkinnedMesh(third, m, mat, rig, hairMap, 1), "a third piece");
        CHECK(!scene->shareSkeleton(third, hair), "a source that is itself a follower is refused");
        CHECK(scene->shareSkeleton(third, body), "...but the master takes it");
        CHECK(!scene->shareSkeleton(body, third), "a follower with followers cannot become one");
        CHECK(scene->shareSkeleton(third, 0), "and it can stop");
        CHECK(!scene->sharesSkeleton(third), "...which it did");
        scene->removeNode(third);
    }
    {
        const NodeId foreign = scene->createNode();
        const MeshId m = scene->createMesh(quad(3));
        CHECK(scene->attachSkinnedMesh(foreign, m, mat, other), "a node on a DIFFERENT rig");
        CHECK(!scene->shareSkeleton(foreign, body),
              "a rig-id mismatch is refused BEFORE Ogre throws");
        const NodeId plain = scene->createNode();
        CHECK(scene->attachMesh(plain, scene->createMesh(quad(1)), mat), "an unrigged node");
        CHECK(!scene->shareSkeleton(plain, body), "an unrigged end is refused");
        scene->removeNode(foreign);
        scene->removeNode(plain);
    }

    // ---- the verb refusals on a follower (§3.3) --------------------------
    {
        std::vector<BonePose> poses(3);
        CHECK(!scene->setBonePoses(hair, poses.data(), poses.size()),
              "setBonePoses refuses on a follower (it IS the master's instance)");
        const ClipDesc c = oneClip("share/clip", 1);
        CHECK(!scene->attachClips(hair, &c, 1), "attachClips refuses on a follower");
        ClipState st; st.name = "share/clip"; st.enabled = true; st.weight = 1.0f;
        CHECK(!scene->setClipStates(hair, &st, 1), "setClipStates refuses on a follower");
        CHECK(!scene->setBoneManual(hair, "mid", true), "setBoneManual refuses on a follower");
        // The MASTER takes all four, and its clips survive its followers.
        CHECK(scene->attachClips(body, &c, 1), "the master takes the clips");
        CHECK(scene->setClipStates(body, &st, 1), "...and plays them");
        CHECK(scene->boneNames(hair).size() == 3,
              "reads are ALLOWED on a follower (a socket on a non-master piece keeps working)");
    }

    // ---- TRAP 1: the follower's Item is re-created ------------------------
    //
    // A material swap on a piece is an everyday editor event and it goes through
    // detachItem -> destroyItem -> createItem. Without the un-share, the
    // detachFromParent inside it nulls the MASTER's instance parent node.
    {
        CHECK(scene->attachSkinnedMesh(hair, hairMesh, mat, rig, hairMap, 1),
              "the follower's renderable is re-created (a material/mesh swap)");
        CHECK(!scene->sharesSkeleton(hair),
              "...which DROPS the share — the host re-arms it, nothing lingers half-shared");
        const RigStats rs = scene->rigStats();
        CHECK(rs.instances == 2 && rs.shared == 0, "both pieces are back on their own instances");
        CHECK(scene->shareSkeleton(hair, body), "and it re-shares cleanly");
        CHECK(scene->rigStats().instances == 1, "one instance again");
        // The MASTER is still whole: it keeps its clips and still answers.
        ClipState st; st.name = "share/clip"; st.enabled = true; st.weight = 1.0f;
        CHECK(scene->setClipStates(body, &st, 1), "the master still drives its clips");
    }

    // ---- TRAP 2: the master's Item is re-created --------------------------
    {
        CHECK(scene->attachSkinnedMesh(body, bodyMesh, mat, rig), "the MASTER's renderable is re-created");
        CHECK(!scene->sharesSkeleton(hair), "every follower was released first");
        const RigStats rs = scene->rigStats();
        CHECK(rs.instances == 2 && rs.shared == 0, "...onto their own instances");
        // The released follower must be USABLE, not a corpse: its fresh instance
        // is parentless out of Ogre (OgreItem.cpp:266-280) until the Item is
        // re-attached, and it must accept clips again.
        const ClipDesc c = oneClip("share/hairclip", 1);
        CHECK(scene->attachClips(hair, &c, 1), "the released follower takes clips of its own");
        ClipState st; st.name = "share/hairclip"; st.enabled = true; st.weight = 1.0f;
        CHECK(scene->setClipStates(hair, &st, 1), "...and plays them");
        CHECK(scene->boneMatrices(hair, std::vector<float>(3 * 12).data(), 3),
              "...and still reads back a pose");
    }

    // ---- the master goes away entirely ------------------------------------
    {
        CHECK(scene->shareSkeleton(hair, body), "re-shared");
        CHECK(scene->removeNode(body), "the master node is destroyed");
        CHECK(!scene->sharesSkeleton(hair),
              "the follower was un-shared before the master's node died");
        const RigStats rs = scene->rigStats();
        CHECK(rs.rigged == 1 && rs.instances == 1 && rs.shared == 0,
              "the survivor owns its instance");
        std::vector<float> m(3 * 12, 0.0f);
        CHECK(scene->boneMatrices(hair, m.data(), 3),
              "...and reads back without touching the dead master's node");
    }

    // ---- teardown with a live share ---------------------------------------
    //
    // The scene is destroyed while a share is armed: nothing may assert inside
    // stopUsingSkeletonInstanceFromMaster's refcount check and no Item may
    // outlive the instance it borrowed.
    {
        const NodeId a = scene->createNode(), b = scene->createNode();
        CHECK(scene->attachSkinnedMesh(a, scene->createMesh(quad(3)), mat, rig), "teardown pair A");
        CHECK(scene->attachSkinnedMesh(b, scene->createMesh(quad(1)), mat, rig, hairMap, 1),
              "teardown pair B");
        CHECK(scene->shareSkeleton(b, a), "shared at teardown time");
    }
    engine->destroyScene(scene);
    std::printf("scene destroyed with a live share\n");

    std::printf(gFailures ? "\nFAILURES: %d\n" : "\nall good\n", gFailures);
    return gFailures ? 1 : 0;
}
