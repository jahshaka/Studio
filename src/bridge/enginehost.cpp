#include "bridge/enginehost.h"
#include "viewport/enginerenderdriver.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QDir>
#include <QFileInfo>
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
    QStringList candidates;
    // 1) Bundled with the app (Jahshaka.app/Contents/Resources/vulkan/...) — the
    //    self-contained form; nothing ships there yet, it is simply looked at first.
    candidates << appDir + QStringLiteral("/../Resources/vulkan/icd.d/MoltenVK_icd.json")
               << appDir + QStringLiteral("/vulkan/icd.d/MoltenVK_icd.json");
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

    for (const QString &path : std::as_const(candidates)) {
        const QString canonical = QFileInfo(path).canonicalFilePath();
        if (canonical.isEmpty()) continue;
        qputenv("VK_DRIVER_FILES", canonical.toLocal8Bit());
        qputenv("VK_ICD_FILENAMES", canonical.toLocal8Bit());   // pre-1.3.207 loaders
        qInfo("Vulkan ICD not in the environment; using %s", qPrintable(canonical));
        return;
    }
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
    const QString appMedia = QCoreApplication::applicationDirPath() + QStringLiteral("/media/");

    if (!envPlugins.isEmpty())                      cfg.pluginDir = envPlugins.toStdString();
    else                                            cfg.pluginDir = JAHSHAKA_OGRE_PLUGIN_DIR_DEFAULT;

    if (!envMedia.isEmpty())                        cfg.hlmsMediaDir = envMedia.toStdString();
    else if (QDir(appMedia + "Hlms/Pbs").exists())  cfg.hlmsMediaDir = appMedia.toStdString();
    else                                            cfg.hlmsMediaDir = JAHSHAKA_OGRE_MEDIA_DIR_DEFAULT;

    cfg.logFile = "jahshaka-ogre.log";
    return cfg;
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

void EngineHost::shutdown()
{
    if (mDriver) {
        mDriver->stop();
        delete mDriver;
        mDriver = nullptr;
    }
    mEngine.reset();
}
