#include "enginehost.h"
#include "../widgets/enginerenderdriver.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QDir>
#include <cstring>

using namespace jahshaka::engine;

#ifndef JAHSHAKA_ENGINE_VIEWPORT_DEFAULT
#define JAHSHAKA_ENGINE_VIEWPORT_DEFAULT 0
#endif

ViewportBackend EngineHost::sBackend =
    JAHSHAKA_ENGINE_VIEWPORT_DEFAULT ? ViewportBackend::Engine : ViewportBackend::Legacy;

EngineHost &EngineHost::instance()
{
    static EngineHost host;
    return host;
}

EngineHost::~EngineHost()
{
    shutdown();
}

static bool parseBackend(const QByteArray &value, ViewportBackend &out)
{
    const QByteArray v = value.trimmed().toLower();
    if (v == "engine") { out = ViewportBackend::Engine; return true; }
    if (v == "legacy") { out = ViewportBackend::Legacy; return true; }
    return false;
}

ViewportBackend EngineHost::resolveViewportBackend(int argc, char **argv)
{
    ViewportBackend backend = sBackend;   // compile-time default
    ViewportBackend parsed;
    if (parseBackend(qgetenv("JAHSHAKA_VIEWPORT"), parsed)) backend = parsed;
    for (int i = 1; i < argc; ++i) {
        if (std::strncmp(argv[i], "--viewport=", 11) == 0 && parseBackend(argv[i] + 11, parsed))
            backend = parsed;
        else if (std::strcmp(argv[i], "--viewport") == 0 && i + 1 < argc && parseBackend(argv[i + 1], parsed))
            backend = parsed;
    }
    return backend;
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

    // Ogre has no Wayland backend: its Vulkan path uses VK_KHR_xcb_surface and needs a
    // real X11 window. main.cpp selects xcb for engine mode; refuse clearly otherwise
    // rather than hand the engine a Wayland handle and crash.
    const QString platform = QGuiApplication::platformName();
    if (platform != QLatin1String("xcb")) {
        error = QStringLiteral("The engine requires the xcb platform (running on '%1'). "
                               "Relaunch with QT_QPA_PLATFORM=xcb.").arg(platform);
        return false;
    }

    EngineConfig cfg = resolveConfig();
    // Hand the engine OUR X connection. Opening a second connection to the same
    // windows causes flicker and lets other windows' content bleed into the viewport.
    if (auto *x11 = qApp->nativeInterface<QNativeInterface::QX11Application>())
        cfg.display = reinterpret_cast<NativeDisplayHandle>(x11->display());
    if (!cfg.display) {
        error = QStringLiteral("Could not obtain the X11 display connection from Qt.");
        return false;
    }

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
