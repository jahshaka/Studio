// ANIMATION_ENGINE_MIGRATION_SPEC M0 / gates G1 + G4(extract half).
//
// THE ONE PLACE THIS PROGRAM FAILS IF IT FAILS (§9 R1). Our clip keys are
// absolute local TRS of a SCENE NODE; an engine skeleton has only real bones;
// and in a pivot-preserving FBX a bone's motion is spread over its
// `$AssimpFbx$` pivot ancestors. A per-key rename of BoneAnimation to a bone
// track therefore passes every glTF fixture in the tree and produces a FROZEN
// character on every Mixamo FBX — invisibly.
//
// So this suite proves the "compose then resample" extractor (§3.1) against
// THE FILE'S OWN MOTION, on both a glTF rig (no pivots) and the tree's first
// FBX fixture (five pivot nodes between two bones, every clip channel on a
// pivot node and none on a bone). Document-only: no engine, no window.
//
// THE ORACLE IS THE FILE. For every sample the suite takes, the bone's
// parent-local TRS is composed straight from assimp's aiScene — the node
// hierarchy's rest transforms and the raw aiNodeAnim keys, sampled by the
// document's key rule (linear position/scale, slerp rotation, held outside the
// key range) — over the same pivot chain. Nothing of the document import path
// or of the extractor is in it, so what it guards is the behaviour: the
// extracted track reproduces the motion the file authored.
#include "irisgl/core/math/mat4.h"
#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include <QFile>
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QHash>
#include <QSet>
#include <QTextStream>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

#include "assimp/Importer.hpp"
#include "assimp/scene.h"
#include "irisgl/core/math/trs.h"
#include "irisgl/document/animation/animation.h"
#include "irisgl/document/animation/clipextractor.h"
#include "irisgl/document/animation/keyframeanimation.h"
#include "irisgl/document/animation/skeletalanimation.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/assets/skeleton.h"
#include "irisgl/document/materials/pbrmaterial.h"
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

// ---- THE FILE ORACLE -----------------------------------------------------

/// The document's key rule (keyframeanimation.h), restated over assimp's raw
/// keys: the first key before the range, the last after it, linear between.
template <typename Key, typename V, typename Lerp>
static V sampleKeys(const Key *keys, unsigned count, double tps, double t, V fallback, Lerp lerp,
                    V (*value)(const Key &))
{
    if (count == 0) return fallback;
    const auto at = [&](unsigned i) { return keys[i].mTime / tps; };
    if (count == 1 || t <= at(0)) return value(keys[0]);
    if (t >= at(count - 1)) return value(keys[count - 1]);
    for (unsigned i = 1; i < count; ++i) {
        if (at(i) <= t) continue;
        const double span = at(i) - at(i - 1);
        const float u = span != 0.0 ? float((t - at(i - 1)) / span) : 0.0f;
        return lerp(value(keys[i - 1]), value(keys[i]), u);
    }
    return value(keys[count - 1]);
}

static iris::Vec3 vecOf(const aiVectorKey &k) { return iris::Vec3(k.mValue.x, k.mValue.y, k.mValue.z); }
static iris::Quat quatOf(const aiQuatKey &k) { return iris::Quat(k.mValue.w, k.mValue.x, k.mValue.y, k.mValue.z); }

/// One file, one clip: the parent-local TRS of every bone at a time, composed
/// from the aiScene alone.
struct FileOracle
{
    const aiScene *scene = nullptr;
    const aiAnimation *anim = nullptr;
    double tps = 25.0;
    QHash<QString, const aiNode *> byName;
    QHash<QString, const aiNodeAnim *> channel;

    bool init(const aiScene *s, const QString &clipName)
    {
        scene = s;
        for (unsigned a = 0; a < s->mNumAnimations; ++a)
            if (QString(s->mAnimations[a]->mName.C_Str()) == clipName) anim = s->mAnimations[a];
        if (!anim) return false;
        tps = anim->mTicksPerSecond > 0.0 ? anim->mTicksPerSecond : 25.0;   // mesh.cpp's rule
        for (unsigned c = 0; c < anim->mNumChannels; ++c)
            channel.insert(QString(anim->mChannels[c]->mNodeName.C_Str()), anim->mChannels[c]);
        QVector<const aiNode *> order{ s->mRootNode };
        for (int i = 0; i < order.size(); ++i) {
            byName.insert(QString(order[i]->mName.C_Str()), order[i]);
            for (unsigned k = 0; k < order[i]->mNumChildren; ++k) order.append(order[i]->mChildren[k]);
        }
        return true;
    }

    iris::Mat4 localAt(const aiNode *n, double t) const
    {
        const auto it = channel.constFind(QString(n->mName.C_Str()));
        if (it == channel.constEnd()) {
            aiVector3D sc, p; aiQuaternion r;
            n->mTransformation.Decompose(sc, r, p);
            return iris::composeTRS(iris::Vec3(p.x, p.y, p.z), iris::Quat(r.w, r.x, r.y, r.z),
                                    iris::Vec3(sc.x, sc.y, sc.z));
        }
        const aiNodeAnim *c = it.value();
        const auto lerpV = [](iris::Vec3 a, iris::Vec3 b, float u) { return a + (b - a) * u; };
        const auto slerpQ = [](iris::Quat a, iris::Quat b, float u) { return iris::Quat::slerp(a, b, u); };
        const iris::Vec3 p = sampleKeys(c->mPositionKeys, c->mNumPositionKeys, tps, t, iris::Vec3(), lerpV, &vecOf);
        const iris::Quat r = sampleKeys(c->mRotationKeys, c->mNumRotationKeys, tps, t, iris::Quat(), slerpQ, &quatOf);
        const iris::Vec3 sc = sampleKeys(c->mScalingKeys, c->mNumScalingKeys, tps, t, iris::Vec3(), lerpV, &vecOf);
        return iris::composeTRS(p, r.normalized(), sc);
    }

    /// Root-first product of every local from `n` up to (not including) `stop`.
    iris::Mat4 chain(const aiNode *n, const aiNode *stop, double t) const
    {
        iris::Mat4 m; m.setToIdentity();
        for (; n && n != stop; n = n->mParent) m = localAt(n, t) * m;
        return m;
    }

    /// `bone` in the frame of `frame` (its parent bone's node, or the skinned
    /// mesh's node for a root bone) at time t. The shared ancestry cancels, so
    /// it is not composed at all.
    bool pose(const QString &bone, const QString &frame, double t, Trs &out) const
    {
        const aiNode *b = byName.value(bone, nullptr), *f = byName.value(frame, nullptr);
        if (!b || !f) return false;
        QSet<const aiNode *> above;
        for (const aiNode *n = f; n; n = n->mParent) above.insert(n);
        const aiNode *common = b;
        while (common && !above.contains(common)) common = common->mParent;
        const iris::Mat4 local = chain(f, common, t).inverted() * chain(b, common, t);
        iris::decomposeTRS(local, out.pos, out.rot, out.scale);
        return true;
    }
};

/// The node that carries the skinned mesh in the FILE (the root bones' frame).
static QString skinnedMeshNodeName(const aiScene *s)
{
    QVector<const aiNode *> order{ s->mRootNode };
    for (int i = 0; i < order.size(); ++i) {
        const aiNode *n = order[i];
        for (unsigned m = 0; m < n->mNumMeshes; ++m)
            if (s->mMeshes[n->mMeshes[m]]->mNumBones > 0) return QString(n->mName.C_Str());
        for (unsigned k = 0; k < n->mNumChildren; ++k) order.append(n->mChildren[k]);
    }
    return QString();
}

static iris::MaterialPtr makeMat(iris::MeshPtr, iris::MeshMaterialData &)
{
    return iris::PbrMaterial::create();
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
    /// The same file read straight through assimp, for the oracle.
    std::shared_ptr<Assimp::Importer> importer;
    const aiScene *file = nullptr;
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
    out.importer = std::make_shared<Assimp::Importer>();
    out.file = out.importer->ReadFile(path.toStdString().c_str(), iris::ImportFlags::Canonical);
    return !out.mesh.isNull() && !out.host.isNull() && out.file;
}

/// The whole gate for one (rig, clip): the extractor's tracks reproduce the
/// FILE's bone-parent-local pose EXACTLY at every key time the extractor
/// emitted, and within the resampling tolerance in between.
static void gateClip(Loaded &f, const iris::AnimationPtr &anim,
                     const char *label, float exactTol, float betweenTol)
{
    f.fragment->setAnimation(anim);
    // Animation::getSampleTime is `fmod(time, length)` while looping; the
    // extractor pins a terminal key AT the length (R4), and the file oracle is
    // unwrapped, so the clip is driven unwrapped too.
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

    // THE ORACLE: this bone's parent-local TRS at time t, composed from the
    // file's own hierarchy and keys (FileOracle). The frame is the parent
    // BONE's node, or the skinned mesh's node for a root bone.
    FileOracle file;
    const bool fileClip = file.init(f.file, anim->getName());
    CHECK(fileClip, (QString("[%1] the clip is in the file the oracle reads").arg(label))
                        .toUtf8().constData());
    bool oracleComplete = fileClip;
    const auto &bones = f.mesh->getSkeleton()->bones;
    const QString meshFrame = skinnedMeshNodeName(f.file);
    const auto oracleAt = [&](float t) {
        QVector<Trs> pose(bones.size());
        for (int b = 0; b < bones.size(); ++b) {
            const QString frame = bones[b]->parentBone.isNull() ? meshFrame : bones[b]->parent()->name;
            if (!fileClip || !file.pose(bones[b]->name, frame, t, pose[b])) oracleComplete = false;
        }
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
          (QString("[%1] the composed track reproduces the file's motion at every key "
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

    CHECK(oracleComplete,
          (QString("[%1] the file oracle answers for every bone this suite samples")
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
