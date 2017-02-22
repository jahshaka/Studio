#ifndef EDITORVRCONTROLLER_H
#define EDITORVRCONTROLLER_H

#include "../irisgl/src/irisglfwd.h"
#include "cameracontrollerbase.h"

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

    //iris::SceneNodePtr tracker;
    iris::ScenePtr scene;

    EditorVrController();

    void setScene(iris::ScenePtr scene)
;
    void updateCameraRot();

    void update(float dt);

private:
    /*
     * Does a ray casy to the scene
     * Returns nearest object hit in the raycast
     */
    float rayCastToScene(QMatrix4x4 handMatrix);
};

#endif // EDITORVRCONTROLLER_H
