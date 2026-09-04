#ifndef ENGINEHOST_H
#define ENGINEHOST_H

// EngineHost — the process-wide owner of THE engine.
//
// jahshaka::engine::Engine is one-per-process (Engine.h). Every Studio component
// that needs it — the editor viewport, the engine preview dialog, later the
// player and asset views — gets it from here rather than creating its own, and
// shares the single EngineRenderDriver (the one render loop).
//
// Includes only the engine abstraction — never Ogre.
#include <memory>
#include <QString>
#include "jahshaka/engine/Engine.h"

class EngineRenderDriver;

class EngineHost
{
public:
    static EngineHost &instance();

    /// Where the engine's plugins and Hlms templates are, resolved at runtime:
    /// env JAHSHAKA_OGRE_PLUGINS / JAHSHAKA_OGRE_MEDIA, then <exe dir>/media,
    /// then the build-machine defaults the engine library was configured with.
    /// The native display is NOT filled in here; start() does that.
    static jahshaka::engine::EngineConfig resolveConfig();

    // ---- Persistent shader cache (SHADER_CACHE_SPEC.md) ----
    /// Where the cache lives: AppDataLocation/shadercache. Empty when the
    /// platform gives us no writable data location. Static because
    /// --clear-shader-cache runs BEFORE any engine exists.
    static QString shaderCacheDirectory();
    /// Deletes that directory. Safe with no engine running; the next launch is
    /// cold. This is our `r.InvalidateCachedShaders`.
    static bool clearShaderCacheOnDisk();
    /// The recorded warm-up set (SHADER_CACHE_SPEC §2.7b): the permutation list
    /// this machine's previous sessions used, replayed at the next startup so
    /// their shaders exist before anything is drawn. Lives beside the cache and
    /// dies with it — it is derived data too, and re-recording costs one
    /// session.
    static QString warmUpSetPath();
    /// Starts the burst-settle save watchdog: once shaders have been compiled
    /// and then NOT compiled for a few seconds, the cache is written. Without
    /// it a crash (or a pkill, which this codebase's history is full of) throws
    /// away the whole session's compile work. Idempotent.
    void startShaderCacheWatchdog();

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

    std::shared_ptr<jahshaka::engine::Engine> mEngine;
    EngineRenderDriver *mDriver = nullptr;
    class QTimer *mCacheWatchdog = nullptr;
    unsigned mShadersSeen = 0;    ///< compiled+cached at the last watchdog tick
    unsigned mShadersSaved = 0;   ///< the value at the last successful save
    int      mQuietTicks = 0;
};

/// Factory for the engine-backed editor viewport (defined in
/// src/widgets/enginesceneviewport.cpp so MainWindow never names that class).
class IEditorViewport;
class QWidget;
IEditorViewport *createEngineSceneViewport(const std::shared_ptr<jahshaka::engine::Engine> &engine,
                                           EngineRenderDriver *driver, QWidget *parent);

/// Factory for the engine-backed player view (defined in
/// src/player/engineplayerview.cpp).
class EnginePlayerView;
EnginePlayerView *createEnginePlayerView(const std::shared_ptr<jahshaka::engine::Engine> &engine,
                                         EngineRenderDriver *driver, QWidget *parent);

/// Factory for the engine-backed Assets page viewer (defined in
/// src/widgets/engineassetviewer.cpp so MainWindow never names that class).
class IAssetViewer;
IAssetViewer *createEngineAssetViewer(const std::shared_ptr<jahshaka::engine::Engine> &engine,
                                      EngineRenderDriver *driver, QWidget *parent);

#endif // ENGINEHOST_H
