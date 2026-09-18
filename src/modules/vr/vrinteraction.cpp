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
#include "irisgl/document/scenegraph/nodegraph.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "services/editgate.h"
#include "services/sceneeditservice.h"
#include "services/selectionservice.h"
#include "services/services.h"
#include "services/undoservice.h"
#include "services/vrorigin.h"
#include "viewport/gizmo.h"
#include "viewport/gizmomode.h"
#include "viewport/gizmoray.h"
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

/// HOW LONG A `menu` PRESS MAY LAST AND STILL BE A SHORT ONE, seconds — the
/// line between "cycle the gizmo mode" and "hold the modifier" (VR_INPUT_SPEC
/// §5.2, the owner's answer 10). 0.25 s is the usual short-press threshold and
/// is comfortably longer than a deliberate click.
///
/// It is charged in the durations step() is given: the nominal frame on the
/// scripted route (so N injected frames are exactly N/90 s, whatever the box is
/// doing) and the driver's own frame time, clamped by vrorigin::frameSeconds,
/// in a worn session — real time there, quantised by the frame rate. See the
/// header's note on mMenuHeldSeconds.
inline constexpr float kMenuHoldSeconds = 0.25f;


}   // namespace

// ---------------------------------------------------------------------------
// THE ONE SOURCE

VrHandState VrEngineInput::hand(unsigned hand) const
{
    if (!mEngine || hand >= VrHandCount) return VrHandState();
    // WHOEVER WROTE IT. The runtime's action system fills these fields inside
    // the session's frame and `Engine::vrInjectInput` fills them from a script
    // with no runtime at all; the engine reports them through the same field
    // either way and marks the injected ones (`fromInjection`), which is what
    // lets every gesture below be driven headlessly without a second store.
    return mEngine->vrStatus().input[hand];
}

bool VrEngineInput::focused() const
{
    if (!mEngine) return false;
    // FOCUS IS THE SESSION'S, ONE BIT (VR-INPUT-1E-FIX, the lead's re-point at
    // merge): `VrStatus::inputFocused` is the session's state, or the injected
    // focus while any hand is injected — so an injected focus loss cancels a
    // gesture in flight, a focused session with the controllers switched off
    // still reads as focused, and a process with no session reads as not. The
    // per-hand bit it replaced was a scripting trap (an unfocus on one hand
    // beside a stale valid injection on the other cancelled nothing).
    return mEngine->vrStatus().inputFocused;
}

// ---------------------------------------------------------------------------
// The service

void VrInteraction::begin()
{
    mInstalled = true;
    for (unsigned i = 0; i < VrHandCount; ++i) mPrev[i] = VrHandState();
    mHover = Hover();
    mTurnArmed = true;
    mMemo = PickMemo();
    mRefreshed = false;
}

void VrInteraction::end()
{
    // A SESSION THAT ENDS MID-GESTURE CANCELS IT (§5.5). The wearer cannot see
    // what they were holding any more, and committing a gesture they could not
    // finish would leave the object somewhere nobody chose.
    cancel();
    // ...AND THE GIZMO GOES BACK TO THE DESK (stage 2): the size rule, the
    // highlight and the camera-facing rules are the desktop camera's again from
    // the viewport's very next frame.
    armGizmo(dominantHand(), false);
    mInstalled = false;
    mHover = Hover();
    mMemo = PickMemo();
    mRefreshed = false;
    for (unsigned i = 0; i < VrHandCount; ++i) mPrev[i] = VrHandState();
    // AN INJECTION DIES WITH THE SESSION — AND THAT IS NOW THE ENGINE'S RULE,
    // NOT THIS OBJECT'S. The Studio-local store this used to clear here (the
    // lead's §666 fix: one console injection in a real session took the
    // controllers away for the life of the process) is GONE, and the samples
    // live in the one place that can enforce the rule for every host at once:
    // the engine clears its store on a session's begin and end, refuses a
    // write while a bound profile reports, and ignores a stale one
    // (VR_INPUT_SPEC §2.4 I1, engine lane VR-INPUT-1E-FIX). A second
    // withdrawal from up here would be a second owner of the same rule, which
    // is how the two stores came about in the first place.
}

iris::ScenePtr VrInteraction::scene() const
{
    return mDeps.scene ? mDeps.scene() : iris::ScenePtr();
}

Engine *VrInteraction::engineNow() const
{
    return mDeps.engine ? mDeps.engine() : nullptr;
}

bool VrInteraction::locomotionBlocked() const
{
    return mDeps.locomotionBlocked && mDeps.locomotionBlocked();
}

bool VrInteraction::playerHosted() const
{
    return mDeps.playerMode && mDeps.playerMode();
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
    // ONE STORE, ONE ANSWER. Whether a runtime or a script wrote the sample is
    // the SAMPLE's word (`fromInjection`) and changes nothing here; the guard
    // against fooling a real smoke lives at the ENGINE's injection entry point
    // (it refuses while a bound profile reports, VR_INPUT_SPEC §2.4 I1),
    // because that is where "a runtime is answering" is knowable.
    if (mSource) return mSource->hand(hand);
    return VrHandState();
}

bool VrInteraction::injectionArmed() const
{
    if (!mSource) return false;
    for (unsigned h = 0; h < VrHandCount; ++h)
        if (mSource->hand(h).fromInjection) return true;
    return false;
}

QString VrInteraction::sourceName() const
{
    if (!mSource) return QStringLiteral("none");
    return injectionArmed() ? QStringLiteral("injection") : mSource->name();
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

    // THE MEMO'S KEY (see the header): the hand, the scene, the aim pose's own
    // numbers and the document's transform/structure epoch. Everything the
    // GEOMETRIC pick depends on and nothing else.
    const unsigned long long writes = iris::graph::transformWrites();
    const bool sameRay =
        mMemo.valid && mMemo.scene == doc.data() && mMemo.hand == hand &&
        mMemo.writes == writes &&
        mMemo.px == state.aim.position.x && mMemo.py == state.aim.position.y &&
        mMemo.pz == state.aim.position.z && mMemo.rx == state.aim.rotation.x &&
        mMemo.ry == state.aim.rotation.y && mMemo.rz == state.aim.rotation.z &&
        mMemo.rw == state.aim.rotation.w;

    if (!sameRay) {
        // THE DOCUMENT'S OWN PICKER, the same entry point a viewport click uses
        // (EngineSceneViewport::pickAt): Ogre's broad phase, our triangles, and
        // `pickable` — the LOCK — decided per candidate inside it. The camera
        // position it ranks from is the RAY's origin, which for a hand is the
        // hand.
        const iris::Vec3 a = out.origin;
        const iris::Vec3 b = out.origin + out.direction * kRayLength;
        // THE `update(0)` WALK, ONLY WHEN SOMETHING WAS WRITTEN — and at this
        // tree it does no transform work at all (scenenode.cpp: composition,
        // invalidation and propagation are Ogre's; world transforms come back
        // through getGlobalTransform), so skipping it is exact for that reason
        // and `ScenePicker::pickAll`'s `refreshTransforms` is a vestige for a
        // viewport CRUD. A local can only change through a transform write or
        // a reparent — both bump the epoch (nodegraph.cpp). So refreshing when the
        // counter has not moved since the last refresh cannot change an answer;
        // it is the full recursive walk of the scene the picker's header tells
        // callers inside a live drag to skip.
        const bool refresh = !mRefreshed || mRefreshedAtWrites != writes;
        const QList<ScenePick> hits =
            ScenePicker::pickAll(doc, a, b, a, false, true, true, refresh, true);
        if (refresh) { mRefreshed = true; mRefreshedAtWrites = writes; }
        const ScenePick best = ScenePicker::nearest(hits);
        mMemo = PickMemo();
        mMemo.valid = true;
        mMemo.scene = doc.data();
        mMemo.hand = hand;
        mMemo.writes = writes;
        mMemo.px = state.aim.position.x;
        mMemo.py = state.aim.position.y;
        mMemo.pz = state.aim.position.z;
        mMemo.rx = state.aim.rotation.x;
        mMemo.ry = state.aim.rotation.y;
        mMemo.rz = state.aim.rotation.z;
        mMemo.rw = state.aim.rotation.w;
        mMemo.origin = out.origin;
        mMemo.direction = out.direction;
        if (best.node) {
            mMemo.hit = true;
            mMemo.picked = best.node;
            mMemo.point = best.hitPoint;
            mMemo.distance = (best.hitPoint - a).length();
            mMemo.triangleIndex = best.triangleIndex;
        }
    }

    if (!mMemo.hit) return out;
    out.hit = true;
    out.picked = mMemo.picked;
    out.point = mMemo.point;
    out.distance = mMemo.distance;
    out.triangleIndex = mMemo.triangleIndex;
    // THE ROOT RULE AGAINST THE SET (EDITOR_MULTISELECT_SPEC §3.5) — a ray on a
    // part of an asset selects the asset, and only a ray on an asset that IS
    // already selected drills into the part. RE-RESOLVED EVERY CALL, memo or
    // not: it depends on the SELECTION, which a press changes, so caching it
    // would be the way the second press on an asset stopped drilling in.
    out.node = ScenePicker::resolveRootSelection(
        mMemo.picked, mDeps.selection ? mDeps.selection->selectedSet() : QList<iris::SceneNodePtr>(),
        true);
    return out;
}

void VrInteraction::pushRay() const
{
    Engine *engine = engineNow();
    if (!engine) return;
    VrRayState ray;
    ray.visible = mHover.hand < VrHandCount && (mHover.hit || !mHover.direction.isNull());
    ray.origin = toEngine(mHover.origin);
    ray.dir = toEngine(mHover.direction);
    ray.hit = mHover.hit;
    ray.hitPoint = toEngine(mHover.point);
    // WHICH HAND IT BELONGS TO, so the session can re-anchor the line to THIS
    // frame's aim pose and keep only its far end as old as the pick
    // (VrRayState::hand). The pick is this side's; the line is the engine's.
    ray.hand = int(mHover.hand);
    engine->setVrRay(ray);
}

void VrInteraction::haptic(unsigned hand, float amplitude, float seconds) const
{
    // REFUSED WITH NO SESSION (there is nothing to buzz), and that is not an
    // error here: a gesture driven by a script on a box with no runtime is a
    // gesture nobody's hand is holding.
    if (Engine *engine = engineNow()) engine->vrHaptic(int(hand), amplitude, seconds);
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
    if (h.node && h.node->isRootNode()) {
        // THE WORLD ROOT REPLACES (D6) — it is not a thing you add to a set.
        sel->select(h.node);
        changed = true;
    } else if (mode == SelectMode::Toggle) {
        // toggle()'s answer is MEMBERSHIP AFTER THE CALL, not "did anything
        // change" — a toggle always changes the set, which is why the return is
        // dropped here rather than tested.
        sel->toggle(h.node);
        changed = true;
    } else if (mode == SelectMode::Add) {
        changed = sel->add(h.node);
    } else {
        sel->select(h.node);
        changed = true;
    }
    if (changed) {
        ++mSelects;
        haptic(h.hand, kTickAmplitude, kTickSeconds);
    }
    return changed;
}

bool VrInteraction::select(unsigned hand, SelectMode mode)
{
    if (hand >= VrHandCount) return false;
    // IN THE PLAYER, ONLY LOCOMOTION RUNS, AND NOW THE VERBS SAY SO TOO. The
    // rule was enforced on the button EDGES only (step() skips the whole
    // editing half), so `vr.select`/`vr.grab`/`vr.release` from a console or an
    // MCP session edited the document of a run the Player is showing —
    // precisely what the header's promise forbids. One refusal, in the service,
    // so an edge and a verb cannot disagree about it.
    if (playerHosted()) return false;
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
    if (playerHosted()) return false;       // the Player edits nothing (see select())
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
    } else {
        // NEAR — either the ray is on something within arm's reach, or there is
        // no hit at all and the wearer is reaching out to take hold of what is
        // already selected (§5.1's near-grab case). Both ride the GRIP pose.
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

// ---------------------------------------------------------------------------
// THE GIZMO (VR_INPUT_SPEC §5.2, phase 4b stage 2)
// ---------------------------------------------------------------------------

Gizmo *VrInteraction::gizmoNow() const
{
    return mDeps.gizmo ? mDeps.gizmo() : nullptr;
}

// WHERE THE WEARER LOOKS FROM. With a session running it is the head, through
// the rig, exactly as locomotion reads it. With NO session — the injection
// route every gesture is gated on — it is the AIMING HAND: the wearer's eye and
// their pointer are then one thing, which keeps the size rule and the view
// direction exact rather than invented, and is the only honest answer a process
// with no runtime can give.
bool VrInteraction::eyePose(unsigned hand, iris::Vec3 &eye, iris::Vec3 &forward) const
{
    if (Engine *engine = engineNow()) {
        const VrStatus st = engine->vrStatus();
        if (st.active && st.posesValid) {
            eye = toIris(st.headPosition);
            forward = toIris(st.headRotation).rotatedVector(iris::Vec3(0, 0, -1)).normalized();
            return true;
        }
    }
    const VrHandState st = handState(hand);
    if (!st.valid || !st.aim.valid) return false;
    eye = toIris(st.aim.position);
    forward = toIris(st.aim.rotation).rotatedVector(iris::Vec3(0, 0, -1)).normalized();
    return true;
}

// THE POINTER, HANDED TO THE GIZMO ONCE A FRAME. Arming does three things at
// once, all of them in the gizmo's own terms: it sizes the handles for the
// wearer (a constant angular size, Gizmo::updateSizeForVr), it gives the
// highlight the controller's ray instead of the desk's mouse, and it gives the
// camera-facing rules (the ring arcs, the plane squares) the head's direction.
// Disarming hands all three straight back to the desktop camera.
void VrInteraction::armGizmo(unsigned hand, bool armed)
{
    Gizmo *g = gizmoNow();
    // A TRACKING BLIP DOES NOT END A DRAG, AND MUST NOT DISARM ONE (the lead's
    // fix round, item 3). One unlocated frame is not a released trigger — the
    // direct gesture has said so since stage 1 — and disarming the pick here
    // would hand the gizmo back to the DESK mid-gesture: its per-frame
    // updateSize would then re-size the handles the drag is measuring against
    // (Gizmo::updateSize now refuses while dragging, which is the second half
    // of the same fix). The pick the drag started with stands until the drag
    // ends; a located hand replaces it below as usual.
    if (mGizmoDrag.active && mGizmoDrag.gizmo == g && g) {
        const VrHandState st = handState(hand);
        iris::Vec3 origin, direction, eye, forward;
        if (!(st.valid && ray(st, origin, direction) && eyePose(hand, eye, forward))) return;
    }
    if (mArmedGizmo && mArmedGizmo != g) {
        mArmedGizmo->setVrPick(GizmoVrPick());
        mArmedGizmo->setDragModifiers(Qt::NoModifier);
        mArmedGizmo = nullptr;
    }
    if (!g) return;
    GizmoVrPick pick;
    // NO SELECTION, NO GIZMO — and therefore no pointer on it. A gizmo with no
    // node is not drawn by the viewport either, and its handle hit tests would
    // answer against an identity transform at the world origin (measured: a ray
    // through the origin "hit" the Y arrow of a gizmo nobody could see).
    if (armed && g->hasSelectedNode()) {
        const VrHandState st = handState(hand);
        iris::Vec3 origin, direction, eye, forward;
        if (st.valid && ray(st, origin, direction) && eyePose(hand, eye, forward)) {
            pick.valid = true;
            pick.eye = eye;
            pick.rayPos = origin;
            pick.rayDir = direction;
            pick.viewDir = forward;
        }
    }
    g->setVrPick(pick);
    mArmedGizmo = pick.valid ? g : nullptr;
    if (!pick.valid) g->setDragModifiers(Qt::NoModifier);
}

bool VrInteraction::beginGizmoDrag(unsigned hand)
{
    if (hand >= VrHandCount) return false;
    if (playerHosted()) return false;          // the Player edits nothing
    if (mGesture.active || mGizmoDrag.active) return false;
    Gizmo *g = gizmoNow();
    if (!g) return false;
    // NO SELECTION, NO GIZMO. The gizmo belongs to the primary selection and
    // the viewport only draws it when there is one; a press with nothing
    // selected is a pick, not a drag.
    if (!mDeps.selection || !mDeps.selection->selected()) return false;
    const VrHandState st = handState(hand);
    if (!st.valid || !st.aim.valid) return false;
    iris::Vec3 origin, direction;
    if (!ray(st, origin, direction)) return false;
    iris::Vec3 eye, forward;
    if (!eyePose(hand, eye, forward)) return false;

    // MEASURED IN ANGLES FOR THE LENGTH OF THIS CALL (Gizmo::RayPickScope): the
    // ring and plane handles are picked as angles at the ray's origin here and
    // as pixels for the desk's own presses, and neither can leak into the
    // other.
    // ...AND NOT A DRAG SOMEBODY ELSE IS HOLDING (the lead's fix round, item 1).
    // There is one gizmo object and two hosts can reach it: the mouse in
    // EngineSceneViewport and the wearer here. A drag belongs to whoever
    // started it — the viewport refuses to take one it did not start (its own
    // mMouseDrag flag) and this refuses to restart one the desk is holding.
    // Without the pair, a controller press during a mouse drag re-entered
    // startDragging and the gesture changed hands mid-flight.
    if (g->isDragging()) return false;

    const Gizmo::RayPickScope raySpace(g);
    if (!g->isHit(origin, direction)) return false;
    // THE EDIT GATE, BEFORE THE DRAG STARTS — the mouse's own rule
    // (enginesceneviewport.cpp: "letting it run only to drop its undo step
    // would move the object and then snap it back").
    if (editgate::refuse()) return false;
    g->setDragModifiers(st.menuPressed ? Qt::KeyboardModifiers(Qt::ControlModifier) : Qt::KeyboardModifiers(Qt::NoModifier));
    g->startDragging(origin, direction, forward);
    if (!g->isDragging()) return false;        // the handle let go of it again
    mGizmoDrag.active = true;
    mGizmoDrag.hand = hand;
    mGizmoDrag.gizmo = g;
    ++mGizmoDrags;
    haptic(hand, kTickAmplitude, kTickSeconds);
    return true;
}

bool VrInteraction::endGizmoDrag(unsigned hand)
{
    if (!mGizmoDrag.active) return false;
    if (hand < VrHandCount && hand != mGizmoDrag.hand) return false;
    Gizmo *g = mGizmoDrag.gizmo;
    const unsigned dragHand = mGizmoDrag.hand;
    mGizmoDrag = GizmoDrag();
    if (!g) return false;
    // ONLY WHAT WE ARE STILL HOLDING (item 1). Something else may have ended
    // this drag while the trigger was down — the desk's own release, a mode
    // switch through setActiveGizmo, a project close — and endDragging() calls
    // createUndoAction unconditionally, so ending a gizmo that is no longer
    // dragging would push a SECOND undo entry for one gesture (a
    // TransformSceneNodeCommand from wherever the node now stands to where it
    // now stands: an undo step that does nothing and eats a Ctrl+Z).
    if (!g->isDragging()) return false;
    // ONE UNDO ENTRY, AND IT IS THE MOUSE DRAG'S OWN (Gizmo::createUndoAction):
    // one TransformSceneNodeCommand for a single node, a macro over the group
    // for several, the edit gate consulted inside it. Nothing is duplicated
    // here — a second undo shape for VR is exactly how the two hosts would come
    // to disagree about what one gesture costs.
    g->endDragging();
    g->setDragModifiers(Qt::NoModifier);
    ++mGizmoCommits;
    haptic(dragHand, kTickAmplitude, kTickSeconds);
    return true;
}

bool VrInteraction::cancelGizmoDrag()
{
    if (!mGizmoDrag.active) return false;
    Gizmo *g = mGizmoDrag.gizmo;
    mGizmoDrag = GizmoDrag();
    if (!g) return false;
    if (!g->isDragging()) return false;       // somebody else already ended it (item 1)
    g->cancelDragging();
    g->setDragModifiers(Qt::NoModifier);
    ++mCancels;
    return true;
}

bool VrInteraction::cycleGizmoMode()
{
    if (playerHosted()) return false;
    if (mGesture.active || mGizmoDrag.active) return false;   // never mid-gesture
    if (!mDeps.setGizmoMode) return false;
    const QString mode = mDeps.gizmoMode ? mDeps.gizmoMode() : QString();
    // THE CYCLE'S ORDER IS THE SPACE KEY'S, from the one place that spells it
    // (viewport/gizmomode.h).
    mDeps.setGizmoMode(gizmomode::next(mode));
    ++mGizmoModes;
    haptic(dominantHand(), kTickAmplitude, kTickSeconds);
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
        // STICK RIGHT SPINS THE HELD OBJECT CLOCKWISE FROM ABOVE — the same
        // convention, and the same minus sign at the same kind of call site, as
        // the wearer's own turn (see step()): `turntableDegrees` returns the
        // stick's own sign and the tree's yaw about +Y is counter-clockwise
        // from above. A turntable that span the other way from the turn would
        // be the one gesture in the editor where right meant left.
        mGesture.turntable -= vrgrab::turntableDegrees(st.stickX, seconds);
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
    if (playerHosted()) return false;       // the Player edits nothing (see select())
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
    // A HANDLE DRAG CANCELS TOO (VR_INPUT_SPEC §5.4): focus loss, the session
    // ending, the Player taking the headset over. It is the same promise the
    // direct gesture makes — the object goes back and nothing is recorded.
    const bool hadHandle = cancelGizmoDrag();
    if (!mGesture.active) return hadHandle;
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
    // THE HOST IS STILL PUTTING THE WEARER SOMEWHERE (see Deps::
    // locomotionBlocked). Turning the rig now would be composed with a head
    // the engine has not yet paired with it, and the placement that lands next
    // frame would overwrite the turn anyway: the flick is not lost, it is
    // answered on the first frame after the placement (the stick is still over
    // and the snap turn stays ARMED, because only a turn that happened disarms
    // it).
    if (locomotionBlocked()) return false;
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

bool VrInteraction::fly(float stickX, float stickY, float seconds, bool boost,
                        const VrPose *aim, bool gazeHeld)
{
    // ...AND THE SAME GUARD ON THE WALK: both hosts refuse their OWN fly while
    // a placement is pending (EditorVrPreview::move, PlayerVr::move), and the
    // stick used to write the rig from here and walk straight past that.
    if (locomotionBlocked()) return false;
    vrorigin::Rig rig;
    iris::Vec3 head;
    iris::Quat headRot;
    if (!rigNow(rig, head, headRot)) return false;
    const float speed = mDeps.wearerSpeed ? mDeps.wearerSpeed() : 0.0f;
    if (speed <= 0.0f) return false;
    // ALONG THE HAND when the option says so and the hand is located (the
    // owner: "fly like Unreal"); level along the head's heading otherwise.
    // WHERE YOU LOOK while the left squeeze is held, or as the gaze option (the
    // owner's second ask of the night); along the hand otherwise (Aim); level
    // along the head's heading as the comfort option.
    const bool alongGaze = gazeHeld || mOptions.fly == Fly::Gaze;
    const bool alongAim = !alongGaze && mOptions.fly == Fly::Aim && aim && aim->valid;
    const iris::Vec3 delta =
        alongGaze ? vrgrab::gazeFlyDelta(headRot, stickY, speed,
                                         vrorigin::frameSeconds(seconds), boost)
        : alongAim ? vrgrab::aimFlyDelta(toIris(aim->rotation), stickY, speed,
                                         vrorigin::frameSeconds(seconds), boost)
                   : vrgrab::stickFlyDelta(headRot, stickX, stickY, speed,
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
    // THE MEMO LIVES ONE FRAME (the lead, from the Fable read at merge): its
    // key covers the geometric inputs (the aim pose, the transform/structure
    // epoch) and nothing the picker decides per candidate — a node locked,
    // hidden or re-meshed under a STILL hand, or a camera moved (a camera's
    // transform write does not bump the epoch), would answer from the old pick
    // until the next pose change. In a headset that is the next frame anyway;
    // for a script or an MCP session it is a wrong answer. Expiring here keeps
    // the measured win (select/grab reuse the FRAME's hover; no second pick)
    // and gives up only the at-rest 0.2 ms.
    mMemo.valid = false;
    const float dt = vrorigin::frameSeconds(seconds > 0.0f ? seconds : kNominalFrame);
    VrHandState hands[VrHandCount];
    for (unsigned i = 0; i < VrHandCount; ++i) hands[i] = handState(i);

    // THE SOURCE'S OWN ANSWER, whoever wrote the samples (VrEngineInput::
    // focused() reads the sample's `focused` bit when a hand is reporting and
    // the session's lifecycle word when none is).
    const bool focused = mSource && mSource->focused();
    if (!focused) {
        // THE RUNTIME TOOK THE INPUT AWAY (the dashboard came up, the session
        // left Focused). A gesture in flight CANCELS — see the header — and
        // that now includes a handle drag.
        cancel();
        armGizmo(dominantHand(), false);
        mHover = Hover();
        pushRay();
        for (unsigned i = 0; i < VrHandCount; ++i) mPrev[i] = hands[i];
        return;
    }

    // IN THE PLAYER, ONLY LOCOMOTION RUNS (the Player edits nothing).
    const bool player = playerHosted();
    const unsigned dominant = dominantHand();

    // THE PLAYER TOOK THE SESSION OVER MID-GESTURE (the editor preview ended,
    // the Player's VR mode began on the same headset). The gesture cannot be
    // followed any more — nothing in the Player steps the editing half — so it
    // is CANCELLED rather than left holding an object nobody will put down,
    // which is the same rule as a lost focus.
    if (player) cancel();
    // THE PLAYER DRAWS NO GIZMO AND EDITS NOTHING: the pointer is taken off it
    // for the duration, so the desk's own sizing is back the moment the Player
    // takes the session over.
    if (player) armGizmo(dominant, false);

    if (!player) {
        const VrHandState &d = hands[dominant];
        const VrHandState &p = mPrev[dominant];
        mHover = d.valid ? pick(dominant, d) : Hover();
        pushRay();
        // THE GIZMO GETS THIS FRAME'S POINTER BEFORE ANY EDGE IS RUN (stage 2):
        // the size, the highlight ray and the view direction a press will be
        // measured against are all this frame's, not the last one's.
        armGizmo(dominant, d.valid);

        // ---- `menu`: A SHORT PRESS CYCLES, A HELD ONE MODIFIES ------------
        //
        // The two meanings of one button (the owner's answer 10 plus the mode
        // cycle), told apart by HOW LONG it was down and by whether anything
        // used it as a modifier meanwhile. The clock is the frame's own
        // duration — counted, never read off a wall clock.
        if (d.menuPressed && !p.menuPressed) {
            mMenuHeldSeconds = 0.0f;
            mMenuConsumed = false;
        } else if (d.menuPressed) {
            mMenuHeldSeconds += dt;
        }

        // THE PRESS EDGES. Counted transitions, never a held value: a verb and
        // a button must produce exactly one gesture each.
        //
        // THE GIZMO GOES FIRST, exactly as it does on the desk
        // (enginesceneviewport.cpp's press: the gizmo hit test, then the pick):
        // a trigger press with the ray on a handle DRAGS that handle and leaves
        // the selection alone; anywhere else it selects.
        if (d.selectPressed && !p.selectPressed && !mGesture.active) {
            if (!beginGizmoDrag(dominant))
                select(dominant, d.menuPressed ? SelectMode::Toggle : SelectMode::Replace);
            if (d.menuPressed) mMenuConsumed = true;   // it was the modifier
        }
        if (!d.selectPressed && p.selectPressed) endGizmoDrag(dominant);
        // ONE BUTTON EACH (§5.2): `grab` is stage 1's direct gesture and is
        // refused while a handle is being dragged (beginGrab's own guard is the
        // gesture flag; the handle drag is this one).
        if (d.grabPressed && !p.grabPressed && !mGizmoDrag.active) beginGrab(dominant);
        else if (!d.grabPressed && p.grabPressed) endGrab(dominant);

        if (mGizmoDrag.active) {
            // MENU HELD SNAPS A HANDLE DRAG TOO — the same modifier, asked per
            // frame, pushed through the same Gizmo::setDragModifiers door the
            // viewport's mouse events use (Ctrl at the desk).
            if (Gizmo *g = mGizmoDrag.gizmo) {
                const VrHandState st = handState(mGizmoDrag.hand);
                g->setDragModifiers(st.menuPressed ? Qt::KeyboardModifiers(Qt::ControlModifier) : Qt::KeyboardModifiers(Qt::NoModifier));
                if (st.menuPressed) mMenuConsumed = true;
                iris::Vec3 origin, direction, eye, forward;
                if (ray(st, origin, direction) && eyePose(mGizmoDrag.hand, eye, forward)) {
                    const Gizmo::RayPickScope raySpace(g);
                    g->drag(origin, direction, forward);
                }
            }
        }

        if (mGesture.active) {
            // MENU HELD SNAPS, per frame — the Ctrl of a desktop drag, which is
            // also asked per frame and not at the press.
            mGesture.snapping = handState(mGesture.hand).menuPressed;
            if (mGesture.snapping) mMenuConsumed = true;
            followGesture(dt);
        }

        // ...and the cycle, on the RELEASE of a press that was short and was
        // never used as a modifier. A short press therefore cannot also fire
        // the held action, and a held one cannot cycle.
        if (!d.menuPressed && p.menuPressed) {
            if (!mMenuConsumed && mMenuHeldSeconds < kMenuHoldSeconds) cycleGizmoMode();
            mMenuHeldSeconds = 0.0f;
            mMenuConsumed = false;
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
        // The left SQUEEZE held = fly where you look (the off hand grabs nothing).
        fly(0.0f, o.stickY, dt, false, &o.aim, o.grabPressed);
        // STICK RIGHT TURNS THE WEARER RIGHT (the lead, from the Fable read at
        // merge): vrgrab's turn functions are "positive = the stick's own sign",
        // and the tree's yaw is the right-handed rotation about +Y (vrorigin.h),
        // under which +30 degrees carries a wearer facing -Z toward -X, i.e. to
        // their LEFT. The sign lives HERE, at the one call site, so the pure
        // functions stay what they say and every VR title's convention holds:
        // a flick right turns the view clockwise from above.
        if (mOptions.turn == Turn::Smooth) {
            turn(-vrgrab::smoothTurnDegrees(o.stickX, dt, mOptions.smoothTurnDegreesPerSecond));
        } else {
            const float deg = -vrgrab::snapTurnDegrees(o.stickX, mTurnArmed, mOptions.snapTurnDegrees);
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
    out[QStringLiteral("gizmo")] = gizmoReport();
    // COUNTS, never a wall clock (VR_SPEC §6 flake class b).
    out[QStringLiteral("selects")] = QVariant::fromValue(qulonglong(mSelects));
    out[QStringLiteral("grabs")] = QVariant::fromValue(qulonglong(mGrabs));
    out[QStringLiteral("commits")] = QVariant::fromValue(qulonglong(mCommits));
    out[QStringLiteral("cancels")] = QVariant::fromValue(qulonglong(mCancels));
    out[QStringLiteral("turns")] = QVariant::fromValue(qulonglong(mTurns));
    return out;
}

// WHAT THE WEARER'S GIZMO IS DOING (`vr.gizmo()`).
//
// `handle` is asked of the gizmo RIGHT NOW, through the same ray-space hit test
// a press takes, so a suite (and a person at an MCP session) can see what a
// trigger would grab before pulling it. `scale` and `toleranceDegrees` are the
// two numbers stage 2 is judged by: the constant-angular size rule's output and
// the pick target it comes with.
QVariantMap VrInteraction::gizmoReport() const
{
    QVariantMap out;
    Gizmo *g = gizmoNow();
    out[QStringLiteral("present")] = g != nullptr;
    out[QStringLiteral("mode")] = mDeps.gizmoMode ? mDeps.gizmoMode() : QString();
    out[QStringLiteral("armed")] = g && g->vrPickArmed();
    out[QStringLiteral("dragging")] = mGizmoDrag.active;
    out[QStringLiteral("scale")] = double(g ? g->getGizmoScale() : 0.0f);
    out[QStringLiteral("toleranceDegrees")] =
        double(g ? gizmoray::degreesOf(g->rayTolerance()) : 0.0f);
    out[QStringLiteral("halfAngleDegrees")] = double(gizmoray::kVrGizmoHalfAngleDeg);
    // WHAT A PRESS WOULD TAKE, AND ONLY WHEN A PRESS WOULD TAKE IT (the lead's
    // fix round, item 9). The name is the answer to "what does the trigger grab
    // here", so it must carry the same refusals `beginGizmoDrag` applies: the
    // PLAYER edits nothing, a gizmo the desk is already holding is not the
    // wearer's to grab, and while a script owns the document a press is refused
    // outright. `blocked()` is the gate's side-effect-free reading (false
    // inside a verb, so a script asking this about its own run gets the honest
    // answer rather than its own refusal). The SELECTION guard is already
    // structural: nothing arms a gizmo with no selected node.
    QString handle;
    const bool pressWouldDrag = g && g->vrPickArmed() && !playerHosted() &&
                                !g->isDragging() && !editgate::blocked();
    if (g && g->vrPickArmed()) {
        const GizmoVrPick &pick = g->vrPick();
        if (pressWouldDrag) {
            const Gizmo::RayPickScope raySpace(g);
            handle = g->handleNameAt(pick.rayPos, pick.rayDir, pick.viewDir);
        }
        QVariantMap eye;
        eye[QStringLiteral("x")] = double(pick.eye.x());
        eye[QStringLiteral("y")] = double(pick.eye.y());
        eye[QStringLiteral("z")] = double(pick.eye.z());
        out[QStringLiteral("eye")] = eye;
    }
    out[QStringLiteral("handle")] = handle;
    out[QStringLiteral("drags")] = QVariant::fromValue(qulonglong(mGizmoDrags));
    out[QStringLiteral("commits")] = QVariant::fromValue(qulonglong(mGizmoCommits));
    out[QStringLiteral("modes")] = QVariant::fromValue(qulonglong(mGizmoModes));
    return out;
}
