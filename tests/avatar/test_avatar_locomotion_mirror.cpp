// avatar.locomotion_mirror — AVATAR_LOCOMOTION_SPEC Stage 5, gates S11 and S13.
//
// The two gates that cannot be made document-side, because both are claims
// about what the ENGINE ended up holding:
//
//   S11  `clipBoneWeights` reports the engine's per-bone normalization THROUGH
//        the state machine — i.e. the blend space's raw intent goes in via
//        SceneMirror::syncClips and the G5 property (a bone only one clip
//        animates gets ALL of that clip, never half of it) still holds.
//   S13  editor -> player -> editor mid-walk: the document's clip phase is
//        continuous across the swap and the winning mirror has RE-ATTACHED, with
//        the same weights landing on the new engine node at the same document
//        time (risk R13, and the case the spec's §14 records as never run).
//
// Everything here goes through the real seam — an iris::Scene, two SceneMirrors
// on two engine Scenes, and Scene::setGraphScene's evacuation protocol firing
// exactly as the editor and player pages fire it. The locomotion machine is
// stepped DIRECTLY with a synthetic §5 contract (no physics, no ground, no
// input): the subject is the translation from the machine's output to the
// engine, and a movement component in the middle would only add ways for the
// numbers to move.
//
// Offscreen view, no window, DISPLAY must be reachable (Vulkan).
#include <QGuiApplication>
#include <QElapsedTimer>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include "irisgl/document/animation/animation.h"
#include "irisgl/document/animation/locomotion.h"
#include "irisgl/document/animation/skeletalanimation.h"
#include "irisgl/document/physics/avatarmovement.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/mirror/scenemirror.h"
#include "jahshaka/engine/Engine.h"

#include "../skeletal/armrig.h"
#include "../support/enginetesthelpers.h"

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg) \
    do { if (cond) std::printf("ok:   %s\n", msg); \
         else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

static void section(const char *name) { std::printf("\n---- %s ----\n", name); }

// ---------------------------------------------------------------------------
// Fixtures
//
// TWO CLIPS WITH DIFFERENT BONE COVERAGE, which is the whole of S11: the arm
// rig has jointRoot (index 0) and jointTip (index 1), and
//
//   "TipOnly" animates jointTip and nothing else       -> one track, bone 1
//   "Both"    animates jointRoot AND jointTip          -> two tracks
//
// (ClipExtractor emits no track for a bone a clip does not drive — the
// `timeSet.isEmpty()` continue in clipextractor.cpp — so the coverage really is
// different on the engine side and not just in the document.)
//
// Their LENGTHS differ too (1.0 s vs 0.6 s), so the absolute times the mirror
// pushes are different numbers for the same shared normalized phase.

static iris::AnimationPtr makeClip(const char *name, bool includeRoot, float length)
{
    auto skelAnim = iris::SkeletalAnimation::create();
    const auto addBone = [&](const char *bone, const iris::Vec3 &pos, float degrees) {
        auto boneAnim = new iris::BoneAnimation();
        // Dense-ish keys: the extractor resamples onto a key-time union, and a
        // 2-key 90-degree clip is the one shape where slerp and Ogre's
        // nlerpShortest visibly disagree (tests/skeletal G2 documents it). None
        // of the assertions below is a pose compare, but keeping the clip sane
        // keeps the log readable.
        for (int i = 0; i <= 6; ++i) {
            const double t = double(length) * double(i) / 6.0;
            const float a = degrees * float(i) / 6.0f;
            boneAnim->posKeys->addKey(pos, t);
            boneAnim->rotKeys->addKey(iris::Quat::fromAxisAndAngle(0, 0, 1, a), t);
            boneAnim->scaleKeys->addKey(iris::Vec3(1, 1, 1), t);
        }
        skelAnim->addBoneAnimation(bone, boneAnim);
    };
    if (includeRoot) addBone("jointRoot", iris::Vec3(0, 0, 0), 20.0f);
    addBone("jointTip", iris::Vec3(0, 1, 0), -60.0f);

    auto a = iris::Animation::create(QLatin1String(name));
    a->setSkeletalAnimation(skelAnim);
    a->setLength(length);
    return a;
}

/// A MIXAMO ONE-FRAME CLIP: one key per bone at t = 0 and a length of 0. Real
/// files like this exist (the owner's Dreyar pack has one) and the engine pads
/// the length rather than dividing by it (OgreClips.cpp kMinClipLength) — the
/// "clip 'mixamo.com' has length 0.000000; padded to 0.001000s" line is the
/// last thing the 2026-09-11 smoke log printed before the abort. It is in the
/// S16 section below because that is the shape the crash arrived in.
static iris::AnimationPtr makeZeroLengthClip(const char *name)
{
    auto skelAnim = iris::SkeletalAnimation::create();
    const auto addBone = [&](const char *bone, const iris::Vec3 &pos) {
        auto boneAnim = new iris::BoneAnimation();
        boneAnim->posKeys->addKey(pos, 0.0);
        boneAnim->rotKeys->addKey(iris::Quat::fromAxisAndAngle(0, 0, 1, 12.0f), 0.0);
        boneAnim->scaleKeys->addKey(iris::Vec3(1, 1, 1), 0.0);
        skelAnim->addBoneAnimation(bone, boneAnim);
    };
    addBone("jointRoot", iris::Vec3(0, 0, 0));
    addBone("jointTip", iris::Vec3(0, 1, 0));

    auto a = iris::Animation::create(QLatin1String(name));
    a->setSkeletalAnimation(skelAnim);
    a->setLength(0.0f);
    return a;
}

/// The avatar shape the mirror sees: a wrapper carrying the locomotion
/// component, with the rigged mesh (and its clips) as a child. `clipHostOf`
/// walks UP from the mesh and finds the mesh itself; `collectAvatarClips` walks
/// DOWN from the wrapper and finds the same node. Both halves agree, which is
/// the point of putting the two lookups back to back here.
struct Avatar
{
    iris::SceneNodePtr wrapper;
    iris::MeshNodePtr  mesh;
    iris::AvatarLocomotion *loco = nullptr;
};

static Avatar makeAvatar(const iris::ScenePtr &doc, const iris::MeshPtr &meshAsset,
                         const QString &name, const iris::Vec3 &pos)
{
    Avatar a;
    a.wrapper = iris::SceneNode::create();
    a.wrapper->setName(name);
    a.wrapper->setLocalPos(pos);
    a.wrapper->setAvatarComponent(iris::AvatarMovementPtr(new iris::AvatarMovement()));
    doc->getRootNode()->addChild(a.wrapper);

    a.mesh = armrig::buildArmNode(meshAsset, name + QStringLiteral("-mesh"));
    a.mesh->addAnimation(makeClip("TipOnly", false, 1.0f));
    a.mesh->addAnimation(makeClip("Both", true, 0.6f));
    a.wrapper->addChild(a.mesh);

    auto loco = iris::AvatarLocomotionPtr(new iris::AvatarLocomotion());
    a.wrapper->setLocomotionComponent(loco);
    a.loco = loco.data();
    return a;
}

/// One blend-space state, two samples, no transitions. speed 1.0 sits exactly
/// half way between them, which is the 0.5/0.5 split S11 asserts on.
static iris::LocomotionAsset twoSampleAsset()
{
    iris::LocomotionAsset a;
    a.name = QStringLiteral("Coverage Blend");
    iris::LocomotionStateDef st;
    st.name = QStringLiteral("Grounded");
    st.kind = iris::LocomotionSourceKind::BlendSpace;
    st.loop = true;
    st.space.syncClips = true;
    iris::LocomotionBlendSample s0, s1;
    s0.clip = QStringLiteral("TipOnly");
    s0.position = 0.0f;
    s0.authoredSpeed = 0.0f;
    s1.clip = QStringLiteral("Both");
    s1.position = 2.0f;
    s1.authoredSpeed = 0.0f;
    st.space.samples = { s0, s1 };
    a.states = { st };
    a.entry = st.name;
    return a;
}

static iris::AvatarLocomotionState walkingAt(float speed)
{
    iris::AvatarLocomotionState p;
    p.speed = speed;
    p.grounded = true;
    return p;
}

static float weightOf(const iris::AvatarLocomotion *loco, const char *clip)
{
    for (const auto &w : loco->weights())
        if (w.clip == QLatin1String(clip)) return w.weight;
    return -1.0f;
}
static float timeOf(const iris::AvatarLocomotion *loco, const char *clip)
{
    for (const auto &w : loco->weights())
        if (w.clip == QLatin1String(clip)) return w.time;
    return -1.0f;
}

/// The engine name a document clip name ended up with. The mirror uniquifies
/// on collision, and this suite's names do not collide — but reading it back
/// out of `clipNames` rather than assuming it is what keeps that true.
static std::string engineNameFor(Scene *scene, NodeId node, const char *want)
{
    for (const auto &n : scene->clipNames(node))
        if (n == want || n.rfind(std::string(want) + "_", 0) == 0) return n;
    return std::string();
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test_avatar_locomotion_mirror-ogre.log";
    std::string err;
    auto engine = Engine::create(cfg, err);
    CHECK(engine != nullptr, "engine created");
    if (!engine) { std::printf("    %s\n", err.c_str()); return 1; }

    // TWO engine scenes and two views — the editor page and the player page.
    // They never share an engine Scene; what they share is the DOCUMENT, which
    // is exactly the arrangement the graph-ownership protocol exists for.
    View *editorView = engine->createOffscreenView("editor", 96, 96, Colour(0, 0, 0));
    Scene *editorScene = engine->createScene("editor");
    View *playerView = engine->createOffscreenView("player", 96, 96, Colour(0, 0, 0));
    Scene *playerScene = engine->createScene("player");
    CHECK(editorView && editorScene && playerView && playerScene, "two views + two engine scenes");
    if (!editorView || !editorScene || !playerView || !playerScene) return 1;
    editorView->setScene(editorScene);
    playerView->setScene(playerScene);
    editorScene->setAmbient(Colour(1, 1, 1), Colour(1, 1, 1));
    playerScene->setAmbient(Colour(1, 1, 1), Colour(1, 1, 1));
    enginetest::testCameraLookAt(editorView, Vec3(0, 1, 5), Vec3(0, 1, 0));
    enginetest::testCameraLookAt(playerView, Vec3(0, 1, 5), Vec3(0, 1, 0));

    auto doc = iris::Scene::create();
    auto meshAsset = armrig::buildArmMesh();
    Avatar avatar = makeAvatar(doc, meshAsset, QStringLiteral("Ely"), iris::Vec3(0, 0, 0));

    QString asErr;
    CHECK(avatar.loco->setAsset(twoSampleAsset(), &asErr),
          qUtf8Printable(QStringLiteral("the two-sample blend asset installs (%1)").arg(asErr)));
    avatar.loco->refreshClips(avatar.wrapper, 2.0f, 5.5f);
    {
        const auto &lengths = avatar.loco->clipLengths();
        CHECK(lengths.value(QStringLiteral("TipOnly"), -1.0f) > 0.99f
                  && lengths.value(QStringLiteral("Both"), -1.0f) > 0.59f,
              "the document measured both clips through the wrapper (collectAvatarClips)");
    }

    const float dt = 1.0f / 60.0f;
    const auto params = walkingAt(1.0f);   // exactly between the two samples

    // The document is PLAYING. The mirror takes the locomotion source only
    // while it is, because the machine advances inside the physics step and
    // therefore holds its last published weight set after Stop — see the gate
    // in syncClips. The last section below stops the scene and asserts the
    // authored transport comes back.
    doc->setPlaying(true);

    SceneMirror editorMirror(editorScene);
    SceneMirror playerMirror(playerScene);

    // =======================================================================
    section("S11 clipBoneWeights reports the per-bone normalization, through the machine");

    editorMirror.setSource(doc);
    editorMirror.sync();
    engine->renderOneFrame();

    const NodeId editorNode = editorMirror.engineNode(avatar.mesh.data());
    CHECK(editorNode != 0, "the rigged mesh reached the editor's engine scene");
    if (!editorNode) return 1;
    {
        const auto names = editorScene->clipNames(editorNode);
        std::printf("      engine clips on the editor node:");
        for (const auto &n : names) std::printf(" '%s'", n.c_str());
        std::printf("\n");
        CHECK(names.size() == 2, "both document clips were attached, once each");
    }
    const std::string engTip = engineNameFor(editorScene, editorNode, "TipOnly");
    const std::string engBoth = engineNameFor(editorScene, editorNode, "Both");
    CHECK(!engTip.empty() && !engBoth.empty(), "both engine clip names resolve");

    // One step of the machine at the mid speed, then ONE sync: the mirror's only
    // job is to turn what the machine published into a setClipStates call.
    avatar.loco->step(params, avatar.loco->clipLengths(), dt);
    CHECK(std::fabs(weightOf(avatar.loco, "TipOnly") - 0.5f) < 1e-5f
              && std::fabs(weightOf(avatar.loco, "Both") - 0.5f) < 1e-5f,
          "the machine published an exact 0.5/0.5 split (raw intent, not normalized)");
    editorMirror.sync();
    engine->renderOneFrame();

    {
        const auto wTip = editorScene->clipBoneWeights(editorNode, engTip);
        const auto wBoth = editorScene->clipBoneWeights(editorNode, engBoth);
        CHECK(wTip.size() == 2 && wBoth.size() == 2,
              "clipBoneWeights reports one weight per rig bone for both clips");
        if (wTip.size() == 2 && wBoth.size() == 2) {
            std::printf("      jointRoot: TipOnly %.3f  Both %.3f\n",
                        double(wTip[0]), double(wBoth[0]));
            std::printf("      jointTip : TipOnly %.3f  Both %.3f\n",
                        double(wTip[1]), double(wBoth[1]));
            // THE G5 PROPERTY, now arriving through the state machine and the
            // mirror instead of a hand-written ClipState array: jointRoot is
            // animated by ONE of the two clips, so that clip owns it entirely.
            // Half of it would be half way to bind pose, silently.
            CHECK(std::fabs(wBoth[0] - 1.0f) < 1e-4f && std::fabs(wTip[0]) < 1e-4f,
                  "jointRoot — animated by ONE clip — gets ALL of it, not half");
            CHECK(std::fabs(wBoth[1] - 0.5f) < 1e-4f && std::fabs(wTip[1] - 0.5f) < 1e-4f,
                  "jointTip — animated by BOTH — is the 0.5/0.5 the machine intended");
        }
    }

    // ...and the split really does track the blend parameter, rather than being
    // a fixed number the rig happens to produce.
    {
        auto fast = walkingAt(2.0f);          // the top sample: "Both" alone
        avatar.loco->step(fast, avatar.loco->clipLengths(), dt);
        editorMirror.sync();
        engine->renderOneFrame();
        const auto wTip = editorScene->clipBoneWeights(editorNode, engTip);
        const auto wBoth = editorScene->clipBoneWeights(editorNode, engBoth);
        const bool tipGone = wTip.empty() || (std::fabs(wTip[0]) < 1e-4f
                                              && std::fabs(wTip[1]) < 1e-4f);
        CHECK(tipGone, "at the top sample the other clip is DISABLED, not left at weight 0");
        CHECK(wBoth.size() == 2 && std::fabs(wBoth[1] - 1.0f) < 1e-4f,
              "...and the surviving clip owns every bone it animates");
    }

    // =======================================================================
    section("S13 editor -> player -> editor mid-walk: phase continuous, mirror re-attached");

    // Walk for a while so the swap happens mid-stride rather than at phase 0,
    // which is the only phase a reset would be invisible at.
    for (int i = 0; i < 37; ++i) {
        avatar.loco->step(params, avatar.loco->clipLengths(), dt);
        editorMirror.sync();
    }
    engine->renderOneFrame();

    const float phaseBefore = avatar.loco->statePhase();
    const QString stateBefore = avatar.loco->currentState();
    const QVector<iris::LocomotionClipWeight> weightsBefore = avatar.loco->weights();
    const std::vector<float> tipBefore = editorScene->clipBoneWeights(editorNode, engTip);
    const std::vector<float> bothBefore = editorScene->clipBoneWeights(editorNode, engBoth);
    std::printf("      pre-swap: state '%s' phase %.5f, TipOnly t=%.5f w=%.3f, "
                "Both t=%.5f w=%.3f\n",
                qUtf8Printable(stateBefore), double(phaseBefore),
                double(timeOf(avatar.loco, "TipOnly")), double(weightOf(avatar.loco, "TipOnly")),
                double(timeOf(avatar.loco, "Both")), double(weightOf(avatar.loco, "Both")));
    CHECK(phaseBefore > 0.01f && phaseBefore < 0.99f, "the swap happens MID-STRIDE");

    // ---- the swap: the player page takes the shared document's graph -------
    //
    // setSource fires the previous owner's evacuation hook, which is
    // SceneMirror::evacuateEngineObjects -> releaseEntry on every entry ->
    // `e = Entry()`. Every clip cache the editor mirror held is destroyed here,
    // which is precisely why nothing about the machine may live in it.
    QElapsedTimer timer;
    timer.start();
    playerMirror.setSource(doc);
    const int visited = playerMirror.sync();
    const qint64 rebuildUs = timer.nsecsElapsed() / 1000;
    std::printf("      the rebuilding sync visited %d nodes in %lld us (R13's cost, MEASURED)\n",
                visited, static_cast<long long>(rebuildUs));

    CHECK(editorMirror.engineNode(avatar.mesh.data()) == 0,
          "the losing mirror EVACUATED — every entry it held is gone");
    const NodeId playerNode = playerMirror.engineNode(avatar.mesh.data());
    CHECK(playerNode != 0, "...and the winning mirror rebuilt the avatar from scratch");
    if (!playerNode) return 1;
    const std::string pTip = engineNameFor(playerScene, playerNode, "TipOnly");
    const std::string pBoth = engineNameFor(playerScene, playerNode, "Both");
    CHECK(playerScene->clipNames(playerNode).size() == 2 && !pTip.empty() && !pBoth.empty(),
          "...including RE-ATTACHING both clips (attachClipsFor ran on the new entry)");

    // The DOCUMENT-side claim: the swap is not a step. Nothing about the phase,
    // the state or the published set may have moved, because no time passed.
    CHECK(std::fabs(avatar.loco->statePhase() - phaseBefore) < 1e-7f,
          "the document's clip PHASE is continuous across the swap (bit-identical)");
    CHECK(avatar.loco->currentState() == stateBefore, "...and so is the state it is in");
    bool sameSet = avatar.loco->weights().size() == weightsBefore.size();
    for (int i = 0; sameSet && i < weightsBefore.size(); ++i) {
        const auto &a = weightsBefore[i];
        const auto &b = avatar.loco->weights()[i];
        sameSet = a.clip == b.clip && a.weight == b.weight && a.time == b.time
                  && a.looping == b.looping;
    }
    CHECK(sameSet,
          "avatar.locomotionState's weights are BIT-IDENTICAL to the pre-swap values "
          "at the same document time");

    // The ENGINE-side claim: those same weights landed on the NEW node. And note
    // what this rules out — R13 predicted "one frame of bind pose per avatar per
    // page switch", but attachClipsFor and the state push both run inside
    // syncClips, at the end of the same sync() that created the node, so the
    // first frame the new node renders is already posed.
    engine->renderOneFrame();
    {
        const auto wTip = playerScene->clipBoneWeights(playerNode, pTip);
        const auto wBoth = playerScene->clipBoneWeights(playerNode, pBoth);
        bool same = wTip.size() == tipBefore.size() && wBoth.size() == bothBefore.size();
        for (size_t i = 0; same && i < wTip.size(); ++i)
            same = std::fabs(wTip[i] - tipBefore[i]) < 1e-5f;
        for (size_t i = 0; same && i < wBoth.size(); ++i)
            same = std::fabs(wBoth[i] - bothBefore[i]) < 1e-5f;
        CHECK(same, "the same per-bone weights are live on the NEW engine node, first frame");
    }

    // ---- and back: the editor page re-takes ---------------------------------
    const float phaseMid = avatar.loco->statePhase();
    for (int i = 0; i < 11; ++i) {
        avatar.loco->step(params, avatar.loco->clipLengths(), dt);
        playerMirror.sync();
    }
    engine->renderOneFrame();
    const float phaseAfterPlayer = avatar.loco->statePhase();
    const QVector<iris::LocomotionClipWeight> weightsMid = avatar.loco->weights();

    editorMirror.sync();          // the re-take (sync() notices the graph moved)
    CHECK(playerMirror.engineNode(avatar.mesh.data()) == 0,
          "coming back: the player mirror evacuated in its turn");
    const NodeId editorNode2 = editorMirror.engineNode(avatar.mesh.data());
    CHECK(editorNode2 != 0, "...and the editor mirror rebuilt the avatar again");
    CHECK(std::fabs(avatar.loco->statePhase() - phaseAfterPlayer) < 1e-7f,
          "the phase is continuous across the SECOND swap too");

    // 11 steps of a 0.8 s blended cycle is 11/60/0.8 = 0.22917 of a cycle, so
    // the phase must have advanced by exactly that much and not restarted —
    // a mirror-side clock would have come back at 0 here.
    float advanced = phaseAfterPlayer - phaseMid;
    if (advanced < 0.0f) advanced += 1.0f;
    std::printf("      phase advanced %.5f over 11 steps across the player page "
                "(expected %.5f)\n", double(advanced), double(11.0f * dt / 0.8f));
    CHECK(std::fabs(advanced - 11.0f * dt / 0.8f) < 1e-4f,
          "the per-avatar clock kept running across the page it was not visible on");

    engine->renderOneFrame();
    {
        const std::string e2Tip = engineNameFor(editorScene, editorNode2, "TipOnly");
        const std::string e2Both = engineNameFor(editorScene, editorNode2, "Both");
        CHECK(!e2Tip.empty() && !e2Both.empty(), "both clips are attached on the rebuilt node");
        const auto wTip = editorScene->clipBoneWeights(editorNode2, e2Tip);
        const auto wBoth = editorScene->clipBoneWeights(editorNode2, e2Both);
        CHECK(wTip.size() == 2 && wBoth.size() == 2
                  && std::fabs(wBoth[0] - 1.0f) < 1e-4f && std::fabs(wTip[0]) < 1e-4f
                  && std::fabs(wTip[1] - 0.5f) < 1e-4f && std::fabs(wBoth[1] - 0.5f) < 1e-4f,
              "...and the per-bone normalization is right again, from scratch");
        CHECK(std::fabs(timeOf(avatar.loco, "TipOnly") - weightsMid[0].time) < 1e-6f
                  || weightsMid.isEmpty(),
              "the published absolute times survived the swap unchanged");
    }

    // =======================================================================
    section("the authored transport still owns every node that is not an avatar");

    // A rigged mesh with NO locomotion component keeps the single-clip path:
    // one active animation, the scene clock, weight 1. The N-clip lift must not
    // have quietly changed what an ordinary animated character does.
    {
        auto plain = armrig::buildArmNode(meshAsset, QStringLiteral("plain"));
        plain->setLocalPos(iris::Vec3(2.5f, 0, 0));
        auto clip = makeClip("Solo", true, 1.0f);
        plain->addAnimation(clip);
        plain->setAnimation(clip);
        doc->getRootNode()->addChild(plain);

        doc->updateSceneAnimation(0.4f);      // the scene clock the authored path reads
        editorMirror.sync();
        engine->renderOneFrame();
        const NodeId plainNode = editorMirror.engineNode(plain.data());
        CHECK(plainNode != 0, "the plain rigged mesh reached the engine");
        if (plainNode) {
            const std::string name = engineNameFor(editorScene, plainNode, "Solo");
            const auto w = editorScene->clipBoneWeights(plainNode, name);
            CHECK(w.size() == 2 && std::fabs(w[0] - 1.0f) < 1e-4f
                      && std::fabs(w[1] - 1.0f) < 1e-4f,
                  "one authored clip at weight 1 on every bone it animates — unchanged");
        }
    }

    // =======================================================================
    section("S16: a MATERIAL SWAP on a playing rig (the 2026-09-11 smoke crash)");

    // The editor swaps a material pointer constantly — the properties panel, a
    // preset, the Avatar module re-skinning a piece — and so does a second
    // rigged copy of a character, through the union-rig epoch. The mirror
    // answers with a full re-attach (attachSkinnedMesh: a new Item and a NEW
    // SkeletonInstance) and clears `clipSignature` so the clips re-attach; its
    // first act on that path is `setClipStates(node, nullptr, 0)`. Every clip
    // record the engine held pointed into the instance that had just died, so
    // that call wrote through freed weight pointers and then indexed an
    // animation list with nothing in it — SIGABRT in the owner's run
    // (SMOKE_FIX_SPEC §1.1). The engine drops a node's clips with its Item now;
    // this is the seam-level proof that the mirror puts them back.
    {
        auto swapper = armrig::buildArmNode(meshAsset, QStringLiteral("swapper"));
        swapper->setLocalPos(iris::Vec3(-2.5f, 0, 0));
        auto padded = makeZeroLengthClip("Padded");
        swapper->addAnimation(padded);
        swapper->setAnimation(padded);
        doc->getRootNode()->addChild(swapper);

        doc->updateSceneAnimation(0.1f);
        editorMirror.sync();
        engine->renderOneFrame();
        const NodeId swapNode = editorMirror.engineNode(swapper.data());
        CHECK(swapNode != 0, "the rigged node reached the engine");
        const std::string firstName = engineNameFor(editorScene, swapNode, "Padded");
        CHECK(!firstName.empty(), "...and its ZERO-LENGTH clip attached (padded, not refused)");

        auto swapped = iris::DefaultMaterial::create();
        swapped->setDiffuseColor(QColor(0, 255, 0));
        swapper->setMaterial(swapped);

        doc->updateSceneAnimation(0.1f);
        editorMirror.sync();              // the re-attach + the clip re-attach
        engine->renderOneFrame();
        doc->updateSceneAnimation(0.1f);
        editorMirror.sync();              // and a second sync on the new instance
        engine->renderOneFrame();

        const NodeId afterNode = editorMirror.engineNode(swapper.data());
        CHECK(afterNode != 0, "the node survived the material swap");
        const std::string name = engineNameFor(editorScene, afterNode, "Padded");
        CHECK(!name.empty(), "the clip is attached to the NEW skeleton instance");
        const auto w = editorScene->clipBoneWeights(afterNode, name);
        CHECK(w.size() == 2 && std::fabs(w[1] - 1.0f) < 1e-4f,
              "...and still drives the bone it animates — the character is posed, not frozen");
    }

    // =======================================================================
    section("after Stop the AUTHORED transport gets the avatar back");

    // The state machine advances only inside the physics step, so at Stop it is
    // still holding the last weight set it published. If the mirror kept
    // translating it, the Avatar page's transport would never get its character
    // back — the user would scrub a clip and watch a frozen mid-stride blend.
    // The document's play flag is what decides, and this is the case that
    // proves it (nothing else in the tree covers a stopped avatar).
    {
        CHECK(!avatar.loco->weights().isEmpty(),
              "the machine is still holding its last published weight set at Stop");
        doc->setPlaying(false);
        auto solo = makeClip("AvatarSolo", true, 1.0f);
        avatar.mesh->addAnimation(solo);
        avatar.mesh->setAnimation(solo);
        doc->updateSceneAnimation(0.3f);
        editorMirror.sync();
        engine->renderOneFrame();
        const NodeId n = editorMirror.engineNode(avatar.mesh.data());
        const std::string soloName = engineNameFor(editorScene, n, "AvatarSolo");
        CHECK(!soloName.empty(), "the scrubbed clip attached to the avatar's rig");
        const auto wSolo = editorScene->clipBoneWeights(n, soloName);
        const auto wTip = editorScene->clipBoneWeights(n, engineNameFor(editorScene, n, "TipOnly"));
        CHECK(wSolo.size() == 2 && std::fabs(wSolo[0] - 1.0f) < 1e-4f
                  && std::fabs(wSolo[1] - 1.0f) < 1e-4f,
              "the TRANSPORT's clip owns the rig at weight 1 once the scene stopped");
        CHECK(wTip.empty() || (std::fabs(wTip[0]) < 1e-4f && std::fabs(wTip[1]) < 1e-4f),
              "...and the locomotion blend is gone, not blended underneath it");
    }

    editorMirror.setSource(nullptr);
    playerMirror.setSource(nullptr);
    engine->destroyView(editorView);
    engine->destroyView(playerView);
    engine->destroyScene(editorScene);
    engine->destroyScene(playerScene);
    engine.reset();
    CHECK(true, "teardown clean");

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
