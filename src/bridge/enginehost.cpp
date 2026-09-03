#include "bridge/enginehost.h"
#include "viewport/enginerenderdriver.h"
#include "data/settingsmanager.h"
#include "data/constants.h"

#include <QTimer>

#include <QCoreApplication>
#include <QGuiApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <cstring>

using namespace jahshaka::engine;

EngineHost &EngineHost::instance()
{
    static EngineHost host;
    return host;
}

#ifdef Q_OS_MACOS
namespace {

/// macOS has no system Vulkan: the loader only finds MoltenVK through an ICD
/// manifest, and there is no /usr/local/share/vulkan/icd.d on a machine that
/// installed the LunarG SDK into $HOME. Launched from Finder (no shell, so no
/// setup-env.sh) vkCreateInstance therefore fails with
/// VK_ERROR_INCOMPATIBLE_DRIVER before any of our code runs.
///
/// Point the loader at a manifest we can find, unless the environment already
/// says where one is (a Vulkan-SDK shell, or a developer pinning a driver).
/// Called before Engine::create; the loader reads these on first instance call.
void ensureVulkanIcdEnvironment()
{
    if (qEnvironmentVariableIsSet("VK_DRIVER_FILES") ||
        qEnvironmentVariableIsSet("VK_ICD_FILENAMES") ||
        qEnvironmentVariableIsSet("VK_ADD_DRIVER_FILES"))
        return;   // the host environment already resolved it

    const QString appDir = QCoreApplication::applicationDirPath();

    const auto pin = [](const QStringList &candidates) {
        for (const QString &path : candidates) {
            const QString canonical = QFileInfo(path).canonicalFilePath();
            if (canonical.isEmpty()) continue;
            qputenv("VK_DRIVER_FILES", canonical.toLocal8Bit());
            qputenv("VK_ICD_FILENAMES", canonical.toLocal8Bit());   // pre-1.3.207 loaders
            qInfo("Vulkan ICD not in the environment; using %s", qPrintable(canonical));
            return true;
        }
        return false;
    };

    // 1) Bundled with the app (Jahshaka.app/Contents/Resources/vulkan/...) — the
    //    self-contained form written by scripts/make-macos-bundle.sh. It wins
    //    over everything else on the machine: a redistributed bundle must use
    //    the MoltenVK it ships, not whatever the user happens to have.
    //    (Ordering defect fixed 2026-09-02: the loader-default-directory probe
    //    below used to run BEFORE this loop, so a system manifest in
    //    /usr/local/share/vulkan/icd.d silently beat the bundled one.)
    if (pin({ appDir + QStringLiteral("/../Resources/vulkan/icd.d/MoltenVK_icd.json"),
              appDir + QStringLiteral("/vulkan/icd.d/MoltenVK_icd.json") }))
        return;

    QStringList candidates;
    // 2) The SDK this process was launched from, if any.
    const QByteArray sdk = qgetenv("VULKAN_SDK");
    if (!sdk.isEmpty())
        candidates << QString::fromLocal8Bit(sdk) +
                          QStringLiteral("/share/vulkan/icd.d/MoltenVK_icd.json");
    // 3) The loader's own search paths — if a manifest lives there it needs no help.
    for (const QString &dir : { QStringLiteral("/usr/local/share/vulkan/icd.d"),
                                QStringLiteral("/etc/vulkan/icd.d"),
                                QDir::homePath() + QStringLiteral("/.local/share/vulkan/icd.d") }) {
        if (QFileInfo::exists(dir + QStringLiteral("/MoltenVK_icd.json")))
            return;
    }
    // 4) A LunarG SDK installed in $HOME (its default): newest version wins.
    QDir sdkRoot(QDir::homePath() + QStringLiteral("/VulkanSDK"));
    if (sdkRoot.exists()) {
        QStringList versions = sdkRoot.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        std::reverse(versions.begin(), versions.end());   // lexical newest-first
        for (const QString &v : versions)
            candidates << sdkRoot.filePath(v) +
                              QStringLiteral("/macOS/share/vulkan/icd.d/MoltenVK_icd.json");
    }

    if (pin(candidates)) return;
    qWarning("No MoltenVK ICD manifest found: the engine will fail to start. "
             "Install the LunarG Vulkan SDK or set VK_DRIVER_FILES.");
}

}  // namespace
#endif  // Q_OS_MACOS

EngineHost::~EngineHost()
{
    shutdown();
}

EngineConfig EngineHost::resolveConfig()
{
    EngineConfig cfg;
    cfg.backend = Backend::Vulkan;

    const QByteArray envPlugins = qgetenv("JAHSHAKA_OGRE_PLUGINS");
    const QByteArray envMedia   = qgetenv("JAHSHAKA_OGRE_MEDIA");
    const QString appDir   = QCoreApplication::applicationDirPath();
    const QString appMedia = appDir + QStringLiteral("/media/");
    // Inside a redistributed Jahshaka.app the render system lives beside the
    // rest of the dylibs in Contents/Frameworks (it needs libOgreNextMain and
    // libvulkan on its @loader_path). JAHSHAKA_OGRE_PLUGIN_DIR_DEFAULT is an
    // absolute build-machine path baked at configure time, so without this
    // probe a bundle on someone else's Mac loads no render system at all.
    // Mirrors the hlmsMediaDir probe two lines down. (MACOS_BUNDLE_SPEC §4.2 A)
    const QString appPlugins = appDir + QStringLiteral("/../Frameworks");

    if (!envPlugins.isEmpty())                      cfg.pluginDir = envPlugins.toStdString();
#ifdef Q_OS_MACOS
    else if (QFileInfo::exists(appPlugins + QStringLiteral("/RenderSystem_Vulkan.dylib")))
        cfg.pluginDir = QDir(appPlugins).canonicalPath().toStdString();
#endif
    else                                            cfg.pluginDir = JAHSHAKA_OGRE_PLUGIN_DIR_DEFAULT;

    if (!envMedia.isEmpty())                        cfg.hlmsMediaDir = envMedia.toStdString();
    else if (QDir(appMedia + "Hlms/Pbs").exists())  cfg.hlmsMediaDir = appMedia.toStdString();
    else                                            cfg.hlmsMediaDir = JAHSHAKA_OGRE_MEDIA_DIR_DEFAULT;

    // The Ogre log is our only crash forensics on a user's machine, and
    // Ogre::Log opens an ofstream without ever checking it (OgreLog.cpp:44-57),
    // so a relative name is silently lost when cwd is not writable — which is
    // exactly what a Finder launch gives you (cwd "/"). Ship builds therefore
    // log to AppDataLocation, the same place the settings file goes when
    // QT_DEBUG is off (src/data/settingsmanager.h:44-57).
    // Debug builds keep the relative name so a dev run still drops the log
    // beside the binary, where every existing doc and habit expects it.
#ifdef QT_DEBUG
    cfg.logFile = "jahshaka-ogre.log";
#else
    const QString logDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!logDir.isEmpty() && QDir().mkpath(logDir))
        cfg.logFile = QDir(logDir).filePath(QStringLiteral("jahshaka-ogre.log")).toStdString();
    else
        cfg.logFile = "jahshaka-ogre.log";
#endif
    // Shadow-caster geometry optimization (POST_CHAIN_SPEC.md §11): an
    // application preference, not a scene setting — the flag is process-wide and
    // consumed when a mesh is BUILT, so a mesh built while it was on keeps its
    // optimized shadow buffers whatever any scene later says.
    // Preferences -> Viewport writes it; WorldSettingsWidget pushes runtime
    // changes straight to the live Engine.
    cfg.optimizeShadowMeshes =
        SettingsManager::getDefaultManager()->getValue("shadow_mesh_optimization", true).toBool();

    // ---- Persistent shader cache (SHADER_CACHE_SPEC.md §4.1) ----
    // AppDataLocation/shadercache: the same root the library DB and the asset
    // store already live in, a sibling directory. It is DERIVED DATA — nothing
    // a user could lose is ever written there, which is what makes "delete the
    // whole directory on any doubt" an acceptable failure mode.
    //
    // The setting exists so a machine with a broken driver cache can turn the
    // feature off without a rebuild; the default is on.
    if (SettingsManager::getDefaultManager()->getValue("shader_cache_enabled", true).toBool()) {
        const QString dataDir = shaderCacheDirectory();
        if (!dataDir.isEmpty()) cfg.shaderCacheDir = dataDir.toStdString();
    }
    // The app's contribution to the cache fingerprint. Version + commit, because
    // OUR C++ decides which Hlms properties are set and which datablocks exist;
    // no hash inside Ogre can see a change to src/. A user updating the app
    // therefore pays exactly one cold launch, which is correct and is the same
    // property Unreal's DDC has.
    cfg.appBuildId = QStringLiteral("%1/%2/%3")
                         .arg(Constants::CONTENT_VERSION,
                              QStringLiteral(GIT_COMMIT_HASH),
                              QStringLiteral(GIT_COMMIT_DATE))
                         .toStdString();

    return cfg;
}

QString EngineHost::shaderCacheDirectory()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty()) return QString();
    return QDir(base).filePath(QStringLiteral("shadercache"));
}

bool EngineHost::clearShaderCacheOnDisk()
{
    const QString dir = shaderCacheDirectory();
    if (dir.isEmpty()) return false;
    QDir d(dir);
    if (!d.exists()) return true;
    // removeRecursively, not rmdir: the directory is entirely ours and entirely
    // derived. The next launch rebuilds it.
    return d.removeRecursively();
}

bool EngineHost::start(QString &error)
{
    if (mEngine) return true;

    EngineConfig cfg = resolveConfig();
#ifdef Q_OS_MACOS
    ensureVulkanIcdEnvironment();
#endif
#ifdef Q_OS_LINUX
    // Ogre has no Wayland backend: its Vulkan path uses VK_KHR_xcb_surface and needs a
    // real X11 window. main.cpp selects xcb for engine mode; refuse clearly otherwise
    // rather than hand the engine a Wayland handle and crash.
    const QString platform = QGuiApplication::platformName();
    if (platform != QLatin1String("xcb")) {
        error = QStringLiteral("The engine requires the xcb platform (running on '%1'). "
                               "Relaunch with QT_QPA_PLATFORM=xcb.").arg(platform);
        return false;
    }

    // Hand the engine OUR X connection. Opening a second connection to the same
    // windows causes flicker and lets other windows' content bleed into the viewport.
    if (auto *x11 = qApp->nativeInterface<QNativeInterface::QX11Application>())
        cfg.display = reinterpret_cast<NativeDisplayHandle>(x11->display());
    if (!cfg.display) {
        error = QStringLiteral("Could not obtain the X11 display connection from Qt.");
        return false;
    }
#else
    // No X11 host connection on this platform: the engine runs headless (null
    // window + offscreen views) until a native window backend exists
    // (DOCS/HANDOFF.md §7). cfg.display stays 0 by design.
    cfg.display = 0;
#endif

    std::string err;
    std::unique_ptr<Engine> engine = Engine::create(cfg, err);
    if (!engine) {
        error = QString::fromStdString(err);
        return false;
    }
    mEngine = std::move(engine);
    mDriver = new EngineRenderDriver(mEngine.get());
    return true;
}

void EngineHost::startShaderCacheWatchdog()
{
    if (mCacheWatchdog || !mEngine) return;
    // One second is a deliberate compromise: fast enough that a crash loses at
    // most a few seconds of compiling, slow enough that the check itself (two
    // atomic reads) is free. The SAVE only happens after three consecutive
    // quiet ticks — vkGetPipelineCacheData is documented as fragile when called
    // close to PSO creation (OgreVulkanRenderSystem.cpp:680-700), so "the burst
    // has settled" is a correctness condition, not just an optimisation.
    mCacheWatchdog = new QTimer(nullptr);
    mCacheWatchdog->setInterval(1000);
    QObject::connect(mCacheWatchdog, &QTimer::timeout, mCacheWatchdog, [this]() {
        if (!mEngine) return;
        unsigned compiled = 0, cached = 0, expected = 0;
        mEngine->shaderBuildProgress(compiled, cached, expected);
        const unsigned total = compiled + cached;
        if (total != mShadersSeen) { mShadersSeen = total; mQuietTicks = 0; return; }
        if (mShadersSeen == mShadersSaved) return;         // nothing new since the last save
        if (++mQuietTicks < 3) return;                     // still inside the burst
        if (mEngine->saveShaderCache()) mShadersSaved = mShadersSeen;
        mQuietTicks = 0;
    });
    mCacheWatchdog->start();
}

void EngineHost::shutdown()
{
    if (mCacheWatchdog) { mCacheWatchdog->stop(); delete mCacheWatchdog; mCacheWatchdog = nullptr; }
    // THE clean-quit save (SHADER_CACHE_SPEC §4.4). The engine's destructor
    // saves too, but a viewport that still holds the shared_ptr can defer that
    // destructor past Qt's own teardown — this is the point we can prove runs,
    // with the render loop stopped a line below and nothing compiling.
    if (mEngine) mEngine->saveShaderCache();
    if (mDriver) {
        mDriver->stop();
        delete mDriver;
        mDriver = nullptr;
    }
    mEngine.reset();
}
