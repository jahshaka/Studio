// Link stubs: the gizmo classes reach into Studio's undo stack and property panel
// from createUndoAction(). The test never drags, so none of this is exercised.
#include <QUndoCommand>
#include "irisgl/irisglfwd.h"
#include "shell/uimanager.h"
#include "commands/transformscenenodecommand.h"
#include "ui/panels/scenenodepropertieswidget.h"

SceneNodePropertiesWidget *UiManager::propertyWidget = nullptr;
void UiManager::pushUndoStack(QUndoCommand *cmd) { delete cmd; }
void SceneNodePropertiesWidget::refreshTransform() {}
TransformSceneNodeCommand::TransformSceneNodeCommand(iris::SceneNodePtr, QVector3D, QQuaternion, QVector3D) {}

void TransformSceneNodeCommand::undo() {}
void TransformSceneNodeCommand::redo() {}
