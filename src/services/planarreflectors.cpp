/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/planarreflectors.h"

#include "irisgl/document/scenegraph/scenenode.h"
#include "viewport/ieditorviewport.h"

namespace planarreflectors {

bool set(const iris::SceneNodePtr &node, bool enabled, IEditorViewport *viewport, QString *error)
{
    if (node.isNull()) {
        if (error) *error = QObject::tr("No object selected.");
        return false;
    }
    const bool was = node->getPlanarReflector();
    node->setPlanarReflector(enabled);
    if (!enabled || was == enabled || !viewport) return true;

    // Two frames: one for SceneMirror to push the flag, one for the arm to have
    // been rebuilt when we read the answer back.
    viewport->renderFrames(2);
    if (viewport->planarReflectorAccepted(node)) return true;

    node->setPlanarReflector(false);
    viewport->renderFrames(1);
    if (error)
        *error = QObject::tr("'%1' is not flat enough to be a reflection plane — its thinnest "
                             "extent must be under a tenth of the next. Use a plane or a thin box.")
                     .arg(node->getName());
    return false;
}

}   // namespace planarreflectors
