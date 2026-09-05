#pragma once
// Link stubs for commands.structural_undo — see test_stubs.cpp.

#include "irisgl/irisglfwd.h"

class SceneEditService;

namespace teststubs
{

/// A SceneEditService the suite can hand to a command. Every method the
/// commands call is stubbed; nothing else is reachable.
SceneEditService *sceneEditService();

/// What the stubbed `rebuildFragment` will return next (and then clear). This
/// is how the suite drives structuralundo::reinstate's REBUILD branch without
/// linking the serializer, the asset store and Sql.
extern iris::SceneNodePtr nextRebuild;

} // namespace teststubs
