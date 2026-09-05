// GPU_SKINNING_SPEC phase 1 / T4 (document variant): per-node RIG ownership.
//
// The defect this pins: MeshNode::createDuplicate passes the SAME MeshPtr to
// the duplicate, and pose state used to live on that shared asset
// (iris::Skeleton::boneTransforms). Two nodes on one mesh asset therefore
// shared one pose and the last writer per frame won — multiple avatars of one
// rig were impossible regardless of where skinning ran. The fix was to clone
// the rig per node, and that clone is what this suite guards.
//
// WHAT MOVED (ANIMATION_ENGINE_MIGRATION_SPEC, full retirement). The document
// no longer computes a pose at all — Skeleton::boneTransforms is gone with the
// clip evaluator — so the "two nodes hold two different POSES" half of this
// suite moved to where a pose now lives: skeletal.gpu_parity T4 renders two
// nodes of ONE mesh asset playing two clips in ONE frame and pixel-asserts that
// each moved independently, and skeletal.gpu_clips proves the per-node clip
// sets. What is left here is the document-side precondition for all of it: each
// node owns its own rig object, cloned, with the hierarchy re-pointed at its own
// bones — which is exactly the thing that regressed.
//
// Document-only: no engine, no window, no GL. Runs in milliseconds.
#include <QGuiApplication>
#include <cstdio>

#include "irisgl/document/animation/animation.h"
#include "irisgl/document/animation/clipextractor.h"
#include "irisgl/document/assets/skeleton.h"
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

    // The two nodes carry DIFFERENT clips, and each translates against its own
    // rig clone — which is what makes two independent SkeletonInstances
    // possible engine-side. (The rendered proof is skeletal.gpu_parity T4.)
    {
        iris::ExtractedClip exA, exB;
        CHECK(iris::ClipExtractor::extract(a, a, a->getSkeleton(), clipA->getSkeletalAnimation(),
                                           "A", clipA->getLength(), nullptr, exA, nullptr) &&
              iris::ClipExtractor::extract(b, b, b->getSkeleton(), clipB->getSkeletalAnimation(),
                                           "B", clipB->getLength(), nullptr, exB, nullptr),
              "both nodes' clips translate against their OWN rig");
        CHECK(exA.tracks.size() == 1 && exB.tracks.size() == 1, "one driven bone each");
        CHECK(exA.tracks[0].keys.last().rotation != exB.tracks[0].keys.last().rotation,
              "two duplicates of ONE rig carry DIFFERENT clip data");
    }

    // The rig template is shared and must never be mistaken for a node's rig.
    CHECK(b->getSkeleton().data() != mesh->getSkeleton().data(),
          "the second node does not alias the mesh asset's rig template either");

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
        CHECK(dup->children().size() == 1 && dup->children()[0]->children().size() == 1,
              "the duplicate kept its own bone scene nodes");

        // The duplicate's rig is a real clone: same names and indices (the
        // blend indices in the vertex data name them), its own Bone objects,
        // hierarchy re-pointed at those.
        CHECK(dupMesh->getSkeleton()->bones.size() == orig->getSkeleton()->bones.size() &&
                  dupMesh->getSkeleton()->boneMap == orig->getSkeleton()->boneMap,
              "the duplicate's rig has the same bones, names and INDEX ORDER");
        CHECK(dupMesh->getSkeleton()->getBone("jointTip")->parentBone.data() ==
                  dupMesh->getSkeleton()->getBone("jointRoot").data(),
              "and its hierarchy points at its OWN bones, not the original's");
        for (int i = 0; i < orig->getSkeleton()->bones.size(); ++i)
            CHECK(dupMesh->getSkeleton()->bones[i].data() != orig->getSkeleton()->bones[i].data(),
                  "every bone object is the duplicate's own");
    }

    std::printf(failures ? "FAILED: %d checks\n" : "all checks passed\n", failures);
    return failures ? 1 : 0;
}
