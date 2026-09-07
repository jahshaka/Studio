/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef FLYSPEEDSETTINGS_H
#define FLYSPEEDSETTINGS_H

// FlySpeedSettings — THE camera fly speed, Unreal's model (owner request
// 2026-09-07): a MULTIPLIER on a fixed base speed, chosen from a step list in
// the toolbar or stepped with the scroll wheel while flying, and persisted.
//
// Two independent surfaces, because they are two different jobs at two
// different scales: the editor explorer flies at 8 u/s and the player's free
// camera at 25 u/s (both measured, both unchanged as BASES — a fresh install
// with the default 1.0x multiplier flies exactly as it did before this
// existed).
//
// Static, and for the same reason SnapSettings is: the two camera controllers
// this feeds sit deep inside per-frame update math with no services plumbed
// through. When a QSettings is bound (the app binds one at startup, beside the
// snap sizes) the multipliers persist as camera/flySpeedEditor and
// camera/flySpeedPlayer; unit tests run unbound and get pure defaults.

#include <QVector>

class QSettings;

class FlySpeedSettings
{
public:
    enum Surface { Editor = 0, Player = 1 };

    /// The dropdown / wheel step list: 0.25x .. 8x, Unreal's doubling ladder.
    static const QVector<float> &steps();

    /// Units per second at 1.0x — Editor 8, Player 25. Not user-settable:
    /// the multiplier IS the user-facing control.
    static float baseSpeed(Surface surface);

    /// Current multiplier (default 1.0, clamped to 0.05..32).
    static float multiplier(Surface surface);
    static void  setMultiplier(Surface surface, float multiplier);

    /// baseSpeed * multiplier — what a controller actually flies at.
    static float speed(Surface surface);

    /// Steps the multiplier one entry up (direction > 0) or down (direction <
    /// 0) the list, clamping at the ends, and returns the value that resulted.
    /// This is what the scroll wheel and the toolbar arrows call.
    static float step(Surface surface, int direction);

    /// Loads persisted values and writes every future set through `settings`.
    /// Nullable (unbinds). Not owned.
    static void bindSettings(QSettings *settings);

    /// Back to defaults, persisted values cleared. Mainly for tests.
    static void reset();
};

#endif // FLYSPEEDSETTINGS_H
