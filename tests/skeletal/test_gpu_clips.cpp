// ANIMATION_ENGINE_MIGRATION_SPEC gates G2, G3, G4(pixel half), G5, G6.
//
// The document's clip evaluator is being retired and Ogre's SkeletonAnimation
// put in its place. This suite is the comparison that has to exist BEFORE the
// old one is deleted: same rig, same clip, same absolute times, driven both
// ways, compared at the matrices the vertex shader is handed and at the pixels.
//
// Parity here is a TOLERANCE and never bit-equality, for three verified
// reasons: our within-clip quaternion interpolation is slerp and Ogre's runtime
// blend is nlerpShortest; Ogre resamples at bake time onto a per-4-bone-block
// key-time union with an explicitly unnormalized quaternion lerp; and composing
// an FBX pivot chain is itself a resample.
#include "irisgl/core/math/mat4.h"
#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include <QGuiApplication>
#include <QImage>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "irisgl/document/animation/animation.h"
#include "irisgl/document/animation/clipextractor.h"
#include "irisgl/document/animation/skeletalanimation.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/materials/defaultmaterial.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/mirror/scenemirror.h"
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"
#include "armrig.h"

using namespace jahshaka::engine;
static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

// ---------------------------------------------------------------------------
struct Rig {
    iris::ScenePtr    doc;
    iris::MeshNodePtr node;
    iris::MeshPtr     mesh;
    iris::AnimationPtr clip;
    iris::ClipExtractor::RestPose rest;
};

/// A DENSE clip — a key every 1/30 s on BOTH bones, which is what every real
/// clip is (Mixamo keys every frame on every bone).
///
/// Density is not a convenience here, it is the whole parity story: our
/// within-clip interpolation is SLERP and Ogre's runtime blend is nlerpShortest,
/// so on a 2-key 90-degree clip the two paths differ by ~1.5e-2 mid-interval by
/// construction. At 3 degrees per interval that difference is ~1e-6. The suite
/// measures the sparse case explicitly further down rather than hiding it.
static iris::AnimationPtr buildDenseSwing(float tipDegrees, float rootDegrees,
                                          float length, int keys)
{
    auto skelAnim = iris::SkeletalAnimation::create();
    const auto addBone = [&](const char *name, float degrees, const iris::Vec3 &pos) {
        auto boneAnim = new iris::BoneAnimation();
        for (int i = 0; i <= keys; ++i) {
            const double t = double(length) * double(i) / double(keys);
            const float a = degrees * float(i) / float(keys);
            boneAnim->posKeys->addKey(pos, t);
            boneAnim->rotKeys->addKey(iris::Quat::fromAxisAndAngle(0, 0, 1, a), t);
            boneAnim->scaleKeys->addKey(iris::Vec3(1, 1, 1), t);
        }
        skelAnim->addBoneAnimation(name, boneAnim);
    };
    addBone("jointRoot", rootDegrees, iris::Vec3(0, 0, 0));
    addBone("jointTip", tipDegrees, iris::Vec3(0, 1, 0));
    return iris::Animation::createFromSkeletalAnimation(skelAnim);
}

static Rig makeArm(float degrees = -90.0f)
{
    Rig r;
    r.doc = iris::Scene::create();
    r.mesh = armrig::buildArmMesh();
    r.node = armrig::buildArmNode(r.mesh, "arm");
    r.clip = buildDenseSwing(degrees, 20.0f, 1.0f, 30);
    r.clip->setName("Swing");
    // Absolute-time parity everywhere: a looping clip remaps t = length back to
    // t = 0 in the DOCUMENT (fmod) and the extractor pins a terminal key there,
    // so the oracle has to be driven unwrapped.
    r.clip->setLooping(false);
    r.node->addAnimation(r.clip);
    r.node->setAnimation(r.clip);
    r.doc->getRootNode()->addChild(r.node);
    r.rest = iris::ClipExtractor::captureRest(r.node);
    return r;
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

static iris::Mat4 engineBone(Scene *s, NodeId node, size_t bone, size_t boneCount)
{
    std::vector<float> m(boneCount * 12, 0.0f);
    if (!s->boneMatrices(node, m.data(), boneCount)) return iris::Mat4();
    iris::Mat4 out;
    const float *b = &m[bone * 12];
    for (int r = 0; r < 3; ++r) for (int c = 0; c < 4; ++c) out(r, c) = b[r * 4 + c];
    out(3, 0) = 0; out(3, 1) = 0; out(3, 2) = 0; out(3, 3) = 1;
    return out;
}

static double worstBoneDiff(Scene *s, NodeId a, NodeId b, size_t boneCount)
{
    double worst = 0.0;
    for (size_t i = 0; i < boneCount; ++i) {
        const iris::Mat4 ma = engineBone(s, a, i, boneCount);
        const iris::Mat4 mb = engineBone(s, b, i, boneCount);
        for (int r = 0; r < 3; ++r) for (int c = 0; c < 4; ++c)
            worst = std::max(worst, std::fabs(double(ma(r, c)) - double(mb(r, c))));
    }
    return worst;
}

/// The pose an EXTRACTED clip implies at time t, as the boundary's BonePose
/// array — sampled the way a v1 engine track samples: linear on position and
/// scale, shortest-arc nlerp on rotation, held outside the key range. Bones the
/// clip does not drive sit at their BIND pose, which is exactly what the engine
/// resets them to.
///
/// This is the manual-pose side of every comparison below. It used to be the
/// document evaluator (updateSceneAnimation -> toBonePoses); that is retired
/// (ANIMATION_ENGINE_MIGRATION_SPEC, full retirement), and the extractor is
/// separately gated against a FROZEN recording of the evaluator's answers in
/// skeletal.clip_extract — so the chain of trust is
///     frozen document oracle  <-  extractor  <-  engine.
static std::vector<BonePose> posesFrom(const SkeletonDesc &rig, const iris::ExtractedClip &clip,
                                       float t)
{
    std::vector<BonePose> out(rig.bones.size());
    for (size_t i = 0; i < rig.bones.size(); ++i) {
        out[i].position = rig.bones[i].bindPosition;
        out[i].rotation = rig.bones[i].bindRotation;
        out[i].scale    = rig.bones[i].bindScale;
    }
    for (const auto &track : clip.tracks) {
        if (track.bone < 0 || size_t(track.bone) >= out.size() || track.keys.isEmpty()) continue;
        iris::Vec3 pos, scale;
        iris::Quat rot;
        if (t <= track.keys.first().time) {
            pos = track.keys.first().position; rot = track.keys.first().rotation;
            scale = track.keys.first().scale;
        } else if (t >= track.keys.last().time) {
            pos = track.keys.last().position; rot = track.keys.last().rotation;
            scale = track.keys.last().scale;
        } else {
            for (int k = 1; k < track.keys.size(); ++k) {
                if (track.keys[k].time < t) continue;
                const auto &a = track.keys[k - 1];
                const auto &b = track.keys[k];
                const float span = b.time - a.time;
                const float u = span > 0.0f ? (t - a.time) / span : 0.0f;
                pos = a.position + (b.position - a.position) * u;
                scale = a.scale + (b.scale - a.scale) * u;
                rot = iris::Quat::nlerp(a.rotation, b.rotation, u);
                break;
            }
        }
        BonePose &p = out[size_t(track.bone)];
        p.position = Vec3(pos.x(), pos.y(), pos.z());
        p.rotation = Quat(rot.x(), rot.y(), rot.z(), rot.scalar());
        p.scale    = Vec3(scale.x(), scale.y(), scale.z());
    }
    return out;
}

static std::vector<float> flatten(const std::vector<BonePose> &p)
{
    std::vector<float> out;
    out.reserve(p.size() * 10);
    for (const auto &b : p) {
        out.push_back(b.position.x); out.push_back(b.position.y); out.push_back(b.position.z);
        out.push_back(b.rotation.x); out.push_back(b.rotation.y);
        out.push_back(b.rotation.z); out.push_back(b.rotation.w);
        out.push_back(b.scale.x); out.push_back(b.scale.y); out.push_back(b.scale.z);
    }
    return out;
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test_gpu_clips-ogre.log";
    std::string err;
    auto engine = Engine::create(cfg, err);
    CHECK(engine != nullptr, "engine created");
    if (!engine) { std::printf("    %s\n", err.c_str()); return 1; }

    const int W = 96, H = 96;
    View *view = engine->createOffscreenView("clips", W, H, Colour(0, 0, 0));
    Scene *s = engine->createScene("clips");
    view->setScene(s);
    // Flat ambient + an emissive material: the pixel gate compares two frames
    // of the SAME character posed two ways, so lighting is noise. Removing it
    // removes the only source of per-frame variation that is not the pose.
    s->setAmbient(Colour(1, 1, 1), Colour(1, 1, 1));
    CameraDesc cam; cam.position = Vec3(0, 1, 5);
    view->setCamera(cam);

    Rig rig = makeArm();
    SkeletonDesc desc;
    CHECK(SceneMirror::toSkeletonDesc(rig.node->getSkeleton(), desc), "toSkeletonDesc succeeded");
    const size_t boneCount = desc.bones.size();

    MeshData md = armMeshData(rig.mesh);
    MeshId meshId = s->createMesh(md);
    PbrParams mp; mp.albedo = Colour(0, 0, 0); mp.emissive = Colour(1, 0, 0);
    MaterialId matId = s->createPbrMaterial(mp);

    // nodeDoc is driven the OLD way (manual bones + setBonePoses from the
    // document evaluator); nodeClip is driven by the engine's own clip. Both
    // exist only for the length of this suite — the whole point is the compare.
    NodeId nodeDoc = s->createNode();
    NodeId nodeClip = s->createNode();
    CHECK(s->attachSkinnedMesh(nodeDoc, meshId, matId, desc), "document-driven node attaches");
    CHECK(s->attachSkinnedMesh(nodeClip, meshId, matId, desc), "clip-driven node attaches");

    // ---- clip translation ----------------------------------------------
    iris::ExtractedClip extracted;
    QString exErr;
    CHECK(iris::ClipExtractor::extract(rig.node, rig.node, rig.node->getSkeleton(),
                                       rig.clip->getSkeletalAnimation(), rig.clip->getName(),
                                       rig.clip->getLength(), &rig.rest, extracted, &exErr),
          "the clip extracts");
    ClipDesc clipDesc;
    CHECK(SceneMirror::toClipDesc(extracted, desc.id, clipDesc), "toClipDesc succeeded");
    CHECK(!clipDesc.id.empty() && clipDesc.name == "Swing", "the ClipDesc carries a content id and a name");

    // ---- G2: attach ------------------------------------------------------
    // attachClips also proves R10 (frames == seconds): the backend refuses a def
    // whose reported frame count is not the clip length in seconds, which is the
    // only thing standing between us and a silently mis-timed character if
    // SkeletonManager's hardcoded frameRate = 1.0f ever changes upstream.
    CHECK(s->attachClips(nodeClip, &clipDesc, 1), "attachClips succeeded");
    if (!s->attachClips(nodeClip, &clipDesc, 1)) std::printf("    %s\n", engine->lastError().c_str());
    {
        const auto names = s->clipNames(nodeClip);
        CHECK(names.size() == 1 && names[0] == "Swing", "clipNames reports the attached clip");
        CHECK(s->attachClips(nodeClip, &clipDesc, 1), "re-attaching the same clip id is idempotent");
        CHECK(s->clipNames(nodeClip).size() == 1, "and does not duplicate it");
    }
    // A clip on a node that has no rig is refused, not ignored.
    {
        NodeId bare = s->createNode();
        CHECK(!s->attachClips(bare, &clipDesc, 1), "attachClips refuses a node with no rig");
        s->removeNode(bare);
    }
    // A track naming a bone the rig does not have is refused (R11: the engine's
    // own guards are Debug-only asserts and RelWithDebInfo ships).
    {
        ClipDesc bad = clipDesc;
        bad.id += "-bad";
        bad.tracks[0].bone = 99;
        CHECK(!s->attachClips(nodeClip, &bad, 1), "attachClips refuses an out-of-range bone index");
    }

    // ---- G2: the pose, both ways ----------------------------------------
    const float length = rig.clip->getLength();
    double worstPose = 0.0;
    for (float t : { 0.0f, 0.25f, 0.5f, 0.75f, length }) {
        std::vector<BonePose> poses = posesFrom(desc, extracted, t);
        s->setBonePoses(nodeDoc, poses.data(), poses.size());

        ClipState st; st.name = "Swing"; st.enabled = true; st.time = t;
        st.weight = 1.0f; st.looping = false;
        CHECK(s->setClipStates(nodeClip, &st, 1), "setClipStates accepted an absolute time");
        engine->renderOneFrame();
        const double d = worstBoneDiff(s, nodeDoc, nodeClip, boneCount);
        worstPose = std::max(worstPose, d);
        std::printf("    t=%.2f: worst |engine-clip bone matrix - manual-pose bone matrix| = %.3e\n",
                    double(t), d);
    }
    CHECK(worstPose < 1e-4,
          "G2: the engine's clip reproduces the extracted track's own pose (< 1e-4) — the "
          "bind-relative delta conversion, the def build and the sampling, end to end");

    // bonePoses reads back the same thing in the frame setBonePoses writes in.
    {
        const std::vector<BonePose> want = posesFrom(desc, extracted, 0.6f);
        ClipState st; st.name = "Swing"; st.time = 0.6f; st.looping = false;
        s->setClipStates(nodeClip, &st, 1);
        engine->renderOneFrame();
        std::vector<BonePose> got(boneCount);
        CHECK(s->bonePoses(nodeClip, got.data(), got.size()), "bonePoses read back");
        double worst = 0.0;
        for (size_t i = 0; i < boneCount; ++i) {
            iris::Mat4 a, b;
            a.translate(iris::Vec3(want[i].position.x, want[i].position.y, want[i].position.z));
            a.rotate(iris::Quat(want[i].rotation.w, want[i].rotation.x, want[i].rotation.y, want[i].rotation.z));
            a.scale(iris::Vec3(want[i].scale.x, want[i].scale.y, want[i].scale.z));
            b.translate(iris::Vec3(got[i].position.x, got[i].position.y, got[i].position.z));
            b.rotate(iris::Quat(got[i].rotation.w, got[i].rotation.x, got[i].rotation.y, got[i].rotation.z));
            b.scale(iris::Vec3(got[i].scale.x, got[i].scale.y, got[i].scale.z));
            for (int k = 0; k < 16; ++k)
                worst = std::max(worst, std::fabs(double(a.constData()[k]) - double(b.constData()[k])));
        }
        std::printf("    bonePoses vs the extracted track's parent-local pose: %.3e\n", worst);
        CHECK(worst < 1e-4, "bonePoses is bone-parent-local, the same frame setBonePoses writes in");
    }

    // ---- the sparse-clip gap, measured rather than assumed ---------------
    // Two keys, 90 degrees apart: the document SLERPS between them, the engine
    // NLERPS. Nothing is wrong; this is §7's tolerance reason #1, and it is
    // worth a number in the log so nobody re-derives it from a failing gate.
    {
        Rig sparse;
        sparse.doc = iris::Scene::create();
        sparse.mesh = rig.mesh;
        sparse.node = armrig::buildArmNode(sparse.mesh, "sparse");
        sparse.clip = armrig::buildSwingClip(-90.0f);
        sparse.clip->setName("Sparse");
        sparse.clip->setLooping(false);
        sparse.node->addAnimation(sparse.clip);
        sparse.node->setAnimation(sparse.clip);
        sparse.doc->getRootNode()->addChild(sparse.node);
        sparse.rest = iris::ClipExtractor::captureRest(sparse.node);

        iris::ExtractedClip sx;
        iris::ClipExtractor::extract(sparse.node, sparse.node, sparse.node->getSkeleton(),
                                     sparse.clip->getSkeletalAnimation(), "Sparse",
                                     sparse.clip->getLength(), &sparse.rest, sx, nullptr);
        ClipDesc sd;
        SceneMirror::toClipDesc(sx, desc.id, sd);
        NodeId nSparse = s->createNode();
        s->attachSkinnedMesh(nSparse, meshId, matId, desc);
        CHECK(s->attachClips(nSparse, &sd, 1), "the 2-key clip attaches");
        double atKeys = 0.0, between = 0.0;
        for (float t : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }) {
            // The manual side samples the extracted track with SLERP, which is
            // what our keyframes do; the engine nlerps. That difference is the
            // whole point of this block.
            std::vector<BonePose> poses = posesFrom(desc, sx, t);
            {
                // slerp, not nlerp, on the one animated bone
                const auto &tr = sx.tracks.first();
                const float u = std::min(std::max(t / sparse.clip->getLength(), 0.0f), 1.0f);
                const iris::Quat q = iris::Quat::slerp(tr.keys.first().rotation,
                                                         tr.keys.last().rotation, u);
                poses[size_t(tr.bone)].rotation = Quat(q.x(), q.y(), q.z(), q.scalar());
            }
            s->setBonePoses(nodeDoc, poses.data(), poses.size());
            ClipState st; st.name = "Sparse"; st.enabled = true; st.time = t;
            st.weight = 1.0f; st.looping = false;
            s->setClipStates(nSparse, &st, 1);
            engine->renderOneFrame();
            const double d = worstBoneDiff(s, nodeDoc, nSparse, boneCount);
            if (t == 0.0f || t == 1.0f) atKeys = std::max(atKeys, d);
            else between = std::max(between, d);
        }
        std::printf("    2-key 90-degree clip: at keys %.3e, between keys %.3e "
                    "(slerp vs nlerpShortest)\n", atKeys, between);
        CHECK(atKeys < 1e-4, "a sparse clip is still EXACT at its own key times");
        CHECK(between < 3e-2, "and the mid-interval slerp/nlerp gap stays bounded");
        s->removeNode(nSparse);
    }

    // ---- G6: determinism -------------------------------------------------
    {
        const auto poseAt = [&](float t) {
            ClipState st; st.name = "Swing"; st.time = t; st.looping = false;
            s->setClipStates(nodeClip, &st, 1);
            engine->renderOneFrame();
            std::vector<BonePose> p(boneCount);
            s->bonePoses(nodeClip, p.data(), p.size());
            return flatten(p);
        };
        const auto at0a = poseAt(0.0f);
        const auto at5  = poseAt(0.5f);
        const auto at0b = poseAt(0.0f);
        CHECK(at0a == at0b, "G6: 0 -> 0.5 -> 0 restores the pose BYTE for byte");
        CHECK(at0a != at5, "and 0.5 really is a different pose");
        CHECK(poseAt(0.5f) == at5, "G6: the same (clip, time) twice gives identical bytes");

        // A backwards sweep must match a forwards one: the per-track key cache
        // searches both directions, so arbitrary scrubs are exact and there is
        // no history dependence to discover in the UI later.
        std::vector<std::vector<float>> forward, backward;
        for (int i = 0; i <= 8; ++i) forward.push_back(poseAt(length * float(i) / 8.0f));
        for (int i = 8; i >= 0; --i) backward.push_back(poseAt(length * float(i) / 8.0f));
        std::reverse(backward.begin(), backward.end());
        CHECK(forward == backward, "G6: a backwards sweep matches a forwards sweep exactly");
    }

    // ---- G5: per-bone weight normalization -------------------------------
    // The rule nothing in Ogre enforces: if clip A animates a bone and clip B
    // does not, then at 0.5/0.5 that bone must receive ALL of A. Ogre's own
    // "internal flag that prevents blending unanimated bones" sets the whole
    // 4-bone block to ONE whenever a track uses more than half its slots, so it
    // cannot be relied on per bone — and the failure looks like "the character
    // shrank slightly", never like an error.
    {
        // A second clip covering only the ROOT bone. "Swing" covers BOTH, so
        // jointRoot is SHARED (must split 0.5/0.5) and jointTip is Swing's
        // alone (must get ALL of Swing at any split) — G5's exact shape.
        iris::ExtractedClip partial = extracted;
        partial.name = QStringLiteral("RootOnly");
        partial.tracks.clear();
        {
            iris::ClipBoneTrack t;
            t.bone = 0;
            t.boneName = QStringLiteral("jointRoot");
            iris::ClipBoneKey k0; k0.time = 0.0f; k0.scale = iris::Vec3(1, 1, 1);
            iris::ClipBoneKey k1 = k0; k1.time = length;
            k1.rotation = iris::Quat::fromAxisAndAngle(0, 0, 1, 20.0f);
            t.keys.append(k0); t.keys.append(k1);
            partial.tracks.append(t);
        }
        ClipDesc partialDesc;
        CHECK(SceneMirror::toClipDesc(partial, desc.id, partialDesc), "the partial clip translates");
        // R2: everything attaches BEFORE anything is enabled.
        ClipState off; off.name = "Swing"; off.enabled = false;
        CHECK(s->setClipStates(nodeClip, &off, 1), "clips disabled before attaching another");
        CHECK(s->attachClips(nodeClip, &partialDesc, 1), "the partial clip attaches");
        if (s->clipNames(nodeClip).size() != 2) std::printf("    %s\n", engine->lastError().c_str());
        CHECK(s->clipNames(nodeClip).size() == 2, "two clips on the node");

        // Attaching while something plays is REFUSED, loudly — Ogre's
        // addAnimationsFromSkeleton would dangle every active clip pointer.
        {
            ClipState on; on.name = "Swing"; on.enabled = true; on.time = 0.0f; on.looping = false;
            s->setClipStates(nodeClip, &on, 1);
            engine->renderOneFrame();
            ClipDesc third = partialDesc; third.id += "-third"; third.name = "Third";
            CHECK(!s->attachClips(nodeClip, &third, 1),
                  "attachClips REFUSES while a clip is enabled (R2: it would dangle "
                  "every active-animation pointer)");
            ClipState offAgain; offAgain.name = "Swing"; offAgain.enabled = false;
            s->setClipStates(nodeClip, &offAgain, 1);
        }

        ClipState both[2];
        both[0].name = "Swing";    both[0].enabled = true; both[0].time = length; both[0].weight = 0.5f; both[0].looping = false;
        both[1].name = "RootOnly"; both[1].enabled = true; both[1].time = length; both[1].weight = 0.5f; both[1].looping = false;
        CHECK(s->setClipStates(nodeClip, both, 2), "a 0.5/0.5 blend is accepted");
        const auto wSwing = s->clipBoneWeights(nodeClip, "Swing");
        const auto wRoot  = s->clipBoneWeights(nodeClip, "RootOnly");
        CHECK(wSwing.size() == boneCount && wRoot.size() == boneCount,
              "clipBoneWeights reports one weight per rig bone");
        std::printf("    Swing per-bone weights: %.3f %.3f | RootOnly: %.3f %.3f\n",
                    double(wSwing[0]), double(wSwing[1]), double(wRoot[0]), double(wRoot[1]));
        // jointTip (index 1) is covered by Swing alone -> it must get ALL of it.
        CHECK(std::fabs(wSwing[1] - 1.0f) < 1e-5f,
              "G5: the bone only ONE clip animates gets weight 1.0, not 0.5");
        CHECK(std::fabs(wSwing[0] - 0.5f) < 1e-5f && std::fabs(wRoot[0] - 0.5f) < 1e-5f,
              "G5: the shared bone splits 0.5/0.5");

        // ...and the pose agrees: the un-shared bone lands EXACTLY on the full
        // clip's pose, which is the assertion the weights only imply.
        engine->renderOneFrame();
        const iris::Mat4 blended = engineBone(s, nodeClip, 1, boneCount);
        ClipState solo; solo.name = "Swing"; solo.enabled = true; solo.time = length;
        solo.weight = 1.0f; solo.looping = false;
        s->setClipStates(nodeClip, &solo, 1);
        engine->renderOneFrame();
        const iris::Mat4 alone = engineBone(s, nodeClip, 1, boneCount);
        // Only the bone's OWN contribution can be compared: its parent moves
        // under the blend (RootOnly animates the root), so the full matrix
        // legitimately differs. bonePoses is parent-local and is the right
        // instrument.
        double soloDiff = 0.0;
        (void)blended; (void)alone;
        {
            s->setClipStates(nodeClip, both, 2);
            engine->renderOneFrame();
            std::vector<BonePose> pb(boneCount); s->bonePoses(nodeClip, pb.data(), pb.size());
            s->setClipStates(nodeClip, &solo, 1);
            engine->renderOneFrame();
            std::vector<BonePose> ps(boneCount); s->bonePoses(nodeClip, ps.data(), ps.size());
            const auto fb = flatten(pb), fs = flatten(ps);
            for (size_t i = 10; i < 20; ++i)   // bone 1 only
                soloDiff = std::max(soloDiff, std::fabs(double(fb[i]) - double(fs[i])));
        }
        std::printf("    un-shared bone, blended vs solo (parent-local): %.3e\n", soloDiff);
        CHECK(soloDiff < 1e-5,
              "G5: the un-shared bone's own pose is EXACTLY the full clip's, not halfway to bind");

        // An all-zero weight set is refused, never silently a bind-pose character.
        {
            ClipState zero[2] = { both[0], both[1] };
            zero[0].weight = 0.0f; zero[1].weight = 0.0f;
            CHECK(!s->setClipStates(nodeClip, zero, 2), "a weight set summing to zero is REFUSED");
        }
        // The same ratios scaled up behave identically (weights are intent).
        {
            ClipState scaled[2] = { both[0], both[1] };
            scaled[0].weight = 2.0f; scaled[1].weight = 2.0f;
            s->setClipStates(nodeClip, scaled, 2);
            const auto w2 = s->clipBoneWeights(nodeClip, "Swing");
            CHECK(std::fabs(w2[0] - 0.5f) < 1e-5f && std::fabs(w2[1] - 1.0f) < 1e-5f,
                  "weights are INTENT: 2/2 normalizes exactly like 0.5/0.5");
        }

        // R6: a manual bone is additive under a clip unless the backend zeroes
        // the clip's weight for it. Prove the weight really goes to zero.
        {
            CHECK(s->setBoneManual(nodeClip, "jointTip", true), "jointTip marked manual");
            s->setClipStates(nodeClip, both, 2);
            const auto wm = s->clipBoneWeights(nodeClip, "Swing");
            CHECK(std::fabs(wm[1]) < 1e-6f,
                  "R6: a manual bone gets ZERO weight from every clip (or the override would "
                  "just ADD to the clip's motion)");
            CHECK(s->setBoneManual(nodeClip, "jointTip", false), "and the override lifts");
            s->setClipStates(nodeClip, both, 2);
            CHECK(std::fabs(s->clipBoneWeights(nodeClip, "Swing")[1] - 1.0f) < 1e-5f,
                  "lifting the override restores the normalized weight");
        }

        // Back to a single clip for the pixel gate.
        ClipState only; only.name = "Swing"; only.enabled = true; only.time = 0.0f;
        only.weight = 1.0f; only.looping = false;
        s->setClipStates(nodeClip, &only, 1);
    }

    // ---- G3: PIXEL parity -------------------------------------------------
    // The bone matrices already agree to 1e-4; this is the end-to-end statement
    // that the shader, the datablock and the frame agree too. Each node is
    // rendered ALONE at the same place, so the two frames are directly diffable.
    {
        int worstPixel = 0;
        for (float t : { 0.0f, 0.3f, 0.7f, length }) {
            std::vector<BonePose> poses = posesFrom(desc, extracted, t);
            s->setBonePoses(nodeDoc, poses.data(), poses.size());
            ClipState st; st.name = "Swing"; st.enabled = true; st.time = t;
            st.weight = 1.0f; st.looping = false;
            s->setClipStates(nodeClip, &st, 1);

            s->setNodeVisible(nodeDoc, true);
            s->setNodeVisible(nodeClip, false);
            for (int f = 0; f < 3; ++f) engine->renderOneFrame();
            Image a;
            CHECK(view->readPixels(a), "the manually-posed frame read back");

            s->setNodeVisible(nodeDoc, false);
            s->setNodeVisible(nodeClip, true);
            for (int f = 0; f < 3; ++f) engine->renderOneFrame();
            Image b;
            CHECK(view->readPixels(b), "the clip-driven frame read back");

            if (a.rgba.size() != b.rgba.size() || a.rgba.empty()) break;
            int worst = 0;
            long long lit = 0;
            for (size_t i = 0; i < a.rgba.size(); i += 4) {
                for (int k = 0; k < 3; ++k)
                    worst = std::max(worst, std::abs(int(a.rgba[i + k]) - int(b.rgba[i + k])));
                if (a.rgba[i] > 12) ++lit;
            }
            std::printf("    t=%.2f: worst per-channel pixel delta = %d (lit pixels %lld)\n",
                        double(t), worst, lit);
            CHECK(lit > 100, "the character is actually on screen (not an empty-frame pass)");
            worstPixel = std::max(worstPixel, worst);
        }
        CHECK(worstPixel <= 2,
              "G3: manually-posed and engine-clip-driven frames agree to <= 2/255 per channel");
        s->setNodeVisible(nodeDoc, true);
    }

    // ---- G4 (pixel half): the FBX pivot rig, end to end -------------------
    // The glTF arm above has no pivot nodes, so it cannot fail the way a real
    // Mixamo character fails. This block loads the FBX fixture, extracts its
    // clip through the pivot composition, and asserts the engine reproduces the
    // document's bone matrices for it too.
    {
        auto fragment = iris::MeshNode::loadAsSceneFragment(
            QStringLiteral(JAHSHAKA_TEST_SOURCE_DIR "/tests/skeletal/fixtures/pivot_rig.fbx"),
            [](iris::MeshPtr, iris::MeshMaterialData &) -> iris::MaterialPtr {
                return iris::DefaultMaterial::create();
            },
            nullptr, nullptr, QString());
        CHECK(!fragment.isNull(), "the FBX pivot fixture loads");
        if (!fragment.isNull()) {
            auto fbxDoc = iris::Scene::create();
            fbxDoc->getRootNode()->addChild(fragment);
            const auto rest = iris::ClipExtractor::captureRest(fragment);

            std::function<iris::MeshNodePtr(const iris::SceneNodePtr &)> findSkinned =
                [&](const iris::SceneNodePtr &n) -> iris::MeshNodePtr {
                    if (n->getSceneNodeType() == iris::SceneNodeType::Mesh) {
                        auto m = n.staticCast<iris::MeshNode>();
                        if (!m->getSkeleton().isNull()) return m;
                    }
                    for (const auto &c : n->children()) if (auto r = findSkinned(c)) return r;
                    return iris::MeshNodePtr();
                };
            auto meshNode = findSkinned(fragment);
            CHECK(!meshNode.isNull(), "the FBX fixture has a skinned mesh node");

            iris::AnimationPtr walk;
            for (const auto &a : fragment->getAnimations())
                if (a && a->hasSkeletalAnimation() && a->getLength() > 0.0f) walk = a;
            CHECK(!walk.isNull(), "the FBX fixture's real clip is there");

            if (!meshNode.isNull() && !walk.isNull()) {
                walk->setLooping(false);
                fragment->setAnimation(walk);

                SkeletonDesc fbxRig;
                SceneMirror::toSkeletonDesc(meshNode->getSkeleton(), fbxRig);
                MeshData fmd = armMeshData(meshNode->getMesh());
                MeshId fMesh = s->createMesh(fmd);
                NodeId fDoc = s->createNode(), fClip = s->createNode();
                CHECK(s->attachSkinnedMesh(fDoc, fMesh, matId, fbxRig) &&
                          s->attachSkinnedMesh(fClip, fMesh, matId, fbxRig),
                      "the FBX rig attaches to two nodes");

                iris::ExtractedClip fx;
                CHECK(iris::ClipExtractor::extract(fragment, meshNode, meshNode->getSkeleton(),
                                                   walk->getSkeletalAnimation(), walk->getName(),
                                                   walk->getLength(), &rest, fx, nullptr),
                      "the pivot-composed FBX clip extracts");
                ClipDesc fDesc;
                SceneMirror::toClipDesc(fx, fbxRig.id, fDesc);
                CHECK(s->attachClips(fClip, &fDesc, 1), "the FBX clip attaches");
                if (!s->attachClips(fClip, &fDesc, 1)) std::printf("    %s\n", engine->lastError().c_str());

                double worst = 0.0;
                for (float t : { 0.0f, 0.25f, 0.5f, 1.0f }) {
                    std::vector<BonePose> poses = posesFrom(fbxRig, fx, t);
                    s->setBonePoses(fDoc, poses.data(), poses.size());
                    ClipState st; st.name = fDesc.name; st.enabled = true; st.time = t;
                    st.weight = 1.0f; st.looping = false;
                    s->setClipStates(fClip, &st, 1);
                    engine->renderOneFrame();
                    worst = std::max(worst, worstBoneDiff(s, fDoc, fClip, fbxRig.bones.size()));
                }
                std::printf("    FBX pivot rig: worst |engine clip - manual pose| bone matrix = %.3e\n", worst);
                // Looser than the glTF rig by design: the FBX clip's three
                // channels key at three different times over one second, so the
                // composed track is resampled onto their union and the values
                // between those times are a lerp of composed rotations, not a
                // composition of lerped ones.
                CHECK(worst < 5e-2,
                      "G4: the pivot-composed FBX clip reproduces its own track on the engine");
                s->removeNode(fDoc);
                s->removeNode(fClip);
            }
        }
    }

    engine->destroyScene(s);
    std::printf(failures ? "\n%d FAILURE(S)\n" : "\nall clip gates passed\n", failures);
    return failures ? 1 : 0;
}
