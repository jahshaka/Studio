/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef VRINTERACTION_H
#define VRINTERACTION_H

// VrInteraction — WHAT THE WEARER'S CONTROLLERS DO TO THE SCENE
// (SPECS/VR_INPUT_SPEC.md stage 1: §3 the ray, §4 select, §5.1 direct
// manipulation, §6 locomotion; the owner's ten answers are §16).
//
// ONE OBJECT, AND IT KNOWS NOTHING ABOUT VR. It takes a VrHandState per hand
// from a SOURCE (the engine's `vrStatus().input[]`, whether the runtime's
// action system or `Engine::vrInjectInput` wrote it) and turns it
// into the editor's own verbs: the document's picker for the ray, the
// SelectionService for the selection, TransformSceneNodeCommand for the undo,
// SnapSettings for the grid, vrgrab.h for the arithmetic. There is no OpenXR in
// it, no Ogre, no widget and no window — which is exactly what lets every
// gesture be driven headlessly, on any box, with no headset (§10): a gesture
// needs INPUT, not a runtime.
//
// WHY A SERVICE AND NOT VIEWPORT CODE. The desktop editor's equivalent lives in
// EngineSceneViewport's mouse handlers, where it is welded to a QWidget's
// events, a pixel and a camera. None of those three exist in a headset: the
// gesture arrives as a pose, the "cursor" is a world-space ray and the eye is
// not a document camera. What IS shared is everything below the gesture — the
// picker, the selection set, the root-selection rule, the snap sizes, the undo
// shape — and this object calls all of it rather than reimplementing any of it.
// A second picker or a second undo shape is how the VR editor and the desktop
// editor would come to disagree about what a click selects.
//
// THE RULES IT ENFORCES, each one the desktop's own:
//
//   * A LOCKED NODE IS NOT A HIT. The lock IS `pickable` (docs/SCRIPTING.md's
//     dropTargetAt note), so the picker refuses it and a press on one
//     deselects, exactly as a desktop click does.
//   * THE ROOT RULE: a ray on a part of an imported asset selects the asset,
//     unless that asset is already the selection — ScenePicker::
//     resolveRootSelection against the SET, the same call the viewport makes.
//   * MENU HELD IS THE CTRL OF VR (owner answer 10): it toggles a selection and
//     it snaps a grab. A modified press on empty space KEEPS the set.
//   * ONE UNDO MACRO PER GESTURE, whatever it moved, in the gizmo's shape: one
//     TransformSceneNodeCommand per member, a macro only when there is more
//     than one, and the EDIT GATE consulted — a grab during a script run is
//     refused before it starts, and a run that begins mid-gesture rewinds it
//     and pushes nothing.
//   * A GESTURE THAT LOSES ITS INPUT IS CANCELLED, NOT COMMITTED. `xrSyncActions`
//     answers "nothing pressed" outside Focused (the dashboard came up), so the
//     object goes back where it was. Committing half a gesture the wearer could
//     no longer see would be worse than the visible snap back.
//   * THE RIG IS THE ENGINE'S. Locomotion reads `vrStatus().origin` every time
//     and writes through `Engine::setVrOrigin` — never a host-side copy, which
//     is the rule VR-4-FIX paid for (a remembered rig overwrites a runtime
//     recentre the session already absorbed).
//   * ...BUT NOT WHILE THE HOST IS STILL PLACING THE WEARER. A host places the
//     rig on the first located frame it can PAIR with the rig it holds, and a
//     correction from a mismatched pair is a teleport; both hosts refuse their
//     own fly meanwhile, and the stick asks the same question through
//     `Deps::locomotionBlocked` instead of writing the origin behind their
//     backs.
//   * IN THE PLAYER, ONLY LOCOMOTION RUNS — FOR THE VERBS AS WELL AS FOR THE
//     BUTTONS. The Player edits nothing — no selection, no gizmo, no transform
//     writes — so the ray, the select and the grab are skipped there, and
//     `select()`/`beginGrab()`/`endGrab()` refuse outright, which is what makes
//     the promise true for a script and an MCP session too. A gesture in flight
//     when the Player takes the session over is CANCELLED, like one that loses
//     focus: nothing in the Player would ever follow it.

#include <functional>

#include <QList>
#include <QString>
#include <QVariantMap>
#include <QVector>

#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include "irisgl/irisglfwd.h"
#include "jahshaka/engine/Engine.h"
#include "modules/vr/vrgrab.h"
#include "viewport/flystep.h"

class SelectionService;
struct StudioServices;

// ---------------------------------------------------------------------------

/// WHERE A FRAME'S HAND STATES COME FROM.
///
/// ONE IMPLEMENTATION, AND THAT IS THE POINT (the 1E integration, 2026-09-17):
/// there is exactly one store of hand samples in the process and it is the
/// ENGINE's (`Engine::vrInjectInput` writes it, `vrStatus().input[]` reports
/// it), so an injected gesture and a worn one arrive here down the same wire
/// and the service could not tell them apart if it tried. The interface
/// survives the merge of the two sources because a HOST stand-in in a test
/// still wants to answer "no controller" without an engine, and because naming
/// the question keeps the service free of `vrStatus()` calls.
class VrInputSource
{
public:
    virtual ~VrInputSource() = default;
    /// `hand` is a jahshaka::engine::VrHand. An out-of-range index answers an
    /// invalid state rather than asserting: a source is asked by a loop.
    virtual jahshaka::engine::VrHandState hand(unsigned hand) const = 0;
    /// Is the input LIVE? A runtime that has taken focus away (the dashboard)
    /// reports nothing pressed, and a gesture in flight must cancel rather than
    /// believe the release.
    virtual bool focused() const = 0;
    /// For `vr.interactionMode().source` — the KIND of source; whether a
    /// given sample was injected is the sample's own word (`fromInjection`).
    virtual QString name() const = 0;
};

/// THE ENGINE'S HAND SAMPLES — `VrStatus::input[hand]`, whoever wrote them.
///
/// THE WHOLE SOURCE, since stage 1's two halves met: the runtime's action
/// system fills those fields inside the session's frame, `Engine::vrInjectInput`
/// fills them from a script with no runtime at all, and each sample says which
/// it was (`fromInjection`). A second, Studio-side injection store used to sit
/// beside this one and was DELETED at the integration: two stores meant two
/// answers to "what is the left hand doing", and the one a gesture read
/// depended on which verb had been called last.
class VrEngineInput : public VrInputSource
{
public:
    explicit VrEngineInput(jahshaka::engine::Engine *engine = nullptr) : mEngine(engine) {}
    void setEngine(jahshaka::engine::Engine *engine) { mEngine = engine; }

    jahshaka::engine::VrHandState hand(unsigned hand) const override;
    bool focused() const override;
    QString name() const override { return QStringLiteral("runtime"); }

private:
    jahshaka::engine::Engine *mEngine = nullptr;
};

/// THE INTERACTION ITSELF.
class VrInteraction
{
public:
    /// Everything it needs, injected. Every member is nullable/absent-safe:
    /// a host with no undo service still hovers and selects.
    struct Deps
    {
        /// The DOCUMENT scene, asked per frame rather than held: a project
        /// switch replaces it, and a service holding the old one is the
        /// dangling-pointer class VR-4-FIX found in this module.
        std::function<iris::ScenePtr()> scene;
        SelectionService *selection = nullptr;
        /// For the undo spine (services->undo). Nullable.
        StudioServices *services = nullptr;
        /// For the rig (locomotion), the ray helper and the haptic tick.
        ///
        /// A CALLABLE, NOT A POINTER: the engine comes up after the modules are
        /// built and goes away before them, and a service holding the raw
        /// pointer it was handed at construction is the lifetime class VR-4-FIX
        /// found twice in this module. Absent or answering null means "no
        /// engine": the service still runs the whole document half.
        std::function<jahshaka::engine::Engine *()> engine;
        /// The wearer's fly speed, in world units per second. The host knows
        /// which surface's speed it is (the editor preview flies at the
        /// editor's, the Player at the Player's).
        std::function<float()> wearerSpeed;
        /// Is the PLAYER the host of the running session? Then only locomotion
        /// runs (the Player edits nothing).
        std::function<bool()> playerMode;
        /// MAY THE WEARER BE MOVED AT ALL THIS FRAME?
        ///
        /// A host PLACES the wearer when a session begins and at every recentre
        /// — it waits for a located frame it can pair with the rig it holds and
        /// then writes the rig that puts the wearer where the editor camera
        /// stands (EditorVrPreview::armPlacement, PlayerVr's the same). A
        /// correction computed from a MISMATCHED pair is a teleport, which is
        /// why both hosts refuse their own fly while one is pending — and the
        /// stick used to walk straight past that refusal, because it writes
        /// `setVrOrigin` from this object instead of from the host. Every
        /// locomotion here asks first; a flick held over the blocked frames is
        /// answered on the first frame after the placement lands.
        std::function<bool()> locomotionBlocked;
    };

    enum class Turn { Snap, Smooth };
    /// How the stick flies: along the stick hand's AIM (Unreal's VR editor; the
    /// owner's pick) or LEVEL along the head's heading (the comfort option).
    enum class Fly { Aim, Level };
    enum class SelectMode { Replace, Toggle, Add };

    /// SESSION OPTIONS (owner answers 2 and 3). Not persisted and not a
    /// preference: they are set on the session, API-first, until the owner asks
    /// for a Preferences row.
    struct Options
    {
        Turn turn = Turn::Snap;
        Fly  fly = Fly::Aim;
        /// Right hand manipulates and the LEFT stick walks; a swap swaps both
        /// (answer 3 — one flag, never two).
        bool dominantRight = true;
        float snapTurnDegrees = vrgrab::kSnapTurnDegrees;
        float smoothTurnDegreesPerSecond = vrgrab::kSmoothTurnDegreesPerSecond;
    };

    /// WHAT THE RAY IS ON.
    struct Hover
    {
        bool hit = false;
        unsigned hand = jahshaka::engine::VrHandRight;
        /// What a press would SELECT (the root rule applied).
        iris::SceneNodePtr node;
        /// What the ray actually struck (the part under the ray).
        iris::SceneNodePtr picked;
        iris::Vec3 origin, direction, point;
        float distance = 0.0f;
        int triangleIndex = -1;
    };

    VrInteraction() = default;

    void setDeps(const Deps &deps) { mDeps = deps; }
    /// The source is NOT owned. Null means "no controller reports", which is
    /// the truthful answer in a process with no engine.
    void setSource(VrInputSource *source) { mSource = source; }
    /// The source the interaction consults, or null before install.
    const VrInputSource *activeSource() const { return mSource; }
    /// IS A TEST WRITING THE HANDS? True while either hand's sample carries
    /// `fromInjection` — the engine's own word, not a flag of ours.
    ///
    /// The render driver stands down from stepping this object while it is
    /// true, and that is what makes a suite exact: with a script writing the
    /// hands, ONE injected sample IS one interaction frame (`vr.step()`), so a
    /// driver tick stepping the same values again would integrate the stick
    /// twice and turn the wearer twice per flick.
    bool injectionArmed() const;

    void setOptions(const Options &options) { mOptions = options; }
    Options options() const { return mOptions; }

    /// A SESSION BEGAN / ENDED. Beginning arms nothing but the bookkeeping;
    /// ending CANCELS a gesture in flight (§5.5) and clears the ray.
    void begin();
    void end();
    bool installed() const { return mInstalled; }

    /// ONE INTERACTION FRAME: read both hands, hover, run the button edges,
    /// follow a live gesture, walk the wearer. `seconds` is the frame's own
    /// duration, clamped by vrorigin::frameSeconds before it may move anybody.
    void step(float seconds);

    // ---- the gestures, as operations -------------------------------------
    //
    // The button EDGES in step() call exactly these, and so do the verbs — one
    // implementation, so a script and a trigger cannot disagree (the API-first
    // rule, SCRIPTING_SPEC §2.3).

    /// What a trigger press does. False when nothing changed (no hand, or the
    /// press landed on empty space with the modifier down) and when the PLAYER
    /// hosts the session (it edits nothing).
    bool select(unsigned hand, SelectMode mode);
    /// What a squeeze press does: capture the selection (or the node under the
    /// ray, selecting it first) and hold it. False when refused — nothing to
    /// grab, the edit gate, or the Player.
    ///
    /// A GRAB TAKEN BY THE OFF HAND IS RELEASABLE ONLY BY `endGrab`. The button
    /// edges in step() run for the DOMINANT hand alone (the off hand's stick is
    /// locomotion), so a `vr.grab({hand:"left"})` is ended by `vr.release`, by
    /// a cancel (focus loss, session end, the Player taking over) — never by
    /// squeezing the off hand.
    bool beginGrab(unsigned hand);
    /// What a squeeze release does: commit ONE undo macro. False when no
    /// gesture was live, or in the Player.
    bool endGrab(unsigned hand);
    /// Put everything back and push nothing (focus loss, session end, a
    /// project switch). False when no gesture was live.
    bool cancel();
    /// Turn the wearer about their own head. False with no session/rig.
    bool turn(float degrees);
    /// One step of stick flight. False with no session/rig, or nothing pushed.
    bool fly(float stickX, float stickY, float seconds, bool boost = false,
             const jahshaka::engine::VrPose *aim = nullptr);

    // ---- what a verb reports ---------------------------------------------
    Hover hover() const { return mHover; }
    bool grabbing() const { return mGesture.active; }
    /// The nodes a live gesture is carrying (empty when none).
    QList<iris::SceneNodePtr> gestureNodes() const;
    unsigned dominantHand() const;
    unsigned offHand() const;
    /// Is the PLAYER hosting the running session (Deps::playerMode)? Then only
    /// locomotion runs — for the button edges AND for the verbs, and public
    /// because a verb that refused for this reason should SAY so rather than
    /// answer "nothing to grab".
    bool playerHosted() const;
    /// The state the service READ for that hand this frame, whoever wrote it.
    jahshaka::engine::VrHandState handState(unsigned hand) const;
    QString sourceName() const;
    /// {dominant, turn, grabbing, hovering, source, snapping, far, distance}
    QVariantMap report() const;

private:
    struct Member
    {
        iris::SceneNodePtr node;
        vrgrab::Pose startGlobal;
        iris::Vec3 startLocalPos, startLocalScale;
        iris::Quat startLocalRot;
    };
    struct Gesture
    {
        bool active = false;
        unsigned hand = jahshaka::engine::VrHandRight;
        bool far = false;
        bool snapping = false;
        float distance = 0.0f;      ///< along the aim ray, far grabs only
        float turntable = 0.0f;     ///< accumulated degrees about world up
        vrgrab::Pose handStart;     ///< the hand as captured (grip, or virtual)
        vrgrab::Pose handNow;       ///< the filtered hand this frame
        QVector<Member> members;
    };

    iris::ScenePtr scene() const;
    /// The aim ray of a hand, or false when it is not located.
    bool ray(const jahshaka::engine::VrHandState &state, iris::Vec3 &origin,
             iris::Vec3 &direction) const;
    /// THE DOCUMENT PICK ALONG A RAY, resolved to what a press would select —
    /// and MEMOISED for the frame, which is the whole of finding (b)'s fix.
    ///
    /// WHAT IT COST BEFORE. Every step() picked once for the hover, and then a
    /// trigger or a squeeze in the same frame picked AGAIN for the same hand at
    /// the same pose; each pick ran `ScenePicker::pickAll` with
    /// `refreshTransforms = true`, i.e. a full recursive `update(0)` walk of
    /// the whole document (the header warns callers inside a live drag to pass
    /// false for exactly this reason) plus the ray query and a QList of hits.
    ///
    /// WHAT IT COSTS NOW. The geometric half of the answer is remembered
    /// against the hand, the scene, the aim pose's own bits and
    /// `iris::graph::transformWrites()` — the document's transform/structure
    /// epoch — so a hand that did not move in a document that did not move is
    /// answered without touching the scene at all, and the walk runs only when
    /// something has actually been written since the last one. The ROOT RULE is
    /// re-resolved on every call regardless (it depends on the SELECTION, which
    /// a press changes), so a second press still drills into the part of an
    /// asset that is already selected.
    ///
    /// THE ONE THING THE EPOCH DOES NOT SEE is a change that moves nothing and
    /// writes no transform — a node LOCKED or hidden while the hand is already
    /// crossing it. That is answered on the next pose change, which in a
    /// headset is the next frame (a tracked hand never repeats a pose bit for
    /// bit); a script that wants it sooner moves the hand.
    Hover pick(unsigned hand, const jahshaka::engine::VrHandState &state) const;
    /// THE SELECTION RULES, over a pick that has already happened — shared by
    /// `select()` (the verb and the trigger) and by a grab on something that
    /// was not selected yet, so the two cannot disagree about the empty-space
    /// and World-root cases.
    bool applySelection(const Hover &hover, SelectMode mode);
    /// Push the ray + hit to the engine so it can DRAW them (the engine owns
    /// the line and the marker; this side owns the hit).
    void pushRay() const;
    /// A short tick in the wearer's hand — a selection changed, a grab took.
    void haptic(unsigned hand, float amplitude, float seconds) const;
    /// One frame of a live gesture.
    void followGesture(float seconds);
    /// Put every member back where the gesture found it.
    void rewindGesture();
    /// The D5-reduced set a grab carries, primary first.
    QList<iris::SceneNodePtr> grabTargets(const iris::SceneNodePtr &under) const;
    /// The rig as the ENGINE holds it, and whether there is one at all.
    bool rigNow(vrorigin::Rig &rig, iris::Vec3 &headPosition, iris::Quat &headRotation) const;
    /// The engine right now, or null (see Deps::engine).
    jahshaka::engine::Engine *engineNow() const;
    /// Is a host mid-placement (see Deps::locomotionBlocked)? False when no
    /// host said — a headless stand-in places nobody.
    bool locomotionBlocked() const;

    Deps mDeps;
    Options mOptions;
    VrInputSource *mSource = nullptr;
    bool mInstalled = false;

    jahshaka::engine::VrHandState mPrev[jahshaka::engine::VrHandCount];
    Hover mHover;
    Gesture mGesture;
    /// The snap turn's re-arm (one flick, one turn — vrgrab::snapTurnRearmed).
    bool mTurnArmed = true;

    /// THE FRAME'S PICK, REMEMBERED (see pick()). Mutable because picking is a
    /// READ of the document: `hover()`, `select()` and `beginGrab()` are all
    /// const-correct about the scene and none of them should have to be a
    /// non-const operation to be cheap.
    struct PickMemo
    {
        bool valid = false;
        const iris::Scene *scene = nullptr;
        unsigned hand = ~0u;
        /// The aim pose, compared FIELD BY FIELD against the next frame's: a
        /// runtime's pose is a copy of what it reported, so two frames in which
        /// the hand did not move carry identical floats.
        float px = 0.0f, py = 0.0f, pz = 0.0f;
        float rx = 0.0f, ry = 0.0f, rz = 0.0f, rw = 0.0f;
        /// The document's transform/structure epoch at the pick
        /// (iris::graph::transformWrites(), which a reparent bumps too).
        unsigned long long writes = 0ull;
        /// The GEOMETRIC half of the hit — everything except the root rule.
        bool hit = false;
        iris::SceneNodePtr picked;
        iris::Vec3 origin, direction, point;
        float distance = 0.0f;
        int triangleIndex = -1;
    };
    mutable PickMemo mMemo;
    /// `transformWrites()` as it stood when this object last asked the picker
    /// to refresh the document's global transforms. A pick refreshes only when
    /// the counter has moved since — the walk is idempotent, so skipping it
    /// when nothing was written is exact rather than optimistic.
    mutable unsigned long long mRefreshedAtWrites = 0ull;
    mutable bool mRefreshed = false;
    /// Counts, for the suites: every number a COUNT, never a wall clock.
    unsigned long long mSelects = 0, mGrabs = 0, mCommits = 0, mCancels = 0, mTurns = 0;
};

#endif   // VRINTERACTION_H
