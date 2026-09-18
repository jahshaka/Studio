/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef GIZMOMODE_H
#define GIZMOMODE_H

// gizmomode — SETTING THE TRANSFORM GIZMO'S MODE, in one place.
//
// "translate" | "rotate" | "scale" is a name three surfaces say: the W/E/R keys
// and the Space cycle (MainWindow's own slots), `editor.setGizmoMode` from a
// script, and — since VR phase 4b stage 2 — a SHORT press of the wearer's
// `menu` button. They must all take the SAME route, because the route is what
// makes the toolbar's checked state follow: through the shell's slot where
// there is a shell, and straight to the viewport where there is not (a headless
// run, a stand-in).
//
// It lived twice before this header: once inside EditorApi::setGizmoMode and
// once, spelled out again, in the VR module's wiring — two copies of the same
// three slot names, which is exactly how a mode set from the headset would have
// stopped moving the toolbar the day somebody renamed one of them.

#include <QMetaObject>
#include <QObject>
#include <QString>

#include "viewport/ieditorviewport.h"

namespace gizmomode {

/// The three modes, in the order a cycle takes them — the Space key's order.
inline bool isKnown(const QString &mode)
{
    return mode == QLatin1String("translate") || mode == QLatin1String("rotate") ||
           mode == QLatin1String("scale");
}

inline QString next(const QString &mode)
{
    if (mode == QLatin1String("translate")) return QStringLiteral("rotate");
    if (mode == QLatin1String("rotate")) return QStringLiteral("scale");
    return QStringLiteral("translate");
}

/// Applies a mode. `shell` is the main window (nullable — a headless host has
/// none); `viewport` the editor viewport (nullable for the same reason). False
/// for an unknown mode, or when there is nothing at all to apply it to; the
/// CALLER owns the error message, because the two callers word it differently
/// (a verb names what was passed, a button press has nobody to tell).
inline bool apply(QObject *shell, IEditorViewport *viewport, const QString &mode)
{
    if (!isKnown(mode)) return false;
    // THROUGH THE SHELL'S OWN SLOT when there is a shell, so the toolbar's
    // checked state follows exactly as it does for the keys.
    const char *slot = mode == QLatin1String("rotate")  ? "rotateGizmo"
                     : mode == QLatin1String("scale")   ? "scaleGizmo"
                                                        : "translateGizmo";
    if (shell && QMetaObject::invokeMethod(shell, slot)) return true;
    if (!viewport) return false;
    if (mode == QLatin1String("rotate")) viewport->setGizmoRot();
    else if (mode == QLatin1String("scale")) viewport->setGizmoScale();
    else viewport->setGizmoLoc();
    return true;
}

}   // namespace gizmomode

#endif   // GIZMOMODE_H
