// The aiScene overload of MeshNode::loadAsSceneFragment — the LIBRARY/IMPORT
// side of the same "no single-mesh rig can animate" defect.
//
// Two callers hand this overload an already-parsed aiScene rather than a path:
// AssetWidget dropping an Object asset into the scene (assetwidget.cpp:359) and
// ProjectAssets resolving a pinned project asset (projectassets.cpp:126). Both
// took the single-mesh shortcut, which builds ONE MeshNode and no child scene
// nodes — and pose evaluation is name-matched over the scene-node hierarchy, so
// every bone stayed at identity while the clip clock advanced. A rigged
// character added from the library was frozen exactly the way one loaded from
// disk was, and just as silently.
//
// This suite drives the overload the way those callers do: parse the file into
// an Assimp::Importer that OUTLIVES the call (an AssimpObject holds its scene),
// then hand over the aiScene. Document-only — no engine, no window.
#include <QGuiApplication>
#include <QTemporaryDir>
#include <cmath>
#include <cstdio>

#include "assimp/Importer.hpp"
#include "assimp/scene.h"
#include "irisgl/import/importflags.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/assets/skeleton.h"
#include "irisgl/document/materials/defaultmaterial.h"
#include "irisgl/document/animation/animation.h"
#include "irisgl/document/animation/clipextractor.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

// The generated two-bone rig (tests/avatar/fixtures/make_rig_glb.py): a skinned
// SINGLE-mesh glTF with two named clips — the shape every Mixamo export has.
static const QString kRig  = QStringLiteral(JAHSHAKA_TEST_SOURCE_DIR "/tests/avatar/fixtures/rig2.glb");
// An unskinned single-mesh model: must keep the shortcut and its node shape.
static const QString kProp = QStringLiteral(JAHSHAKA_TEST_SOURCE_DIR "/tests/importer/fixtures/textured_pbr_quad.glb");

static iris::MaterialPtr makeMat(iris::MeshPtr, iris::MeshMaterialData &)
{
    return iris::DefaultMaterial::create();
}

static iris::MeshNodePtr findSkinned(const iris::SceneNodePtr &n)
{
    if (n->getSceneNodeType() == iris::SceneNodeType::Mesh) {
        auto m = n.staticCast<iris::MeshNode>();
        if (!m->getSkeleton().isNull()) return m;
    }
    for (const auto &c : n->children()) if (auto r = findSkinned(c)) return r;
    return iris::MeshNodePtr();
}
static iris::SceneNodePtr findClipHost(const iris::SceneNodePtr &n)
{
    if (!n->getAnimations().isEmpty()) return n;
    for (const auto &c : n->children()) if (auto r = findClipHost(c)) return r;
    return iris::SceneNodePtr();
}
static int countNodes(const iris::SceneNodePtr &n)
{
    int c = 1;
    for (const auto &k : n->children()) c += countNodes(k);
    return c;
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    QTemporaryDir extract;

    // ---- the rig, through the aiScene overload ----------------------------
    {
        // The importer must outlive the call: the aiScene is borrowed, exactly
        // as AssimpObject lends its own.
        Assimp::Importer importer;
        const aiScene *scene = importer.ReadFile(kRig.toStdString().c_str(),
                                                 iris::ImportFlags::Canonical);
        CHECK(scene != nullptr, "the rig fixture parses");
        if (!scene) return 1;
        CHECK(scene->mNumMeshes == 1 && scene->mMeshes[0]->mNumBones > 0,
              "it really is a SKINNED SINGLE-MESH file (the case that was broken)");

        // AssetWidget passes an EMPTY path — the aiScene already carries
        // everything — so that is what is exercised here.
        auto node = iris::MeshNode::loadAsSceneFragment(QString(), scene, makeMat, extract.path());
        CHECK(!node.isNull(), "the fragment loads");
        if (node.isNull()) return 1;

        CHECK(countNodes(node) == 4,
              "the fragment carries the aiNode tree (Armature/jointRoot/jointTip/arm), not one bare MeshNode");
        auto mn = findSkinned(node);
        CHECK(!mn.isNull(), "a skinned MeshNode is in there");
        if (mn.isNull()) return 1;
        CHECK(mn->getSkeleton()->bones.size() == 2, "two bones");

        auto host = findClipHost(node);
        CHECK(!host.isNull() && !host->getAnimations().isEmpty(), "the clips came with it");
        if (host.isNull()) return 1;
        host->setAnimation(host->getAnimations().first());

        auto doc = iris::Scene::create();
        doc->getRootNode()->addChild(node);
        node->applyDefaultPose();          // captures the rest transforms

        // THE assertion, restated for the world after the document's clip
        // evaluator was retired (ANIMATION_ENGINE_MIGRATION_SPEC): what was
        // broken here is that the single-mesh shortcut built ONE MeshNode and no
        // bone scene nodes, so a clip's channels matched nothing. That is
        // exactly what clip translation needs, so translating the clip is the
        // sharpest possible statement of it — and it stays engine-free.
        // Before the fix this produced ZERO driven bones, silently.
        iris::ExtractedClip clip;
        QString err;
        const auto anim = host->getAnimation();
        CHECK(iris::ClipExtractor::extract(node, mn, mn->getSkeleton(),
                                           anim->getSkeletalAnimation(), anim->getName(),
                                           anim->getLength(), nullptr, clip, &err),
              "the library-imported fragment's clip translates");
        std::printf("    driven bones: %d/%lld, keys %d -> %d\n", clip.tracks.size(),
                    (long long)mn->getSkeleton()->bones.size(),
                    clip.sourceKeyCount, clip.emittedKeyCount);
        CHECK(clip.tracks.size() >= 1, "at least one bone is driven by the clip");
        bool moves = false;
        for (const auto &track : clip.tracks)
            if (track.keys.size() >= 2 &&
                (track.keys.first().rotation != track.keys.last().rotation ||
                 track.keys.first().position != track.keys.last().position))
                moves = true;
        CHECK(moves, "and its track actually MOVES between its first and last key");

        // Per-node ownership holds on this path too: a duplicate poses alone.
        auto dup = node->duplicate();
        CHECK(!dup.isNull(), "the fragment duplicates");
        if (!dup.isNull()) {
            doc->getRootNode()->addChild(dup);
            auto dupMesh = findSkinned(dup);
            CHECK(!dupMesh.isNull() && dupMesh->getSkeleton().data() != mn->getSkeleton().data(),
                  "the duplicate owns its own skeleton");
            if (!dupMesh.isNull()) {
                // Per-node rig ownership: each copy carries its OWN bone
                // objects, which is what lets two library copies of one
                // character hold two poses engine-side (rendered proof:
                // skeletal.gpu_parity T4).
                CHECK(dupMesh->getSkeleton()->boneMap == mn->getSkeleton()->boneMap &&
                          dupMesh->getSkeleton()->bones[0].data() != mn->getSkeleton()->bones[0].data(),
                      "two library copies of one rig own separate bone objects");
            }
        }
    }

    // ---- the regression: unskinned single-mesh models keep the shortcut ----
    {
        Assimp::Importer importer;
        const aiScene *scene = importer.ReadFile(kProp.toStdString().c_str(),
                                                 iris::ImportFlags::Canonical);
        CHECK(scene != nullptr, "the unskinned prop parses");
        if (!scene) return failures ? 1 : 0;
        CHECK(scene->mNumMeshes == 1 && scene->mMeshes[0]->mNumBones == 0,
              "it is an unskinned single-mesh file");

        auto prop = iris::MeshNode::loadAsSceneFragment(kProp, scene, makeMat, extract.path());
        CHECK(!prop.isNull(), "the prop loads");
        if (prop.isNull()) return 1;
        CHECK(prop->getSceneNodeType() == iris::SceneNodeType::Mesh,
              "an unskinned single-mesh file is STILL one MeshNode (shortcut kept)");
        CHECK(prop->children().isEmpty(), "... with no child nodes");
        CHECK(prop.staticCast<iris::MeshNode>()->getSkeleton().isNull(),
              "... and no skeleton");

        // The shortcut's own fix rides along: the authored node transform is
        // applied rather than dropped (a scaled glb used to import at 1:1).
        // Compare against the path-based overload, which shares the branch.
        auto viaPath = iris::MeshNode::loadAsSceneFragment(kProp, makeMat, nullptr, nullptr, extract.path());
        CHECK(!viaPath.isNull() &&
                  (viaPath->getLocalPos() - prop->getLocalPos()).length() < 1e-5f &&
                  (viaPath->getLocalScale() - prop->getLocalScale()).length() < 1e-5f,
              "... and the same transform the path-based overload produces");
    }

    std::printf(failures ? "FAILED: %d checks\n" : "all checks passed\n", failures);
    return failures ? 1 : 0;
}
