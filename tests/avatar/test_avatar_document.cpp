// Avatar Part 0, the document half — no engine, no window (QT_QPA_PLATFORM=offscreen).
//
//   ITEM ZERO (Z1/Z2): a skinned SINGLE-MESH file must get bone scene nodes and
//   a moving pose. Before AVATAR_MODULE_SPEC's fix that was 0 of N bones for
//   every Mixamo character in existence, silently.
//   The preview model (S1-S3b): clips and their display names, transport,
//   the two independent toggles, and the bone tree the overlay draws.
//   Cross-file clips (X1-X6) and MOCAP (B0-B4): a .bvh — animation-only by
//   construction, the format every mocap library ships — loads onto a
//   character through avatar.loadAnimation, and a foreign-rig one is refused.
//
// Everything here is what the `avatar` verbs call, which is why the verb
// surface is testable with no engine at all.
#include "irisgl/core/math/vec.h"
#include <QGuiApplication>
#include <QFileInfo>
#include <QSet>
#include <QTemporaryDir>
#include <functional>
#include <cmath>
#include <cstdio>

#include "assimp/Importer.hpp"
#include "assimp/scene.h"

#include "irisgl/irisglfwd.h"
#include "irisgl/document/animation/animation.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/animation/clipextractor.h"
#include "irisgl/import/importflags.h"
#include "irisgl/document/assets/skeleton.h"
#include "irisgl/document/materials/defaultmaterial.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "modules/avatar/avatarpreviewmodel.h"
#include "services/rigsignature.h"

#include "../support/documentgraph.h"
static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

static const QString kRig = QStringLiteral(JAHSHAKA_TEST_SOURCE_DIR "/tests/avatar/fixtures/rig2.glb");
static const QString kProp = QStringLiteral(JAHSHAKA_TEST_SOURCE_DIR "/tests/importer/fixtures/textured_pbr_quad.glb");
static const QString kAnimProp = QStringLiteral(JAHSHAKA_TEST_SOURCE_DIR "/tests/importer/fixtures/ticks_anim.glb");
// The cross-file clip fixtures: animation-only glTF (zero meshes), one for
// rig2.glb's own rig and one for a foreign rig (fixtures/make_rig_glb.py).
static const QString kWalkAnim = QStringLiteral(JAHSHAKA_TEST_SOURCE_DIR "/tests/avatar/fixtures/rig2_walk_anim.glb");
static const QString kMismatchAnim = QStringLiteral(JAHSHAKA_TEST_SOURCE_DIR "/tests/avatar/fixtures/rig_mismatch_anim.glb");
// The MOCAP fixtures (fixtures/make_bvh_fixtures.py): plain-ASCII .bvh, one on
// rig2.glb's joint names and one on a foreign rig.
static const QString kBvhWalk = QStringLiteral(JAHSHAKA_TEST_SOURCE_DIR "/tests/avatar/fixtures/rig2_walk.bvh");
static const QString kBvhMismatch = QStringLiteral(JAHSHAKA_TEST_SOURCE_DIR "/tests/avatar/fixtures/rig_mismatch.bvh");

static iris::SkeletonPtr findSkeleton(const iris::SceneNodePtr &node)
{
    if (node->getSceneNodeType() == iris::SceneNodeType::Mesh) {
        // The MeshNode's OWN skeleton (GPU_SKINNING_SPEC §7): the one on the
        // iris::Mesh is the shared rig template and is never posed.
        auto skel = node.staticCast<iris::MeshNode>()->getSkeleton();
        if (!skel.isNull()) return skel;
    }
    for (const auto &child : node->children())
        if (auto s = findSkeleton(child)) return s;
    return iris::SkeletonPtr();
}

static iris::SceneNodePtr findSkinnedNode(const iris::SceneNodePtr &node)
{
    if (node->getSceneNodeType() == iris::SceneNodeType::Mesh &&
        !node.staticCast<iris::MeshNode>()->getSkeleton().isNull())
        return node;
    for (const auto &child : node->children())
        if (auto n = findSkinnedNode(child)) return n;
    return iris::SceneNodePtr();
}

static int countNodes(const iris::SceneNodePtr &node)
{
    int n = 1;
    for (const auto &child : node->children()) n += countNodes(child);
    return n;
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    // v1 INTERIM (SPECS/SCENEGRAPH_SPEC.md §3): a document node IS an engine
    // node now, so even a document-only suite needs an engine. Declared here,
    // before anything builds a document, and destroyed last.
    enginetest::DocumentGraph graph("avatar-document-ogre.log");
    if (!graph.require()) return 1;
    // ================= Z1 — ITEM ZERO =================
    // Loaded exactly the way the module loads it (AssetHelper ->
    // MeshNode::loadAsSceneFragment), so this is the real path, not a probe.
    {
        avatar::AvatarPreviewModel model;
        QString error;
        CHECK(model.load(kRig, &error), "Z1: the skinned single-mesh rig loads");
        if (!model.isLoaded()) { std::printf("    %s\n", qUtf8Printable(error)); return 1; }

        auto fragment = model.fragment();
        CHECK(countNodes(fragment) == 4, "Z1: the fragment carries the aiNode tree (Armature/jointRoot/jointTip/arm)");
        CHECK(model.boneCount() == 2, "Z1: two bones");
        CHECK(model.bones().size() == 2, "Z1: both bones have a SCENE NODE (this is what was missing)");

        auto skel = findSkeleton(fragment);
        CHECK(!skel.isNull() && skel->bones.size() == 2, "Z1: the skeleton reached the mesh");

        // THE assertion, restated for the world after the clip evaluator moved
        // to the engine (ANIMATION_ENGINE_MIGRATION_SPEC). What was broken is
        // that a skinned SINGLE-MESH file produced one bare MeshNode and no bone
        // scene nodes, so a clip's channels matched nothing and the character
        // was frozen — silently, 0 of N bones. Clip translation is exactly the
        // consumer of those scene nodes, so it states the same fact and needs no
        // engine: pre-fix this yielded ZERO driven bones.
        model.setClip("Idle");
        model.setTime(0.0f);
        {
            iris::ExtractedClip clip;
            QString err;
            const auto anim = fragment->getAnimation();
            CHECK(!anim.isNull() && iris::ClipExtractor::extract(
                      fragment, findSkinnedNode(fragment), skel, anim->getSkeletalAnimation(),
                      anim->getName(), anim->getLength(), nullptr, clip, &err),
                  "Z1: the loaded rig's clip translates into bone tracks");
            std::printf("    driven bones: %d/2, keys %d -> %d\n", clip.tracks.size(),
                        clip.sourceKeyCount, clip.emittedKeyCount);
            CHECK(clip.tracks.size() >= 1, "Z1: at least one bone is driven at t > 0");
            bool moves = false;
            for (const auto &t : clip.tracks)
                if (t.keys.size() >= 2 && t.keys.first().rotation != t.keys.last().rotation)
                    moves = true;
            CHECK(moves, "Z1: and the track actually moves between its first and last key");
        }
    }

    // ================= Z2 — unskinned regression =================
    // The guard is `mesh->mNumBones > 0`: an unskinned single-mesh model must
    // keep the old shortcut, its node shape AND the transform fix that lives
    // in it (_applyMeshNodeTransform). The importer/thumbnails/assets suites
    // cover the values; this pins the shape.
    {
        auto make = [](iris::MeshPtr, iris::MeshMaterialData &) { return iris::DefaultMaterial::create(); };
        // R0.12, learned the hard way: an EMPTY extractDir writes a file's
        // embedded textures BESIDE the source — here that meant five stray
        // PNGs in tests/importer/fixtures/. Never pass an empty one.
        QTemporaryDir extract;
        auto prop = iris::MeshNode::loadAsSceneFragment(kProp, make, nullptr, nullptr, extract.path());
        CHECK(!prop.isNull(), "Z2: the unskinned single-mesh prop loads");
        if (prop) {
            CHECK(prop->getSceneNodeType() == iris::SceneNodeType::Mesh,
                  "Z2: an unskinned single-mesh file is STILL one MeshNode (shortcut kept)");
            CHECK(prop->children().isEmpty(), "Z2: ... with no child nodes");
        }
        auto animated = iris::MeshNode::loadAsSceneFragment(kAnimProp, make, nullptr, nullptr, extract.path());
        CHECK(!animated.isNull() && animated->getSceneNodeType() == iris::SceneNodeType::Mesh,
              "Z2: an unskinned but ANIMATED single-mesh file also keeps the shortcut");
        if (animated)
            CHECK(!animated->getAnimations().isEmpty(), "Z2: ... and still carries its clip");
    }

    // ================= S1/S2/S3/S3b — the preview model =================
    avatar::AvatarPreviewModel model;
    CHECK(model.load(kRig), "S1: preview load");
    if (!model.isLoaded()) return 1;

    // --- S1: clips and the bone tree ---
    const auto clips = model.clips();
    CHECK(clips.size() == 2, "S1: both clips are listed");
    if (clips.size() == 2) {
        CHECK(clips[0].name == "Idle" && std::fabs(clips[0].length - 1.0f) < 0.01f,
              "S1: 'Idle' is listed with its 1.0 s length");
        CHECK(std::fabs(clips[1].length - 0.5f) < 0.01f, "S1: the second clip is 0.5 s (different length)");
        CHECK(clips[0].active, "S1: the FILE-order first clip is active, not the alphabetically last one");
    }
    const auto bones = model.bones();
    CHECK(bones.size() == 2, "S1: two bones");
    if (bones.size() == 2) {
        CHECK(bones[0].name == "jointRoot" && bones[0].parent.isEmpty(), "S1: jointRoot is a root bone");
        CHECK(bones[1].name == "jointTip" && bones[1].parent == "jointRoot",
              "S1: jointTip's parent is the nearest bone ancestor");
    }

    // --- S3b: display names (every Mixamo clip is called "mixamo.com") ---
    const QString base = QFileInfo(kRig).completeBaseName();
    CHECK(clips[1].rawName == "mixamo.com", "S3b: the raw name is kept verbatim");
    CHECK(clips[1].name == base, "S3b: a junk clip name displays as the source file's base name");
    CHECK(avatar::AvatarPreviewModel::displayNameFor("Walk", "File") == "Walk",
          "S3b: a real clip name is left alone");
    CHECK(avatar::AvatarPreviewModel::displayNameFor("Take 001", "File") == "File",
          "S3b: 'Take 001' is junk too");

    // --- S2: the transport moves the clock, and the rig shape is stable ---
    // The POSE half of S2 moved to avatar.preview (S8), which has an engine:
    // clip evaluation is the engine's now, so bone POSITIONS only exist where an
    // engine does. Without a pose source this model reports the rig's REST
    // pose — the right shape, honestly not a pose — and that is what
    // `avatar.bones` is documented as returning under --headless.
    CHECK(model.setClip("Idle"), "S2: 'Idle' selected");
    model.setTime(0.0f);
    const iris::Vec3 tip0 = model.bones()[1].position;
    model.setTime(0.5f);
    CHECK(std::fabs(model.time() - 0.5f) < 1e-5f, "S2: setTime moves the clock");
    CHECK(!model.hasPoseSource(),
          "S2: this suite has no engine, so no pose source is installed");
    CHECK((model.bones()[1].position - tip0).length() < 1e-5f,
          "S2: and with no pose source the bone list stays at the rig's REST pose");
    model.setTime(0.0f);
    CHECK(std::fabs(model.time()) < 1e-5f, "S2: 0 -> 0.5 -> 0 restores the clock exactly");

    // --- S3: the two toggles, all four combinations ---
    {
        int roots = 0;
        for (const auto &b : model.bones()) if (b.parent.isEmpty()) ++roots;
        const int expectedSegments = model.bones().size() - roots;
        bool allFour = true, segmentsStable = true;
        for (int i = 0; i < 4; ++i) {
            const bool mesh = i & 1, skeleton = i & 2;
            model.setMeshVisible(mesh);
            model.setSkeletonVisible(skeleton);
            if (model.meshVisible() != mesh || model.skeletonVisible() != skeleton) allFour = false;
            // The segment list is a property of the RIG, not of the toggles:
            // the overlay decides what to draw, the model always knows.
            if (model.boneSegments().size() != expectedSegments) segmentsStable = false;
        }
        CHECK(allFour, "S3: all four (mesh, skeleton) combinations are settable and reported");
        CHECK(segmentsStable && expectedSegments == 1,
              "S3: segment count == bones - roots in every state");
    }

    // --- S3c: what the 3D bone overlay needs beyond the two endpoints ---
    // A bone is drawn as an octahedron parent-joint -> child-joint, and a bone
    // with NO child gets a stub in its OWN axis. Which axis a rig's bones run
    // along was MEASURED, not assumed: on this fixture and on a Mixamo
    // character (Ely, 67 bones) the direction to the child is the bone's local
    // +Y for 64 of 66 bones, and a leaf's own +Y is within 7 degrees of the
    // direction it came from for 13 of 15 leaves.
    {
        model.setTime(0.0f);
        const auto segs = model.boneSegments();
        bool ok = segs.size() == 1;
        if (ok) {
            const auto &s = segs.first();
            const iris::Vec3 along = (s.to - s.from).normalized();
            CHECK(s.toIsLeaf, "S3c: the tip bone is reported as a LEAF (nothing hangs off it)");
            CHECK(qAbs(s.toAxis.length() - 1.0f) < 1e-3f,
                  "S3c: the leaf's bone axis comes back normalized");
            CHECK(iris::Vec3::dotProduct(s.toAxis, along) > 0.99f,
                  "S3c: at rest the leaf's own +Y axis IS the bone direction (the rig's bone axis)");
            std::printf("    leaf axis (%.3f %.3f %.3f)  bone direction (%.3f %.3f %.3f)\n",
                        s.toAxis.x(), s.toAxis.y(), s.toAxis.z(), along.x(), along.y(), along.z());
        } else {
            CHECK(false, "S3c: one segment to inspect");
        }
    }

    // --- transport ---
    model.stop();
    CHECK(!model.isPlaying() && model.time() == 0.0f, "transport: stop pauses and rewinds");
    model.play();
    model.advance(0.25f);
    CHECK(model.isPlaying() && model.time() > 0.2f, "transport: play advances time");
    model.pause();
    const float held = model.time();
    model.advance(0.25f);
    CHECK(!model.isPlaying() && qFuzzyCompare(model.time() + 1.0f, held + 1.0f),
          "transport: pause holds the clock");

    // ================= X — cross-file clips (the Mixamo workflow) =========
    // Load the character once, then load animations as separate files. The
    // clip -> bone join is by SCENE-NODE NAME, so the fixtures are meshless
    // glTF documents whose node names either do or do not match rig2.glb's.
    {
        avatar::AvatarPreviewModel cross;
        CHECK(cross.load(kRig), "X: the character loads");
        const int ownClips = cross.clips().size();

        // --- X1: an ANIMATION-ONLY file (zero meshes) loads at all ---
        // Every mesh loader in the tree rejects a meshless file outright
        // (meshnode.cpp returns null on mNumMeshes == 0), which is why this
        // path parses the aiScene itself and reads clips only.
        QString error;
        avatar::ClipLoadReport report;
        const bool added = cross.loadAnimation(kWalkAnim, &error, &report);
        if (!added) std::printf("    %s\n", qUtf8Printable(error));
        CHECK(added, "X1: an animation-only file (0 meshes) loads onto the character");
        CHECK(report.added == 1 && report.matched == 1 && report.boneChannels == 1,
              "X1: one clip added, its one bone channel matched the rig");

        // --- X2: clips ACCUMULATE and are named after the animation file ---
        const auto all = cross.clips();
        CHECK(all.size() == ownClips + 1, "X2: the clip list accumulates (no replacement)");
        const auto &added_clip = all.last();
        CHECK(added_clip.external && added_clip.source == QFileInfo(kWalkAnim).absoluteFilePath(),
              "X2: the new clip records the file it came from");
        CHECK(added_clip.rawName == "mixamo.com" &&
                  added_clip.name == QFileInfo(kWalkAnim).completeBaseName(),
              "X2: a junk clip name displays as the ANIMATION file's base name");
        CHECK(!all.first().external && all.first().active,
              "X2: loading an animation does not switch what is playing");

        // --- X3: switching to it selects a DIFFERENT clip that really moves --
        // The bone POSITIONS used to be the assertion here; they live in the
        // engine now (ANIMATION_ENGINE_MIGRATION_SPEC), and this binary has no
        // engine. Clip translation states the same fact document-side and is
        // sharper: the cross-file clip must translate onto the LOADED rig into
        // a track that moves, and to a different motion than the character's
        // own clip. avatar.preview S8 makes the rendered version of the claim.
        CHECK(cross.setClip(added_clip.name), "X3: the cross-file clip is selectable by name");
        CHECK(cross.activeClip() == added_clip.name, "X3: ... and becomes the active clip");
        CHECK(cross.time() == 0.0f, "X3: ... rewound to 0");
        {
            auto crossFragment = cross.fragment();
            auto crossMesh = findSkinnedNode(crossFragment);
            auto crossSkel = findSkeleton(crossFragment);
            const auto extractOne = [&](const QString &clipName, iris::ExtractedClip &out) {
                for (const auto &a : crossFragment->getAnimations()) {
                    if (a.isNull() || !a->hasSkeletalAnimation()) continue;
                    if (avatar::AvatarPreviewModel::displayNameFor(
                            a->getName(), QFileInfo(kWalkAnim).completeBaseName()) != clipName &&
                        a->getName() != clipName)
                        continue;
                    QString err;
                    return iris::ClipExtractor::extract(crossFragment, crossMesh, crossSkel,
                                                        a->getSkeletalAnimation(), clipName,
                                                        a->getLength(), nullptr, out, &err);
                }
                return false;
            };
            iris::ExtractedClip own, foreign;
            const bool gotOwn = extractOne(QStringLiteral("Idle"), own);
            // The cross-file clip's raw name is the junk one; both clips in this
            // fragment are called "mixamo.com" after the load, so the LAST one
            // added is the foreign one.
            bool gotForeign = false;
            {
                iris::AnimationPtr last;
                for (const auto &a : crossFragment->getAnimations())
                    if (!a.isNull() && a->hasSkeletalAnimation()) last = a;
                if (!last.isNull()) {
                    QString err;
                    gotForeign = iris::ClipExtractor::extract(
                        crossFragment, crossMesh, crossSkel, last->getSkeletalAnimation(),
                        added_clip.name, last->getLength(), nullptr, foreign, &err);
                }
            }
            CHECK(gotOwn && gotForeign, "X3: both the own and the cross-file clip translate");
            bool foreignMoves = false;
            for (const auto &t : foreign.tracks)
                if (t.keys.size() >= 2 && t.keys.first().rotation != t.keys.last().rotation)
                    foreignMoves = true;
            CHECK(foreignMoves,
                  "X3: the foreign clip MOVES the rig's bones (its track is not static)");
            bool differs = own.tracks.size() != foreign.tracks.size();
            if (!differs && !own.tracks.isEmpty() && !foreign.tracks.isEmpty())
                differs = own.tracks[0].keys.last().rotation != foreign.tracks[0].keys.last().rotation;
            CHECK(differs,
                  "X3: ... to a different motion than the character's own clip reaches");
        }

        // --- X4: switching while playing keeps playing, from t = 0 ---
        cross.play();
        cross.advance(0.2f);
        CHECK(cross.isPlaying() && cross.time() > 0.1f, "X4: playing the cross-file clip");
        CHECK(cross.setClip("Idle") && cross.isPlaying() && cross.time() == 0.0f,
              "X4: switching while playing keeps playing, from the start of the new clip");
        cross.stop();

        // --- X5: a foreign rig is REFUSED, by name ---
        QString mismatchError;
        avatar::ClipLoadReport mismatch;
        const int before = cross.clips().size();
        CHECK(!cross.loadAnimation(kMismatchAnim, &mismatchError, &mismatch),
              "X5: a clip animating a different rig is refused, not silently loaded");
        std::printf("    refusal: %s\n", qUtf8Printable(mismatchError));
        CHECK(mismatchError.contains("hips"), "X5: ... and the message names the unmatched bone");
        CHECK(cross.clips().size() == before, "X5: ... and nothing was added");
        CHECK(mismatch.matched == 0 && mismatch.boneChannels == 1, "X5: ... 0 of 1 bones matched");
        // T2 (AVATAR_ASSET_SPEC §9) — ONE MATCHER. The module used to own its
        // own copy of the pivot rule, the 0.5 threshold and this sentence, and
        // `avatar.loadClip` owned a second one; a user who tried the same file
        // in the page and in the scene got two different stories. Both routes
        // now call rigsignature.h, so the refusal is reproducible from the
        // shared code alone — character for character, count for count.
        {
            QSet<QString> rigNames;
            std::function<void(const iris::SceneNodePtr &)> collect =
                [&](const iris::SceneNodePtr &n) {
                    if (!n) return;
                    rigNames.insert(n->getName());
                    for (int i = 0; i < n->childCount(); ++i)
                        if (auto *c = n->childAt(i)) collect(c->sharedFromThis());
                };
            collect(cross.fragment());

            Assimp::Importer importer;
            const aiScene *scene = importer.ReadFile(kMismatchAnim.toStdString(),
                                                     iris::ImportFlags::ClipNamesOnly);
            CHECK(scene != nullptr, "T2: the mismatch fixture parses");
            const auto anims = iris::Mesh::extractAnimations(scene, kMismatchAnim);
            const QVector<rig::ClipScore> scored = rig::scoreClips(anims, rigNames);
            const int best = rig::bestClip(scored);
            CHECK(best >= 0, "T2: the shared matcher scored the same file");
            CHECK(scored[best].matched == mismatch.matched
                      && scored[best].boneChannels == mismatch.boneChannels,
                  "T2: the shared matcher's ratio is the module's ratio");
            CHECK(scored[best].ratio < rig::kRigMatchThreshold,
                  "T2: ... and it is below the ONE threshold");
            CHECK(mismatchError
                      == rig::mismatchMessage(scored[best], QFileInfo(kMismatchAnim).fileName(),
                                              cross.name()),
                  "T2: the module's refusal IS rig::mismatchMessage, word for word");
            CHECK(scored[best].unmatched.contains("hips"),
                  "T2: ... and the unmatched bone names come from the shared scorer");
        }

        // T2b — the shared naming rule. `displayNameFor` is what makes
        // "Walking.fbx" bind the walk role, and both routes call it.
        CHECK(rig::isJunkClipName("mixamo.com") && rig::isJunkClipName("Take 001")
                  && rig::isJunkClipName("Motion") && !rig::isJunkClipName("Idle"),
              "T2b: the junk-name set (Mixamo, the FBX SDK takes, assimp's BVH 'Motion')");
        CHECK(rig::displayNameFor("mixamo.com", "Walking") == "Walking"
                  && rig::displayNameFor("Idle", "Walking") == "Idle"
                  && rig::displayNameFor("mixamo.com", QString()) == "Clip",
              "T2b: a junk name becomes the file's base name, a real one survives");
        CHECK(rig::isPivotChannel("mixamorig:Hips_$AssimpFbx$_Rotation")
                  && !rig::isPivotChannel("mixamorig:Hips"),
              "T2b: the assimp FBX pivot rule");
        CHECK(avatar::AvatarPreviewModel::displayNameFor("mixamo.com", "Walking")
                  == rig::displayNameFor("mixamo.com", "Walking"),
              "T2b: the module's display-name entry point forwards to the one rule");

        // --- X6: the error cases a script can hit ---
        avatar::AvatarPreviewModel empty;
        QString emptyError;
        CHECK(!empty.loadAnimation(kWalkAnim, &emptyError) && !emptyError.isEmpty(),
              "X6: loading an animation with no character loaded fails with a message");
        QString missingError;
        CHECK(!cross.loadAnimation(kRig + ".nope", &missingError) && missingError.contains("no such file"),
              "X6: a missing file fails with a message");
        QString noAnimError;
        CHECK(!cross.loadAnimation(kProp, &noAnimError) && noAnimError.contains("no animation"),
              "X6: a file with no clips at all fails with a message");
    }

    // ================= B — MOCAP (.bvh), the headline of the format lane ==
    // A .bvh is animation-only BY CONSTRUCTION: the format is a joint hierarchy
    // and a table of per-frame channel values, with no geometry in it at all.
    // That makes it the purest test of the cross-file clip path — and the one
    // real-world format the Avatar module's Load Animation dialog exists for
    // (every mocap library on earth ships .bvh).
    //
    // Its importer is only present because irisgl/CMakeLists.txt names
    // ASSIMP_BUILD_BVH_IMPORTER in the allowlist (ALL_IMPORTERS_BY_DEFAULT is
    // OFF): drop that option and B1 goes red instead of the app going quietly
    // deaf to .bvh.
    {
        avatar::AvatarPreviewModel mocap;
        CHECK(mocap.load(kRig), "B: the character loads");
        const int ownClips = mocap.clips().size();

        // --- B0: what assimp actually hands over for a .bvh ---------------
        // NOT zero meshes, despite the format having no geometry: BVHLoader
        // runs SkeletonMeshBuilder and synthesises a stick-figure mesh over the
        // joint hierarchy (AI_CONFIG_IMPORT_NO_SKELETON_MESHES would suppress
        // it; nothing in this tree sets it). loadAnimation never looks at
        // meshes so it is harmless there — but it is exactly why .bvh must
        // stay OUT of Constants::MODEL_EXTS: importing one as a model would
        // put that stick figure in the asset library as a real object.
        {
            Assimp::Importer importer;
            const aiScene *scene = importer.ReadFile(kBvhWalk.toStdString().c_str(), 0);
            if (!scene) std::printf("    assimp: %s\n", importer.GetErrorString());
            CHECK(scene != nullptr, "B0: the BVH importer is compiled in (ASSIMP_BUILD_BVH_IMPORTER)");
            if (scene) {
                CHECK(scene->mNumAnimations == 1, "B0: one clip");
                CHECK(scene->mNumMeshes == 1,
                      "B0: assimp SYNTHESISES a stick-figure mesh for a .bvh "
                      "(why bvh is not a MODEL_EXT)");
                CHECK(QString(scene->mAnimations[0]->mName.C_Str()) == "Motion",
                      "B0: every BVH clip is hard-coded 'Motion' (BVHLoader::CreateAnimation)");
                // Channel names are the ROOT/JOINT names verbatim; `End Site`
                // blocks become EndSite_<parent> nodes and carry no channels,
                // so they never reach the rig-match score.
                CHECK(scene->mAnimations[0]->mNumChannels == 2,
                      "B0: two channels — the two JOINTs, not the End Site");
                QSet<QString> channels;
                for (unsigned c = 0; c < scene->mAnimations[0]->mNumChannels; ++c)
                    channels.insert(QString(scene->mAnimations[0]->mChannels[c]->mNodeName.C_Str()));
                CHECK(channels.contains("jointRoot") && channels.contains("jointTip"),
                      "B0: ... named exactly like the loaded rig's scene nodes");
                // Ticks are FRAMES here: tps = 1/FrameTime, duration = Frames-1.
                CHECK(std::fabs(scene->mAnimations[0]->mTicksPerSecond - 2.0) < 1e-6,
                      "B0: mTicksPerSecond = 1 / Frame Time (2.0 for 0.5 s frames)");
            }
        }

        // --- B1: the .bvh loads onto the character ------------------------
        QString error;
        avatar::ClipLoadReport report;
        const bool added = mocap.loadAnimation(kBvhWalk, &error, &report);
        if (!added) std::printf("    %s\n", qUtf8Printable(error));
        CHECK(added, "B1: a mocap .bvh loads onto the character through avatar.loadAnimation");
        CHECK(report.added == 1 && report.matched == 2 && report.boneChannels == 2,
              "B1: one clip added, BOTH its bone channels matched the rig");
        CHECK(mocap.clips().size() == ownClips + 1, "B1: the clip list accumulates");

        // --- B2: the clip is named after the FILE, not "Motion" -----------
        const auto all = mocap.clips();
        const auto &clip = all.last();
        CHECK(clip.rawName == "Motion", "B2: the raw name is kept verbatim");
        CHECK(clip.name == QFileInfo(kBvhWalk).completeBaseName(),
              "B2: 'Motion' is junk (EVERY bvh clip is called that) so the row "
              "shows the animation file's base name");
        CHECK(clip.external && clip.source == QFileInfo(kBvhWalk).absoluteFilePath(),
              "B2: the clip records the .bvh it came from");
        CHECK(std::fabs(clip.length - 1.0f) < 0.01f,
              "B2: 3 frames at 0.5 s is a 1.0 s clip (frames -> seconds via mTicksPerSecond)");

        // --- B3: and it MOVES the rig's bones ------------------------------
        // Clip translation, the same assertion X3 makes for the glTF case: the
        // engine is what poses bones now, so document-side the sharp statement
        // is that the mocap clip becomes bone tracks with motion in them.
        CHECK(mocap.setClip(clip.name), "B3: the mocap clip is selectable by name");
        {
            auto fragment = mocap.fragment();
            auto skinned = findSkinnedNode(fragment);
            auto skel = findSkeleton(fragment);
            iris::AnimationPtr last;
            for (const auto &a : fragment->getAnimations())
                if (!a.isNull() && a->hasSkeletalAnimation()) last = a;
            iris::ExtractedClip extracted;
            QString err;
            const bool ok = !last.isNull() &&
                            iris::ClipExtractor::extract(fragment, skinned, skel,
                                                         last->getSkeletalAnimation(), clip.name,
                                                         last->getLength(), nullptr, extracted, &err);
            if (!ok) std::printf("    %s\n", qUtf8Printable(err));
            CHECK(ok, "B3: the mocap clip translates into bone tracks");
            std::printf("    driven bones: %d, keys %d -> %d\n", extracted.tracks.size(),
                        extracted.sourceKeyCount, extracted.emittedKeyCount);
            CHECK(extracted.tracks.size() >= 1, "B3: at least one bone is driven");
            bool moves = false;
            for (const auto &t : extracted.tracks)
                if (t.keys.size() >= 2 && t.keys.first().rotation != t.keys.last().rotation)
                    moves = true;
            CHECK(moves, "B3: and a track really moves between its first and last key");
        }

        // --- B4: a foreign-rig .bvh is refused, by name --------------------
        // The honest limit of BVH support: the join is by joint NAME, and a
        // real Mixamo character's joints are prefixed "mixamorig:" while a BVH
        // of the same motion usually is not. Matching names are the user's job
        // (rename in the mocap tool); what the module owes them is a refusal
        // that says so, not a clip that silently moves nothing.
        QString mismatchError;
        avatar::ClipLoadReport mismatch;
        const int before = mocap.clips().size();
        CHECK(!mocap.loadAnimation(kBvhMismatch, &mismatchError, &mismatch),
              "B4: a .bvh animating a different rig is refused, not silently loaded");
        std::printf("    refusal: %s\n", qUtf8Printable(mismatchError));
        CHECK(mismatchError.contains("hips"), "B4: ... and the message names the unmatched joint");
        CHECK(mocap.clips().size() == before, "B4: ... and nothing was added");
        CHECK(mismatch.matched == 0 && mismatch.boneChannels == 2, "B4: ... 0 of 2 joints matched");
    }

    // ================= X7 — root motion (walk in place vs as authored) ====
    {
        avatar::AvatarPreviewModel rm;
        CHECK(rm.load(kRig), "X7: character loaded");
        CHECK(!rm.rootMotion(), "X7: root motion is OFF by default (locomotion plays in place)");
        // rig2.glb's clips are rotation-only, so the stripping must be a no-op
        // on them: the pose at t=0.5 has to be identical either way.
        rm.setClip("Idle");
        rm.setTime(0.5f);
        const iris::Vec3 inPlace = rm.bones()[1].position;
        rm.setRootMotion(true);
        CHECK(rm.rootMotion(), "X7: root motion toggles on");
        rm.setTime(0.5f);
        const iris::Vec3 authored = rm.bones()[1].position;
        CHECK((authored - inPlace).length() < 1e-4f,
              "X7: a rotation-only clip is unaffected by the root-motion policy");
        rm.setRootMotion(false);
        rm.setTime(0.5f);
        CHECK((rm.bones()[1].position - inPlace).length() < 1e-4f,
              "X7: toggling back restores the same pose (the clip is rebuilt, not consumed)");
    }

    // ================= X8 — the session list the left column shows ========
    {
        avatar::AvatarPreviewModel hist;
        CHECK(hist.history().isEmpty(), "X8: the session list starts empty");
        hist.load(kRig);
        CHECK(hist.history().size() == 1 && hist.history().first() == QFileInfo(kRig).absoluteFilePath(),
              "X8: a load records the file");
        hist.load(kRig);
        CHECK(hist.history().size() == 1, "X8: re-loading the same file does not duplicate the row");
        CHECK(!hist.forget(kProp), "X8: forgetting a file that is not listed fails");
        CHECK(hist.forget(kRig), "X8: forget drops the row");
        CHECK(hist.history().isEmpty() && !hist.isLoaded(),
              "X8: ... and clears the preview when it was the loaded file");
    }

    // ================= H — HEIGHT NORMALIZATION (the 2026-09-08 defect) ====
    //
    // "Dreyar is massively huge, head touches the ceiling" (owner). Two
    // separate faults: FBX unit scale (fixed at the importer,
    // tests/importer/importer.fbx section 2) and a package whose unit
    // DECLARATION is wrong, which no importer can fix — Dreyar says
    // centimetres and is authored in millimetres, so he still arrives 17.25 m
    // tall with clips to match. The module normalizes the SUBJECT.
    //
    // The fixtures are rig2.glb rewritten at two implausible sizes
    // (fixtures/make_scaled_rigs.py), so this needs no third-party content and
    // states the rule as arithmetic.
    {
        const QString kGiant = QStringLiteral(JAHSHAKA_TEST_SOURCE_DIR "/tests/avatar/fixtures/rig2_giant.glb");
        const QString kTiny = QStringLiteral(JAHSHAKA_TEST_SOURCE_DIR "/tests/avatar/fixtures/rig2_tiny.glb");
        const auto close = [](float a, float b, float tol) { return std::fabs(a - b) <= tol; };

        // H1 — a PLAUSIBLE character is left exactly as authored. rig2.glb is
        // the 2 m arm every other section here uses: no scale node, no log
        // line, and the readback says so.
        {
            avatar::AvatarPreviewModel m;
            CHECK(m.load(kRig), "H1: the 2 m rig loads");
            const auto &n = m.normalization();
            CHECK(!n.applied && close(n.factor, 1.0f, 1e-6f),
                  "H1: a 2 m character is NOT normalized");
            CHECK(close(n.sourceHeight, 2.0f, 1e-3f) && close(n.height, 2.0f, 1e-3f),
                  "H1: ... and measures 2 m before and after");
            const iris::Vec3 scale = m.fragment()->getLocalScale();
            CHECK(close(scale.x(), 1.0f, 1e-6f) && close(scale.y(), 1.0f, 1e-6f),
                  "H1: ... with its authored root scale untouched");
        }

        // H2 — THE DREYAR CASE: 17.25 m in, 1.75 m out, factor reported.
        {
            avatar::AvatarPreviewModel m;
            CHECK(m.load(kGiant), "H2: the 17.25 m rig loads");
            const auto &n = m.normalization();
            CHECK(n.applied, "H2: a 17.25 m character IS normalized");
            CHECK(close(n.sourceHeight, 17.25f, 0.01f),
                  qUtf8Printable(QStringLiteral("H2: source height read as %1 m (expected 17.25)")
                                     .arg(double(n.sourceHeight))));
            CHECK(close(n.height, 1.75f, 0.01f),
                  qUtf8Printable(QStringLiteral("H2: normalized to %1 m (expected 1.75)")
                                     .arg(double(n.height))));
            CHECK(close(n.factor, 1.75f / 17.25f, 1e-4f),
                  qUtf8Printable(QStringLiteral("H2: factor %1 (expected 0.1014)")
                                     .arg(double(n.factor))));
            CHECK(close(avatar::measureCharacterHeight(m.fragment()), 1.75f, 0.01f),
                  "H2: ... and the geometry really is 1.75 m now");
            // THE RIG MOVED WITH IT. jointTip binds one unit above jointRoot in
            // rig2.glb, i.e. 8.625 units in the giant: after normalization its
            // WORLD position must be 8.625 * 0.1014 = 0.875 m, which is what
            // makes the clips (authored at the file's scale) still fit.
            const auto bones = m.bones();
            CHECK(bones.size() == 2, "H2: both bones survive normalization");
            if (bones.size() == 2)
                CHECK(close(bones[1].position.y(), 0.875f, 0.01f),
                      qUtf8Printable(QStringLiteral("H2: jointTip sits at y=%1 m (expected 0.875) "
                                                    "— the rig scaled with the mesh")
                                         .arg(double(bones[1].position.y()))));
        }

        // H3 — the other end: a 10 cm rig is scaled UP.
        {
            avatar::AvatarPreviewModel m;
            CHECK(m.load(kTiny), "H3: the 0.10 m rig loads");
            const auto &n = m.normalization();
            CHECK(n.applied && close(n.height, 1.75f, 0.01f) && n.factor > 1.0f,
                  qUtf8Printable(QStringLiteral("H3: 0.10 m normalized UP to %1 m (x%2)")
                                     .arg(double(n.height)).arg(double(n.factor))));
        }

        // H4 — the explicit override (the owner's "he really is an ogre" case),
        // and H5 — going back to AUTO. The record stays anchored to the FILE.
        {
            avatar::AvatarPreviewModel m;
            m.load(kGiant);
            QString error;
            CHECK(m.setCharacterHeight(2.4f, &error),
                  qUtf8Printable(QStringLiteral("H4: setCharacterHeight(2.4) accepted (%1)").arg(error)));
            CHECK(close(avatar::measureCharacterHeight(m.fragment()), 2.4f, 0.01f),
                  "H4: ... and the subject measures 2.4 m");
            CHECK(close(m.normalization().sourceHeight, 17.25f, 0.01f) &&
                      close(m.normalization().factor, 2.4f / 17.25f, 1e-3f),
                  "H4: ... with the report still anchored to the file's 17.25 m");
            CHECK(m.setCharacterHeight(0.0f, &error), "H5: setCharacterHeight(0) re-runs AUTO");
            CHECK(close(avatar::measureCharacterHeight(m.fragment()), 1.75f, 0.01f),
                  "H5: ... back to 1.75 m");
            CHECK(close(m.normalization().factor, 1.75f / 17.25f, 1e-3f) &&
                      !m.normalization().explicitTarget,
                  "H5: ... with the file-anchored factor, and no longer flagged explicit");
            CHECK(!m.setCharacterHeight(100000.0f, &error),
                  "H5: a height that is not a length is refused");
        }

        // H5b — the reset is against the FILE, so a PLAUSIBLE character comes
        // back to the size it was authored at, not to 1.75. (The rule reads
        // the file's height; it does not invent a target for a file that never
        // needed one.)
        {
            avatar::AvatarPreviewModel m;
            m.load(kRig);
            QString error;
            CHECK(m.setCharacterHeight(2.5f, &error), "H5b: override the 2 m rig to 2.5 m");
            CHECK(close(avatar::measureCharacterHeight(m.fragment()), 2.5f, 0.01f),
                  "H5b: ... it measures 2.5 m");
            CHECK(m.setCharacterHeight(0.0f, &error), "H5b: reset to automatic");
            CHECK(close(avatar::measureCharacterHeight(m.fragment()), 2.0f, 0.01f),
                  "H5b: ... back to the AUTHORED 2 m, not to 1.75");
            CHECK(!m.normalization().applied && close(m.normalization().factor, 1.0f, 1e-3f),
                  "H5b: ... and the record says nothing is applied any more");
        }

        // H6 — THE OWNER'S ORACLE, from the debug-runner's REPRO script: the
        // head must be BELOW the room's ceiling. Pre-fix Dreyar wanted a room
        // scale of 943 and got the 400 clamp, so his 1604-unit head stood
        // above a 1604-unit ceiling. The room scale is tracked even with no
        // room built (no qrc mesh in this suite), which is what lets the gate
        // be arithmetic instead of a screenshot.
        for (const QString &path : { kRig, kGiant, kTiny }) {
            avatar::AvatarPreviewModel m;
            m.load(path);
            const float head = avatar::measureCharacterHeight(m.fragment());
            const float ceiling = m.ceilingHeight();
            CHECK(head < ceiling,
                  qUtf8Printable(QStringLiteral("H6: %1 — head %2 m, ceiling %3 m (clearance %4 m)")
                                     .arg(QFileInfo(path).fileName())
                                     .arg(double(head), 0, 'f', 3)
                                     .arg(double(ceiling), 0, 'f', 3)
                                     .arg(double(ceiling - head), 0, 'f', 3)));
            CHECK(close(m.roomScale(), head / 1.75f, 1e-3f),
                  "H6: ... the room scale is the subject's height over the design height");
        }

        // H7 — a file with no geometry (an animation-only .bvh) is not
        // normalizable and must be left alone rather than guessed at.
        {
            avatar::AvatarPreviewModel m;
            m.load(kRig);
            const float before = avatar::measureCharacterHeight(m.fragment());
            CHECK(m.loadAnimation(kBvhWalk), "H7: a mocap clip loads onto the character");
            CHECK(close(avatar::measureCharacterHeight(m.fragment()), before, 1e-4f),
                  "H7: ... and loading a clip never rescales the subject");
        }
    }

    // --- clear ---
    const QString scratch = model.extractDir();
    model.clear();
    CHECK(!model.isLoaded() && model.bones().isEmpty() && model.clips().isEmpty(),
          "clear: the subject and its rig are gone");
    CHECK(scratch.isEmpty() || !QFileInfo::exists(scratch),
          "clear: the scratch extract dir is removed (never beside the source file)");

    std::printf(failures ? "\n%d FAILURES\n" : "\nall document checks passed\n", failures);
    return failures ? 1 : 0;
}
