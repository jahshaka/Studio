/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// The SCENE_STATIC half of undo, in a translation unit of its OWN and with no
// dependency on the service layer: TransformSceneNodeCommand needs it, and two
// test targets compile that command straight into themselves without a
// StudioServices anywhere in sight (tests/ui, tests/importer). A document-only
// concern gets a document-only TU.

#include "commands/staticstate.h"

#include <functional>

#include "irisgl/document/scenegraph/scenenode.h"

namespace structuralundo
{

namespace {

/// Pre-order over the DOCUMENT children (childAt skips the engine's own helper
/// nodes and can answer null — a walk must not assume the range is dense).
void walk(const iris::SceneNodePtr &node, const std::function<void(iris::SceneNode *)> &fn)
{
    if (!node) return;
    fn(node.data());
    const int kids = node->childCount();
    for (int i = 0; i < kids; ++i)
        if (iris::SceneNode *c = node->childAt(i)) walk(c->sharedFromThis(), fn);
}

} // namespace

StaticState captureStatic(const iris::SceneNodePtr &node)
{
    StaticState state;
    walk(node, [&](iris::SceneNode *n) {
        state.hints.append(n->staticHint());
        state.overrides.append(static_cast<quint8>(n->staticOverride()));
    });
    return state;
}

void restoreStatic(const iris::SceneNodePtr &node, const StaticState &state)
{
    if (state.isEmpty()) return;
    int i = 0;
    walk(node, [&](iris::SceneNode *n) {
        if (i >= state.hints.size()) return;      // the subtree grew since the capture
        n->_setStaticOverride(static_cast<iris::StaticOverride>(state.overrides[i]));
        // _applyStaticHint, not setStaticHint: restoring a classification is not
        // the user deciding one, and stamping an override here would write the
        // derived policy into the file on the next save.
        //
        // TOP-DOWN, which the pre-order walk already is: a node can only be made
        // static once its parent is (rule 2), and this is the order that lets a
        // whole restored branch come back rather than only its root.
        n->_applyStaticHint(state.hints[i]);
        ++i;
    });
}

} // namespace structuralundo
