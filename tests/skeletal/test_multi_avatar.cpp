// GPU_SKINNING_SPEC phase 1 / T4 (document variant): per-node pose ownership.
//
// The defect this pins: MeshNode::createDuplicate passes the SAME MeshPtr to
// the duplicate, and pose state used to live on that shared asset
// (iris::Skeleton::boneTransforms). Two nodes on one mesh asset therefore
// shared one pose and the last writer per frame won — multiple avatars of one
// rig were impossible regardless of where skinning ran.
//
// Document-only: no engine, no window, no GL. Runs in milliseconds and fails
// loudly if pose ownership ever moves back onto the asset.
#include <QGuiApplication>
#include <cstdio>

#include "irisgl/document/scenegraph/scene.h"
#include "../skeletal/armrig.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    // ---- 1. one mesh asset, two nodes, two different clips ----------------
    auto mesh = armrig::buildArmMesh();
    auto doc = iris::Scene::create();

    auto a = armrig::buildArmNode(mesh, "armA");
    auto b = armrig::buildArmNode(mesh, "armB");
    doc->getRootNode()->addChild(a);
    doc->getRootNode()->addChild(b);

    CHECK(a->getMesh().data() == b->getMesh().data(), "both nodes share ONE mesh asset");
    CHECK(!a->getSkeleton().isNull() && !b->getSkeleton().isNull(),
          "each node has its own skeleton");
    CHECK(a->getSkeleton().data() != b->getSkeleton().data(),
          "the two node skeletons are DIFFERENT objects");
    CHECK(a->getSkeleton().data() != mesh->getSkeleton().data(),
          "neither node aliases the mesh asset's rig template");
    CHECK(a->getSkeleton()->bones.size() == 2 && a->getSkeleton()->boneMap.contains("jointTip"),
          "the clone kept the bone list, names and indices");
    CHECK(a->getSkeleton()->getBone("jointTip")->parentBone
              == a->getSkeleton()->getBone("jointRoot"),
          "the clone kept the bone hierarchy, re-pointed at its own bones");

    // Different clips: A swings -90 degrees, B swings +45.
    auto clipA = armrig::buildSwingClip(-90.0f);
    auto clipB = armrig::buildSwingClip(+45.0f);
    a->addAnimation(clipA); a->setAnimation(clipA);
    b->addAnimation(clipB); b->setAnimation(clipB);

    doc->updateSceneAnimation(0.5f);

    const QVector<QMatrix4x4> poseA = a->getSkeleton()->boneTransforms;
    const QVector<QMatrix4x4> poseB = b->getSkeleton()->boneTransforms;
    CHECK(poseA.size() == 2 && poseB.size() == 2, "both poses have two bone transforms");
    CHECK(poseA[1] != QMatrix4x4(), "A's tip bone is posed");
    CHECK(poseB[1] != QMatrix4x4(), "B's tip bone is posed");
    // THE assertion: fails by construction before phase 1.
    CHECK(poseA != poseB, "two duplicates of ONE rig hold DIFFERENT skin matrices");

    // The rig template is never posed.
    int templateNonIdentity = 0;
    for (const auto &m : mesh->getSkeleton()->boneTransforms)
        if (!m.isIdentity()) ++templateNonIdentity;
    CHECK(templateNonIdentity == 0, "the mesh asset's rig template stayed at identity");

    // ---- 2. same clip, different TIMES ------------------------------------
    // (What a crowd of one animation with per-character offsets looks like.)
    {
        auto mesh2 = armrig::buildArmMesh();
        auto doc2 = iris::Scene::create();
        auto n0 = armrig::buildArmNode(mesh2, "arm0");
        auto n1 = armrig::buildArmNode(mesh2, "arm1");
        doc2->getRootNode()->addChild(n0);
        doc2->getRootNode()->addChild(n1);
        auto clip = armrig::buildSwingClip(-90.0f);
        n0->addAnimation(clip); n0->setAnimation(clip);
        n1->addAnimation(clip); n1->setAnimation(clip);

        // Advance them independently — updateAnimation is per node.
        n0->updateAnimation(0.25f);
        n1->updateAnimation(0.75f);
        CHECK(n0->getSkeleton()->boneTransforms != n1->getSkeleton()->boneTransforms,
              "one clip at two times gives two poses");
    }

    // ---- 3. through the real duplicate() path -----------------------------
    // SceneNode::duplicate deep-copies the child hierarchy AND goes through
    // MeshNode::setMesh, so the duplicate gets its own skeleton for free.
    {
        auto mesh3 = armrig::buildArmMesh();
        auto doc3 = iris::Scene::create();
        auto orig = armrig::buildArmNode(mesh3, "orig");
        auto clip = armrig::buildSwingClip(-90.0f);
        orig->addAnimation(clip); orig->setAnimation(clip);
        doc3->getRootNode()->addChild(orig);

        auto dup = orig->duplicate();
        CHECK(!dup.isNull(), "duplicate() returned a node");
        doc3->getRootNode()->addChild(dup);
        auto dupMesh = dup.staticCast<iris::MeshNode>();
        CHECK(dupMesh->getMesh().data() == mesh3.data(), "the duplicate shares the mesh asset");
        CHECK(!dupMesh->getSkeleton().isNull() &&
                  dupMesh->getSkeleton().data() != orig->getSkeleton().data(),
              "the duplicate owns a separate skeleton");
        CHECK(dup->children.size() == 1 && dup->children[0]->children.size() == 1,
              "the duplicate kept its own bone scene nodes");

        // Pose them at different times; the duplicate is the only one that moves.
        // (t == the clip length wraps to 0 through Animation::getSampleTime —
        // 0.5 is mid-swing and unambiguous.)
        orig->updateAnimation(0.0f);
        dupMesh->updateAnimation(0.5f);
        CHECK(!dupMesh->getSkeleton()->boneTransforms[1].isIdentity(),
              "the duplicate's tip bone is posed");
        CHECK(orig->getSkeleton()->boneTransforms != dupMesh->getSkeleton()->boneTransforms,
              "the original and its duplicate hold different poses in one frame");
    }

    std::printf(failures ? "FAILED: %d checks\n" : "all checks passed\n", failures);
    return failures ? 1 : 0;
}
