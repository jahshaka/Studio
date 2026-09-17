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
// from a SOURCE (the runtime's action system, or an injection) and turns it
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
//   * IN THE PLAYER, ONLY LOCOMOTION RUNS. The Player edits nothing — no
//     selection, no gizmo, no transform writes — so the ray, the select and the
//     grab are not installed there; the stick still walks the wearer.

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
// THE ENGINE CONTRACT, STATED HERE UNTIL THE ENGINE CARRIES IT.
//
// The engine lane (VR-INPUT-1E) adds this struct to
// irisgl/engine/include/jahshaka/engine/Types.h beside VrPose, together with
// `VrStatus::input[VrHandCount]`, `Engine::vrInjectInput/vrHaptic/setVrRay`. It
// is written out here, byte for byte the same shape, so the Studio half could
// be built, tested and reviewed against the contract before the engine half
// landed — and it disappears the moment Types.h defines
// JAH_ENGINE_HAS_VRHANDSTATE (the engine lane defines it beside the struct; if
// it does not, deleting this block at integration is a one-line change).
//
// WORLD SPACE, THROUGH THE RIG, exactly like VrStatus::hands and the head: the
// only frame a host can reason in. The aim pose's ray points along -Z of its
// rotation.
#ifndef JAH_ENGINE_HAS_VRHANDSTATE
namespace jahshaka {
namespace engine {

struct VrHandState
{
    bool   valid = false;          ///< the runtime located this hand THIS frame
    VrPose aim;                    ///< the pointing pose (-Z is the ray)
    VrPose grip;                   ///< where the hand holds the controller
    float  select = 0.0f;          ///< the trigger, 0..1
    bool   selectPressed = false;  ///< ...past its threshold, hysteresis applied
    float  grab = 0.0f;            ///< the squeeze, 0..1
    bool   grabPressed = false;
    bool   menuPressed = false;    ///< the modifier (the Ctrl of VR)
    float  stickX = 0.0f, stickY = 0.0f;
    bool   stickPressed = false;
    /// The values came from Engine::vrInjectInput, not from a runtime. A suite
    /// asserts it so a smoke on a headset can never be reading a stale
    /// injection and calling it the controller.
    bool   fromInjection = false;
};

}   // namespace engine
}   // namespace jahshaka
#endif

// ---------------------------------------------------------------------------

/// WHERE A FRAME'S HAND STATES COME FROM. Two implementations below; the
/// service cannot tell them apart, which is the whole point of the injection
/// route (VR_INPUT_SPEC §2.4 I1).
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
    /// For `vr.interactionMode().source` — "runtime" or "injection".
    virtual QString name() const = 0;
};

/// THE INJECTION SOURCE — the test hook that makes every gesture gateable.
///
/// It holds one state per hand and answers with it until it is changed, which
/// is the contract `vr.inputInject` documents: a script sets a hand's pose and
/// buttons, steps the interaction, and reads what the editor did. Nothing about
/// the gesture code knows it is being driven.
class VrInjectedInput : public VrInputSource
{
public:
    jahshaka::engine::VrHandState hand(unsigned hand) const override;
    bool focused() const override { return mFocused; }
    QString name() const override { return QStringLiteral("injection"); }

    void set(unsigned hand, const jahshaka::engine::VrHandState &state);
    void setFocused(bool focused) { mFocused = focused; }
    /// Forget everything — both hands invalid, as if no controller existed.
    void clear();
    /// Has anything ever been injected? The service prefers the runtime's own
    /// input until it has.
    bool armed() const { return mArmed; }

private:
    jahshaka::engine::VrHandState mHands[jahshaka::engine::VrHandCount];
    bool mArmed = false;
    bool mFocused = true;
};

/// THE RUNTIME SOURCE — `VrStatus::input[hand]`, once the engine carries it.
/// Until then it answers "no controller", which is the truthful answer on a box
/// whose engine has no action system: the service then does nothing until
/// something is injected.
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
    };

    enum class Turn { Snap, Smooth };
    enum class SelectMode { Replace, Toggle, Add };

    /// SESSION OPTIONS (owner answers 2 and 3). Not persisted and not a
    /// preference: they are set on the session, API-first, until the owner asks
    /// for a Preferences row.
    struct Options
    {
        Turn turn = Turn::Snap;
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
    /// The source is NOT owned. Null falls back to the injection store, which
    /// is what every headless gate uses.
    void setSource(VrInputSource *source) { mSource = source; }
    /// The source the interaction consults this frame: the injection while it
    /// is armed, else the engine's report (null before install).
    const VrInputSource *activeSource() const { return mInjected.armed() ? &mInjected : mSource; }
    VrInjectedInput &injection() { return mInjected; }
    const VrInjectedInput &injection() const { return mInjected; }

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
    /// press landed on empty space with the modifier down).
    bool select(unsigned hand, SelectMode mode);
    /// What a squeeze press does: capture the selection (or the node under the
    /// ray, selecting it first) and hold it. False when refused — nothing to
    /// grab, or the edit gate.
    bool beginGrab(unsigned hand);
    /// What a squeeze release does: commit ONE undo macro. False when no
    /// gesture was live.
    bool endGrab(unsigned hand);
    /// Put everything back and push nothing (focus loss, session end, a
    /// project switch). False when no gesture was live.
    bool cancel();
    /// Turn the wearer about their own head. False with no session/rig.
    bool turn(float degrees);
    /// One step of stick flight. False with no session/rig, or nothing pushed.
    bool fly(float stickX, float stickY, float seconds, bool boost = false);

    // ---- what a verb reports ---------------------------------------------
    Hover hover() const { return mHover; }
    bool grabbing() const { return mGesture.active; }
    /// The nodes a live gesture is carrying (empty when none).
    QList<iris::SceneNodePtr> gestureNodes() const;
    unsigned dominantHand() const;
    unsigned offHand() const;
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
    /// The document pick along a ray, resolved to what a press would select.
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

    Deps mDeps;
    Options mOptions;
    VrInjectedInput mInjected;
    VrInputSource *mSource = nullptr;
    bool mInstalled = false;

    jahshaka::engine::VrHandState mPrev[jahshaka::engine::VrHandCount];
    Hover mHover;
    Gesture mGesture;
    /// The snap turn's re-arm (one flick, one turn — vrgrab::snapTurnRearmed).
    bool mTurnArmed = true;
    /// Counts, for the suites: every number a COUNT, never a wall clock.
    unsigned long long mSelects = 0, mGrabs = 0, mCommits = 0, mCancels = 0, mTurns = 0;
};

#endif   // VRINTERACTION_H
