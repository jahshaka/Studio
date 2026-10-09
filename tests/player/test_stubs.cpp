// Link stubs: PlayBack's controllers can push undo commands through the
// services. The test drives the mouse controller only; none of this is
// exercised, so the command is a do-nothing stand-in.
#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include <QUndoCommand>
#include "irisgl/irisglfwd.h"
#include "commands/transformscenenodecommand.h"

TransformSceneNodeCommand::TransformSceneNodeCommand(iris::SceneNodePtr, iris::Vec3, iris::Quat, iris::Vec3) {}
void TransformSceneNodeCommand::undo() {}
void TransformSceneNodeCommand::redo() {}

// THE FRAME MONITOR'S HOST STAGE (PlayerVr::step wraps its vrWaitFrame in
// `host.vr_wait`, lane VR-REORDER-1): no capture runs in these binaries, and
// the real scope's translation unit pulls the app's whole service layer in.
#include "services/framemonitor.h"

framemonitor::Stage::Stage(const char *name) : mName(name) {}
framemonitor::Stage::~Stage() {}
