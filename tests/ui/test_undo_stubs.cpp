/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// Link stubs for the panel suites that now carry the UNDO spine (debt L6).
//
// Every properties row is undoable, so the panel slices link
// SetNodePropertyCommand — which, for the three transform keys, restores a
// node's SCENE_STATIC classification through structuralundo. That one path
// reaches SceneEditService::rebuildFragment, i.e. the whole document-fragment
// stack, for a case no widget suite can produce (a panel row is never a
// reparent). Stubbed here so the suites stay widgets + a document, exactly as
// the other stubs in this directory do.

#include "irisgl/irisglfwd.h"
#include "services/sceneeditservice.h"

iris::SceneNodePtr SceneEditService::rebuildFragment(const SceneFragment &) const
{
    return iris::SceneNodePtr();
}
