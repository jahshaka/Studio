// avatar.locomotion — AVATAR_LOCOMOTION_SPEC Stage 4, gates S1-S7.
//
// The locomotion state machine (§7) driven through the REAL seam wherever a
// seam exists — an iris::Scene, its iris::Environment, and Scene::update(dt),
// i.e. exactly the chain PlayBack::update drives at Play — and driven DIRECTLY
// (a synthetic §5 contract into AvatarLocomotion::step) where the gate is about
// arithmetic the physics would only obscure. Both are named at each section.
//
// Runs offscreen with DISPLAY unset (the document graph boots Ogre's NULL
// render system). Framework-free; non-zero exit on failure.

#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "irisgl/irisglfwd.h"
#include "irisgl/core/math/vec.h"
#include "irisgl/document/animation/animation.h"
#include "irisgl/document/animation/locomotion.h"
#include "irisgl/document/animation/skeletalanimation.h"
#include "irisgl/document/physics/avatarmovement.h"
#include "irisgl/document/physics/environment.h"
#include "irisgl/document/physics/physicsproperties.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"

#include "../support/documentgraph.h"

static int failures = 0;
#define CHECK(cond, msg) \
    do { if (cond) printf("ok:   %s\n", msg); \
         else { printf("FAIL: %s\n", msg); ++failures; } } while (0)

static void section(const char *name) { printf("\n---- %s ----\n", name); }

// ---------------------------------------------------------------------------
// Fixtures

struct Rig
{
    iris::ScenePtr scene;
    QSharedPointer<iris::Environment> env;
};

static Rig makeScene()
{
    Rig r;
    r.scene = iris::Scene::create();
    r.env = r.scene->getPhysicsEnvironment();
    return r;
}

static void addGroundPlane(Rig &r)
{
    auto node = iris::MeshNode::create();
    node->setName("Ground");
    node->isPhysicsBody = true;
    node->physicsProperty.shape = iris::PhysicsCollisionShape::Plane;
    node->physicsProperty.type = iris::PhysicsType::Static;
    node->physicsProperty.objectMass = 0.0f;
    node->physicsProperty.isStatic = true;
    r.scene->getRootNode()->addChild(node);
}

/// A clip the document can measure: a real (empty) SkeletalAnimation so
/// `hasSkeletalAnimation()` is true, and an explicit length AFTER it — because
/// `setSkeletalAnimation` recomputes the length from the (absent) keys and
/// would otherwise leave every fixture clip zero seconds long.
static iris::AnimationPtr makeClip(const char *name, float length)
{
    auto a = iris::Animation::create(QLatin1String(name));
    a->setSkeletalAnimation(iris::SkeletalAnimation::create());
    a->setLength(length);
    return a;
}

/// An avatar wrapper carrying a movement component, a locomotion component and
/// the clips named in `clipNames` — which is what an imported character looks
/// like from the state machine's side.
static iris::SceneNodePtr addAvatar(Rig &r, const QStringList &clipNames,
                                    const QVector<float> &lengths,
                                    const iris::Vec3 &pos = iris::Vec3(0, 0, 0))
{
    auto node = iris::SceneNode::create();
    node->setName("Avatar");
    node->setAvatarComponent(iris::AvatarMovementPtr(new iris::AvatarMovement()));
    for (int i = 0; i < clipNames.size(); ++i)
        node->addAnimation(makeClip(qUtf8Printable(clipNames[i]),
                                    i < lengths.size() ? lengths[i] : 1.0f));
    auto loco = iris::AvatarLocomotionPtr(new iris::AvatarLocomotion());
    node->setLocomotionComponent(loco);
    node->setLocalPos(pos);
    r.scene->getRootNode()->addChild(node);
    const auto &p = node->avatar()->params();
    loco->refreshClips(node, p.walkSpeed, p.runSpeed);
    return node;
}

/// The six canonical roles, spelled the way real Mixamo downloads spell them.
static QStringList canonicalClips()
{
    return { QStringLiteral("Idle"),        QStringLiteral("Walking"),
             QStringLiteral("Slow Run"),    QStringLiteral("Jumping Up"),
             QStringLiteral("Falling Idle"), QStringLiteral("Falling To Landing"),
             QStringLiteral("mixamo.com") };
}
static QVector<float> canonicalLengths()
{
    return { 2.0f, 1.0f, 0.8f, 0.4f, 0.8f, 0.5f, 0.0f };
}

static void startPlay(Rig &r)
{
    r.env->initializePhysicsWorldFromScene(r.scene->getRootNode());
    r.env->simulatePhysics();
}

static void frames(Rig &r, int n, float dt = 1.0f / 60.0f)
{
    for (int i = 0; i < n; ++i) r.scene->update(dt);
}

static float weightOf(const iris::AvatarLocomotion *loco, const char *clip)
{
    for (const auto &w : loco->weights())
        if (w.clip == QLatin1String(clip)) return w.weight;
    return -1.0f;
}

static QString stateJson(const iris::LocomotionAsset &a)
{
    return QString::fromUtf8(
        QJsonDocument(iris::locomotionAssetToJson(a)).toJson(QJsonDocument::Compact));
}

// ---------------------------------------------------------------------------
// S0 — the CLOSED condition vocabulary (the parser S5's refusals ride on)

static void testVocabulary()
{
    section("S0  closed condition vocabulary — parse, canonical text, round trip");

    struct Case { const char *text; const char *canonical; };
    const Case good[] = {
        { "speed > 2.5", "speed > 2.5" },
        { "speed>2.5", "speed > 2.5" },
        { "  speed   >   2.5 ", "speed > 2.5" },
        { "speed < 0.1", "speed < 0.1" },
        { "grounded", "grounded" },
        { "!grounded", "!grounded" },
        { "! grounded", "!grounded" },
        { "clipEnded(0.9)", "clipEnded(0.9)" },
        { "ClipEnded( 0.9 )", "clipEnded(0.9)" },
        { "jumpRequested", "jumpRequested" },
    };
    for (const auto &c : good) {
        iris::LocomotionCondition cond;
        QString err;
        const bool ok = iris::LocomotionCondition::parse(QLatin1String(c.text), cond, &err);
        CHECK(ok, qUtf8Printable(QStringLiteral("'%1' parses").arg(QLatin1String(c.text))));
        if (!ok) { printf("      (%s)\n", qUtf8Printable(err)); continue; }
        CHECK(cond.text() == QLatin1String(c.canonical),
              qUtf8Printable(QStringLiteral("'%1' -> canonical '%2' (got '%3')")
                                 .arg(QLatin1String(c.text), QLatin1String(c.canonical),
                                      cond.text())));
        // The canonical spelling must re-parse to the SAME condition: that is
        // what makes a save/load round trip an equality check on strings.
        iris::LocomotionCondition again;
        CHECK(iris::LocomotionCondition::parse(cond.text(), again, &err)
                  && again.kind == cond.kind
                  && std::fabs(again.value - cond.value) < 1e-6f,
              "...and the canonical text re-parses identically");
    }

    // Six forms, and NOTHING else. Every row below is a plausible thing a user
    // would type, which is the point of a closed vocabulary: the refusal has to
    // be legible, not just correct.
    const char *bad[] = {
        "speed >= 3", "speed == 3", "speed > abc", "speed >", "clipEnded(1.5)",
        "clipEnded(-0.1)", "clipEnded(0.5", "clipEnded(half)", "!jumpRequested",
        "grounded && speed > 1", "falling", "", "   ",
    };
    for (const char *t : bad) {
        iris::LocomotionCondition cond;
        QString err;
        const bool ok = iris::LocomotionCondition::parse(QLatin1String(t), cond, &err);
        CHECK(!ok, qUtf8Printable(QStringLiteral("'%1' is REFUSED").arg(QLatin1String(t))));
        // NAMES the offending text, and lists the alternatives.
        const bool names = err.contains(QLatin1String(t)) || QLatin1String(t).size() == 0
                           || err.contains(QString(QLatin1String(t)).trimmed());
        CHECK(!ok && names && err.contains(QLatin1String("clipEnded(fraction)")),
              qUtf8Printable(QStringLiteral("...and the message names it and the vocabulary: %1")
                                 .arg(err.left(90))));
    }
}

// ---------------------------------------------------------------------------
// S1 — a speed ramp drives the expected state sequence

static void testS1SpeedRamp()
{
    section("S1  a speed ramp drives the expected state sequence");

    // ---- (a) an AUTHORED three-state asset, through the real play chain ----
    //
    // The default asset keeps the whole ramp inside one Grounded blend space
    // (that is its design), so the sequence claim is made against an asset
    // whose states ARE the speeds — which is also the closed vocabulary's
    // `speed >` / `speed <` forms doing their only job.
    Rig r = makeScene();
    addGroundPlane(r);
    auto node = addAvatar(r, canonicalClips(), canonicalLengths());

    iris::LocomotionAsset a;
    a.name = QStringLiteral("Speed Ramp");
    auto clipState = [](const char *name, const char *clip) {
        iris::LocomotionStateDef s;
        s.name = QLatin1String(name);
        s.kind = iris::LocomotionSourceKind::Clip;
        s.clip = QLatin1String(clip);
        s.loop = true;
        return s;
    };
    a.states = { clipState("Idle", "Idle"), clipState("Walk", "Walking"),
                 clipState("Run", "Slow Run") };
    a.entry = QStringLiteral("Idle");
    auto tr = [](const char *from, const char *to, const char *cond, float blend) {
        iris::LocomotionTransitionDef t;
        t.from = QLatin1String(from);
        t.to = QLatin1String(to);
        QString err;
        iris::LocomotionCondition::parse(QLatin1String(cond), t.condition, &err);
        t.blendDuration = blend;
        return t;
    };
    a.transitions = { tr("Idle", "Walk", "speed > 0.1", 0.05f),
                      tr("Walk", "Run", "speed > 3.0", 0.05f),
                      tr("Run", "Walk", "speed < 3.0", 0.05f),
                      tr("Walk", "Idle", "speed < 0.1", 0.05f) };
    QString err;
    CHECK(node->locomotion()->setAsset(a, &err),
          qUtf8Printable(QStringLiteral("the three-state ramp asset installs (%1)").arg(err)));

    startPlay(r);
    frames(r, 30);   // settle on the floor at speed 0

    QStringList sequence;
    auto record = [&]() {
        const QString s = node->locomotion()->currentState();
        if (sequence.isEmpty() || sequence.last() != s) sequence.append(s);
    };
    record();
    auto *movement = node->avatar();
    movement->setMoveInput(iris::Vec3(0, 0, -1));   // walk
    for (int i = 0; i < 60; ++i) { frames(r, 1); record(); }
    movement->setSprint(true);                       // run
    for (int i = 0; i < 60; ++i) { frames(r, 1); record(); }
    movement->setSprint(false);                      // back to a walk
    for (int i = 0; i < 90; ++i) { frames(r, 1); record(); }
    movement->setMoveInput(iris::Vec3(0, 0, 0));     // stop
    for (int i = 0; i < 120; ++i) { frames(r, 1); record(); }

    const QString joined = sequence.join(QStringLiteral(","));
    printf("      sequence: %s\n", qUtf8Printable(joined));
    CHECK(joined == QLatin1String("Idle,Walk,Run,Walk,Idle"),
          "the ramp produced exactly Idle -> Walk -> Run -> Walk -> Idle");

    // ---- (b) the DEFAULT asset: one state, weights that follow the speed ---
    Rig r2 = makeScene();
    addGroundPlane(r2);
    auto node2 = addAvatar(r2, canonicalClips(), canonicalLengths());
    auto *loco2 = node2->locomotion();
    CHECK(loco2->usesDefaultAsset() && loco2->asset().name == QLatin1String("Biped Locomotion"),
          "a spawned character is already driving the shipped default asset — zero authoring");
    startPlay(r2);
    frames(r2, 30);

    CHECK(loco2->currentState() == QLatin1String("Grounded"),
          "the default asset's entry state is Grounded");
    const float idleAtRest = weightOf(loco2, "Idle");
    CHECK(std::fabs(idleAtRest - 1.0f) < 1e-4f,
          "at speed 0 the blend space is 100% idle");

    node2->avatar()->setMoveInput(iris::Vec3(0, 0, -1));
    frames(r2, 90);
    const float speedWalk = node2->avatar()->state().speed;
    const float wWalk = weightOf(loco2, "Walking");
    printf("      walk: speed %.3f, Walking weight %.3f, Idle %.3f, Slow Run %.3f\n",
           double(speedWalk), double(wWalk), double(weightOf(loco2, "Idle")),
           double(weightOf(loco2, "Slow Run")));
    CHECK(loco2->currentState() == QLatin1String("Grounded"),
          "...and the whole ramp stays inside Grounded (the blend space IS the ramp)");
    CHECK(wWalk > 0.95f, "at walkSpeed the blend space is (near) 100% walk");

    node2->avatar()->setSprint(true);
    frames(r2, 120);
    const float wRun = weightOf(loco2, "Slow Run");
    printf("      run:  speed %.3f, Slow Run weight %.3f, Walking %.3f\n",
           double(node2->avatar()->state().speed), double(wRun),
           double(weightOf(loco2, "Walking")));
    CHECK(wRun > 0.95f, "at runSpeed the blend space is (near) 100% run");

    // MONOTONE across the ramp, sampled by driving the SPACE directly at
    // increasing speeds — the property Stage 5's S8 sweeps in detail.
    float prevRun = -1.0f, prevIdle = 2.0f;
    bool runUp = true, idleDown = true;
    for (int i = 0; i <= 20; ++i) {
        const float v = 5.5f * float(i) / 20.0f;
        iris::AvatarLocomotionState p;
        p.speed = v;
        p.grounded = true;
        p.mode = iris::AvatarMovementMode::Walking;
        // A FRESH machine per sample: this is a weights question, not a
        // history question.
        iris::AvatarLocomotion probe;
        probe.setDefaultAsset(iris::matchClipRoles(canonicalClips()), 2.0f, 5.5f);
        probe.step(p, loco2->clipLengths(), 1.0f / 60.0f);
        const float run = std::max(0.0f, weightOf(&probe, "Slow Run"));
        const float idle = std::max(0.0f, weightOf(&probe, "Idle"));
        if (run < prevRun - 1e-5f) runUp = false;
        if (idle > prevIdle + 1e-5f) idleDown = false;
        prevRun = run;
        prevIdle = idle;
    }
    CHECK(runUp, "the run weight is monotone NON-DECREASING across a 0 -> runSpeed sweep");
    CHECK(idleDown, "the idle weight is monotone NON-INCREASING across the same sweep");
}

// ---------------------------------------------------------------------------
// S2 — transition timing is exact, and the weights are monotone
//
// DRIVEN DIRECTLY (a synthetic §5 contract into step()), because the claim is
// arithmetic: "a 0.25 s transition takes exactly 15 fixed frames at 1/60".
// Routing it through the physics would add an acceleration ramp between the
// input and the condition and prove nothing extra about the blend clock.

static void testS2TransitionTiming()
{
    section("S2  a 0.25 s transition takes exactly 15 fixed frames at 1/60, monotonically");

    iris::LocomotionAsset a;
    a.name = QStringLiteral("Two State");
    iris::LocomotionStateDef s0, s1;
    s0.name = QStringLiteral("A");
    s0.kind = iris::LocomotionSourceKind::Clip;
    s0.clip = QStringLiteral("Idle");
    s1.name = QStringLiteral("B");
    s1.kind = iris::LocomotionSourceKind::Clip;
    s1.clip = QStringLiteral("Walking");
    a.states = { s0, s1 };
    a.entry = QStringLiteral("A");
    iris::LocomotionTransitionDef t;
    t.from = QStringLiteral("A");
    t.to = QStringLiteral("B");
    QString err;
    iris::LocomotionCondition::parse(QStringLiteral("speed > 0.5"), t.condition, &err);
    t.blendDuration = 0.25f;
    a.transitions = { t };

    QMap<QString, float> lengths;
    lengths.insert(QStringLiteral("Idle"), 2.0f);
    lengths.insert(QStringLiteral("Walking"), 1.0f);

    iris::AvatarLocomotion loco;
    CHECK(loco.setAsset(a, &err), qUtf8Printable(QStringLiteral("asset installs (%1)").arg(err)));

    const float dt = 1.0f / 60.0f;
    iris::AvatarLocomotionState p;
    p.grounded = true;
    p.speed = 0.0f;
    for (int i = 0; i < 5; ++i) loco.step(p, lengths, dt);
    CHECK(loco.currentState() == QLatin1String("A") && !loco.isTransitioning(),
          "before the condition holds: in A, not transitioning");

    // The step that TAKES it. u is published at 0 there — the destination is
    // in the set at weight 0, the source still carries the whole pose.
    p.speed = 1.0f;
    loco.step(p, lengths, dt);
    CHECK(loco.currentState() == QLatin1String("B") && loco.previousState() == QLatin1String("A")
              && loco.isTransitioning(),
          "the taking step: current is already B, previous is A, a blend is in flight");
    CHECK(std::fabs(loco.transitionProgress()) < 1e-6f, "...published at u = 0");
    CHECK(std::fabs(weightOf(&loco, "Idle") - 1.0f) < 1e-6f
              && std::fabs(weightOf(&loco, "Walking")) < 1e-6f,
          "...so the outgoing clip carries weight 1 and the incoming 0");

    int steps = 0;
    float prevU = -1.0f, prevDest = -1.0f, prevSrc = 2.0f;
    bool uUp = true, destUp = true, srcDown = true;
    while (loco.isTransitioning() && steps < 200) {
        loco.step(p, lengths, dt);
        ++steps;
        if (!loco.isTransitioning()) break;
        const float u = loco.transitionProgress();
        const float dest = weightOf(&loco, "Walking");
        const float src = weightOf(&loco, "Idle");
        if (u < prevU - 1e-6f) uUp = false;
        if (dest < prevDest - 1e-6f) destUp = false;
        if (src > prevSrc + 1e-6f) srcDown = false;
        prevU = u;
        prevDest = dest;
        prevSrc = src;
        // The exact schedule: u = k/15 at step k.
        const float expected = float(steps) / 15.0f;
        if (std::fabs(u - expected) > 1e-4f) {
            printf("FAIL: step %d: u = %.6f, expected %.6f\n", steps, double(u), double(expected));
            ++failures;
        }
    }
    printf("      the 0.25 s blend completed after %d steps of 1/60\n", steps);
    CHECK(steps == 15, "EXACTLY 15 fixed frames at 1/60");
    CHECK(uUp, "u is monotone non-decreasing");
    CHECK(destUp, "the destination clip's weight is monotone non-decreasing");
    CHECK(srcDown, "the source clip's weight is monotone non-increasing");
    CHECK(!loco.isTransitioning() && loco.previousState().isEmpty()
              && std::fabs(weightOf(&loco, "Walking") - 1.0f) < 1e-6f,
          "on completion the destination carries weight 1 and the source is gone");

    // A ZERO blend duration is an instant cut, not a divide by zero.
    iris::AvatarLocomotion cut;
    a.transitions[0].blendDuration = 0.0f;
    CHECK(cut.setAsset(a, &err), "a zero-duration transition is a legal asset");
    cut.step(p, lengths, dt);
    CHECK(cut.currentState() == QLatin1String("B") && !cut.isTransitioning()
              && std::fabs(weightOf(&cut, "Walking") - 1.0f) < 1e-6f,
          "...and it cuts in one step with no blend in flight");
}

// ---------------------------------------------------------------------------
// S3 — the jump chain fires with NO authored condition

static void testS3JumpChain()
{
    section("S3  the jump chain start -> fall -> land -> grounded, zero authored conditions");

    Rig r = makeScene();
    addGroundPlane(r);
    auto node = addAvatar(r, canonicalClips(), canonicalLengths());
    auto *loco = node->locomotion();
    auto *movement = node->avatar();

    CHECK(loco->usesDefaultAsset(),
          "the asset under test is the GENERATED default — nothing was authored");
    CHECK(loco->asset().states.size() == 4,
          qUtf8Printable(QStringLiteral("...with four states (got %1)")
                             .arg(loco->asset().states.size())));
    CHECK(loco->asset().transitions.size() == 5,
          qUtf8Printable(QStringLiteral("...and five transitions (got %1)")
                             .arg(loco->asset().transitions.size())));

    startPlay(r);
    frames(r, 60);
    CHECK(loco->currentState() == QLatin1String("Grounded"), "settled in Grounded");

    QStringList sequence;
    sequence.append(loco->currentState());
    auto record = [&]() {
        if (sequence.last() != loco->currentState()) sequence.append(loco->currentState());
    };

    movement->requestJump();
    frames(r, 1);
    record();
    CHECK(loco->currentState() == QLatin1String("JumpStart"),
          "the jump step enters JumpStart — the latch beat the `!grounded` wildcard, "
          "because transition ORDER is the priority");

    for (int i = 0; i < 400 && sequence.size() < 5; ++i) { frames(r, 1); record(); }
    const QString joined = sequence.join(QStringLiteral(" -> "));
    printf("      chain: %s\n", qUtf8Printable(joined));
    CHECK(joined == QLatin1String("Grounded -> JumpStart -> Fall -> Land -> Grounded"),
          "Grounded -> JumpStart -> Fall -> Land -> Grounded, with no user condition anywhere");
    CHECK(movement->state().grounded, "...and the character is standing on the floor again");

    // R2: the set is never empty and never all-zero, at any point of the chain.
    bool everEmpty = false, everZero = false;
    Rig r2 = makeScene();
    addGroundPlane(r2);
    auto n2 = addAvatar(r2, canonicalClips(), canonicalLengths());
    startPlay(r2);
    frames(r2, 30);
    n2->avatar()->requestJump();
    for (int i = 0; i < 200; ++i) {
        frames(r2, 1);
        const auto &w = n2->locomotion()->weights();
        if (w.isEmpty()) everEmpty = true;
        float total = 0.0f;
        for (const auto &e : w) total += e.weight;
        if (total <= 1e-6f) everZero = true;
    }
    CHECK(!everEmpty, "R2: the clip set is never EMPTY across the whole jump chain");
    CHECK(!everZero, "R2: the clip set is never ALL-ZERO across the whole jump chain");
}

// ---------------------------------------------------------------------------
// S4 — degradation: a missing role is not an error

static void testS4Degradation()
{
    section("S4  degradation — a missing role degrades the asset, it never refuses it");

    // TOLERANT NAME MATCHING first, since every degradation below rides on it.
    const iris::ClipRoles full = iris::matchClipRoles(canonicalClips());
    CHECK(full.get(iris::ClipRole::Idle) == QLatin1String("Idle"), "'Idle' -> idle");
    CHECK(full.get(iris::ClipRole::Walk) == QLatin1String("Walking"), "'Walking' -> walk");
    CHECK(full.get(iris::ClipRole::Run) == QLatin1String("Slow Run"), "'Slow Run' -> run");
    CHECK(full.get(iris::ClipRole::JumpStart) == QLatin1String("Jumping Up"),
          "'Jumping Up' -> jump-start");
    CHECK(full.get(iris::ClipRole::FallLoop) == QLatin1String("Falling Idle"),
          "'Falling Idle' -> fall-loop, NOT idle (fall-loop resolves first)");
    CHECK(full.get(iris::ClipRole::Land) == QLatin1String("Falling To Landing"),
          "'Falling To Landing' -> land, NOT fall-loop (land resolves first)");

    const iris::ClipRoles junk =
        iris::matchClipRoles({ QStringLiteral("mixamo.com"), QStringLiteral("Take 001"),
                               QStringLiteral("Armature|Action") });
    CHECK(junk.boundCount() == 0, "a set of pure Mixamo/FBX junk names binds NO role");

    const iris::ClipRoles snake =
        iris::matchClipRoles({ QStringLiteral("char_idle_01"), QStringLiteral("char_walk_fwd"),
                               QStringLiteral("RUN_CYCLE"), QStringLiteral("jump_start"),
                               QStringLiteral("fall_loop"), QStringLiteral("land_soft") });
    CHECK(snake.boundCount() == 6, "snake_case / SHOUTING names bind all six roles");
    CHECK(snake.get(iris::ClipRole::Run) == QLatin1String("RUN_CYCLE"), "...case-insensitively");

    // ---- no `run` -> a TWO-sample blend space ------------------------------
    QStringList noRun = canonicalClips();
    noRun.removeAll(QStringLiteral("Slow Run"));
    iris::LocomotionAsset a = iris::buildDefaultLocomotionAsset(iris::matchClipRoles(noRun),
                                                               2.0f, 5.5f);
    QString err;
    CHECK(a.validate(&err), qUtf8Printable(QStringLiteral("the degraded asset validates (%1)")
                                               .arg(err)));
    const iris::LocomotionStateDef *g = a.findState(QStringLiteral("Grounded"));
    CHECK(g && g->kind == iris::LocomotionSourceKind::BlendSpace
              && g->space.samples.size() == 2,
          qUtf8Printable(QStringLiteral("no `run` -> a TWO-sample blend space (got %1)")
                             .arg(g ? g->space.samples.size() : -1)));
    CHECK(g && g->space.samples[0].clip == QLatin1String("Idle")
              && g->space.samples[1].clip == QLatin1String("Walking"),
          "...idle then walk, in that order");

    // ---- no `land` -> the state is SKIPPED and Fall returns to Grounded ----
    QStringList noLand = canonicalClips();
    noLand.removeAll(QStringLiteral("Falling To Landing"));
    iris::LocomotionAsset b = iris::buildDefaultLocomotionAsset(iris::matchClipRoles(noLand),
                                                               2.0f, 5.5f);
    CHECK(b.validate(&err), "the no-land asset validates");
    CHECK(b.findState(QStringLiteral("Land")) == nullptr, "no `land` -> no Land state");
    bool fallToGrounded = false;
    for (const auto &t : b.transitions)
        if (t.from == QLatin1String("Fall") && t.to == QLatin1String("Grounded")
            && t.condition.kind == iris::LocomotionConditionKind::Grounded)
            fallToGrounded = true;
    CHECK(fallToGrounded, "...and Fall -> Grounded on `grounded` replaces it");

    // Prove it in flight: the jump chain still completes with no Land state.
    Rig r = makeScene();
    addGroundPlane(r);
    QVector<float> lens = canonicalLengths();
    lens.remove(5);
    auto node = addAvatar(r, noLand, lens);
    startPlay(r);
    frames(r, 60);
    QStringList seq;
    seq.append(node->locomotion()->currentState());
    node->avatar()->requestJump();
    for (int i = 0; i < 400 && seq.size() < 4; ++i) {
        frames(r, 1);
        if (seq.last() != node->locomotion()->currentState())
            seq.append(node->locomotion()->currentState());
    }
    printf("      no-land chain: %s\n", qUtf8Printable(seq.join(QStringLiteral(" -> "))));
    CHECK(seq.join(QStringLiteral(" -> "))
              == QLatin1String("Grounded -> JumpStart -> Fall -> Grounded"),
          "the jump chain skips the missing state instead of stalling in Fall");

    // ---- one clip only -> Grounded is a CLIP state, not a 1-sample space ---
    iris::LocomotionAsset c = iris::buildDefaultLocomotionAsset(
        iris::matchClipRoles({ QStringLiteral("Idle") }), 2.0f, 5.5f);
    CHECK(c.validate(&err), "an idle-only character still produces a valid asset");
    const iris::LocomotionStateDef *cg = c.findState(QStringLiteral("Grounded"));
    CHECK(cg && cg->kind == iris::LocomotionSourceKind::Clip
              && cg->clip == QLatin1String("Idle"),
          "one bound role -> Grounded is a plain clip state (a 1-sample space is refused, "
          "correctly: there is nothing to blend)");
    CHECK(c.states.size() == 1 && c.transitions.isEmpty(),
          "...and no transition survives that names a state that does not exist");

    // ---- nothing bound -> an EMPTY asset, and no crash --------------------
    iris::LocomotionAsset d = iris::buildDefaultLocomotionAsset(junk, 2.0f, 5.5f);
    CHECK(d.isEmpty(), "nothing bound -> an EMPTY asset");
    Rig r2 = makeScene();
    addGroundPlane(r2);
    auto n2 = addAvatar(r2, { QStringLiteral("mixamo.com") }, { 0.5f });
    startPlay(r2);
    frames(r2, 60);
    CHECK(n2->locomotion()->weights().isEmpty() && n2->locomotion()->currentState().isEmpty(),
          "an unmatchable character publishes NO weights and no state, and steps 60 frames "
          "without a crash — a degradation, not a frozen half-pose");

    // ---- a knob change moves the sample positions -------------------------
    iris::AvatarLocomotion loco;
    loco.setDefaultAsset(full, 2.0f, 5.5f);
    const iris::LocomotionStateDef *before = loco.asset().findState(QStringLiteral("Grounded"));
    CHECK(before && std::fabs(before->space.samples[2].position - 5.5f) < 1e-4f,
          "the run sample sits at runSpeed");
    loco.refreshDefaultAsset(2.0f, 8.0f);
    const iris::LocomotionStateDef *after = loco.asset().findState(QStringLiteral("Grounded"));
    CHECK(after && std::fabs(after->space.samples[2].position - 8.0f) < 1e-4f,
          "...and raising runSpeed to 8 moves it, so the space keeps bracketing the real speeds");
    // An AUTHORED asset is never regenerated behind the user's back.
    iris::LocomotionAsset authored = iris::buildDefaultLocomotionAsset(full, 2.0f, 5.5f);
    authored.name = QStringLiteral("Mine");
    CHECK(loco.setAsset(authored, &err) && !loco.usesDefaultAsset(), "an authored asset installs");
    loco.refreshDefaultAsset(2.0f, 99.0f);
    CHECK(loco.asset().name == QLatin1String("Mine")
              && std::fabs(loco.asset().findState(QStringLiteral("Grounded"))
                               ->space.samples[2].position - 5.5f) < 1e-4f,
          "...and a knob change does NOT regenerate over the top of it");
}

// ---------------------------------------------------------------------------
// S5 — an invalid asset is refused with a message naming the bad condition

static void testS5Refusal()
{
    section("S5  an invalid asset is REFUSED, and the message names what was wrong");

    const iris::ClipRoles roles = iris::matchClipRoles(canonicalClips());
    const iris::LocomotionAsset good = iris::buildDefaultLocomotionAsset(roles, 2.0f, 5.5f);
    iris::AvatarLocomotion loco;
    QString err;
    CHECK(loco.setAsset(good, &err), "the baseline asset installs");
    const QString baseline = stateJson(loco.asset());

    // THE CONDITION, through the JSON grammar the verb and the scene file share.
    QJsonObject json = iris::locomotionAssetToJson(good);
    QJsonArray trans = json[QStringLiteral("transitions")].toArray();
    QJsonObject bad = trans[0].toObject();
    bad[QStringLiteral("condition")] = QStringLiteral("speed >= 3");
    trans[0] = bad;
    json[QStringLiteral("transitions")] = trans;
    iris::LocomotionAsset parsed;
    CHECK(!iris::locomotionAssetFromJson(json, parsed, &err),
          "an out-of-vocabulary condition is REFUSED");
    printf("      message: %s\n", qUtf8Printable(err));
    CHECK(err.contains(QLatin1String("speed >= 3")),
          "...and the message NAMES the offending condition");
    CHECK(err.contains(QLatin1String("clipEnded(fraction)"))
              && err.contains(QLatin1String("jumpRequested")),
          "...and lists the vocabulary it is not in");
    CHECK(err.contains(QLatin1String("Grounded")) && err.contains(QLatin1String("JumpStart")),
          "...and names the transition it sits on");
    CHECK(stateJson(loco.asset()) == baseline, "...and NOTHING on the component changed");

    // Every other structural refusal, each naming its own subject.
    struct Case { const char *what; const char *mustName; iris::LocomotionAsset a; };
    QVector<Case> cases;
    {
        iris::LocomotionAsset a = good;
        a.entry = QStringLiteral("Nowhere");
        cases.append({ "an entry that names no state", "Nowhere", a });
    }
    {
        iris::LocomotionAsset a = good;
        a.transitions[0].to = QStringLiteral("Ghost");
        cases.append({ "a `to` that names no state", "Ghost", a });
    }
    {
        iris::LocomotionAsset a = good;
        a.transitions[0].from = QStringLiteral("Phantom");
        cases.append({ "a `from` that names no state", "Phantom", a });
    }
    {
        iris::LocomotionAsset a = good;
        a.states[0].space.samples.remove(2);
        a.states[0].space.samples.remove(1);
        cases.append({ "a blend space with one sample", "Grounded", a });
    }
    {
        iris::LocomotionAsset a = good;
        a.states[0].space.samples[2].position = 0.0f;   // not strictly increasing
        cases.append({ "blend positions that are not strictly increasing", "Grounded", a });
    }
    {
        iris::LocomotionAsset a = good;
        a.states[1].clip.clear();
        cases.append({ "a state that resolves to no clip (R2: a frozen pose)", "JumpStart", a });
    }
    {
        iris::LocomotionAsset a = good;
        a.transitions[0].blendDuration = -1.0f;
        cases.append({ "a negative blend duration", "Grounded", a });
    }
    {
        iris::LocomotionAsset a = good;
        a.states[1].name = QStringLiteral("Grounded");
        cases.append({ "two states with one name", "Grounded", a });
    }
    for (const auto &c : cases) {
        QString e;
        const bool refused = !c.a.validate(&e);
        CHECK(refused, qUtf8Printable(QStringLiteral("%1 is refused").arg(QLatin1String(c.what))));
        CHECK(refused && e.contains(QLatin1String(c.mustName)),
              qUtf8Printable(QStringLiteral("...naming '%1': %2")
                                 .arg(QLatin1String(c.mustName), e.left(90))));
        iris::AvatarLocomotion probe;
        probe.setAsset(good, &e);
        const QString was = stateJson(probe.asset());
        probe.setAsset(c.a, &e);
        CHECK(stateJson(probe.asset()) == was, "...and the refusal changed nothing");
    }
}

// ---------------------------------------------------------------------------
// S6 — save / load round trip

static void testS6RoundTrip()
{
    section("S6  save / load round trip");

    const iris::ClipRoles roles = iris::matchClipRoles(canonicalClips());
    const iris::LocomotionAsset original = iris::buildDefaultLocomotionAsset(roles, 2.0f, 5.5f);

    const QJsonObject written = iris::locomotionAssetToJson(original);
    iris::LocomotionAsset read;
    QString err;
    CHECK(iris::locomotionAssetFromJson(written, read, &err),
          qUtf8Printable(QStringLiteral("the written asset reads back (%1)").arg(err)));
    const QJsonObject rewritten = iris::locomotionAssetToJson(read);
    CHECK(QJsonDocument(written).toJson(QJsonDocument::Compact)
              == QJsonDocument(rewritten).toJson(QJsonDocument::Compact),
          "write -> read -> write is BYTE-IDENTICAL");

    // And structurally, field by field, so a byte-identical accident cannot
    // hide a dropped field.
    CHECK(read.name == original.name && read.entry == original.entry
              && read.states.size() == original.states.size()
              && read.transitions.size() == original.transitions.size(),
          "name, entry, state count and transition count survive");
    bool statesMatch = true;
    for (int i = 0; i < read.states.size(); ++i) {
        const auto &x = original.states[i], &y = read.states[i];
        if (x.name != y.name || x.kind != y.kind || x.loop != y.loop || x.clip != y.clip
            || std::fabs(x.speed - y.speed) > 1e-6f
            || x.space.samples.size() != y.space.samples.size()
            || x.space.syncClips != y.space.syncClips)
            statesMatch = false;
        for (int k = 0; k < x.space.samples.size(); ++k) {
            const auto &sx = x.space.samples[k], &sy = y.space.samples[k];
            if (sx.clip != sy.clip || std::fabs(sx.position - sy.position) > 1e-6f
                || std::fabs(sx.authoredSpeed - sy.authoredSpeed) > 1e-6f)
                statesMatch = false;
        }
    }
    CHECK(statesMatch, "every state — kind, loop, clip, speed and every blend sample — survives");
    bool transMatch = true;
    for (int i = 0; i < read.transitions.size(); ++i) {
        const auto &x = original.transitions[i], &y = read.transitions[i];
        if (x.from != y.from || x.to != y.to || x.condition.kind != y.condition.kind
            || std::fabs(x.condition.value - y.condition.value) > 1e-6f
            || std::fabs(x.blendDuration - y.blendDuration) > 1e-6f)
            transMatch = false;
    }
    CHECK(transMatch, "every transition — from, to, condition and blend duration — survives, "
                      "IN ORDER (order is the priority)");

    // THE COMPONENT round trip, replaying exactly what SceneWriter writes and
    // SceneReader reads (the same two functions, plus the roles block).
    iris::AvatarLocomotion src;
    src.setDefaultAsset(roles, 2.0f, 5.5f);
    src.setClipRole(iris::ClipRole::Walk, QStringLiteral("Walking"));
    QJsonObject rolesObj;
    for (int i = 0; i < iris::kClipRoleCount; ++i) {
        const auto role = iris::ClipRole(i);
        if (src.roles().has(role))
            rolesObj[QLatin1String(iris::clipRoleName(role))] = src.roles().get(role);
    }
    QJsonObject block;
    block[QStringLiteral("default")] = src.usesDefaultAsset();
    block[QStringLiteral("roles")] = rolesObj;
    block[QStringLiteral("asset")] = iris::locomotionAssetToJson(src.asset());

    iris::AvatarLocomotion dst;
    iris::ClipRoles restored;
    for (int i = 0; i < iris::kClipRoleCount; ++i) {
        const auto role = iris::ClipRole(i);
        const QString key = QLatin1String(iris::clipRoleName(role));
        if (block[QStringLiteral("roles")].toObject().contains(key))
            restored.set(role, block[QStringLiteral("roles")].toObject()[key].toString());
    }
    dst.markRolesFromFile(restored, block[QStringLiteral("default")].toBool(true));
    iris::LocomotionAsset restoredAsset;
    CHECK(iris::locomotionAssetFromJson(block[QStringLiteral("asset")].toObject(),
                                        restoredAsset, &err),
          "the component's asset block reads back");
    CHECK(dst.setAssetPreservingDefaultFlag(restoredAsset, &err),
          "...and installs on a fresh component");
    CHECK(stateJson(dst.asset()) == stateJson(src.asset()),
          "the component round trip is identical");
    CHECK(dst.usesDefaultAsset() == src.usesDefaultAsset(),
          "...including whether the asset is the generated default");
    bool rolesMatch = true;
    for (int i = 0; i < iris::kClipRoleCount; ++i)
        if (dst.roles().names[i] != src.roles().names[i]) rolesMatch = false;
    CHECK(rolesMatch, "...and every role binding");

    // A role restored FROM A FILE is manual: a later auto-match must not
    // clobber a binding somebody saved — while a role the file left UNBOUND
    // still fills in from whatever the character has since been given, which is
    // the Mixamo workflow (one character download, then one file per animation).
    iris::ClipRoles partial = restored;
    partial.set(iris::ClipRole::Run, QString());   // as if the file had no run clip
    iris::AvatarLocomotion late;
    late.markRolesFromFile(partial, true);
    late.setDefaultAsset(partial, 2.0f, 5.5f);
    late.markRolesFromFile(partial, true);
    late.bindRolesFromClips({ QStringLiteral("Idle"), QStringLiteral("Walking"),
                              QStringLiteral("Sprint") }, 2.0f, 5.5f);
    CHECK(late.roles().get(iris::ClipRole::Walk) == QLatin1String("Walking"),
          "a role a file recorded survives a later automatic re-match");
    CHECK(late.roles().get(iris::ClipRole::Run) == QLatin1String("Sprint"),
          "...while a role the file left UNBOUND auto-fills from the new clips");
    CHECK(late.asset().findState(QStringLiteral("Grounded"))->space.samples.size() == 3,
          "...and the default asset regenerated with the new sample in it");
}

// ---------------------------------------------------------------------------
// S7 — determinism: the same input sequence twice is bit-identical

static void testS7Determinism()
{
    section("S7  determinism — the same input sequence twice is bit-identical");

    // The trace is every published number: state, phase, blend progress, and
    // every clip's name / weight / time, at every step. Compared BIT for bit
    // (memcmp on the float bits), not within a tolerance.
    auto run = [](QByteArray &trace) {
        Rig r = makeScene();
        addGroundPlane(r);
        auto node = addAvatar(r, canonicalClips(), canonicalLengths());
        startPlay(r);
        auto *movement = node->avatar();
        auto *loco = node->locomotion();
        auto sample = [&]() {
            trace.append(loco->currentState().toUtf8());
            trace.append('|');
            trace.append(loco->previousState().toUtf8());
            trace.append('|');
            const float nums[3] = { loco->statePhase(), loco->transitionProgress(),
                                    movement->state().speed };
            trace.append(reinterpret_cast<const char *>(nums), sizeof(nums));
            for (const auto &w : loco->weights()) {
                trace.append(w.clip.toUtf8());
                const float wn[2] = { w.weight, w.time };
                trace.append(reinterpret_cast<const char *>(wn), sizeof(wn));
            }
            trace.append('\n');
        };
        for (int i = 0; i < 40; ++i) { frames(r, 1); sample(); }
        movement->setMoveInput(iris::Vec3(0, 0, -1));
        for (int i = 0; i < 80; ++i) { frames(r, 1); sample(); }
        movement->setSprint(true);
        for (int i = 0; i < 60; ++i) { frames(r, 1); sample(); }
        movement->requestJump();
        for (int i = 0; i < 150; ++i) { frames(r, 1); sample(); }
        movement->setSprint(false);
        movement->setMoveInput(iris::Vec3(0, 0, 0));
        for (int i = 0; i < 80; ++i) { frames(r, 1); sample(); }
    };

    QByteArray a, b;
    run(a);
    run(b);
    printf("      trace: %d bytes, %d sampled steps\n", int(a.size()), int(a.count('\n')));
    CHECK(a.size() > 1000, "the trace is substantial (the run really exercised the machine)");
    CHECK(a == b, "two identical input sequences produce BIT-IDENTICAL traces");

    // ...and two avatars in ONE scene, started at different times, do NOT
    // lock-step: the per-avatar phase is the whole reason the clock lives in
    // the document (§3.2).
    Rig r = makeScene();
    addGroundPlane(r);
    auto n1 = addAvatar(r, canonicalClips(), canonicalLengths(), iris::Vec3(-3, 0, 0));
    auto n2 = addAvatar(r, canonicalClips(), canonicalLengths(), iris::Vec3(3, 0, 0));
    startPlay(r);
    frames(r, 30);
    n1->avatar()->setMoveInput(iris::Vec3(0, 0, -1));
    frames(r, 45);
    n2->avatar()->setMoveInput(iris::Vec3(0, 0, -1));
    frames(r, 60);
    const float p1 = n1->locomotion()->statePhase();
    const float p2 = n2->locomotion()->statePhase();
    printf("      phases: avatar A %.4f, avatar B %.4f\n", double(p1), double(p2));
    CHECK(std::fabs(p1 - p2) > 1e-3f,
          "two avatars that started walking at different times are NOT in lock-step");
}

// ---------------------------------------------------------------------------
// Stage 5 helpers (S8-S10, S12) — one blend-space state, no transitions.
//
// The blend space is the whole subject from here down, so the fixtures below
// hand it a state machine with NOTHING else in it: one Grounded state, no
// transitions, no cross-fade. What `speed` does to the weights, the times and
// the play rate is then the only thing that can move.

struct BlendSample { const char *clip; float position; float authoredSpeed; float length; };

static iris::LocomotionAsset blendSpaceAsset(const QVector<BlendSample> &samples,
                                             QMap<QString, float> &lengths)
{
    iris::LocomotionAsset a;
    a.name = QStringLiteral("Blend Space");
    iris::LocomotionStateDef st;
    st.name = QStringLiteral("Grounded");
    st.kind = iris::LocomotionSourceKind::BlendSpace;
    st.loop = true;
    st.space.syncClips = true;
    lengths.clear();
    for (const auto &s : samples) {
        iris::LocomotionBlendSample b;
        b.clip = QLatin1String(s.clip);
        b.position = s.position;
        b.authoredSpeed = s.authoredSpeed;
        st.space.samples.append(b);
        lengths.insert(QLatin1String(s.clip), s.length);
    }
    a.states = { st };
    a.entry = st.name;
    return a;
}

/// The published ABSOLUTE time for `clip`, or -1 when it is not in the set.
static float timeOf(const iris::AvatarLocomotion *loco, const char *clip)
{
    for (const auto &w : loco->weights())
        if (w.clip == QLatin1String(clip)) return w.time;
    return -1.0f;
}

// ---------------------------------------------------------------------------
// S8 — bracketing: exactly one clip at a sample, monotone and continuous between

static void testS8BlendSpaceWeights()
{
    section("S8  blend space — one clip at each sample, monotone + continuous between");

    QMap<QString, float> lengths;
    // idle carries authoredSpeed 0 on purpose (a stationary clip has no authored
    // speed); the play rate is S10's subject, not this one's.
    const iris::LocomotionAsset a = blendSpaceAsset(
        { { "Idle", 0.0f, 0.0f, 2.0f }, { "Walking", 2.0f, 2.0f, 1.0f },
          { "Slow Run", 5.5f, 5.5f, 0.8f } }, lengths);

    QString err;
    iris::AvatarLocomotion loco;
    CHECK(loco.setAsset(a, &err), qUtf8Printable(QStringLiteral("asset installs (%1)").arg(err)));

    const float dt = 1.0f / 60.0f;
    iris::AvatarLocomotionState p;
    p.grounded = true;

    // ---- (a) AT each sample position: exactly one clip at weight 1 ----------
    struct AtSample { float speed; const char *clip; };
    const AtSample at[] = { { 0.0f, "Idle" }, { 2.0f, "Walking" }, { 5.5f, "Slow Run" } };
    for (const auto &c : at) {
        p.speed = c.speed;
        loco.step(p, lengths, dt);
        int ones = 0, others = 0;
        for (const auto &w : loco.weights()) {
            if (std::fabs(w.weight - 1.0f) < 1e-5f && w.clip == QLatin1String(c.clip)) ++ones;
            else if (w.weight > 1e-5f) ++others;
        }
        CHECK(ones == 1 && others == 0,
              qUtf8Printable(QStringLiteral("speed %1: EXACTLY '%2' at weight 1, nothing else "
                                            "(ones=%3 others=%4)")
                                 .arg(double(c.speed)).arg(QLatin1String(c.clip))
                                 .arg(ones).arg(others)));
    }

    // ---- (b) NO EXTRAPOLATION outside the space ----------------------------
    p.speed = -3.0f;
    loco.step(p, lengths, dt);
    CHECK(std::fabs(weightOf(&loco, "Idle") - 1.0f) < 1e-5f,
          "below the first sample: the first clip at weight 1, clamped, never extrapolated");
    p.speed = 40.0f;
    loco.step(p, lengths, dt);
    CHECK(std::fabs(weightOf(&loco, "Slow Run") - 1.0f) < 1e-5f,
          "above the last sample: the last clip at weight 1, clamped");

    // ---- (c) a 20-step sweep: monotone, continuous, unit sum ---------------
    const int kSteps = 20;
    float prevIdle = 2.0f, prevRun = -1.0f, prevWalk = -1.0f;
    bool idleDown = true, runUp = true, sums = true, continuous = true, walkUnimodal = true;
    bool walkFalling = false;
    float maxJump = 0.0f;
    for (int i = 0; i <= kSteps; ++i) {
        p.speed = 5.5f * float(i) / float(kSteps);
        loco.step(p, lengths, dt);
        const float wi = std::max(0.0f, weightOf(&loco, "Idle"));
        const float ww = std::max(0.0f, weightOf(&loco, "Walking"));
        const float wr = std::max(0.0f, weightOf(&loco, "Slow Run"));
        if (std::fabs(wi + ww + wr - 1.0f) > 1e-5f) sums = false;
        if (wi > prevIdle + 1e-5f) idleDown = false;          // idle only ever falls
        if (wr < prevRun - 1e-5f) runUp = false;              // run only ever rises
        if (prevWalk >= 0.0f) {
            // walk rises to 1 at its own sample and falls after: one turning
            // point, and never a rise once it has started falling.
            if (ww < prevWalk - 1e-5f) walkFalling = true;
            else if (ww > prevWalk + 1e-5f && walkFalling) walkUnimodal = false;
            const float jump = std::max({ std::fabs(wi - prevIdle), std::fabs(ww - prevWalk),
                                          std::fabs(wr - prevRun) });
            maxJump = std::max(maxJump, jump);
            if (jump > 0.2f) continuous = false;
        }
        prevIdle = wi; prevWalk = ww; prevRun = wr;
    }
    printf("      largest single-step weight change over the sweep: %.4f\n", double(maxJump));
    CHECK(sums, "every sample's weights sum to 1 (before the engine's per-bone normalization)");
    CHECK(idleDown, "idle's weight is MONOTONE non-increasing across the sweep");
    CHECK(runUp, "run's weight is MONOTONE non-decreasing across the sweep");
    CHECK(walkUnimodal, "walk rises then falls — one turning point, no oscillation");
    CHECK(continuous, "CONTINUOUS: no single 1/20 speed step moves a weight by more than 0.2");
}

// ---------------------------------------------------------------------------
// S9 — clip sync: two clips of different lengths sit at ONE normalized phase

static void testS9ClipSync()
{
    section("S9  clip sync — different lengths, one shared normalized phase");

    QMap<QString, float> lengths;
    // Positions 2 and 4 so speed 3 brackets them at EXACTLY 0.5/0.5; lengths
    // 1.0 s and 0.6 s so an unsynced pair would drift within one cycle.
    const iris::LocomotionAsset a = blendSpaceAsset(
        { { "Walking", 2.0f, 0.0f, 1.0f }, { "Slow Run", 4.0f, 0.0f, 0.6f } }, lengths);

    QString err;
    iris::AvatarLocomotion loco;
    CHECK(loco.setAsset(a, &err), qUtf8Printable(QStringLiteral("asset installs (%1)").arg(err)));

    const float dt = 1.0f / 60.0f;
    iris::AvatarLocomotionState p;
    p.grounded = true;
    p.speed = 3.0f;

    loco.step(p, lengths, dt);
    CHECK(std::fabs(weightOf(&loco, "Walking") - 0.5f) < 1e-5f
              && std::fabs(weightOf(&loco, "Slow Run") - 0.5f) < 1e-5f,
          "speed 3 between samples at 2 and 4 is an exact 0.5/0.5 split");

    // AT EVERY SAMPLED TIME, for three full blended cycles. The blended cycle
    // length is the weighted sum, 0.5*1.0 + 0.5*0.6 = 0.8 s, so 48 steps at
    // 1/60 is one cycle and the loop below crosses the wrap twice.
    bool inPhase = true, matchesState = true, advancesAtL = true;
    float worst = 0.0f;
    float prevPhase = loco.statePhase();
    for (int i = 0; i < 150; ++i) {
        loco.step(p, lengths, dt);
        const float tw = timeOf(&loco, "Walking");
        const float tr = timeOf(&loco, "Slow Run");
        const float phi = loco.statePhase();
        const float d = std::fabs(tw / 1.0f - tr / 0.6f);
        worst = std::max(worst, d);
        if (d > 1e-4f) inPhase = false;
        if (std::fabs(tw / 1.0f - phi) > 1e-4f) matchesState = false;
        // ...and the phase advances at dt / L, L being the WEIGHTED length. A
        // wrap subtracts 1, so compare the wrapped delta.
        float dphi = phi - prevPhase;
        if (dphi < 0.0f) dphi += 1.0f;
        if (std::fabs(dphi - dt / 0.8f) > 1e-4f) advancesAtL = false;
        prevPhase = phi;
    }
    printf("      worst normalized-phase disagreement over 150 steps: %.3e\n", double(worst));
    CHECK(inPhase, "the 1.0 s and the 0.6 s clip sit at the SAME normalized phase, every step");
    CHECK(matchesState, "...which is the state's own phase, so nothing derives it twice");
    CHECK(advancesAtL, "...and the shared phase advances at dt / (weighted cycle length)");

    // A ZERO-LENGTH sample is excluded from L and from sync (§7.3) rather than
    // dividing by zero — every Mixamo character download ships one.
    QMap<QString, float> zlengths;
    const iris::LocomotionAsset z = blendSpaceAsset(
        { { "mixamo.com", 0.0f, 0.0f, 0.0f }, { "Walking", 2.0f, 0.0f, 1.0f } }, zlengths);
    iris::AvatarLocomotion zl;
    CHECK(zl.setAsset(z, &err), qUtf8Printable(QStringLiteral("zero-length asset installs (%1)")
                                                   .arg(err)));
    iris::AvatarLocomotionState zp;
    zp.grounded = true;
    zp.speed = 1.0f;                       // 0.5/0.5 across the zero-length sample
    for (int i = 0; i < 30; ++i) zl.step(zp, zlengths, dt);
    const float zphi = zl.statePhase();
    CHECK(std::isfinite(zphi) && zphi > 0.0f,
          qUtf8Printable(QStringLiteral("a zero-length sample does not divide the phase by zero "
                                        "(phase %1)").arg(double(zphi))));
    CHECK(std::fabs(timeOf(&zl, "mixamo.com")) < 1e-6f,
          "...and the zero-length clip is published at time 0, not NaN");
}

// ---------------------------------------------------------------------------
// S10 — authoredSpeed: raising runSpeed changes the PLAY RATE, not the
//       foot-plant distance per cycle

static void testS10PlayRate()
{
    section("S10 authoredSpeed — a faster runSpeed changes the rate, not the stride");

    const float dt = 1.0f / 60.0f;

    // The derived rate, MEASURED rather than recomputed: run the machine at a
    // pure sample (weight 1 on one clip, so L is that clip's own length) and
    // read the rate back out of how far the phase moved.
    //
    //     phase advances by dt * rate / L   =>   rate = dphase * L / dt
    auto measureRate = [&](float samplePosition, float authoredSpeed, float clipLength,
                           float speed) {
        QMap<QString, float> lengths;
        const iris::LocomotionAsset a = blendSpaceAsset(
            { { "Idle", 0.0f, 0.0f, 2.0f },
              { "Slow Run", samplePosition, authoredSpeed, clipLength } }, lengths);
        QString err;
        iris::AvatarLocomotion loco;
        loco.setAsset(a, &err);
        iris::AvatarLocomotionState p;
        p.grounded = true;
        p.speed = speed;
        loco.step(p, lengths, dt);
        const float before = loco.statePhase();
        loco.step(p, lengths, dt);
        float d = loco.statePhase() - before;
        if (d < 0.0f) d += 1.0f;
        return d * clipLength / dt;
    };

    // The run clip was MEASURED at 5.5 u/s (§7.5 acceptance item 4). At the
    // speed it was authored for, it plays at exactly 1.0.
    const float rateAt5_5 = measureRate(5.5f, 5.5f, 0.8f, 5.5f);
    CHECK(std::fabs(rateAt5_5 - 1.0f) < 1e-3f,
          qUtf8Printable(QStringLiteral("runSpeed 5.5, clip authored at 5.5 -> play rate 1.000 "
                                        "(got %1)").arg(double(rateAt5_5))));

    // Now the user raises runSpeed to 8. The SAMPLE moves (it sits at the knob)
    // but `authoredSpeed` does not — it is a property of the clip, not of the
    // knob. The rate has to take up the difference.
    const float rateAt8 = measureRate(8.0f, 5.5f, 0.8f, 8.0f);
    CHECK(std::fabs(rateAt8 - 8.0f / 5.5f) < 1e-3f,
          qUtf8Printable(QStringLiteral("runSpeed 8, same clip -> play rate 8/5.5 = 1.4545 "
                                        "(got %1)").arg(double(rateAt8))));

    // ...and THAT is what keeps the feet planted. Distance covered in one
    // animation cycle is speed * (L / rate); with the rate scaling, it is
    // L * authoredSpeed and does not depend on the knob at all.
    const float stride5_5 = 5.5f * (0.8f / rateAt5_5);
    const float stride8 = 8.0f * (0.8f / rateAt8);
    printf("      distance per cycle: at runSpeed 5.5 = %.4f u, at runSpeed 8 = %.4f u\n",
           double(stride5_5), double(stride8));
    CHECK(std::fabs(stride5_5 - stride8) < 1e-3f,
          "the FOOT-PLANT DISTANCE PER CYCLE is unchanged by raising runSpeed");

    // The counterfactual, so the assertion above is not vacuous: an UNMEASURED
    // clip (authoredSpeed 0 disables rate scaling) keeps rate 1.0 and the stride
    // grows with the knob — which is the foot slide every user reads as a broken
    // state machine.
    const float rateUnmeasured = measureRate(8.0f, 0.0f, 0.8f, 8.0f);
    CHECK(std::fabs(rateUnmeasured - 1.0f) < 1e-3f,
          qUtf8Printable(QStringLiteral("an UNMEASURED sample (authoredSpeed 0) keeps rate 1.0 "
                                        "(got %1)").arg(double(rateUnmeasured))));
    const float strideUnmeasured = 8.0f * (0.8f / rateUnmeasured);
    CHECK(strideUnmeasured > stride8 + 1.0f,
          qUtf8Printable(QStringLiteral("...and its stride grows to %1 u — the slide "
                                        "authoredSpeed exists to remove")
                             .arg(double(strideUnmeasured))));

    // The clamp is real and it is [0.5, 2.0]: past it the character out-runs
    // anything the clip can be stretched into without looking wrong.
    const float rateHuge = measureRate(30.0f, 5.5f, 0.8f, 30.0f);
    CHECK(std::fabs(rateHuge - 2.0f) < 1e-3f,
          qUtf8Printable(QStringLiteral("a 30 u/s knob CLAMPS the rate at 2.0 (got %1)")
                             .arg(double(rateHuge))));
    const float rateTiny = measureRate(0.5f, 5.5f, 0.8f, 0.5f);
    CHECK(std::fabs(rateTiny - 0.5f) < 1e-3f,
          qUtf8Printable(QStringLiteral("...and a very slow one clamps at 0.5 (got %1)")
                             .arg(double(rateTiny))));
}

// ---------------------------------------------------------------------------
// S12 — two avatars at different phases do not lock-step
//
// S7's tail already makes this claim about the PHASE. This section makes it
// about the thing the mirror actually pushes — the per-clip ABSOLUTE TIMES —
// and it makes it through the real seam (one scene, one Environment, one
// Scene::update per step), because "two avatars" is a property of the whole
// chain and not of one component.

static void testS12NoLockStep()
{
    section("S12 two avatars at different phases do not lock-step");

    Rig r = makeScene();
    addGroundPlane(r);
    auto n1 = addAvatar(r, canonicalClips(), canonicalLengths(), iris::Vec3(-3, 0, 0));
    auto n2 = addAvatar(r, canonicalClips(), canonicalLengths(), iris::Vec3(3, 0, 0));
    startPlay(r);

    frames(r, 20);
    n1->avatar()->setMoveInput(iris::Vec3(0, 0, -1));
    frames(r, 90);                       // A has been walking for 1.5 s...
    n2->avatar()->setMoveInput(iris::Vec3(0, 0, -1));
    frames(r, 120);                      // ...before B starts, and both settle

    auto *l1 = n1->locomotion();
    auto *l2 = n2->locomotion();
    CHECK(l1->currentState() == l2->currentState(),
          "both avatars are in the SAME state (so the difference is phase, not routing)");
    CHECK(!l1->weights().isEmpty() && !l2->weights().isEmpty(),
          "both publish a weight set");

    // The clip both are playing, and the absolute time each is playing it at:
    // the exact pair of numbers SceneMirror::syncClips turns into two
    // independent ClipState arrays.
    bool anyShared = false, allDiffer = true;
    float worstSame = 1.0f;
    for (const auto &w1 : l1->weights()) {
        for (const auto &w2 : l2->weights()) {
            if (w1.clip != w2.clip || w1.weight <= 1e-4f || w2.weight <= 1e-4f) continue;
            anyShared = true;
            const float d = std::fabs(w1.time - w2.time);
            worstSame = std::min(worstSame, d);
            if (d < 1e-3f) allDiffer = false;
            printf("      '%s': A at %.4f s, B at %.4f s\n", qUtf8Printable(w1.clip),
                   double(w1.time), double(w2.time));
        }
    }
    CHECK(anyShared, "the two avatars share at least one enabled clip (the lock-step risk)");
    CHECK(allDiffer,
          "...and each is at its OWN absolute time in it — the clock is PER AVATAR");

    // And they stay apart: a shared clock would converge them, a per-avatar one
    // holds the offset for as long as the inputs match.
    frames(r, 120);
    const float p1 = l1->statePhase(), p2 = l2->statePhase();
    printf("      after 2 more seconds: phase A %.4f, phase B %.4f\n", double(p1), double(p2));
    CHECK(std::fabs(p1 - p2) > 1e-3f, "...and they are still apart two seconds later");
}

// ---------------------------------------------------------------------------

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    enginetest::DocumentGraph graph("avatar-locomotion-ogre.log");
    if (!graph.require()) return 1;

    testVocabulary();
    testS1SpeedRamp();
    testS2TransitionTiming();
    testS3JumpChain();
    testS4Degradation();
    testS5Refusal();
    testS6RoundTrip();
    testS7Determinism();
    testS8BlendSpaceWeights();
    testS9ClipSync();
    testS10PlayRate();
    testS12NoLockStep();

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures,
           failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
