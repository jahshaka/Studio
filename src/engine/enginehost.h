#ifndef ENGINEHOST_H
#define ENGINEHOST_H

// EngineHost — the process-wide owner of THE engine.
//
// jahshaka::engine::Engine is one-per-process (Engine.h). Every Studio component
// that needs it — the editor viewport, the engine preview dialog, later the
// player and asset views — gets it from here rather than creating its own, and
// shares the single EngineRenderDriver (the one render loop).
//
// Also holds the runtime viewport choice (--viewport=engine|legacy, env
// JAHSHAKA_VIEWPORT, CMake JAHSHAKA_ENGINE_VIEWPORT) so main.cpp and MainWindow
// agree on it. Includes only the engine abstraction — never Ogre.
#include <memory>
#include <QString>
#include "jahshaka/engine/Engine.h"

class EngineRenderDriver;

enum class ViewportBackend
{
    Legacy,   // SceneViewWidget on IrisGL (QOpenGLWidget, wayland only)
    Engine    // EngineSceneViewport on jahshaka::engine (xcb)
};

class EngineHost
{
public:
    static EngineHost &instance();

    /// Decides the backend from, in priority order: --viewport=<engine|legacy> on
    /// the command line, env JAHSHAKA_VIEWPORT, then the compile-time default.
    static ViewportBackend resolveViewportBackend(int argc, char **argv);
    static ViewportBackend viewportBackend() { return sBackend; }
    static void setViewportBackend(ViewportBackend b) { sBackend = b; }

    /// Where the engine's plugins and Hlms templates are, resolved at runtime:
    /// env JAHSHAKA_OGRE_PLUGINS / JAHSHAKA_OGRE_MEDIA, then <exe dir>/media,
    /// then the build-machine defaults the engine library was configured with.
    /// The native display is NOT filled in here; start() does that.
    static jahshaka::engine::EngineConfig resolveConfig();

    /// Creates the Engine and its render driver (idempotent: true if already
    /// running). Requires the xcb platform and a QApplication. The driver is
    /// created stopped; whoever shows the first view starts it.
    bool start(QString &error);
    bool isRunning() const { return static_cast<bool>(mEngine); }

    std::shared_ptr<jahshaka::engine::Engine> engine() const { return mEngine; }
    EngineRenderDriver *driver() const { return mDriver; }

    /// Stops the loop and drops the host's reference. Viewports that still hold
    /// the Engine keep it alive until they are destroyed (weak/shared contract in
    /// EngineViewWidget), so call this before QApplication goes away.
    void shutdown();

private:
    EngineHost() = default;
    ~EngineHost();
    EngineHost(const EngineHost &) = delete;
    EngineHost &operator=(const EngineHost &) = delete;

    static ViewportBackend sBackend;
    std::shared_ptr<jahshaka::engine::Engine> mEngine;
    EngineRenderDriver *mDriver = nullptr;
};

/// Factory for the engine-backed editor viewport (defined in
/// src/widgets/enginesceneviewport.cpp so MainWindow never names that class).
class IEditorViewport;
class QWidget;
IEditorViewport *createEngineSceneViewport(const std::shared_ptr<jahshaka::engine::Engine> &engine,
                                           EngineRenderDriver *driver, QWidget *parent);

/// Factory for the engine-backed player view (defined in
/// src/player/engineplayerview.cpp so MainWindow never names that class).
class IPlayerView;
IPlayerView *createEnginePlayerView(const std::shared_ptr<jahshaka::engine::Engine> &engine,
                                    EngineRenderDriver *driver, QWidget *parent);

/// Factory for the engine-backed Assets page viewer (defined in
/// src/widgets/engineassetviewer.cpp so MainWindow never names that class).
class IAssetViewer;
IAssetViewer *createEngineAssetViewer(const std::shared_ptr<jahshaka::engine::Engine> &engine,
                                      EngineRenderDriver *driver, QWidget *parent);

#endif // ENGINEHOST_H
