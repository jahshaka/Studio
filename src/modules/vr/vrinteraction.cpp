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
#include "irisgl/core/geometry/trimesh.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/scenegraph/meshnode.h"
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

/// THE WORLD'S OWN FLOOR, metres — the fallback plane's fallback, used only
/// when there is no session and therefore no wearer whose floor to continue
/// (see traceArc's `planeY`). y = 0 is where this editor's grid is drawn, where
/// the default ground sits and where `scene.addPrimitive` puts a thing with no
/// transform, so it is the one plane a document with no rig can mean.
inline constexpr float kWorldFloorY = 0.0f;

/// CAN A GESTURE CHANGE THIS NODE'S SIZE? Lights and cameras are refused (§5.1:
/// "scale is refused for lights and cameras — the two-hand gesture rotates and
/// translates only"), which is not a policy but a meaning: a light has no size,
/// a camera has no size, and the scale field on either of them is a number that
/// changes the icon and nothing in the picture.
inline bool scalable(const iris::SceneNodePtr &node)
{
    if (!node) return false;
    const iris::SceneNodeType type = node->getSceneNodeType();
    return type != iris::SceneNodeType::Light && type != iris::SceneNodeType::Camera;
}

/// THE WORLD-SPACE NORMAL OF THE TRIANGLE A PICK LANDED ON, oriented back
/// along the ray that struck it — the one thing ScenePick cannot carry and the
/// only thing a teleport needs beyond the point: a floor is a face you can
/// stand on and a wall is a face you cannot.
///
/// The corners are transformed and THEN crossed (rather than the local normal
/// being transformed) because that is exact under any affine transform, a
/// non-uniform scale included — a normal pushed through a squashed matrix
/// points somewhere else, and "somewhere else" here is the difference between
/// a ramp and a wall. The vertex-snap path (EngineSceneViewport::
/// snapDragToVertexUnderCursor) reads a triangle back the same way.
bool triangleNormal(const ScenePick &pick, const iris::Vec3 &direction, iris::Vec3 &out)
{
    if (!pick.node || pick.triangleIndex < 0) return false;
    if (pick.node->getSceneNodeType() != iris::SceneNodeType::Mesh) return false;
    const auto meshNode = pick.node.staticCast<iris::MeshNode>();
    const auto mesh = meshNode->getMesh();
    if (!mesh || !mesh->getTriMesh() ||
        pick.triangleIndex >= mesh->getTriMesh()->triangles.size())
        return false;
    const iris::Triangle &tri = mesh->getTriMesh()->triangles[pick.triangleIndex];
    const iris::Mat4 &xf = meshNode->getGlobalTransform();
    const iris::Vec3 a = xf * tri.a, b = xf * tri.b, c = xf * tri.c;
    iris::Vec3 n = iris::Vec3::crossProduct(b - a, c - a);
    if (n.isNull()) return false;
    n = n.normalized();
    // FACING THE THROW. A mesh's winding says which side is "out" and a
    // document does not promise one, so the normal is turned to face the arc
    // that hit it — the face a person would land on is the one they came at.
    if (iris::Vec3::dotProduct(n, direction) > 0.0f) n = n * -1.0f;
    out = n;
    return true;
}

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
    releaseArc();
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

/// THE PROJECT'S SETTINGS WITH THE SESSION'S OVERRIDES (VR-WORLD-1). Asked
/// rather than stored: the defaults live in the document and nowhere else.
vrworld::Settings VrInteraction::locomotion() const
{
    // THE FRAME'S COPY WHILE A FRAME IS OPEN (see FrameSettings): inside one
    // step() the project cannot change, and this is asked six or more times a
    // frame through a callable that reads the document.
    if (mFrameLocoValid) return mFrameLoco;
    return mDeps.locomotion ? mDeps.locomotion() : vrworld::Settings();
}

unsigned VrInteraction::dominantHand() const
{
    return locomotion().dominantRight ? unsigned(VrHandRight) : unsigned(VrHandLeft);
}

unsigned VrInteraction::offHand() const
{
    return locomotion().dominantRight ? unsigned(VrHandLeft) : unsigned(VrHandRight);
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
    // A SECOND HAND ON A LIVE GESTURE IS NOT A SECOND GESTURE — it is the SAME
    // one, upgraded (VR_INPUT_SPEC §5.1; the owner's answer 7, after the stage-1
    // smoke). The routing is here rather than in the caller so that a squeeze,
    // `vr.grab({hand})` and the MCP all do the identical thing.
    if (mGesture.active) return upgradeToTwoHand(hand);
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
        m.phaseScale = node->getLocalScale();
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
// TWO HANDS ON ONE OBJECT (VR_INPUT_SPEC §5.1's two-hand paragraph; the owner's
// answer 7 — "the full version with roll", polish after the stage-1 smoke)
// ---------------------------------------------------------------------------
//
// THE WHOLE TRANSITION RULE, in one sentence: a hand joining or leaving
// RE-CAPTURES the frame the follow is measured from and NOTHING ELSE, so the
// object is exactly where it was on the frame the count changed, and the undo
// step is still written from where the very first squeeze found it.

void VrInteraction::recaptureMembers()
{
    for (Member &m : mGesture.members) {
        if (!m.node) continue;
        m.startGlobal = vrgrab::Pose{ m.node->getGlobalPosition(), m.node->getGlobalRotation() };
        m.phaseScale = m.node->getLocalScale();
    }
}

bool VrInteraction::upgradeToTwoHand(unsigned hand)
{
    if (!mGesture.active || mGesture.two) return false;
    if (hand >= VrHandCount || hand == mGesture.hand) return false;
    // BOTH GRIPS, ALWAYS, AND ALWAYS IN THE SAME ORDER. The axis is
    // right-minus-left whichever hand squeezed second, so the roll's sign and
    // the minimal rotation's direction are decided by the wearer's anatomy and
    // not by the order they pressed two buttons.
    const VrHandState left = handState(VrHandLeft);
    const VrHandState right = handState(VrHandRight);
    if (!left.valid || !right.valid || !left.grip.valid || !right.grip.valid) return false;
    mGesture.pair = vrgrab::twoHandStart(
        vrgrab::Pose{ toIris(left.grip.position), toIris(left.grip.rotation) },
        vrgrab::Pose{ toIris(right.grip.position), toIris(right.grip.rotation) });
    // THE PAIR MUST BE A PAIR. Two hands touching have no axis and no span, and
    // dividing by that span is how a held object flies to infinity on the frame
    // somebody claps.
    if (mGesture.pair.span < 0.02f) return false;
    recaptureMembers();
    // WHERE THE PAIR TURNS AND SCALES ABOUT (the lead's read, item 7). For a
    // near hold it is the point between the palms — the hands are ON the
    // object. For a FAR one it is the VIRTUAL HAND out on the ray, which is the
    // exact point the one-hand far gesture already rotates the object about
    // (rigidFollow's hand): so a second hand joining a far grab changes what
    // the gesture can do — scale, and roll about the palms' axis — and not
    // where it happens. The first cut used the primary member's own ORIGIN,
    // which is a different point whenever the ray did not hit a node's origin
    // (i.e. nearly always), so the object stepped sideways as the pair turned.
    mGesture.pivot = mGesture.far ? mGesture.handNow.position : mGesture.pair.midpoint;
    mGesture.two = true;
    mGesture.hand2 = hand;
    mGesture.scale = 1.0f;
    mGesture.rollDegrees = 0.0f;
    // THE TURNTABLE STOPS with the second hand: it exists because ONE hand at
    // ten metres cannot turn a thing about the world's up, and two hands can.
    mGesture.turntable = 0.0f;
    ++mTwoHands;
    haptic(hand, kTickAmplitude, kTickSeconds);
    return true;
}

void VrInteraction::downgradeToOneHand(unsigned remaining)
{
    if (!mGesture.active) return;
    mGesture.two = false;
    mGesture.hand = remaining;
    mGesture.hand2 = remaining;
    mGesture.scale = 1.0f;
    mGesture.rollDegrees = 0.0f;
    const VrHandState st = handState(remaining);
    // A FRESH CAPTURE ON BOTH SIDES OF THE HAND-OFF (§5.1: "releasing either
    // hand drops back to one-hand with a fresh capture — no jump"): the hand's
    // pose NOW and the objects' poses NOW, so the next frame's rigid follow
    // starts from a zero delta.
    const vrgrab::Pose grip{ toIris(st.grip.position), toIris(st.grip.rotation) };
    const vrgrab::Pose aim{ toIris(st.aim.position), toIris(st.aim.rotation) };
    // THE HAND THAT KEEPS HOLDING MUST BE LOCATED TO BE CAPTURED FROM — IN THE
    // POSE THIS ARRANGEMENT ACTUALLY USES (the lead's read, item 6). The first
    // cut accepted EITHER pose, which is two bugs in one line: a NEAR hand-off
    // with a located aim and an unlocated grip captured `handStart` from a grip
    // nobody located (the jump this guard exists to prevent, still reachable),
    // and a FAR one with a located grip and an unlocated aim welded a
    // ten-metre object to the wrist. A near hold rides the GRIP and a far hold
    // rides the AIM, so each asks for its own.
    const bool located = mGesture.far ? st.aim.valid : st.grip.valid;
    if (!st.valid || !located) {
        mGesture.recapture = true;
        recaptureMembers();
        return;
    }
    mGesture.recapture = false;
    if (mGesture.far && st.aim.valid) {
        // THE FAR ARRANGEMENT KEEPS ITS DISTANCE: the object stays where the
        // pair left it, at whatever range that now is along the remaining
        // hand's ray, so the virtual hand is re-derived rather than remembered.
        iris::Vec3 pivot = mGesture.members.isEmpty()
                               ? aim.position
                               : (mGesture.members.first().node
                                      ? mGesture.members.first().node->getGlobalPosition()
                                      : aim.position);
        float d = (pivot - aim.position).length();
        if (d < vrgrab::kMinGrabDistance) d = vrgrab::kMinGrabDistance;
        if (d > vrgrab::kMaxGrabDistance) d = vrgrab::kMaxGrabDistance;
        mGesture.distance = d;
        mGesture.handStart = vrgrab::virtualFarHand(aim, d);
    } else {
        mGesture.far = false;
        mGesture.handStart = grip;
    }
    mGesture.handNow = mGesture.handStart;
    recaptureMembers();
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

    // ---- BOTH HANDS ON IT (§5.1's two-hand paragraph) --------------------
    //
    // THE THREE NUMBERS AND NOTHING ELSE (vrgrab::twoHandDelta): the span's
    // ratio is the scale, the axis' turn plus the wrists' average roll is the
    // rotation, the midpoint's move is the translation. The stick does nothing
    // here — push/pull and the turntable are what ONE hand needs to do what two
    // hands do directly.
    if (mGesture.two) {
        const VrHandState left = handState(VrHandLeft);
        const VrHandState right = handState(VrHandRight);
        // A HAND THAT STOPPED REPORTING HOLDS THE OBJECT WHERE IT IS — the
        // one-hand rule (a skipped locate is not a released trigger), and with
        // two hands it matters more: half a pair would read as an enormous
        // scale and roll on the frame one controller blinked.
        if (!left.valid || !right.valid || !left.grip.valid || !right.grip.valid) return;
        const vrgrab::Pose lp{ toIris(left.grip.position), toIris(left.grip.rotation) };
        const vrgrab::Pose rp{ toIris(right.grip.position), toIris(right.grip.rotation) };
        vrgrab::TwoHandDelta delta = vrgrab::twoHandDelta(mGesture.pair, lp, rp);
        // SNAP QUANTISES THE FACTOR (SnapSettings::scaleSize(), the same dial
        // the desktop scale gizmo's Ctrl uses) — the factor, never the size.
        if (mGesture.snapping)
            delta.scale = vrgrab::snappedScale(delta.scale, SnapSettings::scaleSize());
        mGesture.scale = delta.scale;
        mGesture.rollDegrees = delta.rollDegrees;
        const float translateStepTwo = mGesture.snapping ? SnapSettings::translateSize() : 0.0f;
        const float rotateStepTwo = mGesture.snapping ? SnapSettings::rotateSize() : 0.0f;
        // THE PIVOT, CAPTURED AT THE UPGRADE (Gesture::pivot): the point
        // between the palms for a near hold, and the virtual hand on the ray
        // for a far one (vrgrab.h's note on the far arrangement — at ten metres
        // a scale about the wearer's own midpoint throws the thing another ten
        // metres away).
        const iris::Vec3 pivot = mGesture.pivot;
        for (int i = 0; i < mGesture.members.size(); ++i) {
            const Member &m = mGesture.members.at(i);
            if (!m.node) continue;
            // A LIGHT AND A CAMERA HAVE NO SIZE (§5.1): the pair turns and
            // carries them, and the factor is 1 for them alone — including in
            // the offset from the pivot, or a group with a lamp in it would see
            // the lamp fly out while the furniture grew in place.
            vrgrab::TwoHandDelta mine = delta;
            if (!scalable(m.node)) mine.scale = 1.0f;
            vrgrab::Pose followed = vrgrab::twoHandFollow(mGesture.pair, mine, m.startGlobal, pivot);
            if (mGesture.snapping)
                followed = vrgrab::snappedFrom(m.startGlobal, followed, translateStepTwo,
                                               rotateStepTwo);
            m.node->setGlobalPosRot(followed.position, followed.rotation);
            // WRITTEN EVERY FRAME, factor 1 INCLUDED — and that is a fix, not
            // a redundancy: skipping the write when the factor happened to read
            // exactly 1 left the object at whatever size the PREVIOUS frame
            // gave it, so a wearer who spread their hands and brought them back
            // together got the size of the widest moment (caught by
            // `scripting.e2e.vr_input_headless`, "back to 0.5 m brings the size
            // back to 1"). A member that cannot be scaled is skipped instead,
            // which is a different question and the one above it.
            if (scalable(m.node)) m.node->setLocalScale(m.phaseScale * mine.scale);
        }
        return;
    }

    const VrHandState st = handState(mGesture.hand);
    // A HAND THAT STOPPED REPORTING HOLDS THE OBJECT WHERE IT IS. One skipped
    // locate is not a released trigger (VrPose's own note), and moving the
    // object to a pose nobody located would be an invented gesture.
    if (!st.valid) return;
    // ...AND A HAND-OFF THAT COULD NOT BE TAKEN IS TAKEN NOW, on the first
    // frame this hand reports (Gesture::recapture): the follow's frame is this
    // pose and the objects' frame is wherever they stand, so the gesture
    // resumes as a hold rather than as a jump.
    if (mGesture.recapture) {
        downgradeToOneHand(mGesture.hand);
        if (mGesture.recapture) return;
    }

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
    // ONE HAND LETS GO OF A TWO-HAND HOLD — THE GESTURE CONTINUES (§5.1).
    // Nothing is committed and nothing moves: the remaining hand re-captures
    // and carries on, which is what makes "put it down with one hand" a
    // correction rather than an accident. The undo step is still the one the
    // FIRST squeeze armed, because the members' origin was never re-captured.
    if (mGesture.two && hand < VrHandCount &&
        (hand == mGesture.hand || hand == mGesture.hand2)) {
        downgradeToOneHand(hand == mGesture.hand ? mGesture.hand2 : mGesture.hand);
        return true;
    }
    // THE OTHER HAND'S RELEASE IS NOT THIS GESTURE'S. With one hand on the
    // object the off hand's squeeze does nothing to it, and must not end what
    // the holding hand is carrying.
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

bool VrInteraction::cancelEditing()
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

bool VrInteraction::cancel()
{
    // THE EDITING HALF, AND THE ARC — but they are two halves and only one
    // caller wants both (the lead's read, item 1). AN ARMED THROW IS
    // LOCOMOTION: the Player runs locomotion and edits nothing, so its own
    // per-frame "cancel whatever the editor was doing" must not touch the arc.
    // It did, and because step() runs that cancel EVERY frame the Player's arc
    // was cancelled and re-armed sixty times a second and could never be taken
    // at all — the counters spun and the wearer never moved.
    //
    // Focus loss, `end()` and a project switch still want both: the wearer
    // cannot see either the object or the curve any more.
    const bool hadEdit = cancelEditing();
    const bool hadArc = teleportCancel();
    return hadEdit || hadArc;
}

bool VrInteraction::gestureHands(unsigned *primary, unsigned *second) const
{
    if (!mGesture.active) return false;
    if (primary) *primary = mGesture.hand;
    if (second) *second = mGesture.two ? mGesture.hand2 : mGesture.hand;
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
    const vrworld::Settings loco = locomotion();
    const float speed = loco.flySpeed;
    if (speed <= 0.0f) return false;
    // ALONG THE HAND when the option says so and the hand is located (the
    // owner: "fly like Unreal"); level along the head's heading otherwise.
    // WHERE YOU LOOK while the left squeeze is held, or as the gaze option (the
    // owner's second ask of the night); along the hand otherwise (Aim); level
    // along the head's heading as the comfort option.
    const bool alongGaze = gazeHeld || loco.fly == iris::VrFlyMode::Gaze;
    const bool alongAim = !alongGaze && loco.fly == iris::VrFlyMode::Aim && aim && aim->valid;
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

// ---------------------------------------------------------------------------
// TELEPORT (VR_INPUT_SPEC §6 row L4; the owner's answer 8 — after stage 1)
// ---------------------------------------------------------------------------
//
// THE BINDING, AND WHY IT NEEDS NO NEW ACTION. The spec's table already has a
// `thumbstick` per hand, and the DOMINANT one is unbound whenever the wearer is
// not holding something (it steers a far grab's distance and turntable, and
// nothing else, §5.1). So the throw is the dominant stick pushed FORWARD —
// Unreal's and SteamVR's own convention, the one a person who has worn a
// headset before will try first — and no `xrSuggestInteractionProfileBindings`
// block changes: an action added after `xrAttachSessionActionSets` is illegal
// anyway (§2.1), and a new action would have to be bound on every profile for a
// gesture the existing one already carries. The cost is stated: on a profile
// with NO stick (khr/simple_controller) there is no teleport from the
// controller, exactly as there is no grab and no push/pull there today, and the
// injection route drives it in the gate.
//
// THE ARC IS TRACED ONLY WHILE IT IS ARMED, and that is the whole cost control:
// it is up to kTeleportSegments picker segments a frame, against the one the
// hover ray costs, for the second or so a person holds the stick forward.

VrInteraction::Teleport VrInteraction::traceArc(unsigned hand, const VrHandState &state) const
{
    Teleport t;
    t.hand = hand;
    if (!state.aim.valid) {
        t.reason = QStringLiteral("the hand is not located");
        return t;
    }
    const vrgrab::Pose aim{ toIris(state.aim.position), toIris(state.aim.rotation) };
    const iris::ScenePtr doc = scene();
    const float step = vrgrab::kTeleportMaxSeconds / float(vrgrab::kTeleportSegments);

    // THE FLOOR THE WEARER IS STANDING ON, as the fallback plane (the lead's
    // read, item 4). The first cut used the WORLD's y = 0, which is a guess
    // about the content: a scene built on a raised floor, or on terrain, would
    // land the wearer in mid-air and report it GREEN. The rig's own y is the
    // plane the wearer's feet are on right now, so the rule reads "the floor I
    // am standing on continues" — which is the only thing a throw that met no
    // geometry can honestly mean.
    //
    // With NO SESSION there is no wearer and no floor of theirs, and the
    // world's y = 0 is the only defensible answer — it is also what every
    // headless gate measures against.
    float planeY = kWorldFloorY;
    {
        vrorigin::Rig rig;
        iris::Vec3 head;
        iris::Quat headRot;
        if (rigNow(rig, head, headRot)) planeY = rig.position.y();
    }

    // THE TRANSFORM WALK, ONCE AND ONLY IF SOMETHING WAS WRITTEN — pick()'s own
    // rule and its own counter, because this traces up to twenty segments and
    // running the document's recursive update for each of them would be twenty
    // walks of the whole scene per frame.
    const unsigned long long writes = iris::graph::transformWrites();
    bool refresh = doc && (!mRefreshed || mRefreshedAtWrites != writes);

    iris::Vec3 prev = aim.position;
    t.points.append(prev);
    for (int i = 1; i <= vrgrab::kTeleportSegments; ++i) {
        const iris::Vec3 next = vrgrab::arcPoint(aim, float(i) * step);
        iris::Vec3 landing, normal;
        bool landed = false;
        if (doc) {
            // NO LIGHTS, NO DECALS, NO CAMERAS: their picks are 0.5 m spheres
            // around an origin (ScenePicker's note) and a person cannot stand
            // on an icon. Meshes only — and `pickable` is still honoured inside
            // the picker, so a LOCKED object is not a floor either.
            // FORCE-PICKABLE, AND THAT IS THE POINT (the lead's read, item 3).
            // The `pickable` flag is an EDIT guard — it stops a click from
            // SELECTING a thing — and people lock the floor for exactly that
            // reason (the default Ground ships locked). A teleport is not an
            // edit: it is locomotion, and a floor you cannot select is still a
            // floor you stand on. Passing the lock through here read the flag
            // on the wrong axis and refused the commonest throw in the editor.
            const QList<ScenePick> hits =
                ScenePicker::pickAll(doc, prev, next, prev, true, false, false, refresh, false);
            if (refresh) {
                mRefreshed = true;
                mRefreshedAtWrites = writes;
                refresh = false;
            }
            const ScenePick best = ScenePicker::nearest(hits);
            if (best.node) {
                landed = true;
                landing = best.hitPoint;
                iris::Vec3 dir = next - prev;
                if (!dir.isNull()) dir = dir.normalized();
                if (!triangleNormal(best, dir, normal)) normal = iris::Vec3();
            }
        }
        if (!landed && prev.y() > planeY && next.y() <= planeY) {
            // THE FALLBACK PLANE, SOLVED RATHER THAN SAMPLED: the crossing is a
            // quadratic with an exact root, so the landing does not depend on
            // how finely the curve happened to be chopped. (A DOCUMENT hit, by
            // contrast, is where the straight PIECE met the surface, which is
            // up to a couple of centimetres short of where the curve does — and
            // that is right: the hit is ON the surface, which is the thing that
            // matters, and moving it along the curve would take it off.)
            const float when = vrgrab::arcPlaneTime(aim, planeY);
            if (when > 0.0f) {
                landed = true;
                landing = vrgrab::arcPoint(aim, when);
                normal = iris::Vec3(0, 1, 0);
            }
        }
        if (landed) {
            t.points.append(landing);
            t.landed = true;
            t.landing = landing;
            t.normal = normal;
            break;
        }
        t.points.append(next);
        prev = next;
    }
    if (!t.landed) {
        t.reason = QStringLiteral("the arc reaches nothing to stand on");
        return t;
    }
    t.valid = vrgrab::landingAllowed(t.normal);
    if (!t.valid)
        t.reason = QStringLiteral("that face is steeper than %1 degrees from level")
                       .arg(double(vrgrab::kTeleportMaxSlopeDegrees));
    return t;
}

VrInteraction::~VrInteraction() { releaseArc(); }

void VrInteraction::releaseArc()
{
    // NOTHING BUILT, NOTHING ASKED. This is the state every ordinary path is
    // in by the time anything destroys this object — `end()` runs at the
    // session's edge and at a script's withdrawal — and it matters that the
    // host's callable is NOT consulted here: at shutdown the viewport behind it
    // is being taken apart, and a destructor is the worst place to find out.
    if (!mArc) return;
    // THE SCENE THAT OWNS THE NODES, if it still answers (vrarc.h's lifetime
    // note): give them back. A different answer — or none — means the scene
    // was destroyed and took them with it, so they are merely forgotten.
    Scene *target = mDeps.engineScene ? mDeps.engineScene() : nullptr;
    if (target && target == mArc->target()) mArc->clear();
    else mArc->forget();
    mArc.reset();
}

void VrInteraction::drawArc()
{
    Scene *target = mDeps.engineScene ? mDeps.engineScene() : nullptr;
    if (mArc && mArc->target() != target) {
        // THE SCENE IT WAS BUILT ON IS GONE (a viewport taken apart under a
        // running session — the lifetime class this module has been bitten by
        // twice). Forget the ids; the nodes died with the scene.
        mArc->forget();
        mArc.reset();
    }
    if (!target) return;
    if (!mArc) {
        if (!mTeleport.armed) return;       // nothing to draw, so nothing to build
        mArc.reset(new VrArc(target));
    }
    if (!mTeleport.armed) { mArc->hide(); return; }
    mArc->update(mTeleport.points, mTeleport.valid, mTeleport.landed);
}

bool VrInteraction::teleportArm(unsigned hand)
{
    if (hand >= VrHandCount) return false;
    // A HAND THAT IS HOLDING SOMETHING IS NOT AIMING A THROW. The gesture owns
    // that stick (push/pull and the turntable), and a handle drag owns the
    // frame; both refusals are the same one the gizmo and the grab make about
    // each other (§5.2, "one button each, no overlap").
    if (mGesture.active || mGizmoDrag.active) return false;
    const VrHandState st = handState(hand);
    if (!st.valid || !st.aim.valid) return false;
    const bool was = mTeleport.armed;
    Teleport t = traceArc(hand, st);
    t.armed = true;
    mTeleport = t;
    if (!was) ++mTeleportArms;
    drawArc();
    return true;
}

bool VrInteraction::teleportCancel()
{
    if (!mTeleport.armed) return false;
    mTeleport = Teleport();
    ++mTeleportCancels;
    drawArc();
    return true;
}

bool VrInteraction::teleportTo(const iris::Vec3 &point)
{
    if (!std::isfinite(point.x()) || !std::isfinite(point.y()) || !std::isfinite(point.z()))
        return false;
    // THE HOST IS STILL PUTTING THE WEARER SOMEWHERE (turn()'s own guard, and
    // for the same reason): a rig written from a head the engine has not yet
    // paired with it is a teleport on top of a teleport.
    if (locomotionBlocked()) return false;
    vrorigin::Rig rig;
    iris::Vec3 head;
    iris::Quat headRot;
    if (!rigNow(rig, head, headRot)) return false;
    Engine *engine = engineNow();
    if (!engine) return false;
    const vrorigin::Rig out = vrgrab::teleportedTo(rig, head, headRot, point);
    engine->setVrOrigin(toEngine(out.position), out.yaw);
    ++mTeleports;
    return true;
}

bool VrInteraction::teleportFire()
{
    if (!mTeleport.armed) return false;
    const bool ok = mTeleport.landed && mTeleport.valid;
    const iris::Vec3 landing = mTeleport.landing;
    const unsigned hand = mTeleport.hand;
    mTeleport = Teleport();
    drawArc();
    if (!ok) {
        // A REFUSED LANDING IS NOT A MOVE AND NOT AN ERROR — the wearer aimed
        // at a wall, saw the arc go red, and let go. The arc goes away.
        ++mTeleportCancels;
        return false;
    }
    if (!teleportTo(landing)) return false;
    haptic(hand, kTickAmplitude, kTickSeconds);
    return true;
}

QVariantMap VrInteraction::teleportReport() const
{
    QVariantMap out;
    out[QStringLiteral("armed")] = mTeleport.armed;
    out[QStringLiteral("valid")] = mTeleport.armed && mTeleport.valid;
    out[QStringLiteral("landed")] = mTeleport.armed && mTeleport.landed;
    out[QStringLiteral("reason")] = mTeleport.reason;
    out[QStringLiteral("hand")] =
        mTeleport.hand == VrHandRight ? QStringLiteral("right") : QStringLiteral("left");
    if (mTeleport.armed && mTeleport.landed) {
        QVariantMap at;
        at[QStringLiteral("x")] = double(mTeleport.landing.x());
        at[QStringLiteral("y")] = double(mTeleport.landing.y());
        at[QStringLiteral("z")] = double(mTeleport.landing.z());
        out[QStringLiteral("landing")] = at;
        QVariantMap n;
        n[QStringLiteral("x")] = double(mTeleport.normal.x());
        n[QStringLiteral("y")] = double(mTeleport.normal.y());
        n[QStringLiteral("z")] = double(mTeleport.normal.z());
        out[QStringLiteral("normal")] = n;
    }
    out[QStringLiteral("points")] = mTeleport.points.size();
    // WHAT IS ACTUALLY IN THE WORLD (vrarc.h): the number of line nodes shown
    // and whether the landing ring is up. A suite that asserts "the wearer can
    // SEE the arc" has to ask the drawer, not the tracer — the two are
    // different objects on purpose.
    out[QStringLiteral("drawn")] = mArc ? mArc->segmentsShown() : 0;
    out[QStringLiteral("marker")] = mArc && mArc->markerShown();
    out[QStringLiteral("speed")] = double(vrgrab::kTeleportSpeed);
    out[QStringLiteral("maxSlopeDegrees")] = double(vrgrab::kTeleportMaxSlopeDegrees);
    out[QStringLiteral("arms")] = QVariant::fromValue(qulonglong(mTeleportArms));
    out[QStringLiteral("teleports")] = QVariant::fromValue(qulonglong(mTeleports));
    out[QStringLiteral("cancels")] = QVariant::fromValue(qulonglong(mTeleportCancels));
    return out;
}

void VrInteraction::step(float seconds)
{
    // THE PROJECT'S LOCOMOTION SETTINGS, RESOLVED ONCE FOR THE WHOLE FRAME
    // (the lead's read, item 10) — see FrameSettings. Every `dominantHand()`,
    // `offHand()`, turn and report below takes this copy, including on the
    // early return.
    const FrameSettings frame(this);
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
    if (player) cancelEditing();
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

        // ---- THE OFF HAND JOINS, AND LEAVES (two-hand grab, §5.1) --------
        //
        // ONLY ONTO A LIVE GESTURE. The off hand's squeeze means "fly where I
        // look" while nothing is held (the owner's own ask at the first
        // controller smoke) and "put your other hand on it" while something is
        // — one button, two meanings, told apart by whether there is anything
        // in the wearer's hand. It cannot START a gesture: the dominant hand
        // manipulates (the owner's answer 3), and a grab begun by the off hand
        // would have no ray behind it.
        if (mGesture.active) {
            const VrHandState &off = hands[offHand()];
            const VrHandState &offPrev = mPrev[offHand()];
            if (off.grabPressed && !offPrev.grabPressed && !mGesture.two && !mGizmoDrag.active)
                beginGrab(offHand());
            else if (!off.grabPressed && offPrev.grabPressed &&
                     (mGesture.two || mGesture.hand == offHand()))
                // ...AND IT ENDS A GESTURE IT OWNS (the lead's read, item 2).
                // The dominant hand letting go of a two-hand hold hands the
                // object to the off hand, and until this edge existed NOTHING
                // could put it down: the dominant edge needs the dominant's own
                // transition and this one asked for a pair. The object stayed
                // welded to the off grip with no button pressed, and the stick,
                // the gizmo and the gaze fly were all refused meanwhile because
                // a gesture was live.
                endGrab(offHand());
        }

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
        // The off-hand SQUEEZE held = fly where you look — UNLESS that squeeze
        // is the second hand on a held object (the two-hand grab above took
        // the same button). A wearer holding something with both hands is not
        // also flying.
        fly(0.0f, o.stickY, dt, false, &o.aim, o.grabPressed && !mGesture.active);
        // STICK RIGHT TURNS THE WEARER RIGHT (the lead, from the Fable read at
        // merge): vrgrab's turn functions are "positive = the stick's own sign",
        // and the tree's yaw is the right-handed rotation about +Y (vrorigin.h),
        // under which +30 degrees carries a wearer facing -Z toward -X, i.e. to
        // their LEFT. The sign lives HERE, at the one call site, so the pure
        // functions stay what they say and every VR title's convention holds:
        // a flick right turns the view clockwise from above.
        const vrworld::Settings loco = locomotion();
        if (loco.turn == iris::VrTurnMode::Smooth) {
            turn(-vrgrab::smoothTurnDegrees(o.stickX, dt, loco.smoothTurnDegreesPerSecond));
        } else {
            const float deg = -vrgrab::snapTurnDegrees(o.stickX, mTurnArmed, loco.snapTurnDegrees);
            if (std::fabs(deg) > 1e-4f && turn(deg)) mTurnArmed = false;
            if (vrgrab::snapTurnRearmed(o.stickX)) mTurnArmed = true;
        }
    }

    // ---- THE DOMINANT STICK'S THROW: TELEPORT (§6 row L4) ----------------
    //
    // IT RUNS IN BOTH HOSTS, because it is LOCOMOTION: the editor's preview and
    // the Player alike, exactly like the fly and the turn above it (the Player
    // edits nothing, and moving the wearer is not an edit).
    {
        const VrHandState &t = hands[dominant];
        const bool busy = mGesture.active || mGizmoDrag.active;
        if (busy) {
            // THAT STICK BELONGS TO THE GESTURE while something is held, and a
            // throw armed before the grab is not the wearer's intent any more.
            if (mTeleport.armed) teleportCancel();
            mTeleportArmable = false;
        } else if (!t.valid) {
            // A TRACKING BLIP HOLDS THE ARC WHERE IT IS (the lead's read, item
            // 8) — the grab's own rule, one line of which reads "a skipped
            // locate is not a released trigger". This used to CANCEL, and
            // because the next located frame re-armed, one dropped frame of a
            // controller threw the curve away, re-armed it, and reset the
            // counters; a stick held over a blink could not be released into a
            // teleport at all. The stick's value is unknown while the hand is
            // unlocated, so nothing is decided from it.
        } else {
            const float y = t.stickY;
            // TWO THRESHOLDS, NOT ONE (the lead's read, item 5). Arming takes
            // the dead zone (0.5); the throw is taken only once the stick has
            // come back through the RE-ARM band (0.2) — the snap turn's own
            // number, and for the same reason. With one threshold a stick
            // resting on the edge of the dead zone armed and fired on
            // alternating frames; between the two the arc simply stays up and
            // keeps re-aiming, which is what a wearer moving their thumb
            // slowly is doing.
            const bool aiming = y >= vrgrab::kStickDeadZone;
            const bool holding = y > vrgrab::kStickRearm;
            const bool back = y <= -vrgrab::kStickDeadZone;
            if (mTeleport.armed) {
                if (t.menuPressed) {
                    // `menu` CANCELS (the brief's rule, and the one modifier
                    // this surface has), and keeps it cancelled until the stick
                    // comes back.
                    teleportCancel();
                    mTeleportArmable = false;
                    mMenuConsumed = true;
                } else if (back) {
                    // A FLICK BACKWARDS is the other way to say no — what a
                    // hand does when it changes its mind.
                    teleportCancel();
                } else if (holding) {
                    teleportArm(dominant);      // still aiming: re-trace
                } else {
                    teleportFire();             // came back through the re-arm
                }
            } else if (aiming && mTeleportArmable) {
                if (t.menuPressed) {
                    mTeleportArmable = false;
                    mMenuConsumed = true;
                } else {
                    teleportArm(dominant);
                }
            }
            // RE-ARMABLE ONLY FROM THE RE-ARM BAND, so a cancelled throw and a
            // throw taken cannot be followed by another until the thumb has
            // really come back.
            if (y <= vrgrab::kStickRearm) mTeleportArmable = true;
        }
    }

    for (unsigned i = 0; i < VrHandCount; ++i) mPrev[i] = hands[i];
}

QVariantMap VrInteraction::report() const
{
    QVariantMap out;
    const vrworld::Settings loco = locomotion();
    out[QStringLiteral("dominant")] =
        loco.dominantRight ? QStringLiteral("right") : QStringLiteral("left");
    out[QStringLiteral("turn")] =
        loco.turn == iris::VrTurnMode::Snap ? QStringLiteral("snap") : QStringLiteral("smooth");
    out[QStringLiteral("snapTurnDegrees")] = double(loco.snapTurnDegrees);
    out[QStringLiteral("smoothTurnDegreesPerSecond")] =
        double(loco.smoothTurnDegreesPerSecond);
    out[QStringLiteral("flySpeed")] = double(loco.flySpeed);
    out[QStringLiteral("fly")] = QString::fromLatin1(iris::vrFlyModeName(loco.fly));
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
    out[QStringLiteral("twoHanded")] = mGesture.active && mGesture.two;
    // WHICH HANDS ARE ON IT — the answer a verb's refusal needs and a suite
    // asserts the hand-off with (an empty string when nothing is held).
    out[QStringLiteral("hand")] = mGesture.active
                                     ? (mGesture.hand == VrHandRight ? QStringLiteral("right")
                                                                     : QStringLiteral("left"))
                                     : QString();
    out[QStringLiteral("hand2")] =
        mGesture.active && mGesture.two
            ? (mGesture.hand2 == VrHandRight ? QStringLiteral("right") : QStringLiteral("left"))
            : QString();
    out[QStringLiteral("scale")] = double(mGesture.active ? mGesture.scale : 1.0f);
    out[QStringLiteral("rollDegrees")] = double(mGesture.active ? mGesture.rollDegrees : 0.0f);
    out[QStringLiteral("twoHands")] = QVariant::fromValue(qulonglong(mTwoHands));
    out[QStringLiteral("gizmo")] = gizmoReport();
    out[QStringLiteral("teleport")] = teleportReport();
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
