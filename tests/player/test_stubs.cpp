// Link stubs: PlayBack's controllers reach into Studio's globals (UiManager, the
// undo stack). The test drives the mouse controller only; none of this is exercised.
#include <QUndoCommand>
#include "irisgl/irisglfwd.h"
#include "uimanager.h"
#include "commands/transfrormscenenodecommand.h"

IEditorViewport *UiManager::sceneViewWidget = nullptr;
SceneMode UiManager::sceneMode = SceneMode::PlayMode;
bool UiManager::isSimulationRunning = false;
void UiManager::pushUndoStack(QUndoCommand *cmd) { delete cmd; }
TransformSceneNodeCommand::TransformSceneNodeCommand(iris::SceneNodePtr, QVector3D, QQuaternion, QVector3D) {}
void TransformSceneNodeCommand::undo() {}
void TransformSceneNodeCommand::redo() {}

// SettingsManager's inline constructor (src/core/settingsmanager.h:47) resolves its
// settings file against Globals::appWorkingDir. Nothing else in Globals is reached.
#include "globals.h"
QString Globals::appWorkingDir;
