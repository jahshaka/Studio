#pragma once
// THE v1 INTERIM, in one place (SPECS/SCENEGRAPH_SPEC.md §3 / audit §3.10).
//
// Since the scene-graph swap an `iris::SceneNode` IS an `Ogre::SceneNode`: the
// document has no transforms, no hierarchy and no storage of its own. So a
// suite that only builds a DOCUMENT — no rendering, no pixels, no view — still
// needs an engine to exist, because that is where its nodes live.
//
// This boots one offscreen: a real Vulkan engine with a surfaceless window and
// no view at all. It needs a reachable X display (Ogre's VulkanXcbSupport
// connects at plugin load) and a Vulkan driver, exactly like every other engine
// suite; `QT_QPA_PLATFORM=offscreen` still applies, nothing is ever shown.
//
// v2 replaces this with `RenderSystem_NULL`, which the research spike verified
// works with no display and no GPU at all (spikes/scenegraph-null-rs). When it
// lands, the fixture keeps its shape and only its render system changes — which
// is why every suite takes it as one object rather than open-coding a boot.
//
// LIFETIME: declare it FIRST in main(), so it is destroyed LAST. Every document
// object in the process holds handles into its scene managers.

#include <cstdio>
#include <memory>
#include <string>

#include "irisgl/document/scenegraph/nodegraph.h"
#include "jahshaka/engine/Engine.h"

namespace enginetest {

class DocumentGraph {
public:
    explicit DocumentGraph(const char *logFile = "document-graph-ogre.log")
    {
        jahshaka::engine::EngineConfig cfg;
        cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
        cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
        cfg.logFile = logFile;
        mEngine = jahshaka::engine::Engine::create(cfg, mError);
        if (mEngine)
            iris::graph::setStagingScene(reinterpret_cast<iris::graph::SceneHandle>(
                mEngine->documentGraphScene()));
    }

    ~DocumentGraph()
    {
        iris::graph::setStagingScene(nullptr);
        mEngine.reset();
    }

    DocumentGraph(const DocumentGraph &) = delete;
    DocumentGraph &operator=(const DocumentGraph &) = delete;

    bool ok() const { return mEngine != nullptr; }
    const std::string &error() const { return mError; }
    jahshaka::engine::Engine *engine() const { return mEngine.get(); }

    /// Boot or die, with the reason on stderr. Suites call this in main().
    bool require() const
    {
        if (mEngine) return true;
        std::fprintf(stderr,
                     "FAIL: the document scene graph needs an engine (SCENEGRAPH_SPEC v1 "
                     "interim) and it would not start: %s\n",
                     mError.c_str());
        return false;
    }

private:
    std::unique_ptr<jahshaka::engine::Engine> mEngine;
    std::string mError;
};

}  // namespace enginetest
