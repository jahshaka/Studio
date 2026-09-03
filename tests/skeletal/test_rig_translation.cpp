// GPU_SKINNING_SPEC phase 2 / T1, T2, T7: the v1-skeleton translation layer,
// WITHOUT rendering anything.
//
// This suite exists because R1 — reconciling our mesh-node-relative skin
// matrices with Ogre's world-relative bone matrices — has three places to be off
// by an inverse, and the symptom is "the character explodes", not "the character
// is 2% wrong". So the algebra is proved here, in a unit test, in milliseconds,
// before a single pixel is asked for.
//
// The oracle is CLOSED FORM (armrig::swingSkinMatrices): for any pose, the
// engine's per-bone matrix — which the shader multiplies the vertex by — must
// equal derived_i * inverseBind_i computed by hand. It used to be the document
// evaluator's skin matrices; that evaluator is retired
// (ANIMATION_ENGINE_MIGRATION_SPEC), and a closed form is the better oracle
// anyway because it cannot drift with the thing it checks. Scene::boneMatrices
// reads back exactly what HlmsPbs streams into the bone buffer per draw, so the
// check is against what the shader will actually be handed.
//
// An engine and an offscreen view exist because a live Ogre::Root is what the v1
// resource managers and the VaoManager need — nothing here reads a pixel.
#include <QGuiApplication>
#include <QMatrix4x4>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "irisgl/document/scenegraph/scene.h"
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"
#include "irisgl/mirror/scenemirror.h"
#include "armrig.h"

using namespace jahshaka::engine;
static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

// A document rig plus its scene nodes, ready to pose.
struct Rig {
    iris::ScenePtr doc;
    iris::MeshNodePtr node;
    iris::MeshPtr mesh;
};
static Rig makeArm()
{
    Rig r;
    r.doc = iris::Scene::create();
    r.mesh = armrig::buildArmMesh();
    r.node = armrig::buildArmNode(r.mesh, "arm");
    auto clip = armrig::buildSwingClip(-90.0f);
    r.node->addAnimation(clip); r.node->setAnimation(clip);
    r.doc->getRootNode()->addChild(r.node);
    return r;
}

/// The analytic pose as the boundary's BonePose array.
static std::vector<BonePose> armPoses(float t)
{
    const QVector<armrig::ArmPose> local = armrig::swingLocalPoses(-90.0f, t);
    std::vector<BonePose> out(size_t(local.size()));
    for (int i = 0; i < local.size(); ++i) {
        out[size_t(i)].position = Vec3(local[i].pos.x(), local[i].pos.y(), local[i].pos.z());
        out[size_t(i)].rotation = Quat(local[i].rot.x(), local[i].rot.y(),
                                       local[i].rot.z(), local[i].rot.scalar());
        out[size_t(i)].scale = Vec3(local[i].scale.x(), local[i].scale.y(), local[i].scale.z());
    }
    return out;
}

// The engine's resolved bone matrix as a QMatrix4x4 (row-major 3x4 + [0,0,0,1]).
static QMatrix4x4 engineBone(Scene *s, NodeId node, size_t bone, size_t boneCount)
{
    std::vector<float> m(boneCount * 12, 0.0f);
    if (!s->boneMatrices(node, m.data(), boneCount)) return QMatrix4x4();
    QMatrix4x4 out;
    const float *b = &m[bone * 12];
    for (int r = 0; r < 3; ++r) for (int c = 0; c < 4; ++c) out(r, c) = b[r * 4 + c];
    out(3, 0) = 0; out(3, 1) = 0; out(3, 2) = 0; out(3, 3) = 1;
    return out;
}

static MeshData armMeshData(const iris::MeshPtr &mesh)
{
    MeshData d;
    SceneMirror::toMeshData(mesh.data(), d);
    std::vector<float> bi, bw;
    SceneMirror::toSkinData(mesh.data(), bi, bw);
    d.blendIndices.resize(bi.size());
    for (size_t i = 0; i < bi.size(); ++i) d.blendIndices[i] = (unsigned char)(int)bi[i];
    d.blendWeights = bw;
    return d;
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test_rig_translation-ogre.log";
    std::string err;
    auto engine = Engine::create(cfg, err);
    CHECK(engine != nullptr, "engine created");
    if (!engine) { std::printf("    %s\n", err.c_str()); return 1; }

    View *view = engine->createOffscreenView("rig", 32, 32, Colour(0, 0, 0));
    Scene *s = engine->createScene("rig");
    view->setScene(s);

    // ================= descriptor translation =================
    Rig rig = makeArm();
    SkeletonDesc desc;
    CHECK(SceneMirror::toSkeletonDesc(rig.node->getSkeleton(), desc), "toSkeletonDesc succeeded");
    CHECK(desc.bones.size() == 2, "two bones in the descriptor");
    CHECK(desc.bones[0].name == "jointRoot" && desc.bones[1].name == "jointTip",
          "bone names and INDEX ORDER survive (the index is what blend indices name)");
    CHECK(desc.bones[0].parent == -1 && desc.bones[1].parent == 0, "the hierarchy survives");
    CHECK(!desc.id.empty(), "the descriptor carries an id");

    // The id must be STRUCTURE-derived: a second, independently built copy of the
    // same rig must hash identically (two files of one rig share one engine rig,
    // which is what lets clips from elsewhere drive this character later).
    {
        Rig other = makeArm();
        SkeletonDesc d2;
        SceneMirror::toSkeletonDesc(other.node->getSkeleton(), d2);
        CHECK(d2.id == desc.id, "the same rig built twice hashes to the SAME id");
        // A rig that differs in a bind transform must NOT alias it.
        other.node->getSkeleton()->bones[1]->meshSpacePoseMatrix.translate(0, 0.25f, 0);
        SkeletonDesc d3;
        SceneMirror::toSkeletonDesc(other.node->getSkeleton(), d3);
        CHECK(d3.id != desc.id, "a rig with a different bind pose gets a DIFFERENT id");
        // ... and so must one that differs only in a bone name.
        Rig named = makeArm();
        named.node->getSkeleton()->bones[1]->name = "jointToe";
        SkeletonDesc d4;
        SceneMirror::toSkeletonDesc(named.node->getSkeleton(), d4);
        CHECK(d4.id != desc.id, "a rig with a renamed bone gets a DIFFERENT id");
    }

    // ================= attach =================
    MeshData md = armMeshData(rig.mesh);
    CHECK(md.hasSkinData(), "the mesh data carries 4 blend indices + 4 weights per vertex");
    MeshId meshId = s->createMesh(md);
    CHECK(meshId != 0, "skinned mesh created");
    PbrParams mp; mp.albedo = Colour(1, 0, 0);
    MaterialId matId = s->createPbrMaterial(mp);
    NodeId node = s->createNode();

    CHECK(s->attachSkinnedMesh(node, meshId, matId, desc), "attachSkinnedMesh succeeded");
    CHECK(s->hasSkeleton(node), "the node reports a rig");
    const std::vector<std::string> names = s->boneNames(node);
    CHECK(names.size() == 2 && names[0] == "jointRoot" && names[1] == "jointTip",
          "boneNames comes back in rig order");

    // ---- T2: v2 integrity (R7/R9) ----
    // The v1 skeleton is a build-time scaffold only: after attaching, the
    // geometry must still be the ONE v2 VAO we uploaded (v1 geometry renders
    // nothing on Vulkan), and the renderable must actually come out
    // skeleton-animated — which is what puts hlms_skeleton in the shader hash.
    // Both are asserted INSIDE attachSkinnedMesh on the production path (see
    // OgreSkeleton.cpp), so a true return above IS the T2 gate; that keeps Ogre
    // out of the test binary, which is the one-directory rule.
    CHECK(s->attachSkinnedMesh(node, meshId, matId, desc),
          "T2: re-attaching still satisfies the v2/skinned invariants");

    // ================= T1: bind-pose round trip (R1) =================
    // At bind pose every skin matrix is identity, so every bone's full
    // transform must be identity too.
    std::vector<BonePose> poses = armPoses(0.0f);
    CHECK(poses.size() == 2, "one pose per bone");
    CHECK(s->setBonePoses(node, poses.data(), poses.size()), "setBonePoses accepted the bind pose");
    engine->renderOneFrame();   // runs the engine's bone FK; no pixel is read

    double worst = 0.0;
    for (size_t b = 0; b < 2; ++b) {
        QMatrix4x4 got = engineBone(s, node, b, 2);
        for (int r = 0; r < 3; ++r) for (int c = 0; c < 4; ++c)
            worst = std::max(worst, std::fabs(double(got(r, c)) - (r == c ? 1.0 : 0.0)));
    }
    std::printf("    bind pose: worst |bone matrix - identity| = %.3e\n", worst);
    CHECK(worst < 1e-4, "T1: at bind pose every engine bone matrix is the identity");

    // ---- T1 continued: a POSED rig. The engine's per-bone matrix must equal
    // derived_i * inverseBind_i, computed by hand. This is the reconciliation —
    // three places to be off by an inverse, and the symptom is "the character
    // explodes", not "the character is 2% wrong". Everything downstream is
    // pixels.
    for (float t : { 0.25f, 0.5f, 0.9f }) {
        poses = armPoses(t);
        s->setBonePoses(node, poses.data(), poses.size());
        engine->renderOneFrame();
        const QVector<QMatrix4x4> want = armrig::swingSkinMatrices(-90.0f, t);
        double w = 0.0;
        for (size_t b = 0; b < 2; ++b) {
            const QMatrix4x4 got = engineBone(s, node, b, 2);
            for (int r = 0; r < 3; ++r) for (int c = 0; c < 4; ++c)
                w = std::max(w, std::fabs(double(got(r, c)) - double(want[int(b)](r, c))));
        }
        std::printf("    t=%.2f: worst |engine bone matrix - analytic skin matrix| = %.3e\n", double(t), w);
        char msg[128];
        std::snprintf(msg, sizeof(msg), "T1: engine bone matrices == the analytic skin matrices at t=%.2f", double(t));
        CHECK(w < 1e-4, msg);
    }

    // ================= per-node independence =================
    // Two nodes, one mesh, one rig, two poses — the multiple-avatars case, at the
    // level of the matrices the shader is handed.
    {
        NodeId n2 = s->createNode();
        CHECK(s->attachSkinnedMesh(n2, meshId, matId, desc),
              "a SECOND node attaches the same mesh and rig");
        std::vector<BonePose> poseA = armPoses(0.5f);
        std::vector<BonePose> poseB = armPoses(0.0f);
        s->setBonePoses(node, poseA.data(), poseA.size());
        s->setBonePoses(n2, poseB.data(), poseB.size());
        engine->renderOneFrame();
        const QMatrix4x4 m1 = engineBone(s, node, 1, 2);
        const QMatrix4x4 m2 = engineBone(s, n2, 1, 2);
        CHECK(m1 != m2, "two nodes on ONE mesh hold two DIFFERENT bone matrices");
        double idErr = 0.0;
        for (int r = 0; r < 3; ++r) for (int c = 0; c < 4; ++c)
            idErr = std::max(idErr, std::fabs(double(m2(r, c)) - (r == c ? 1.0 : 0.0)));
        CHECK(idErr < 1e-4, "and the second one is exactly at bind pose");
        s->removeNode(n2);
    }

    // ================= T7: limits and refusals =================
    {
        // Unskinned mesh: refused, loudly.
        MeshData plain = enginetest::unitCubeMesh();
        MeshId plainId = s->createMesh(plain);
        NodeId n = s->createNode();
        CHECK(!s->attachSkinnedMesh(n, plainId, matId, desc),
              "T7: a mesh with no blend data is refused");
        CHECK(!s->hasSkeleton(n), "T7: ... and gets no rig");

        // Empty rig, and a rig that does not cover the mesh's blend indices.
        SkeletonDesc empty; empty.id = "empty";
        CHECK(!s->attachSkinnedMesh(n, meshId, matId, empty), "T7: an empty rig is refused");
        SkeletonDesc oneBone; oneBone.id = "one"; oneBone.bones.resize(1);
        oneBone.bones[0].name = "solo";
        CHECK(!s->attachSkinnedMesh(n, meshId, matId, oneBone),
              "T7: a rig too small for the mesh's blend indices is refused");

        // A cyclic hierarchy would hang the engine's depth walk.
        SkeletonDesc cyclic = desc; cyclic.id = "cyclic";
        cyclic.bones[0].parent = 1; cyclic.bones[1].parent = 0;
        CHECK(!s->attachSkinnedMesh(n, meshId, matId, cyclic), "T7: a cyclic rig is refused");

        // Over 256 bones: warned, attached UNSKINNED at bind pose, not crashed.
        // A FRESH skinned mesh, because `meshId` is already bound to `desc` and
        // an unskinned attach to it would inherit that rig's instance.
        MeshId freshId = s->createMesh(armMeshData(rig.mesh));
        NodeId big = s->createNode();
        SkeletonDesc huge; huge.id = "huge"; huge.bones.resize(300);
        for (size_t i = 0; i < huge.bones.size(); ++i) {
            huge.bones[i].name = "b" + std::to_string(i);
            huge.bones[i].parent = i ? int(i) - 1 : -1;
        }
        CHECK(!s->attachSkinnedMesh(big, freshId, matId, huge), "T7: a 300-bone rig is refused");
        CHECK(!s->hasSkeleton(big), "T7: ... and rides unskinned instead of crashing");
        engine->renderOneFrame();
        CHECK(true, "T7: the frame after an over-limit attach still renders");
        s->removeNode(big);

        // Wrong pose count.
        BonePose one;
        CHECK(!s->setBonePoses(node, &one, 1), "T7: a pose count that misses the rig is refused");
        CHECK(!s->setBonePoses(9999, poses.data(), poses.size()), "T7: an unknown node is refused");
        s->removeNode(n);
    }

    // ================= the mesh may not be re-rigged =================
    {
        SkeletonDesc other = desc; other.id = "other-rig";
        NodeId n = s->createNode();
        CHECK(!s->attachSkinnedMesh(n, meshId, matId, other),
              "a mesh already bound to one rig refuses a second");
        s->removeNode(n);
    }

    rig.doc.reset();
    engine->destroyView(view);
    engine->destroyScene(s);
    engine.reset();

    std::printf(failures ? "FAILED: %d checks\n" : "all checks passed\n", failures);
    return failures ? 1 : 0;
}
