/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef EDITORVRCONTROLLER_H
#define EDITORVRCONTROLLER_H

#include "../irisgl/src/irisglfwd.h"
#include "cameracontrollerbase.h"
#include <QMatrix4x4>

class EditorVrController : public CameraControllerBase
{
public:
    iris::Mesh* leftHandMesh;
    iris::Mesh* rightHandMesh;

    iris::RenderItem* leftHandRenderItem;
    iris::RenderItem* rightHandRenderItem;

    iris::Mesh* beamMesh;
    iris::RenderItem* leftBeamRenderItem;
    iris::RenderItem* rightBeamRenderItem;

    iris::VrDevice* vrDevice;

    iris::SceneNodePtr leftPickedNode;
    iris::SceneNodePtr rightPickedNode;

    // These are the offsets from the controllers to the
    // selected object
    QMatrix4x4 leftNodeOffset;
    QMatrix4x4 rightNodeOffset;

    //iris::SceneNodePtr tracker;
    iris::ScenePtr scene;

    EditorVrController();

    void setScene(iris::ScenePtr scene)
;
    void updateCameraRot();

    void update(float dt);

private:
    /*
     * Does a ray cast to the scene
     * Returns nearest object hit in the raycast
     */
    bool rayCastToScene(QMatrix4x4 handMatrix, iris::PickingResult& result);
};

#endif // EDITORVRCONTROLLER_H
