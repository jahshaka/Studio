#pragma once
// A HEADLESS ENGINE FOR A DOCUMENT SUITE, in one place. Since ENGINE-UPGRADE-U2 a document needs no engine at all
// (iris::graph is the document's own store; the engine owns its own nodes): suites keep this where they also want an
// engine Scene beside the document, or the engine's process state (the shader cache's NULL-run rule) under test.
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
// LIFETIME: declare it FIRST in main(), so it is destroyed LAST.

#include <cstdio>
#include <memory>
#include <string>

#include "irisgl/document/scenegraph/nodegraph.h"
#include "jahshaka/engine/Engine.h"
#include "enginetesthelpers.h"

namespace enginetest {

class DocumentGraph {
public:
    explicit DocumentGraph(const char *logFile = "document-graph-ogre.log")
    {
        // headless: RenderSystem_NULL — no display, no device, no window
        const auto cfg = testEngineConfig(logFile, EngineOverrides().headless());
        mEngine = jahshaka::engine::Engine::create(cfg, mError);
    }

    ~DocumentGraph()
    {
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
