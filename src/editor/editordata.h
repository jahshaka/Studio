#ifndef EDITORDATA_H
#define EDITORDATA_H

#include "../irisgl/src/irisglfwd.h"

class EditorData
{
public:
    iris::CameraNodePtr editorCamera;
    float distFromPivot;
};

#endif // EDITORDATA_H
