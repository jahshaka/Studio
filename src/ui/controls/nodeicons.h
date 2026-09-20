/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef NODEICONS_H
#define NODEICONS_H

// ONE ICON SOURCE FOR A SCENE NODE (COMPONENTS-1).
//
// The outliner's type icons, its eye and its padlock were a table and four
// members inside SceneHierarchyWidget. The Properties column's Components
// section draws the same rows for the same nodes, and a second table would
// have been two pictures of one idea drifting apart the first time a node type
// was added. This is that table, moved out whole; the hierarchy widget reads it
// like everybody else.
//
// EVERY ICON HERE IS LEAKED ON PURPOSE, as they were before: a QIcon holds
// QPixmaps, and a QPixmap destroyed after QApplication is gone (which is when a
// function-local static's destructor runs) is the classic Qt shutdown crash.
// One set for the process, never destroyed. QIcon is implicitly shared, so
// handing the same one to a thousand rows costs one pixmap pair.

#include <QIcon>

#include "irisgl/document/scenegraph/scenenode.h"

namespace nodeicons {

/// The icon for a node KIND — mesh, light, emitter, empty, decal, camera. A
/// type with no picture of its own answers with an empty icon (drawn as blank,
/// never as a missing-file box).
const QIcon &forType(iris::SceneNodeType type);

/// The eye. `visible` is the node's OWN flag (what the outliner's eye edits),
/// never the inherited state.
const QIcon &visibility(bool visible);

/// The padlock. THE LOCK IS `pickable`: a locked node cannot be picked in the
/// viewport and refuses a drop.
const QIcon &lock(bool locked);

}   // namespace nodeicons

#endif   // NODEICONS_H
