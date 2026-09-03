/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef PLANARREFLECTORS_H
#define PLANARREFLECTORS_H

// Marking a node a planar reflection plane, once (PLANAR_REFLECTIONS_SPEC.md §7).
//
// WHY THIS IS A SERVICE AND NOT TWO COPIES OF FOUR LINES: setting the flag is
// the easy half. The hard half is that only the RENDERER can say whether the
// geometry can BE a plane — the plane, its size and its normal are derived from
// the mesh's bounds, and the document model carries a bounding SPHERE, which
// cannot tell a plate from a ball. So the flag has to be pushed, the frame
// stepped, the renderer asked, and the flag reverted with an explanation when
// the answer is no. node.setPlanarReflector, the Properties row and the
// "Make Reflective" context action all need exactly that, and a second copy of
// it would be the one that forgets to revert.

#include <QString>

#include "irisgl/irisglfwd.h"

class IEditorViewport;

namespace planarreflectors {

/// Sets (or clears) the node's reflector flag. Turning it ON is validated
/// against the live renderer: on refusal the flag is put back, `error` is
/// filled with a message meant for a user, and false is returned. With no
/// engine viewport the flag is simply set — a caller must not report a failure
/// it cannot actually see (headless runs, document-only stand-in viewports).
bool set(const iris::SceneNodePtr &node, bool enabled, IEditorViewport *viewport,
         QString *error = nullptr);

}   // namespace planarreflectors

#endif   // PLANARREFLECTORS_H
