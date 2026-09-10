/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef LOOKSTACKEDITOR_H
#define LOOKSTACKEDITOR_H

// THE ORDERED LOOKS-STACK EDITOR, once (SPECS/POST_LOOKS_SPEC.md §4.1).
//
// Two panels edit a looks stack — the World panel's Post Process section (the
// scene's stack) and the camera panel's (a camera's whole-stack override) — and
// they are the SAME editor over two different arrays. This is that editor,
// written once, as a free function that draws into whichever accordion blade
// calls it.
//
// It is not a widget class: there is no state to own. The stack is a QJsonArray
// that lives on the document, the caller says how to read it and how to write
// it back, and every row is rebuilt from the array each time.
//
// GENERATED FROM THE TWO CATALOGUE TABLES, like everything else in this
// program: the document's (irisgl/document/scenegraph/looks.h) for which looks
// exist, their parameters, ranges and defaults, and services/looks.h for the
// labels and tooltips. This file names no look, no parameter and no range.

#include <QJsonArray>
#include <QString>

#include <functional>

class AccordianBladeWidget;

namespace lookstack
{

/// Draws the stack editor into `blade`.
///
/// `stack` is the array as it stands (it is normalised here, so a caller may
/// pass whatever the document holds). `write` receives the NEW array and a flag
/// saying whether the panel has to be rebuilt: false while a parameter is being
/// scrubbed — rebuilding under the cursor would destroy the widget being
/// dragged — and true for anything that changes which rows exist.
/// `gestureEnd` (optional) fires when a parameter SCRUB finishes — the drag's
/// mouse-up or the typed commit. A caller that records undo steps needs it:
/// `write` streams a value per scrubbed pixel, and one undo step per pixel is
/// not an undo history (debt L6). Nothing else in the editor is a gesture: every
/// other change is structural and arrives once.
void build(AccordianBladeWidget *blade, const QJsonArray &stack,
           const std::function<void(const QJsonArray &, bool)> &write,
           const std::function<void()> &gestureEnd = {});

}   // namespace lookstack

#endif   // LOOKSTACKEDITOR_H
