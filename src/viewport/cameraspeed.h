/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef CAMERASPEED_H
#define CAMERASPEED_H

// CameraSpeed — THE camera speed, and there is exactly one of it (owner R15,
// 2026-09-18: "one camera speed value for the editor's fly and the VR fly, an
// integer 1-32 so we don't hold decimals in the UI").
//
// THE VALUE IS AN INTEGER 1..32 and it is a PREFERENCE (`camera/speed`),
// editor-global, not a property of a project. 10 IS TODAY: the factor it
// applies is n/10, so a fresh install flies exactly as it did before this
// existed, to the last float, on every surface.
//
// THE FACTOR RIDES ON EACH SURFACE'S OWN BASE, and the bases are different
// because they are different jobs at different scales:
//
//   * the editor's RMB fly — 8 units/second (EditorCameraController::
//     linearSpeed);
//   * the Player's free camera — 25 units/second, and its play-mode fly 15;
//   * a VR wearer — the PROJECT's `world.vr.flySpeed` in metres per second
//     (default 15), which stays the project's number: how fast a world is
//     meant to be walked is authored, and this dial is the person at the
//     controls saying "faster than that, today".
//
// So `factor()` is what a surface multiplies by, and `applyTo(base)` is the
// one spelling of that multiplication.
//
// WHY 1..32 LINEAR (the lead's mapping note): ten steps below today's speed and
// twenty-two above, no decimals anywhere a person can see. If slow precise work
// ever needs more room at the bottom the mapping can go geometric without the
// 1-32 dial changing at all — every consumer reads `factor()`, none of them
// reads n.
//
// Static, and for the same reason SnapSettings is: the camera controllers this
// feeds sit deep inside per-frame update maths with no services plumbed
// through. When a QSettings is bound (the app binds one at startup, beside the
// snap sizes) the value persists as `camera/speed`; unit tests run unbound and
// get the pure default.

class QSettings;

class CameraSpeed
{
public:
    /// The dial. 10 = today's speed on every surface.
    static constexpr int kDefault = 10;
    static constexpr int kMin = 1;
    static constexpr int kMax = 32;

    /// The bases the factor rides on. Not user-settable: the dial IS the
    /// user-facing control, and these are what "1.0x" means on each surface.
    static constexpr float kEditorBase = 8.0f;    ///< units/second, the RMB fly
    static constexpr float kPlayerBase = 25.0f;   ///< units/second, the free camera

    /// `n` into 1..32 (a non-integer never reaches here — the verb refuses it).
    static int clamp(int n);

    /// The dial's current value, 1..32.
    static int value();
    /// Sets it (clamped) and persists it.
    static void setValue(int n);
    /// Moves it by `delta` (the wheel's +/-1, Shift's +/-5) and returns the
    /// value that resulted.
    static int step(int delta);

    /// n / 10 — what every surface multiplies its own base by.
    static float factor();
    /// `base` * factor(), the one spelling of the multiplication.
    static float applyTo(float base);

    /// The editor's RMB fly, units/second (before Shift's boost).
    static float editorSpeed();
    /// The Player's free camera, units/second.
    static float playerSpeed();

    /// Loads the persisted value and writes every future set through
    /// `settings`. Nullable (unbinds). Not owned.
    static void bindSettings(QSettings *settings);

    /// Back to the default, the persisted value cleared. Mainly for tests.
    static void reset();
};

#endif // CAMERASPEED_H
