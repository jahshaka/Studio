// Avatar Part 0, the document half — no engine, no window (QT_QPA_PLATFORM=offscreen).
//
//   ITEM ZERO (Z1/Z2): a skinned SINGLE-MESH file must get bone scene nodes and
//   a moving pose. Before AVATAR_MODULE_SPEC's fix that was 0 of N bones for
//   every Mixamo character in existence, silently.
//   The preview model (S1-S3b): clips and their display names, transport,
//   the two independent toggles, and the bone tree the overlay draws.
//
// Everything here is what the `avatar` verbs call, which is why the verb
// surface is testable with no engine at all.
#include <QGuiApplication>
#include <QFileInfo>
#include <QMatrix4x4>
#include <QSet>
#include <QTemporaryDir>
#include <QVector3D>
#include <cmath>
#include <cstdio>

#include "irisgl/irisglfwd.h"
#include "irisgl/document/animation/animation.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/assets/skeleton.h"
#include "irisgl/document/materials/defaultmaterial.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "modules/avatar/avatarpreviewmodel.h"

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

static iris::SkeletonPtr findSkeleton(const iris::SceneNodePtr &node)
{
    if (node->getSceneNodeType() == iris::SceneNodeType::Mesh) {
        auto mesh = node.staticCast<iris::MeshNode>()->getMesh();
        if (mesh && mesh->hasSkeleton()) return mesh->getSkeleton();
    }
    for (const auto &child : node->children)
        if (auto s = findSkeleton(child)) return s;
    return iris::SkeletonPtr();
}

static int countNodes(const iris::SceneNodePtr &node)
{
    int n = 1;
    for (const auto &child : node->children) n += countNodes(child);
    return n;
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

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
        CHECK(!skel.isNull() && skel->boneTransforms.size() == 2, "Z1: the skeleton reached the mesh");

        model.setClip("Idle");
        model.setTime(0.0f);
        const QVector<QMatrix4x4> bind = skel->boneTransforms;
        int nonIdentityBind = 0;
        for (const auto &m : bind) if (!m.isIdentity()) ++nonIdentityBind;

        model.setTime(0.5f);
        int nonIdentity = 0;
        for (const auto &m : skel->boneTransforms) if (!m.isIdentity()) ++nonIdentity;
        std::printf("    non-identity skin matrices: t=0 %d/2, t=0.5 %d/2\n", nonIdentityBind, nonIdentity);
        // THE assertion the audit's failing probe becomes: pre-fix this was 0.
        CHECK(nonIdentity >= 1, "Z1: at least one skin matrix is non-identity at t > 0");
        CHECK(skel->boneTransforms != bind, "Z1: the pose actually changed between t=0 and t=0.5");
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
            CHECK(prop->children.isEmpty(), "Z2: ... with no child nodes");
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

    // --- S2: setTime moves the pose, and is exactly reversible ---
    CHECK(model.setClip("Idle"), "S2: 'Idle' selected");
    model.setTime(0.0f);
    const QVector3D tip0 = model.bones()[1].position;
    model.setTime(0.5f);
    const QVector3D tipHalf = model.bones()[1].position;
    std::printf("    jointTip: t=0 (%.3f, %.3f, %.3f)  t=0.5 (%.3f, %.3f, %.3f)\n",
                tip0.x(), tip0.y(), tip0.z(), tipHalf.x(), tipHalf.y(), tipHalf.z());
    CHECK((tipHalf - tip0).length() > 0.05f, "S2: the tip bone moved between t=0 and t=0.5");
    model.setTime(0.0f);
    CHECK((model.bones()[1].position - tip0).length() < 1e-5f,
          "S2: 0 -> 0.5 -> 0 restores the t=0 pose exactly");

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
            const QVector3D along = (s.to - s.from).normalized();
            CHECK(s.toIsLeaf, "S3c: the tip bone is reported as a LEAF (nothing hangs off it)");
            CHECK(qAbs(s.toAxis.length() - 1.0f) < 1e-3f,
                  "S3c: the leaf's bone axis comes back normalized");
            CHECK(QVector3D::dotProduct(s.toAxis, along) > 0.99f,
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

        // --- X3: switching to it MOVES BONES, differently from the own clip -
        cross.setClip("Idle");
        cross.setTime(0.5f);
        const QVector3D idleTip = cross.bones()[1].position;
        CHECK(cross.setClip(added_clip.name), "X3: the cross-file clip is selectable by name");
        CHECK(cross.activeClip() == added_clip.name, "X3: ... and becomes the active clip");
        CHECK(cross.time() == 0.0f, "X3: ... rewound to 0");
        const QVector3D restTip = cross.bones()[1].position;
        // MID-clip, deliberately: the cross-file clip is 0.5 s and looping, so
        // sampling it at exactly 0.5 wraps to 0 (Animation::getSampleTime is
        // an fmod) and would read as "the clip does nothing".
        cross.setTime(0.25f);
        const QVector3D crossTip = cross.bones()[1].position;
        std::printf("    jointTip: Idle@0.5 (%.3f, %.3f, %.3f)  cross@0 (%.3f, %.3f, %.3f)  "
                    "cross@0.5 (%.3f, %.3f, %.3f)\n",
                    idleTip.x(), idleTip.y(), idleTip.z(), restTip.x(), restTip.y(), restTip.z(),
                    crossTip.x(), crossTip.y(), crossTip.z());
        CHECK((crossTip - restTip).length() > 0.05f,
              "X3: the foreign clip MOVES the rig's bones over time (non-identity pose)");
        CHECK((crossTip - idleTip).length() > 0.05f,
              "X3: ... to a different pose than the character's own clip reaches");

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

    // ================= X7 — root motion (walk in place vs as authored) ====
    {
        avatar::AvatarPreviewModel rm;
        CHECK(rm.load(kRig), "X7: character loaded");
        CHECK(!rm.rootMotion(), "X7: root motion is OFF by default (locomotion plays in place)");
        // rig2.glb's clips are rotation-only, so the stripping must be a no-op
        // on them: the pose at t=0.5 has to be identical either way.
        rm.setClip("Idle");
        rm.setTime(0.5f);
        const QVector3D inPlace = rm.bones()[1].position;
        rm.setRootMotion(true);
        CHECK(rm.rootMotion(), "X7: root motion toggles on");
        rm.setTime(0.5f);
        const QVector3D authored = rm.bones()[1].position;
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
