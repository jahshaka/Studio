/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef NODENAMING_H
#define NODENAMING_H

// nodenaming — what a COPY of a scene node is called (owner decision
// 2026-09-09, the Unreal rule): a numeric suffix and NO space.
//
//     Cube  -> Cube2 -> Cube3 -> Cube4 ...
//
// never "Cube Copy", never "Cube.001". Uniqueness is among SIBLINGS — two
// nodes under different parents may share a name, exactly as two files in
// different folders may — because that is the scope the outliner shows side by
// side and the scope a user reads as "the same thing twice".
//
// ONE rule for Duplicate (SceneEditService::duplicateNode) and for Paste
// (SceneEditService::insertFragment, which node.deserialize also goes
// through), because a copy is a copy however it was made.
//
// A name is only changed when it is actually TAKEN. Pasting into a parent that
// has no node of that name keeps the name — the suffix exists to disambiguate,
// not to mark a node as second-hand.

#include <QString>

#include "irisgl/irisglfwd.h"

namespace nodenaming {

/// Splits a trailing decimal run off `name`. "Cube12" -> ("Cube", 12);
/// "Cube" -> ("Cube", 0). Leading zeros are NOT preserved ("Cube007" -> 7):
/// the suffix is a count, not a format.
void splitNumericSuffix(const QString &name, QString &stem, int &number);

/// `desired`, or `desired` with the next free numeric suffix, such that no
/// child of `parent` other than `except` already carries it. A null `parent`
/// (a node with no siblings yet) returns `desired` unchanged.
///
/// The first candidate is max(2, n + 1) where n is the number already on the
/// name, so "Cube" -> "Cube2" (never "Cube1") and "Cube2" -> "Cube3".
QString uniqueSiblingName(const iris::SceneNodePtr &parent, const QString &desired,
                          const iris::SceneNode *except = nullptr);

} // namespace nodenaming

#endif // NODENAMING_H
