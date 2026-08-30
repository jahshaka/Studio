/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SNAPSETTINGS_H
#define SNAPSETTINGS_H

// SnapSettings — the editor-global snap sizes (EDITOR_SHORTCUTS_SPEC §4),
// Unreal-style: one translate size (world units — also the ground grid's
// spacing), one rotate size (degrees), one scale size. Replaces the three
// per-gizmo DEFAULT_SNAP_LENGTH #defines.
//
// Static on purpose: the gizmos' Ctrl-snap runs deep inside drag math with no
// services plumbed through; the values live here in-process and, when a
// QSettings is bound (the app does at startup), persist as snap/translate,
// snap/rotate, snap/scale. Unit tests run unbound and get pure defaults.

#include <QVector>

class QSettings;

class SnapSettings
{
public:
    static float translateSize();   // default 1.0, clamped 0.01..100
    static float rotateSize();      // default 10 degrees, clamped 0.1..180
    static float scaleSize();       // default 0.25, clamped 0.01..10

    static void setTranslateSize(float size);
    static void setRotateSize(float size);
    static void setScaleSize(float size);

    /// The [ / ] step lists (spec: translate 0.1/0.25/0.5/1/5/10).
    static const QVector<float> &translateSteps();
    static const QVector<float> &rotateSteps();
    static const QVector<float> &scaleSteps();

    /// The next step above (direction > 0) or below (direction < 0) `current`
    /// in `steps`; clamps at the ends. A `current` off the list moves to the
    /// nearest step in the requested direction.
    static float stepped(const QVector<float> &steps, float current, int direction);

    /// Loads persisted values from `settings` and writes every future set
    /// through it. Nullable (unbinds). Not owned.
    static void bindSettings(QSettings *settings);

    /// Back to defaults, persisted values cleared. Mainly for tests.
    static void reset();
};

#endif // SNAPSETTINGS_H
