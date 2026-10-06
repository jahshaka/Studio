/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef PHYSICSCONSTRAINTS_H
#define PHYSICSCONSTRAINTS_H

// THE CONSTRAINT RULE, in one place (audit D5). node.addConstraint and the
// outliner's Physics > Add Constraint menu both call add(); neither carries a
// copy of the rule. The menu it replaced built a combo box per menu open (never
// freed), appended one constraint per combo CHANGE (browsing the list stacked
// several), offered a "null" row that stored a constraint to nothing, listed
// only the root's direct children and recorded no undo step.

#include <QString>
#include <QStringList>
#include <QVariantList>

#include "irisgl/irisglfwd.h"

class UndoService;

namespace physicsconstraints {

/// The type names a caller may pass: "ball", "dof6".
QStringList typeNames();

/// Joins `from` to `to` with a constraint of `type` (a typeNames() entry),
/// stored on `from` and built when a simulation starts. Refused, with `error`
/// set and NOTHING written, when a type is unknown, either node is not a
/// physics body, the two are the same node, or `from` already has that type
/// of constraint to `to`. With an `undo` service the edit is one undo step.
bool add(const iris::SceneNodePtr &from, const iris::SceneNodePtr &to, const QString &type,
         UndoService *undo, QString *error);

/// `node`'s constraints as `[{to, type}]`, type by name.
QVariantList describe(const iris::SceneNodePtr &node);

}   // namespace physicsconstraints

#endif   // PHYSICSCONSTRAINTS_H
