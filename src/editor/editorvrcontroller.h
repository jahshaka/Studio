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

    iris::VrDevice* vrDevice;

    //iris::SceneNodePtr tracker;
    iris::ScenePtr scene;

    EditorVrController();

    void setScene(iris::ScenePtr scene)
;
    void updateCameraRot();

    void update(float dt);
};

#endif // EDITORVRCONTROLLER_H
