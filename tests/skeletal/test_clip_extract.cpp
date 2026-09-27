// ANIMATION_ENGINE_MIGRATION_SPEC M0 / gates G1 + G4(extract half).
//
// THE ONE PLACE THIS PROGRAM FAILS IF IT FAILS (§9 R1). Our clip keys are
// absolute local TRS of a SCENE NODE; an engine skeleton has only real bones;
// and in a pivot-preserving FBX a bone's motion is spread over its
// `$AssimpFbx$` pivot ancestors. A per-key rename of BoneAnimation to a bone
// track therefore passes every glTF fixture in the tree and produces a FROZEN
// character on every Mixamo FBX — invisibly.
//
// So this suite proves the "compose then resample" extractor (§3.1) against the
// pose COMPOSED FROM THE FIXTURE'S OWN KEYS, on both a glTF rig (no pivots) and
// the tree's first FBX fixture (five pivot nodes between two bones, every clip
// channel on a pivot node and none on a bone). Document-only: no engine, no window.
//
// THE ORACLE IS A BEHAVIOUR, NOT A RECORDING (lane D6B-GATE-SHAPE; the suites
// audit §12 item 10). It used to be fixtures/golden_document_poses.txt, a frozen
// recording of the document clip evaluator this program deleted — a bar nobody
// could re-derive. composedPose() below states the rule instead: every node of
// the fragment posed at t (its clip channel if the clip animates it, its authored
// rest otherwise), composed root-down into fragment space, and each bone read in
// its rig parent's frame. It shares no code with the extractor's chain walk (no
// common-prefix cancellation, no key-time union), and before the recording was
// deleted it reproduced every recorded sample (spikes/d6b-gate-shape).
#include "irisgl/core/math/mat4.h"
#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include <QGuiApplication>
#include <QHash>
#include <QTemporaryDir>
#include <QSet>
#include <QVector>
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "assimp/Importer.hpp"
#include "assimp/scene.h"
#include "irisgl/core/math/trs.h"
#include "irisgl/document/animation/animation.h"
#include "irisgl/document/animation/clipextractor.h"
#include "irisgl/document/animation/keyframeanimation.h"
#include "irisgl/document/animation/skeletalanimation.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/assets/skeleton.h"
#include "irisgl/document/materials/defaultmaterial.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/import/importflags.h"

#include "../support/documentgraph.h"
static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

static const QString kGlbRig =
    QStringLiteral(JAHSHAKA_TEST_SOURCE_DIR "/tests/avatar/fixtures/rig2.glb");
static const QString kFbxRig =
    QStringLiteral(JAHSHAKA_TEST_SOURCE_DIR "/tests/skeletal/fixtures/pivot_rig.fbx");
/// The Mixamo CHARACTER clip in its real shape (make_pivot_fbx.py --mixamo-tpose):
/// two identical keys one frame apart at 30 fps.
static const QString kFbxTpose =
    QStringLiteral(JAHSHAKA_TEST_SOURCE_DIR "/tests/skeletal/fixtures/mixamo_tpose.fbx");

struct Trs { iris::Vec3 pos; iris::Quat rot; iris::Vec3 scale; };

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

static void collectNames(const iris::SceneNodePtr &n, QStringList &out)
{
    out.append(n->name);
    for (const auto &c : n->children()) collectNames(c, out);
}

/// Rotations are compared through the matrix they build: q and -q are the same
/// rotation, and a component-wise compare would call them a failure.
static float trsError(const Trs &a, const Trs &b)
{
    const iris::Mat4 ma = iris::composeTRS(a.pos, a.rot, a.scale);
    const iris::Mat4 mb = iris::composeTRS(b.pos, b.rot, b.scale);
    float worst = 0.0f;
    for (int i = 0; i < 16; ++i)
        worst = std::max(worst, std::fabs(ma.constData()[i] - mb.constData()[i]));
    return worst;
}

/// Sampling an extracted track the way an engine v1 track does: linear on
/// position and scale, shortest-arc nlerp on rotation, HELD outside the key
/// range (the terminal key the extractor pins at the clip length is what makes
/// "held" and Ogre's "wrap" agree — §9 R4).
static Trs sampleTrack(const iris::ClipBoneTrack &track, float t)
{
    Trs out;
    if (track.keys.isEmpty()) return out;
    if (t <= track.keys.first().time) {
        const auto &k = track.keys.first();
        return Trs{ k.position, k.rotation, k.scale };
    }
    if (t >= track.keys.last().time) {
        const auto &k = track.keys.last();
        return Trs{ k.position, k.rotation, k.scale };
    }
    for (int i = 1; i < track.keys.size(); ++i) {
        if (track.keys[i].time < t) continue;
        const auto &a = track.keys[i - 1];
        const auto &b = track.keys[i];
        const float span = b.time - a.time;
        const float u = span > 0.0f ? (t - a.time) / span : 0.0f;
        out.pos = a.position + (b.position - a.position) * u;
        out.scale = a.scale + (b.scale - a.scale) * u;
        out.rot = iris::Quat::nlerp(a.rotation, b.rotation, u);
        return out;
    }
    return out;
}

// ---------------------------------------------------------------------------

struct Loaded
{
    iris::ScenePtr      doc;
    iris::SceneNodePtr  fragment;
    iris::MeshNodePtr   mesh;
    iris::SceneNodePtr  host;
    /// Captured ONCE, before anything is evaluated. The document evaluator
    /// leaves the nodes it moved where it left them, so a rest pose taken after
    /// a clip has played is not the rest pose — the Avatar page's
    /// snapshot/restore hack exists for exactly this reason.
    iris::ClipExtractor::RestPose rest;
};

static bool load(const QString &path, const QString &extractDir, Loaded &out)
{
    auto node = iris::MeshNode::loadAsSceneFragment(path, makeMat, nullptr, nullptr, extractDir);
    if (node.isNull()) return false;
    out.doc = iris::Scene::create();
    out.doc->getRootNode()->addChild(node);
    out.fragment = node;
    out.mesh = findSkinned(node);
    out.host = findClipHost(node);
    out.rest = iris::ClipExtractor::captureRest(node);
    return !out.mesh.isNull() && !out.host.isNull();
}

/// THE ORACLE: the rig's bone-parent-local pose at time t, composed from the
/// fixture's own keys. Every node of the fragment takes its clip channel at t (the
/// document's KeyFrame evaluation — the keys ARE the authored motion) or, with no
/// channel, its authored rest local; the locals are composed ROOT-DOWN into a
/// fragment-space matrix per node; a bone's pose is then its node's matrix in the
/// frame of its rig parent's node (the mesh node for a root bone — the frame
/// SceneMirror::toSkeletonDesc authors the bind in). `complete` goes false when a
/// bone has no scene node to pose.
static QVector<Trs> composedPose(const Loaded &f, const iris::SkeletalAnimationPtr &clip,
                                 float t, bool &complete)
{
    QHash<const iris::SceneNode *, iris::Mat4> world;
    QHash<QString, const iris::SceneNode *> byName;
    struct Item { iris::SceneNode *node; iris::Mat4 parent; };
    iris::Mat4 identity;
    identity.setToIdentity();
    QVector<Item> stack{ Item{ f.fragment.data(), identity } };
    while (!stack.isEmpty()) {
        const Item it = stack.takeLast();
        iris::Mat4 local;
        const auto ch = clip->boneAnimations.constFind(it.node->name);
        if (ch != clip->boneAnimations.constEnd() && !ch.value().isNull()) {
            local = iris::composeTRS(ch.value()->posKeys->getValueAt(t),
                                     ch.value()->rotKeys->getValueAt(t).normalized(),
                                     ch.value()->scaleKeys->getValueAt(t));
        } else {
            const auto r = f.rest.constFind(it.node);
            local = r != f.rest.constEnd() ? iris::composeTRS(r->pos, r->rot, r->scale)
                                           : iris::composeTRS(it.node->getLocalPos(),
                                                              it.node->getLocalRot(),
                                                              it.node->getLocalScale());
        }
        const iris::Mat4 m = it.parent * local;
        world.insert(it.node, m);
        byName.insert(it.node->name, it.node);
        for (int c = 0; c < it.node->childCount(); ++c)
            if (iris::SceneNode *child = it.node->childAt(c)) stack.append(Item{ child, m });
    }
    const auto &bones = f.mesh->getSkeleton()->bones;
    QVector<Trs> pose(bones.size());
    for (int b = 0; b < bones.size(); ++b) {
        const iris::SceneNode *boneNode = byName.value(bones[b]->name, nullptr);
        const iris::SceneNode *frameNode = f.mesh.data();
        if (!bones[b]->parentBone.isNull())
            frameNode = byName.value(bones[b]->parent()->name, f.mesh.data());
        if (!boneNode || !world.contains(boneNode) || !world.contains(frameNode)) {
            complete = false;
            continue;
        }
        const iris::Mat4 local = world.value(frameNode).inverted() * world.value(boneNode);
        iris::decomposeTRS(local, pose[b].pos, pose[b].rot, pose[b].scale);
    }
    return pose;
}

/// The whole gate for one (rig, clip): the extractor's tracks reproduce the pose
/// composed from the clip's own keys EXACTLY at every key time the extractor
/// emitted, and within the resampling tolerance in between.
static void gateClip(Loaded &f, const iris::AnimationPtr &anim,
                     const char *label, float exactTol, float betweenTol)
{
    f.fragment->setAnimation(anim);
    // The extractor pins a terminal key AT the length (R4). The oracle evaluates the
    // keys at t itself, unwrapped, so that key is compared against the clip's END
    // pose (a looping evaluator's fmod(t, length) would answer for t = 0).
    const bool wasLooping = anim->getLooping();
    anim->setLooping(false);
    iris::ExtractedClip clip;
    QString error;
    const bool ok = iris::ClipExtractor::extract(f.fragment, f.mesh, f.mesh->getSkeleton(),
                                                 anim->getSkeletalAnimation(), anim->getName(),
                                                 anim->getLength(), &f.rest, clip, &error);
    std::printf("  [%s] length=%.4f tracks=%d keys %d -> %d%s\n", label, double(clip.length),
                clip.tracks.size(), clip.sourceKeyCount, clip.emittedKeyCount,
                ok ? "" : (" ERROR: " + error).toUtf8().constData());
    CHECK(ok, (QString("[%1] the clip extracts").arg(label)).toUtf8().constData());
    if (!ok) { anim->setLooping(wasLooping); return; }
    CHECK(!clip.tracks.isEmpty(),
          (QString("[%1] at least one bone is driven — a naive per-bone-channel "
                   "translation yields none on a pivot rig").arg(label)).toUtf8().constData());
    CHECK(clip.restDiffersFromBind.isEmpty(),
          (QString("[%1] every bone's authored rest local equals its bind local "
                   "(an untracked bone would land elsewhere engine-side)").arg(label)).toUtf8().constData());

    // Every track is sorted, strictly increasing, and spans [0, length] — the
    // host-side validation §9 R11 demands, because the engine's own asserts are
    // Debug-only and Release is a shipping configuration now.
    bool wellFormed = true;
    for (const auto &track : clip.tracks) {
        if (track.keys.isEmpty()) { wellFormed = false; break; }
        if (std::fabs(track.keys.first().time) > 1e-6f) wellFormed = false;
        if (clip.length > 0.0f && std::fabs(track.keys.last().time - clip.length) > 1e-4f)
            wellFormed = false;
        for (int i = 1; i < track.keys.size(); ++i)
            if (!(track.keys[i].time > track.keys[i - 1].time)) wellFormed = false;
    }
    CHECK(wellFormed, (QString("[%1] tracks are sorted, strictly increasing, and pinned "
                               "at 0 and at the clip length (R4)").arg(label)).toUtf8().constData());

    // THE ORACLE: the bone-parent-local pose composed from the clip's own keys.
    bool oracleComplete = true;
    const iris::SkeletalAnimationPtr keys = anim->getSkeletalAnimation();
    const auto oracleAt = [&](float t) { return composedPose(f, keys, t, oracleComplete); };

    // ---- G1: EXACT at every emitted key time ------------------------------
    float worstAtKeys = 0.0f;
    int   samplesAtKeys = 0;
    for (const auto &track : clip.tracks) {
        for (const auto &key : track.keys) {
            const QVector<Trs> oracle = oracleAt(key.time);
            const Trs mine{ key.position, key.rotation, key.scale };
            worstAtKeys = std::max(worstAtKeys, trsError(mine, oracle[track.bone]));
            ++samplesAtKeys;
        }
    }
    std::printf("    worst |error| at %d key times: %.3e\n", samplesAtKeys, double(worstAtKeys));
    CHECK(worstAtKeys < exactTol,
          (QString("[%1] the composed track reproduces the pose composed from the clip's "
                   "own keys at every key time (< %2)").arg(label).arg(double(exactTol))).toUtf8().constData());

    // ---- resampling error between keys ------------------------------------
    // Lossy by construction (§7): a rotation split across two pivots composes
    // to something a lerp between the composed keys cannot reproduce
    // mid-interval. Measured and bounded rather than asserted to zero — a dense
    // clip (Mixamo keys every frame on every bone) lands on real keys and is
    // exact, these fixtures are deliberately sparse.
    float worstBetween = 0.0f;
    if (clip.length > 0.0f) {
        for (int s = 0; s <= 20; ++s) {
            const float t = clip.length * float(s) / 20.0f;
            const QVector<Trs> oracle = oracleAt(t);
            for (const auto &track : clip.tracks)
                worstBetween = std::max(worstBetween, trsError(sampleTrack(track, t), oracle[track.bone]));
        }
    }
    std::printf("    worst |error| resampled over 21 uniform times: %.3e\n", double(worstBetween));
    CHECK(worstBetween < betweenTol,
          (QString("[%1] resampled error stays inside the documented tolerance (< %2)")
               .arg(label).arg(double(betweenTol))).toUtf8().constData());

    CHECK(oracleComplete,
          (QString("[%1] the oracle composes a pose for every bone of the rig")
               .arg(label)).toUtf8().constData());
    anim->setLooping(wasLooping);
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    // v1 INTERIM (SPECS/SCENEGRAPH_SPEC.md §3): a document node IS an engine
    // node now, so even a document-only suite needs an engine. Declared here,
    // before anything builds a document, and destroyed last.
    enginetest::DocumentGraph graph("skeletal-clip-extract-ogre.log");
    if (!graph.require()) return 1;
    QTemporaryDir extract;


    // =====================================================================
    // 1. The FBX fixture really is what it claims to be.
    // =====================================================================
    {
        Assimp::Importer importer;
        const aiScene *scene = importer.ReadFile(kFbxRig.toStdString().c_str(),
                                                 iris::ImportFlags::Canonical);
        CHECK(scene != nullptr, "the FBX fixture parses");
        if (!scene) return 1;
        CHECK(scene->mNumMeshes == 1 && scene->mMeshes[0]->mNumBones == 2,
              "one skinned mesh, two bones — the same shape as the glTF rig");
        CHECK(scene->mNumAnimations == 2, "two clips: 'Walk' and the zero-length 'mixamo.com'");

        int pivotChannels = 0, boneChannels = 0;
        for (unsigned a = 0; a < scene->mNumAnimations; ++a)
            for (unsigned c = 0; c < scene->mAnimations[a]->mNumChannels; ++c) {
                const QString name(scene->mAnimations[a]->mChannels[c]->mNodeName.C_Str());
                if (name.contains(QStringLiteral("$AssimpFbx$"))) ++pivotChannels; else ++boneChannels;
            }
        std::printf("    FBX channels: %d on pivot nodes, %d on bone nodes\n",
                    pivotChannels, boneChannels);
        // THE fact the whole fixture exists for: every channel is on a pivot
        // node. A translation that reads a bone's own BoneAnimation gets
        // nothing at all, and the character freezes.
        CHECK(boneChannels == 0 && pivotChannels > 0,
              "every clip channel targets a $AssimpFbx$ pivot node, none targets a bone");
    }

    // =====================================================================
    // 2. glTF rig — no pivots. The case every existing fixture covers.
    // =====================================================================
    {
        Loaded f;
        CHECK(load(kGlbRig, extract.path(), f), "rig2.glb loads as a fragment");
        if (f.mesh.isNull()) return 1;
        QStringList names; collectNames(f.fragment, names);
        CHECK(!names.filter(QStringLiteral("$AssimpFbx$")).size(),
              "the glTF rig has no pivot nodes (which is why it could never have caught R1)");

        // The exporter's bind pose (§1.5 F1): Bone::binding* were never written
        // on the live import path, so every joint the web exporter emitted had
        // an identity bind. They now carry the bone's parent-local bind.
        const auto &bones = f.mesh->getSkeleton()->bones;
        bool bindWritten = true;
        for (const auto &b : bones) {
            const iris::Mat4 expect = !b->parentBone.isNull()
                ? b->parent()->inverseMeshSpacePoseMatrix * b->meshSpacePoseMatrix
                : b->meshSpacePoseMatrix;
            iris::Vec3 p, s; iris::Quat r;
            iris::decomposeTRS(expect, p, r, s);
            if ((p - b->bindingPos).length() > 1e-5f) bindWritten = false;
            if (std::fabs(s.x() * s.y() * s.z()) < 1e-6f) bindWritten = false;   // not the zero default
        }
        CHECK(bindWritten, "every bone carries its parent-local bind TRS (F1: the exporter's "
                           "identity-bind defect)");
        // jointTip binds one unit up from jointRoot; identity binds would read (0,0,0).
        const auto tip = f.mesh->getSkeleton()->getBone(QStringLiteral("jointTip"));
        CHECK(!tip.isNull() && (tip->bindingPos - iris::Vec3(0, 1, 0)).length() < 1e-5f,
              "jointTip's bind translation is (0,1,0), not the (0,0,0) the exporter used to write");

        for (const auto &anim : f.fragment->getAnimations()) {
            if (!anim || !anim->hasSkeletalAnimation()) continue;
            gateClip(f, anim, ("glTF/" + anim->getName()).toUtf8().constData(), 1e-5f, 1e-2f);
        }
    }

    // =====================================================================
    // 3. THE FBX gate (G4, extract half): five pivot nodes between two bones,
    //    three different key-time sets, and a zero-length clip.
    // =====================================================================
    {
        Loaded f;
        CHECK(load(kFbxRig, extract.path(), f), "pivot_rig.fbx loads as a fragment");
        if (f.mesh.isNull()) return 1;

        QStringList names; collectNames(f.fragment, names);
        const int pivotNodes = names.filter(QStringLiteral("$AssimpFbx$")).size();
        std::printf("    fragment nodes: %d, of which %d are pivot nodes\n",
                    names.size(), pivotNodes);
        CHECK(pivotNodes >= 5, "the pivot chain survived import into the document");

        // The rig's own hierarchy skips them: jointTip's parent BONE is
        // jointRoot even though five scene nodes sit in between.
        const auto tip = f.mesh->getSkeleton()->getBone(QStringLiteral("jointTip"));
        CHECK(!tip.isNull() && !tip->parentBone.isNull() &&
                  tip->parent()->name == QStringLiteral("jointRoot"),
              "the bone hierarchy links through the pivot chain (nearest bone ancestor)");

        int walkClips = 0, zeroLengthClips = 0;
        for (const auto &anim : f.fragment->getAnimations()) {
            if (!anim || !anim->hasSkeletalAnimation()) continue;
            if (anim->getLength() > 0.0f) ++walkClips; else ++zeroLengthClips;
            // The FBX composition is genuinely lossier than the glTF one: three
            // channels on one bone chain, sampled at 2, 3 and 3 times over a
            // second, are as sparse as a clip ever gets.
            gateClip(f, anim, ("FBX/" + anim->getName()).toUtf8().constData(), 1e-5f, 6e-2f);
        }
        CHECK(walkClips == 1 && zeroLengthClips == 1,
              "the file carries one real clip and one ZERO-LENGTH clip (one key, no "
              "declared duration; engine-side it is fmod(t,0) = NaN, so the engine pads it)");

        // The zero-length clip must still produce something usable: one key, at
        // t = 0. Padding its LENGTH is the engine backend's job (§9 R3 / D5);
        // producing a NaN-free static pose is this one's.
        for (const auto &anim : f.fragment->getAnimations()) {
            if (!anim || !anim->hasSkeletalAnimation() || anim->getLength() > 0.0f) continue;
            f.fragment->setAnimation(anim);
            iris::ExtractedClip clip;
            iris::ClipExtractor::extract(f.fragment, f.mesh, f.mesh->getSkeleton(),
                                         anim->getSkeletalAnimation(), anim->getName(),
                                         anim->getLength(), &f.rest, clip, nullptr);
            bool single = !clip.tracks.isEmpty();
            for (const auto &track : clip.tracks)
                if (track.keys.size() != 1 || std::fabs(track.keys[0].time) > 1e-6f) single = false;
            CHECK(single, "the zero-length clip extracts as exactly one key at t=0 per driven bone");
        }
    }

    // =====================================================================
    // 4. THE ONE-FRAME MIXAMO CLIP (smoke L10 item 2). Every Mixamo character
    //    download ships "mixamo.com": two identical keys one frame apart, so
    //    the file DECLARES one frame (mDuration 1 tick at 30 ticks/s). The
    //    canonical preset's FindInvalidData collapses the identical keys to one
    //    key at t = 0, the key span is then 0, and the importer used to record
    //    a ZERO-length clip — which the engine padded to 1 ms, with a log
    //    line, on every attach. The declared duration survives the collapse
    //    and is what the length comes from now.
    // =====================================================================
    {
        Assimp::Importer importer;
        const aiScene *scene = importer.ReadFile(kFbxTpose.toStdString().c_str(),
                                                 iris::ImportFlags::Canonical);
        CHECK(scene && scene->mNumAnimations == 1, "the Mixamo T-pose fixture parses, one clip");
        if (scene && scene->mNumAnimations == 1) {
            const aiAnimation *a = scene->mAnimations[0];
            unsigned maxKeys = 0;
            for (unsigned c = 0; c < a->mNumChannels; ++c)
                maxKeys = std::max(maxKeys, a->mChannels[c]->mNumRotationKeys);
            std::printf("    mixamo_tpose.fbx: duration %.3f ticks at %.1f ticks/s, %u key(s)\n",
                        a->mDuration, a->mTicksPerSecond, maxKeys);
            CHECK(maxKeys == 1 && std::fabs(a->mDuration - 1.0) < 1e-9 &&
                      std::fabs(a->mTicksPerSecond - 30.0) < 1e-9,
                  "...in the real Mixamo shape: ONE key after the preset, a declared "
                  "duration of 1 tick at 30 ticks/s");
        }

        Loaded f;
        CHECK(load(kFbxTpose, extract.path(), f), "mixamo_tpose.fbx loads as a fragment");
        if (!f.mesh.isNull()) {
            iris::AnimationPtr tpose;
            for (const auto &anim : f.fragment->getAnimations())
                if (anim && anim->hasSkeletalAnimation()) tpose = anim;
            CHECK(!tpose.isNull(), "its 'mixamo.com' clip is on the fragment");
            if (!tpose.isNull()) {
                std::printf("    clip length %.6f s\n", double(tpose->getLength()));
                CHECK(std::fabs(tpose->getLength() - 1.0f / 30.0f) < 1e-6f,
                      "the one-frame clip is ONE FRAME long (1/30 s, the file's declared "
                      "duration), not zero");
                iris::ExtractedClip clip;
                const bool ok = iris::ClipExtractor::extract(
                    f.fragment, f.mesh, f.mesh->getSkeleton(), tpose->getSkeletalAnimation(),
                    tpose->getName(), tpose->getLength(), &f.rest, clip, nullptr);
                bool held = ok && !clip.tracks.isEmpty() && clip.length > 0.0f;
                for (const auto &track : clip.tracks) {
                    if (track.keys.size() != 2 || std::fabs(track.keys[0].time) > 1e-6f ||
                        std::fabs(track.keys[1].time - clip.length) > 1e-6f) { held = false; continue; }
                    const Trs a{ track.keys[0].position, track.keys[0].rotation, track.keys[0].scale };
                    const Trs b{ track.keys[1].position, track.keys[1].rotation, track.keys[1].scale };
                    if (trsError(a, b) > 1e-6f) held = false;
                }
                CHECK(held, "it extracts as a HELD pose over [0, length] — two identical keys "
                            "per driven bone, a clip the engine never has to pad");
            }
        }
    }

    std::printf(failures ? "\n%d FAILURE(S)\n" : "\nall clip-extraction gates passed\n", failures);
    return failures ? 1 : 0;
}
