/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "modules/vr/vrinteraction.h"

#include <QObject>
#include <QUndoStack>

#include "commands/transformscenenodecommand.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "services/editgate.h"
#include "services/sceneeditservice.h"
#include "services/selectionservice.h"
#include "services/services.h"
#include "services/undoservice.h"
#include "services/vrorigin.h"
#include "viewport/scenepicker.h"
#include "viewport/snapsettings.h"

using namespace jahshaka::engine;

namespace {

inline iris::Quat toIris(const Quat &q) { return iris::Quat(q.w, q.x, q.y, q.z); }
inline iris::Vec3 toIris(const Vec3 &v) { return iris::Vec3(v.x, v.y, v.z); }
inline Vec3 toEngine(const iris::Vec3 &v) { return Vec3(v.x(), v.y(), v.z()); }

/// HOW FAR A CONTROLLER'S RAY REACHES, metres. 200 is Scene::rayCast's own
/// default maxDistance (VR_INPUT_SPEC §3) — far enough to point at a landscape,
/// short enough that a miss costs one broad-phase sweep and not a walk of the
/// whole scene's AABB tree.
inline constexpr float kRayLength = 200.0f;

/// THE FRAME A SESSION-LESS STEP CHARGES, seconds. A gate drives this object
/// through injected input with no runtime and no render loop, so there is no
/// frame to measure; 1/90 s is the headset's own period and makes a scripted
/// step cost exactly what a worn one does. (Counting a frame rather than
/// reading a clock is the rule this whole surface follows — CLAUDE.md: a
/// wall-clock settle measures nothing in this engine.)
inline constexpr float kNominalFrame = 1.0f / 90.0f;

/// A selection tick and a grab tick, in the wearer's hand. Short: a haptic
/// that lasts long enough to notice as a buzz reads as a fault.
inline constexpr float kTickSeconds = 0.02f;
inline constexpr float kTickAmplitude = 0.4f;

}   // namespace

// ---------------------------------------------------------------------------
// The two sources

VrHandState VrInjectedInput::hand(unsigned hand) const
{
    if (hand >= VrHandCount) return VrHandState();
    return mHands[hand];
}

void VrInjectedInput::set(unsigned hand, const VrHandState &state)
{
    if (hand >= VrHandCount) return;
    mHands[hand] = state;
    // SAID BY THE SOURCE, NOT BY THE CALLER: whatever a script passes, a state
    // that arrived here did not come from a runtime, and the flag is what a
    // suite asserts to prove a smoke on a headset is not reading an injection.
    mHands[hand].fromInjection = true;
    mArmed = true;
}

void VrInjectedInput::clear()
{
    for (unsigned i = 0; i < VrHandCount; ++i) mHands[i] = VrHandState();
    mArmed = false;
    mFocused = true;
}

VrHandState VrEngineInput::hand(unsigned hand) const
{
    if (!mEngine || hand >= VrHandCount) return VrHandState();
#ifdef JAH_ENGINE_HAS_VRHANDSTATE
    return mEngine->vrStatus().input[hand];
#else
    // THE ENGINE HALF HAS NOT LANDED (see the header's contract note). "No
    // controller reports" is the truthful answer, and it is also what a box
    // with a headset but no action set would say — so nothing downstream needs
    // a second code path for it.
    return VrHandState();
#endif
}

bool VrEngineInput::focused() const
{
    if (!mEngine) return false;
    const VrStatus st = mEngine->vrStatus();
    // FOCUSED IS THE ONLY STATE IN WHICH A RUNTIME REPORTS INPUT
    // (VR_INPUT_SPEC §2.3): xrSyncActions answers XR_SESSION_NOT_FOCUSED
    // otherwise and every action reads "nothing pressed". Asking the lifecycle
    // word rather than the values is what lets a gesture CANCEL instead of
    // believing a release nobody made.
    return st.active && st.state == VrState::Focused;
}

// ---------------------------------------------------------------------------
// The service

void VrInteraction::begin()
{
    mInstalled = true;
    for (unsigned i = 0; i < VrHandCount; ++i) mPrev[i] = VrHandState();
    mHover = Hover();
    mTurnArmed = true;
}

void VrInteraction::end()
{
    // A SESSION THAT ENDS MID-GESTURE CANCELS IT (§5.5). The wearer cannot see
    // what they were holding any more, and committing a gesture they could not
    // finish would leave the object somewhere nobody chose.
    cancel();
    mInstalled = false;
    mHover = Hover();
    for (unsigned i = 0; i < VrHandCount; ++i) mPrev[i] = VrHandState();
}

iris::ScenePtr VrInteraction::scene() const
{
    return mDeps.scene ? mDeps.scene() : iris::ScenePtr();
}

Engine *VrInteraction::engineNow() const
{
    return mDeps.engine ? mDeps.engine() : nullptr;
}

unsigned VrInteraction::dominantHand() const
{
    return mOptions.dominantRight ? unsigned(VrHandRight) : unsigned(VrHandLeft);
}

unsigned VrInteraction::offHand() const
{
    return mOptions.dominantRight ? unsigned(VrHandLeft) : unsigned(VrHandRight);
}

VrHandState VrInteraction::handState(unsigned hand) const
{
    // THE INJECTION WINS ONCE ANYTHING HAS BEEN INJECTED, and that is the whole
    // of the test route on this side: the guard against fooling a real smoke
    // lives at the ENGINE's injection entry point (it refuses while a bound
    // profile reports, VR_INPUT_SPEC §2.4 I1), because that is where "a runtime
    // is answering" is knowable.
    if (mInjected.armed()) return mInjected.hand(hand);
    if (mSource) return mSource->hand(hand);
    return VrHandState();
}

QString VrInteraction::sourceName() const
{
    if (mInjected.armed()) return mInjected.name();
    return mSource ? mSource->name() : QStringLiteral("none");
}

bool VrInteraction::ray(const VrHandState &state, iris::Vec3 &origin, iris::Vec3 &direction) const
{
    if (!state.valid || !state.aim.valid) return false;
    origin = toIris(state.aim.position);
    direction = vrgrab::aimDirection(toIris(state.aim.rotation));
    return true;
}

VrInteraction::Hover VrInteraction::pick(unsigned hand, const VrHandState &state) const
{
    Hover out;
    out.hand = hand;
    if (!ray(state, out.origin, out.direction)) return out;
    const iris::ScenePtr doc = scene();
    if (!doc) return out;
    // THE DOCUMENT'S OWN PICKER, the same entry point a viewport click uses
    // (EngineSceneViewport::pickAt): Ogre's broad phase, our triangles, and
    // `pickable` — the LOCK — decided per candidate inside it. The camera
    // position it ranks from is the RAY's origin, which for a hand is the hand.
    const iris::Vec3 a = out.origin;
    const iris::Vec3 b = out.origin + out.direction * kRayLength;
    const QList<ScenePick> hits = ScenePicker::pickAll(doc, a, b, a);
    const ScenePick best = ScenePicker::nearest(hits);
    if (!best.node) return out;
    out.hit = true;
    out.picked = best.node;
    out.point = best.hitPoint;
    out.distance = (best.hitPoint - a).length();
    out.triangleIndex = best.triangleIndex;
    // THE ROOT RULE AGAINST THE SET (EDITOR_MULTISELECT_SPEC §3.5) — a ray on a
    // part of an asset selects the asset, and only a ray on an asset that IS
    // already selected drills into the part.
    out.node = ScenePicker::resolveRootSelection(
        best.node, mDeps.selection ? mDeps.selection->selectedSet() : QList<iris::SceneNodePtr>(),
        true);
    return out;
}

void VrInteraction::pushRay() const
{
#ifdef JAH_ENGINE_HAS_VRHANDSTATE
    Engine *engine = engineNow();
    if (!engine) return;
    VrRayState ray;
    ray.visible = mHover.hand < VrHandCount && (mHover.hit || !mHover.direction.isNull());
    ray.origin = toEngine(mHover.origin);
    ray.dir = toEngine(mHover.direction);
    ray.hit = mHover.hit;
    ray.hitPoint = toEngine(mHover.point);
    engine->setVrRay(ray);
#endif
    // WITHOUT THE ENGINE HALF the ray is still COMPUTED and reported by
    // `vr.hover()` — nothing about the gesture depends on it being drawn, which
    // is why the hit is this side's and the line is the engine's.
}

void VrInteraction::haptic(unsigned hand, float amplitude, float seconds) const
{
#ifdef JAH_ENGINE_HAS_VRHANDSTATE
    if (Engine *engine = engineNow()) engine->vrHaptic(hand, amplitude, seconds);
#else
    Q_UNUSED(hand); Q_UNUSED(amplitude); Q_UNUSED(seconds);
#endif
}

bool VrInteraction::applySelection(const Hover &h, SelectMode mode)
{
    SelectionService *sel = mDeps.selection;
    if (!sel) return false;
    if (!h.hit) {
        // A MODIFIED PRESS ON EMPTY SPACE KEEPS THE SET — the desktop rule
        // (EngineSceneViewport::mousePressEvent): clearing it would make a
        // slightly missed toggle throw the whole selection away, and in a
        // headset a slightly missed anything is the normal case.
        if (mode != SelectMode::Replace) return false;
        if (sel->selectedSet().isEmpty()) return false;
        sel->select(iris::SceneNodePtr());
        ++mSelects;
        return true;
    }
    bool changed = false;
    // THE WORLD ROOT REPLACES (D6) — it is not a thing you add to a set.
    if (h.node && h.node->isRootNode()) { sel->select(h.node); changed = true; }
    else if (mode == SelectMode::Toggle)  changed = sel->toggle(h.node) || true;
    else if (mode == SelectMode::Add)     changed = sel->add(h.node);
    else                                { sel->select(h.node); changed = true; }
    if (changed) {
        ++mSelects;
        haptic(h.hand, kTickAmplitude, kTickSeconds);
    }
    return changed;
}

bool VrInteraction::select(unsigned hand, SelectMode mode)
{
    if (hand >= VrHandCount) return false;
    const VrHandState st = handState(hand);
    if (!st.valid) return false;
    return applySelection(pick(hand, st), mode);
}

QList<iris::SceneNodePtr> VrInteraction::grabTargets(const iris::SceneNodePtr &under) const
{
    SelectionService *sel = mDeps.selection;
    QList<iris::SceneNodePtr> set = sel ? sel->selectedSet() : QList<iris::SceneNodePtr>();
    if (under) {
        bool member = false;
        for (const auto &n : set) if (n == under) { member = true; break; }
        // A GRAB ON SOMETHING THAT IS NOT SELECTED TAKES THAT ONE THING (§4's
        // rule: select-then-grab is two intents, and a squeeze on an unselected
        // object is the one gesture that means both).
        if (!member) set = QList<iris::SceneNodePtr>{ under };
    }
    if (set.size() < 2) return set;
    // D5: a member with a selected ancestor already moves with it, and
    // transforming both moves it twice. The same reduction the gizmo's group
    // uses (EngineSceneViewport::pushGizmoGroup).
    return SceneEditService::effectiveSet(set);
}

bool VrInteraction::beginGrab(unsigned hand)
{
    if (hand >= VrHandCount) return false;
    if (mGesture.active) return false;      // stage 1 is SINGLE-hand (owner answer 7)
    const VrHandState st = handState(hand);
    if (!st.valid || !st.grip.valid) return false;
    // THE EDIT GATE, ASKED BEFORE THE GESTURE STARTS, not when it ends (owner,
    // ledger §423; the viewport's gizmo does exactly this): a grab writes the
    // document live, so letting it run and then dropping its undo step would
    // move the object and snap it back. The ray still hovers and still selects
    // while a script runs — those are reads.
    if (editgate::refuse()) return false;

    const Hover h = pick(hand, st);
    if (h.hit && (!mDeps.selection || !mDeps.selection->isSelected(h.node)))
        applySelection(h, SelectMode::Replace);

    const QList<iris::SceneNodePtr> targets = grabTargets(h.hit ? h.node : iris::SceneNodePtr());
    if (targets.isEmpty()) return false;

    Gesture g;
    g.active = true;
    g.hand = hand;
    g.snapping = st.menuPressed;
    // NEAR OR FAR is one comparison and nothing else changes (§5.1): within
    // arm's reach the object is in the hand and rides the GRIP pose; beyond it
    // the object is out on the ray and rides a virtual hand at the hit, which
    // is what lets the stick push it away and turn it.
    const vrgrab::Pose grip{ toIris(st.grip.position), toIris(st.grip.rotation) };
    const vrgrab::Pose aim{ toIris(st.aim.position), toIris(st.aim.rotation) };
    if (h.hit && h.distance > vrgrab::kArmReach) {
        g.far = true;
        g.distance = h.distance;
        g.handStart = vrgrab::virtualFarHand(aim, g.distance);
    } else if (!h.hit) {
        // NO HIT, BUT A SELECTION: a near grab of what is already selected —
        // the wearer reaches out and takes hold of it (§5.1's near-grab case).
        g.far = false;
        g.handStart = grip;
    } else {
        g.far = false;
        g.handStart = grip;
    }
    g.handNow = g.handStart;
    for (const auto &node : targets) {
        if (!node) continue;
        Member m;
        m.node = node;
        m.startGlobal = vrgrab::Pose{ node->getGlobalPosition(), node->getGlobalRotation() };
        m.startLocalPos = node->getLocalPos();
        m.startLocalRot = node->getLocalRot();
        m.startLocalScale = node->getLocalScale();
        g.members.append(m);
    }
    if (g.members.isEmpty()) return false;
    mGesture = g;
    ++mGrabs;
    haptic(hand, kTickAmplitude, kTickSeconds);
    return true;
}

void VrInteraction::followGesture(float seconds)
{
    if (!mGesture.active) return;
    const VrHandState st = handState(mGesture.hand);
    // A HAND THAT STOPPED REPORTING HOLDS THE OBJECT WHERE IT IS. One skipped
    // locate is not a released trigger (VrPose's own note), and moving the
    // object to a pose nobody located would be an invented gesture.
    if (!st.valid) return;

    vrgrab::Pose hand;
    if (mGesture.far) {
        if (!st.aim.valid) return;
        // THE STICK ALONG THE RAY, and a turntable about the world's up — the
        // two things a far grab cannot do with the wrist.
        mGesture.distance = vrgrab::pushPulled(mGesture.distance, st.stickY, seconds);
        mGesture.turntable += vrgrab::turntableDegrees(st.stickX, seconds);
        const vrgrab::Pose aim{ toIris(st.aim.position), toIris(st.aim.rotation) };
        const vrgrab::Pose target = vrgrab::virtualFarHand(aim, mGesture.distance);
        // THE LEVER'S FILTER (§12.6): at range the hand's own tremor is
        // multiplied by the distance, so the virtual hand is low-passed with a
        // time constant that grows with it.
        mGesture.handNow = vrgrab::smoothedTowards(
            mGesture.handNow, target,
            vrgrab::onePoleAlpha(seconds, vrgrab::leverTau(mGesture.distance)));
        hand = mGesture.handNow;
    } else {
        if (!st.grip.valid) return;
        hand = vrgrab::Pose{ toIris(st.grip.position), toIris(st.grip.rotation) };
        mGesture.handNow = hand;
    }

    const float translateStep = mGesture.snapping ? SnapSettings::translateSize() : 0.0f;
    const float rotateStep = mGesture.snapping ? SnapSettings::rotateSize() : 0.0f;

    // THE TURNTABLE'S PIVOT IS THE PRIMARY'S, not each member's own position:
    // a group must turn TOGETHER (one object spinning in place beside another
    // spinning in place is not a rotation of the group).
    iris::Vec3 pivot;
    bool havePivot = false;
    for (int i = 0; i < mGesture.members.size(); ++i) {
        const Member &m = mGesture.members.at(i);
        if (!m.node) continue;
        vrgrab::Pose followed = vrgrab::rigidFollow(mGesture.handStart, hand, m.startGlobal);
        if (!havePivot) { pivot = followed.position; havePivot = true; }
        if (mGesture.far && std::fabs(mGesture.turntable) > 1e-4f)
            followed = vrgrab::spunAboutUp(followed, pivot, mGesture.turntable);
        // SNAP QUANTISES THE DELTA off where this member STARTED (never the
        // absolute — see vrgrab.h's conventions).
        if (mGesture.snapping)
            followed = vrgrab::snappedFrom(m.startGlobal, followed, translateStep, rotateStep);
        // A WORLD WRITE, the setter the gizmo writes live through: the hand's
        // pose is world and a member may be parented to anything.
        m.node->setGlobalPosRot(followed.position, followed.rotation);
    }
}

void VrInteraction::rewindGesture()
{
    for (const Member &m : mGesture.members) {
        if (!m.node) continue;
        m.node->setLocalPos(m.startLocalPos);
        m.node->setLocalRot(m.startLocalRot);
        m.node->setLocalScale(m.startLocalScale);
    }
}

bool VrInteraction::endGrab(unsigned hand)
{
    if (!mGesture.active) return false;
    // THE OTHER HAND'S RELEASE IS NOT THIS GESTURE'S. With single-hand grab
    // (stage 1) the off hand's squeeze does nothing at all, and must not end
    // what the dominant hand is holding.
    if (hand < VrHandCount && hand != mGesture.hand) return false;

    // ONE UNDO MACRO PER GESTURE, in the gizmo's shape (Gizmo::createUndoAction,
    // src/viewport/gizmo.cpp:255-285): every member is put back to where the
    // gesture started and re-applied through its own TransformSceneNodeCommand,
    // all inside one macro; N = 1 is a single command with no macro, which is
    // what keeps a one-object grab's undo stack identical to a mouse drag's.
    //
    // THE SHAPE IS DUPLICATED RATHER THAN SHARED, deliberately and as recorded
    // debt: VR_INPUT_SPEC §5.1 wants it extracted as one free function both
    // callers use, and that extraction edits gizmo.cpp — a file this lane does
    // not own. Reported upward instead of reached into.
    struct Applied
    {
        iris::SceneNodePtr node;
        iris::Vec3 pos, scale;
        iris::Quat rot;
    };
    QVector<Applied> applied;
    for (const Member &m : mGesture.members) {
        if (!m.node) continue;
        applied.append({ m.node, m.node->getLocalPos(), m.node->getLocalScale(),
                         m.node->getLocalRot() });
    }

    UndoService *undo = mDeps.services ? mDeps.services->undo : nullptr;
    if (editgate::blocked()) {
        // A SCRIPT RUN STARTED WHILE THE WEARER WAS HOLDING SOMETHING. The
        // document is the run's; the gesture is rewound and NOTHING is pushed
        // (an empty macro on the stack would eat the user's next Ctrl+Z — the
        // gizmo learned this the hard way, round 2 item 8).
        rewindGesture();
        mGesture = Gesture();
        ++mCancels;
        return true;
    }
    if (!undo) {
        // NO UNDO SPINE (a headless host, a test stand-in): keep the move. The
        // alternative — rewinding it because nobody can record it — would make
        // the gesture silently do nothing at all.
        mGesture = Gesture();
        return true;
    }
    for (const Member &m : mGesture.members) {
        if (!m.node) continue;
        m.node->setLocalPos(m.startLocalPos);
        m.node->setLocalRot(m.startLocalRot);
        m.node->setLocalScale(m.startLocalScale);
    }
    QUndoStack *stack = undo->stack();
    const bool macro = applied.size() > 1 && stack;
    if (macro) stack->beginMacro(QObject::tr("Transform %1 objects").arg(applied.size()));
    for (const Applied &a : applied)
        undo->push(new TransformSceneNodeCommand(a.node, a.pos, a.rot, a.scale));
    if (macro) stack->endMacro();
    ++mCommits;
    haptic(mGesture.hand, kTickAmplitude, kTickSeconds);
    mGesture = Gesture();
    return true;
}

bool VrInteraction::cancel()
{
    if (!mGesture.active) return false;
    rewindGesture();
    mGesture = Gesture();
    ++mCancels;
    return true;
}

QList<iris::SceneNodePtr> VrInteraction::gestureNodes() const
{
    QList<iris::SceneNodePtr> out;
    for (const Member &m : mGesture.members) if (m.node) out.append(m.node);
    return out;
}

bool VrInteraction::rigNow(vrorigin::Rig &rig, iris::Vec3 &headPosition,
                           iris::Quat &headRotation) const
{
    Engine *engine = engineNow();
    if (!engine) return false;
    const VrStatus st = engine->vrStatus();
    // THE RIG ONLY EXISTS WHILE A SESSION DOES. Engine::setVrOrigin is a no-op
    // without one (OgreEngine.cpp:1296 — "a host sets it right after
    // beginVrSession"), so locomotion REFUSES rather than pretending: the
    // arithmetic is asserted by `vr.grab_maths`, and the rig by the session
    // suites.
    if (!st.active || !st.posesValid) return false;
    // THE ENGINE'S ORIGIN IS THE ONE TRUTH — read every time, never cached: it
    // is whatever the host last pushed COMPOSED with any runtime recentre the
    // pump absorbed since (the rule VR-4-FIX paid for).
    rig.position = toIris(st.origin);
    rig.yaw = st.originYaw;
    headPosition = toIris(st.headPosition);
    headRotation = toIris(st.headRotation);
    return true;
}

bool VrInteraction::turn(float degrees)
{
    if (std::fabs(degrees) < 1e-4f) return false;
    vrorigin::Rig rig;
    iris::Vec3 head;
    iris::Quat headRot;
    if (!rigNow(rig, head, headRot)) return false;
    Engine *engine = engineNow();
    if (!engine) return false;
    const vrorigin::Rig out = vrgrab::turnedAboutHead(rig, head, degrees);
    engine->setVrOrigin(toEngine(out.position), out.yaw);
    ++mTurns;
    return true;
}

bool VrInteraction::fly(float stickX, float stickY, float seconds, bool boost)
{
    vrorigin::Rig rig;
    iris::Vec3 head;
    iris::Quat headRot;
    if (!rigNow(rig, head, headRot)) return false;
    const float speed = mDeps.wearerSpeed ? mDeps.wearerSpeed() : 0.0f;
    if (speed <= 0.0f) return false;
    const iris::Vec3 delta = vrgrab::stickFlyDelta(headRot, stickX, stickY, speed,
                                                   vrorigin::frameSeconds(seconds), boost);
    if (delta.isNull()) return false;
    Engine *engine = engineNow();
    if (!engine) return false;
    rig.position += delta;
    engine->setVrOrigin(toEngine(rig.position), rig.yaw);
    return true;
}

void VrInteraction::step(float seconds)
{
    const float dt = vrorigin::frameSeconds(seconds > 0.0f ? seconds : kNominalFrame);
    VrHandState hands[VrHandCount];
    for (unsigned i = 0; i < VrHandCount; ++i) hands[i] = handState(i);

    const bool focused = mInjected.armed() ? mInjected.focused()
                                           : (mSource ? mSource->focused() : false);
    if (!focused) {
        // THE RUNTIME TOOK THE INPUT AWAY (the dashboard came up, the session
        // left Focused). A gesture in flight CANCELS — see the header.
        cancel();
        mHover = Hover();
        pushRay();
        for (unsigned i = 0; i < VrHandCount; ++i) mPrev[i] = hands[i];
        return;
    }

    // IN THE PLAYER, ONLY LOCOMOTION RUNS (the Player edits nothing).
    const bool player = mDeps.playerMode && mDeps.playerMode();
    const unsigned dominant = dominantHand();

    if (!player) {
        const VrHandState &d = hands[dominant];
        const VrHandState &p = mPrev[dominant];
        mHover = d.valid ? pick(dominant, d) : Hover();
        pushRay();

        // THE PRESS EDGES. Counted transitions, never a held value: a verb and
        // a button must produce exactly one gesture each.
        if (d.selectPressed && !p.selectPressed && !mGesture.active)
            select(dominant, d.menuPressed ? SelectMode::Toggle : SelectMode::Replace);
        if (d.grabPressed && !p.grabPressed) beginGrab(dominant);
        else if (!d.grabPressed && p.grabPressed) endGrab(dominant);

        if (mGesture.active) {
            // MENU HELD SNAPS, per frame — the Ctrl of a desktop drag, which is
            // also asked per frame and not at the press.
            mGesture.snapping = handState(mGesture.hand).menuPressed;
            followGesture(dt);
        }
    }

    // ---- LOCOMOTION: THE OFF HAND'S STICK --------------------------------
    //
    // Y WALKS, X TURNS, on ONE stick (the brief's arrangement, and the Quest's
    // comfort default). The dominant hand's stick belongs to a far grab's
    // push/pull and turntable, so it cannot also steer; strafing is therefore
    // not on a stick in stage 1 — `vr.move({left:true})` still does it, and the
    // wearer's own feet always did. Reviewed after the owner's smoke.
    const VrHandState &o = hands[offHand()];
    if (o.valid) {
        fly(0.0f, o.stickY, dt, false);
        if (mOptions.turn == Turn::Smooth) {
            turn(vrgrab::smoothTurnDegrees(o.stickX, dt, mOptions.smoothTurnDegreesPerSecond));
        } else {
            const float deg = vrgrab::snapTurnDegrees(o.stickX, mTurnArmed, mOptions.snapTurnDegrees);
            if (std::fabs(deg) > 1e-4f && turn(deg)) mTurnArmed = false;
            if (vrgrab::snapTurnRearmed(o.stickX)) mTurnArmed = true;
        }
    }

    for (unsigned i = 0; i < VrHandCount; ++i) mPrev[i] = hands[i];
}

QVariantMap VrInteraction::report() const
{
    QVariantMap out;
    out[QStringLiteral("dominant")] =
        mOptions.dominantRight ? QStringLiteral("right") : QStringLiteral("left");
    out[QStringLiteral("turn")] =
        mOptions.turn == Turn::Snap ? QStringLiteral("snap") : QStringLiteral("smooth");
    out[QStringLiteral("snapTurnDegrees")] = double(mOptions.snapTurnDegrees);
    out[QStringLiteral("smoothTurnDegreesPerSecond")] =
        double(mOptions.smoothTurnDegreesPerSecond);
    out[QStringLiteral("installed")] = mInstalled;
    out[QStringLiteral("grabbing")] = mGesture.active;
    out[QStringLiteral("far")] = mGesture.active && mGesture.far;
    out[QStringLiteral("snapping")] = mGesture.active && mGesture.snapping;
    out[QStringLiteral("distance")] = double(mGesture.active ? mGesture.distance : 0.0f);
    out[QStringLiteral("nodes")] = mGesture.members.size();
    out[QStringLiteral("hovering")] = mHover.hit;
    out[QStringLiteral("source")] = sourceName();
    // THE RIG, AS THE ENGINE HOLDS IT — read, never remembered (see rigNow).
    // It is reported HERE rather than on vr.state() because this is the object
    // that writes it: a suite asserting that a thumbstick walked the wearer
    // needs the number the walk came out of, beside the counters that say the
    // walk happened at all. `live` false means there is no session and so no
    // rig — the state in which locomotion refuses.
    {
        vrorigin::Rig r;
        iris::Vec3 head;
        iris::Quat headRot;
        const bool live = rigNow(r, head, headRot);
        QVariantMap rig;
        rig[QStringLiteral("live")] = live;
        rig[QStringLiteral("x")] = double(r.position.x());
        rig[QStringLiteral("y")] = double(r.position.y());
        rig[QStringLiteral("z")] = double(r.position.z());
        rig[QStringLiteral("yaw")] = double(r.yaw);
        out[QStringLiteral("rig")] = rig;
    }
    // COUNTS, never a wall clock (VR_SPEC §6 flake class b).
    out[QStringLiteral("selects")] = QVariant::fromValue(qulonglong(mSelects));
    out[QStringLiteral("grabs")] = QVariant::fromValue(qulonglong(mGrabs));
    out[QStringLiteral("commits")] = QVariant::fromValue(qulonglong(mCommits));
    out[QStringLiteral("cancels")] = QVariant::fromValue(qulonglong(mCancels));
    out[QStringLiteral("turns")] = QVariant::fromValue(qulonglong(mTurns));
    return out;
}
