/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef EDITORVR_H
#define EDITORVR_H

// EditorVrPreview — THE EDITOR'S VR PREVIEW (SPECS/VR_SPEC.md §5 phase 4).
//
// WHAT IT IS, AND HOW IT DIFFERS FROM THE PLAYER'S VR MODE (phase 3). Both put
// the ONE scene in the headset through the same engine session. The Player
// SWITCHES THE DESKTOP OFF: its View stops drawing, the window becomes a mirror
// of the left eye, and the wearer IS the play camera — which is right for
// "explore this world". The editor does the opposite, and that is the whole
// design of this phase:
//
//   * THE DESKTOP VIEWPORT KEEPS RENDERING ITS OWN PICTURE. It stays an editor:
//     its camera, its framing, its gizmos, its selection, its grid. The cost is
//     real and is MEASURED rather than assumed (§3.4: a second workspace means a
//     second shadow-atlas pass a frame) — the measurement is in the lane's
//     report and the mirror is therefore OFF by default here, because painting
//     the headset's eye over the editor's picture would pay for both and show
//     one.
//   * THE EDITOR'S CAMERA IS NOT THE WEARER. Nothing writes the head pose back
//     into the document camera (the Player does, deliberately). The person at
//     the desk — or the person who takes the headset off — finds the viewport
//     exactly where they left it, and where the wearer is REACHING is visible
//     in it as the two controller proxies (VrApi pushes those; they belong to
//     any session, including the Player's).
//   * ...AND THE WEARER SEES THE EDITOR WORKING. The session opens the ordinary
//     helper channel (VrConfig::helpers): grid, light and camera icons,
//     selection outline, gizmo — all of it in the headset, which is the whole
//     purpose of an EDITOR preview and the opposite of the Player's VR mode.
//   * THE FLY KEYS MOVE THE WEARER. The editor's own gesture — right button
//     plus the arrow cluster, Shift to boost, at the editor's own speed
//     setting — walks the RIG along the head's LEVEL heading, exactly as the
//     Player's does (vrorigin::flyDelta). It is the only input this object
//     takes, and the viewport suppresses its camera's fly for the duration
//     (IEditorViewport::setVrPreview).
//
// THE RIG IS THE ENGINE'S (Engine::setVrOrigin) — this object holds no copy and
// reads `vrStatus().origin` every time, so a runtime recentre the session
// absorbed is never overwritten by a host that remembered something older. The
// arithmetic is vrorigin.h's, shared with the Player and asserted headlessly by
// `player.vr`; nothing here re-derives it.

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QVariantMap>
#include <memory>

#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include "irisgl/irisglfwd.h"
#include "jahshaka/engine/Engine.h"
#include "player/vrorigin.h"

class EngineRenderDriver;
class IEditorViewport;

class EditorVrPreview : public QObject
{
    Q_OBJECT
public:
    explicit EditorVrPreview(QObject *parent = nullptr) : QObject(parent) {}
    ~EditorVrPreview() override;

    /// Starts a session on the editor's scene and takes the viewport's fly
    /// keys. False with `error` set; never throws. The options are vr.begin's
    /// (`mirror`, `worldScale`, `eyeWidth`/`eyeHeight`).
    bool begin(const std::shared_ptr<jahshaka::engine::Engine> &engine,
               IEditorViewport *viewport, EngineRenderDriver *driver,
               const QVariantMap &options, QString *error);
    /// Ends it and puts the viewport back. False when none was running.
    bool end();
    /// A session started HERE is still running.
    bool isActive() const;

    /// One frame: the placement, then the wearer's fly. Called from the render
    /// driver's beforeFrame — the one place in the editor that runs once per
    /// rendered frame, which is what a rig must be moved on (a wall-clock timer
    /// would walk the wearer while the loop is blocked in xrWaitFrame).
    void step();

    /// What this object is doing, for `vr.state().preview`.
    QVariantMap report() const;

private:
    void applyRig(const vrorigin::Rig &rig);
    static vrorigin::Rig rigOf(const jahshaka::engine::VrStatus &st);
    /// Arms the "put the wearer where the editor camera stands" placement for
    /// the next located frame that can be PAIRED with the rig this object holds
    /// (PlayerVr's rule: correcting from a mismatched pair is a teleport).
    void armPlacement(const jahshaka::engine::VrStatus &st);
    /// Gives the viewport its camera and its helpers back. Safe twice.
    void release();

    /// WEAK, NOT RAW. This object outlives nothing and is outlived by
    /// everything: the ScriptEngine that owns the VrApi holding it is torn down
    /// beside the shell, and an end() reaching a destroyed Engine from a
    /// destructor is the kind of shutdown crash that only happens on somebody
    /// else's machine. The driver is a QObject and gets a QPointer for the same
    /// reason; the viewport is neither, and is cleared by release().
    std::weak_ptr<jahshaka::engine::Engine> mEngine;
    IEditorViewport *mViewport = nullptr;
    QPointer<EngineRenderDriver> mDriver;
    bool mOwnsSession = false;
    bool mPlacePending = false;
    unsigned long long mPlaceAfterRendered = 0ull;
    /// Where the editor's render camera stood when the session began — the
    /// anchor the wearer is placed on, captured once (the pose BEFORE any
    /// frame of the session moved anything).
    iris::Vec3 mStartPos;
    iris::Quat mStartRot;
    /// The wall clock of the frame just gone, clamped by vrorigin::frameSeconds
    /// before it may move anybody.
    QElapsedTimer mFrameTimer;
};

#endif // EDITORVR_H
