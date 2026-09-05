#pragma once
// THE DOCUMENT-ONLY ENGINE, in one place (SPECS/SCENEGRAPH_SPEC.md §3b, v2).
//
// Since the scene-graph swap an `iris::SceneNode` IS an `Ogre::SceneNode`: the
// document has no transforms, no hierarchy and no storage of its own. So a
// suite that only builds a DOCUMENT — no rendering, no pixels, no view — still
// needs an engine to exist, because that is where its nodes live.
//
// This boots one HEADLESS: `EngineConfig::headless`, i.e. Ogre's NULL render
// system. It opens no display, needs no GPU and no driver, and creates no
// window beyond the 1x1 surfaceless one the render system makes for itself.
// Suites built on it pass with DISPLAY unset — verified in the gate by running
// them under `env -u DISPLAY`.
//
// (Until 2026-09-06 this booted Vulkan offscreen — the v1 interim — which meant
// every document suite still needed a reachable X display and a working driver.
// The fixture's shape is unchanged because that was always the plan: only the
// render system moved.)
//
// WHAT IT CANNOT DO: render. createView() and createOffscreenView() both refuse
// with a reason (Types.h EngineConfig::headless). A suite that wants pixels is
// not a document suite — it boots the real engine like every other engine suite.
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
        cfg.headless = true;   // RenderSystem_NULL: no display, no device, no window
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
                     "FAIL: the document scene graph needs an engine (SCENEGRAPH_SPEC D2) and "
                     "the headless one would not start: %s\n",
                     mError.c_str());
        return false;
    }

private:
    std::unique_ptr<jahshaka::engine::Engine> mEngine;
    std::string mError;
};

}  // namespace enginetest
