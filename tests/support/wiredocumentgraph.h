#pragma once
// enginetest::wireDocumentGraph — the host wiring a document needs from an engine, in
// ONE place for the test tree (it mirrors src/bridge/enginehost.cpp, which the app keeps
// for itself): the document's nodes live in the engine's staging scene manager
// (SCENEGRAPH_SPEC D2). (What moved reaches the renderer through each frame's ChangeSet,
// which a mirror drains from the document's write journal — U0 3b; nothing to wire.) Call it right
// after Engine::create and before the first document node; unwire (null) before the
// engine is destroyed.
#include "irisgl/document/scenegraph/nodegraph.h"
#include "jahshaka/engine/Engine.h"

namespace enginetest {

inline void wireDocumentGraph(jahshaka::engine::Engine *engine)
{
    iris::graph::setStagingScene(reinterpret_cast<iris::graph::SceneHandle>(engine->documentGraphScene()));
}

inline void unwireDocumentGraph() { iris::graph::setStagingScene(nullptr); }

}   // namespace enginetest
