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
// CURRENT document evaluator, on both a glTF rig (no pivots) and the tree's
// first FBX fixture (five pivot nodes between two bones, every clip channel on
// a pivot node and none on a bone). Document-only: no engine, no window.
//
// The oracle is FROZEN. The document evaluator this suite was written against
// no longer exists — full retirement was the point of the program — so its
// answers were recorded first, into fixtures/golden_document_poses.txt, and
// that recording is what the extractor is checked against now.
#include "irisgl/core/math/mat4.h"
#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include <QFile>
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QSet>
#include <QTextStream>
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

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

static const QString kGlbRig =
    QStringLiteral(JAHSHAKA_TEST_SOURCE_DIR "/tests/avatar/fixtures/rig2.glb");
static const QString kFbxRig =
    QStringLiteral(JAHSHAKA_TEST_SOURCE_DIR "/tests/skeletal/fixtures/pivot_rig.fbx");

struct Trs { iris::Vec3 pos; iris::Quat rot; iris::Vec3 scale; };

// THE FROZEN ORACLE.
//
// This suite's whole point is comparing the extractor against the document's
// clip evaluator — and that evaluator is being deleted. A parity gate cannot
// outlive its oracle unless the oracle's ANSWERS are kept, so they are: the
// evaluator's bone-parent-local pose for every (fixture, clip, time, bone) this
// suite samples, written out by `--write-golden` while the evaluator still
// existed and committed beside the fixtures.
//
// Regenerating it is not possible any more, by design: the `--write-golden`
// mode that produced it was deleted with the evaluator it read. The file
// records what the document evaluator said before it was retired; if the
// extractor stops agreeing with it, the extractor changed, and no amount of
// re-running will make that go away.
static const QString kGolden =
    QStringLiteral(JAHSHAKA_TEST_SOURCE_DIR "/tests/skeletal/fixtures/golden_document_poses.txt");

static QMap<QString, Trs> gGolden;      // "fixture|clip|time|bone" -> pose

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

/// The frozen evaluator's answer for one sample, or a null pose when the file
/// has no entry — which is itself a failure: the samples this suite takes and
/// the ones the file holds must be the same set.
static bool frozen(const QString &fixture, const QString &clip, float t, int bone, Trs &out)
{
    const QString key = QStringLiteral("%1|%2|%3|%4")
                            .arg(fixture, clip, QString::number(double(t), 'f', 6))
                            .arg(bone);
    const auto it = gGolden.constFind(key);
    if (it == gGolden.constEnd()) return false;
    out = it.value();
    return true;
}

static bool loadGolden()
{
    QFile f(kGolden);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    QTextStream in(&f);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
        const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (parts.size() != 11) continue;
        Trs v;
        v.pos = iris::Vec3(parts[1].toFloat(), parts[2].toFloat(), parts[3].toFloat());
        v.rot = iris::Quat(parts[7].toFloat(), parts[4].toFloat(), parts[5].toFloat(), parts[6].toFloat());
        v.scale = iris::Vec3(parts[8].toFloat(), parts[9].toFloat(), parts[10].toFloat());
        gGolden.insert(parts[0], v);
    }
    return !gGolden.isEmpty();
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

/// The whole gate for one (rig, clip): the extractor's tracks reproduce the
/// document evaluator's bone-parent-local pose EXACTLY at every key time the
/// extractor emitted, and within the resampling tolerance in between.
static void gateClip(Loaded &f, const iris::AnimationPtr &anim, const QString &fixture,
                     const char *label, float exactTol, float betweenTol)
{
    f.fragment->setAnimation(anim);
    // Animation::getSampleTime is `fmod(time, length)` while looping, so the
    // evaluator sampled at exactly the clip length answers for t = 0. The
    // extractor pins a terminal key AT the length (R4), so the oracle has to be
    // driven unwrapped or the comparison at that key is against the wrong pose.
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

    // THE ORACLE: what the document clip evaluator said this bone's
    // parent-local TRS was at time t, read out of the frozen recording.
    bool goldenComplete = true;
    const int boneCount = f.mesh->getSkeleton()->bones.size();
    const auto oracleAt = [&](float t) {
        QVector<Trs> pose(boneCount);
        for (int b = 0; b < boneCount; ++b)
            if (!frozen(fixture, anim->getName(), t, b, pose[b])) goldenComplete = false;
        return pose;
    };

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
          (QString("[%1] the composed track reproduces the document evaluator at every key "
                   "time (< %2)").arg(label).arg(double(exactTol))).toUtf8().constData());

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

    CHECK(goldenComplete,
          (QString("[%1] the frozen oracle covers every sample this suite takes")
               .arg(label)).toUtf8().constData());
    anim->setLooping(wasLooping);
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    QTemporaryDir extract;

    CHECK(loadGolden(), "the frozen document-evaluator oracle loads");
    std::printf("    frozen oracle entries: %lld\n", (long long)gGolden.size());

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
                ? b->parentBone->inverseMeshSpacePoseMatrix * b->meshSpacePoseMatrix
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
            gateClip(f, anim, QStringLiteral("rig2.glb"),
                     ("glTF/" + anim->getName()).toUtf8().constData(), 1e-5f, 1e-2f);
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
                  tip->parentBone->name == QStringLiteral("jointRoot"),
              "the bone hierarchy links through the pivot chain (nearest bone ancestor)");

        int walkClips = 0, zeroLengthClips = 0;
        for (const auto &anim : f.fragment->getAnimations()) {
            if (!anim || !anim->hasSkeletalAnimation()) continue;
            if (anim->getLength() > 0.0f) ++walkClips; else ++zeroLengthClips;
            // The FBX composition is genuinely lossier than the glTF one: three
            // channels on one bone chain, sampled at 2, 3 and 3 times over a
            // second, are as sparse as a clip ever gets.
            gateClip(f, anim, QStringLiteral("pivot_rig.fbx"),
                     ("FBX/" + anim->getName()).toUtf8().constData(), 1e-5f, 6e-2f);
        }
        CHECK(walkClips == 1 && zeroLengthClips == 1,
              "the file carries one real clip and one ZERO-LENGTH clip (every Mixamo "
              "character download ships one; engine-side it is fmod(t,0) = NaN)");

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

    std::printf(failures ? "\n%d FAILURE(S)\n" : "\nall clip-extraction gates passed\n", failures);
    return failures ? 1 : 0;
}
