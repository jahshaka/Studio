/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/planarreflectors.h"

#include <QObject>

#include "irisgl/document/scenegraph/scenenode.h"
#include "viewport/ieditorviewport.h"

namespace planarreflectors {

void set(const iris::SceneNodePtr &node, bool enabled, IEditorViewport *viewport, QObject *owner, Done done)
{
    if (node.isNull()) {
        if (done) done(false, QObject::tr("No object selected."));
        return;
    }
    const bool was = node->getPlanarReflector();
    node->setPlanarReflector(enabled);
    if (!enabled || was == enabled || !viewport) {
        if (done) done(true, QString());
        return;
    }
    // THE RENDERER'S ANSWER, ON ITS COMPLETION: the mirror pushes the flag at the next sync, the frame that applies it
    // rebuilds the arm, and the facts of the frame after it say whether the arm took. Never a frame stepped here.
    viewport->afterApplied(owner, [node, viewport, done]() {
        if (viewport->planarReflectorAccepted(node)) {
            if (done) done(true, QString());
            return;
        }
        node->setPlanarReflector(false);   // the next sync takes it back out
        if (done)
            done(false, QObject::tr("'%1' is not flat enough to be a reflection plane — its thinnest "
                                    "extent must be under a tenth of the next. Use a plane or a thin box.")
                            .arg(node->getName()));
    });
}

}   // namespace planarreflectors
