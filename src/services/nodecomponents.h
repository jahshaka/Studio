/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef NODECOMPONENTS_H
#define NODECOMPONENTS_H

// THE PARTS OF A GROUPED NODE (COMPONENTS-1, owner review R14).
//
// An imported model arrives as ONE node with its meshes hanging off it, and
// those children are `attached` — the outliner deliberately does not draw them
// (SceneHierarchyWidget::isAssetPart / editor.outlinerRows), because a model
// with two hundred parts would bury the scene. That is the right rule for the
// outliner and it left the parts unreachable: to give one of them its own
// material, hide it, or look at where it sits, there was nowhere to click.
//
// So: ONE definition of "the parts of this node", read by the `node.components`
// verb and by the Properties column's Components section alike. Neither owns
// it, so the list a script sees and the list the user clicks cannot drift.
//
// The list is the node's DESCENDANTS in document pre-order (the order the scene
// writer walks: a parent before its children, siblings by index) with the depth
// each one sits at — 1 for a direct child. The node itself is never in it, and
// nothing here filters by `attached`: a group the user built by hand out of
// plain children is the same thing to a person, and the section is just as
// useful for it.

#include <QVector>

#include "irisgl/irisglfwd.h"
#include "irisgl/document/scenegraph/scenenode.h"

namespace nodecomponents {

struct Part
{
    iris::SceneNodePtr node;
    /// How far below the node the list was asked about — 1 for a direct child.
    int depth = 1;
};

inline void collect(const iris::SceneNodePtr &node, int depth, QVector<Part> &out)
{
    if (!node) return;
    const int kids = node->childCount();
    for (int i = 0; i < kids; ++i) {
        iris::SceneNode *raw = node->childAt(i);
        if (!raw) continue;
        const iris::SceneNodePtr child = raw->sharedFromThis();
        out.append(Part{ child, depth });
        collect(child, depth + 1, out);
    }
}

/// The node's parts, flattened, pre-order. Empty for a leaf — which is exactly
/// how both callers decide the section does not apply.
inline QVector<Part> partsOf(const iris::SceneNodePtr &node)
{
    QVector<Part> out;
    collect(node, 1, out);
    return out;
}

}   // namespace nodecomponents

#endif   // NODECOMPONENTS_H
