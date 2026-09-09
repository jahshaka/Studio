// Link stubs for gizmo.group_transform. Unlike tests/gizmo/test_stubs.cpp this
// suite uses the REAL undo service and the REAL TransformSceneNodeCommand (the
// undo shape is what it measures) — only the scene-edit notification, which
// needs half the io/ layer to link and does nothing without a UI, is stubbed.
#include "services/sceneeditservice.h"

void SceneEditService::notifyTransformChanged() {}
