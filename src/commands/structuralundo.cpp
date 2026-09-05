/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "commands/structuralundo.h"

#include <functional>

#include "irisgl/document/scenegraph/scenenode.h"
#include "services/sceneeditservice.h"
#include "services/services.h"

namespace structuralundo
{

bool liveIsUsable(const iris::SceneNodePtr &live, const SceneFragment &snapshot)
{
    if (!live) return false;
    // A handle with no graph node is a node whose Ogre side was destroyed —
    // re-attaching it would put a corpse in the tree.
    if (!live->graphNode()) return false;
    // Already somewhere: something else has claimed it since (a script that
    // held the subtree, a second command). The snapshot is then the only
    // honest copy of where it used to be.
    if (live->hasParent()) return false;
    // And it must still be the node the snapshot describes. The guid is the
    // document's identity and survives everything the graph does to a node.
    const QString capturedGuid = snapshot.node.value(QStringLiteral("guid")).toString();
    if (!capturedGuid.isEmpty() && live->getGUID() != capturedGuid) return false;
    return true;
}

iris::SceneNodePtr reinstate(StudioServices *services,
                             const iris::SceneNodePtr &parent,
                             const iris::SceneNodePtr &live,
                             const SceneFragment &snapshot,
                             int index)
{
    if (!parent) return iris::SceneNodePtr();

    if (liveIsUsable(live, snapshot)) {
        // keepTransform = FALSE. The node's local TRS is the one it had under
        // this very parent; recomposing it against the parent's world matrix
        // would move it (and, on a non-uniformly scaled parent, rotate it).
        parent->insertChild(index, live, false);
        return live;
    }

    if (!services || !services->sceneEdit || snapshot.isNull()) return iris::SceneNodePtr();

    auto rebuilt = services->sceneEdit->rebuildFragment(snapshot);
    if (!rebuilt) return iris::SceneNodePtr();
    parent->insertChild(index, rebuilt, false);
    return rebuilt;
}

} // namespace structuralundo
