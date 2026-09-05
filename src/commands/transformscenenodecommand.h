/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef TRANSFRORMSCENENODECOMMAND_H
#define TRANSFRORMSCENENODECOMMAND_H

#include "irisgl/core/math/mat4.h"
#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include "commands/staticstate.h"
#include "commands/studiocommand.h"
#include "irisgl/irisglfwd.h"

/// Moves/rotates/scales one node, undoably.
///
/// UNDO v1.5 (scripting audit F3): it also carries the subtree's SCENE_STATIC
/// classification. Writing a transform DEMOTES a static subtree by design
/// (SCENEGRAPH_SPEC §6 rule 4), and undoing the write is itself a transform
/// write — so without this, one nudge-and-undo left the ground and every prop
/// permanently dynamic, and (since the user override is what the serializer
/// writes) permanently so across saves. See commands/structuralundo.h.
class TransformSceneNodeCommand : public StudioCommand
{
    //iris::Mat4 oldTransform;
    //iris::Mat4 newTransform;
	iris::Vec3 oldPos, oldScale;
	iris::Quat oldRot;

	iris::Vec3 newPos, newScale;
	iris::Quat newRot;

    iris::SceneNodePtr sceneNode;
    /// The SCENE_STATIC classification of the subtree before the move, captured
    /// on the first redo (the last moment it is still intact).
    structuralundo::StaticState staticBefore;
    /// ...and after it, so a redo does not silently re-promote what the move
    /// demoted.
    structuralundo::StaticState staticAfter;
public:

    TransformSceneNodeCommand(iris::SceneNodePtr node, iris::Mat4 localTransform);
	TransformSceneNodeCommand(iris::SceneNodePtr node, iris::Vec3 pos, iris::Quat rot, iris::Vec3 scale);
	TransformSceneNodeCommand(iris::SceneNodePtr node,
							  iris::Vec3 oldPos, iris::Quat oldRot, iris::Vec3 oldScale,
							  iris::Vec3 newPos, iris::Quat newRot, iris::Vec3 newScale);
    void undo() override;
    void redo() override;
};

#endif // TRANSFRORMSCENENODECOMMAND_H
