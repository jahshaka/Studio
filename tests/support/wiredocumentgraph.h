#pragma once
// enginetest::wireDocumentGraph — the host wiring a document needs from an engine, in
// ONE place for the test tree (it mirrors src/bridge/enginehost.cpp, which the app keeps
// for itself): the document's nodes live in the engine's staging scene manager
// (SCENEGRAPH_SPEC D2), and the renderer learns "nothing moved" from the document's
// transform-write counter instead of re-scanning every item every frame. Call it right
// after Engine::create and before the first document node; unwire (null) before the
// engine is destroyed.
#include "irisgl/document/scenegraph/nodegraph.h"
#include "jahshaka/engine/Engine.h"

namespace enginetest {

inline void wireDocumentGraph(jahshaka::engine::Engine *engine)
{
    iris::graph::setStagingScene(reinterpret_cast<iris::graph::SceneHandle>(engine->documentGraphScene()));
    engine->setTransformWriteCounter(&iris::graph::transformWriteCounter());
}

inline void unwireDocumentGraph() { iris::graph::setStagingScene(nullptr); }

}   // namespace enginetest
