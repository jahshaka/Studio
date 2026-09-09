/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/ogresamples.h"

#include "data/settingsmanager.h"
#include "services/filewriteatomic.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHash>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>

#ifdef Q_OS_UNIX
#include <cerrno>
#include <csignal>
#endif

namespace ogresamples {

namespace {

// The samples this program ports (spec §3, owner decision D2). The first three
// are the proving slice — one base scene, one room, one cheap rider; the other
// five ride harnesses those three pay for. Rows the spec ruled out (Atmosphere:
// their AtmosphereNpr against our Preetham sky is apples to oranges;
// MorphAnimations: no blendshapes in the document model yet; the CPU-buffer
// authoring demos: no document concept) are deliberately absent — a tile that
// implies parity we cannot reach is worse than no tile.
const QVector<Entry> &table()
{
    static const QVector<Entry> t = {
        { QStringLiteral("PbsMaterials"), QStringLiteral("PBS Materials"),
          QStringLiteral("Their sky is Ogre's AtmosphereNpr; ours is a Preetham bake. "
                         "3 of their 6 BRDF names exist here.") },
        { QStringLiteral("LocalCubemaps"), QStringLiteral("Local Cubemaps"),
          QStringLiteral("Probe placement is grid-only.") },
        { QStringLiteral("Refractions"), QStringLiteral("Refractions"),
          QString() },
        { QStringLiteral("Hdr"), QStringLiteral("HDR"),
          QStringLiteral("Their exposure presets are a keyboard cycle; ours is a scene setting.") },
        { QStringLiteral("AreaApproxLights"), QStringLiteral("Area Lights"),
          QStringLiteral("Area lights never cast shadows — on either side.") },
        { QStringLiteral("ScreenSpaceReflections"), QStringLiteral("Screen Space Reflections"),
          QString() },
        { QStringLiteral("InstantRadiosity"), QStringLiteral("Instant Radiosity"),
          QString() },
        { QStringLiteral("Decals"), QStringLiteral("Decals"),
          QString() },
    };
    return t;
}

/// pid of every sample THIS session spawned, by name. Detached processes are
/// not children we can wait on, so aliveness is a probe, not a signal.
QHash<QString, qint64> &livePids()
{
    static QHash<QString, qint64> pids;
    return pids;
}

bool processAlive(qint64 pid)
{
    if (pid <= 0) return false;
#ifdef Q_OS_UNIX
    // kill(pid, 0) — permission/existence probe only, sends nothing. ESRCH
    // means gone; EPERM means alive and not ours (a recycled pid), which is
    // still "do not spawn a second one" as far as this file is concerned.
    return ::kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM;
#else
    Q_UNUSED(pid);
    return false;
#endif
}

/// A sample name must be a plain identifier: it becomes a FILE NAME under a
/// directory we then hand to QProcess. "../../bin/sh" is the attack this
/// refuses, and the scripting console is a real (local) untrusted input.
bool nameIsSane(const QString &name)
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9_]+$"));
    return !name.isEmpty() && name.size() < 64 && re.match(name).hasMatch();
}

bool isHeadless()
{
    // The offscreen QPA platform IS the definition of headless in this app
    // (bridge/enginehost.cpp makes the same test to choose the NULL render
    // system). A --headless run has no display to put a Vulkan sample window
    // on, and spawning one from a document-only session would be a surprise
    // the script never asked for.
    return !qobject_cast<QGuiApplication *>(QCoreApplication::instance())
           || QGuiApplication::platformName() == QLatin1String("offscreen");
}

}  // namespace

const QVector<Entry> &catalog() { return table(); }

const Entry *entry(const QString &name)
{
    for (const Entry &e : table())
        if (e.name == name) return &e;
    return nullptr;
}

QString samplesDir()
{
    // 1. an explicit preference wins — a developer who built the samples
    //    somewhere else (or keeps a second Ogre checkout) says so once.
    if (SettingsManager *s = SettingsManager::getDefaultManager()) {
        const QString pref = s->getValue(QStringLiteral("samples/ogreSamplesDir"), QString())
                                 .toString().trimmed();
        if (!pref.isEmpty() && QFileInfo(pref).isDir())
            return QDir(pref).absolutePath();
    }

    // 2. this TREE's own samples, found by walking up from the binary
    //    (build-linux/bin/Jahshaka -> 2 levels; a macOS bundle -> 5). Never a
    //    hard-coded path: a worktree must find its own samples, exactly like
    //    the per-tree engine install it was built against.
    QDir dir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 7; ++i) {
        const QString candidate =
            dir.absoluteFilePath(QStringLiteral("irisgl/thirdparty/ogre-next/build/bin"));
        if (QFileInfo(candidate).isDir())
            return QDir(candidate).absolutePath();
        if (!dir.cdUp()) break;
    }
    return QString();
}

QString binaryPath(const QString &name)
{
    if (!nameIsSane(name)) return QString();
    const QString dir = samplesDir();
    if (dir.isEmpty()) return QString();
    // Sample_<Name>, and RelWithDebInfo (what build-ogre.sh uses) adds no _d
    // suffix. The unsuffixed name is a symlink to Sample_<Name>-<version>;
    // both are fine to exec.
    const QString path = QDir(dir).absoluteFilePath(QStringLiteral("Sample_") + name);
    const QFileInfo fi(path);
    if (!fi.exists() || !fi.isExecutable()) return QString();
    return fi.absoluteFilePath();
}

bool writeConfig(const QString &dir, int width, int height, bool fullscreen, QString *error)
{
    if (dir.isEmpty() || !QFileInfo(dir).isDir()) {
        if (error) *error = QStringLiteral("no samples directory");
        return false;
    }
    if (width <= 0 || height <= 0) { width = 1920; height = 1080; }

    // Option NAMES must all exist or restoreConfig() throws
    // (VulkanXcbSupport::setConfigOption raises ERR_INVALIDPARAMS on an
    // unknown one); VALUES are not validated against possibleValues, so an
    // exotic Video Mode is merely honoured. "<w> x <h>" is the spelling their
    // support object produces (OgreVulkanXcbSupport.cpp) and the spelling
    // GraphicsSystem parses back out into the SDL window size.
    const QString cfg =
        QStringLiteral(
            "Render System=Vulkan Rendering Subsystem\n"
            "\n"
            "[Vulkan Rendering Subsystem]\n"
            "Device=(default)\n"
            "Interface=xcb\n"
            "FSAA=1\n"
            "sRGB Gamma Conversion=Yes\n"
            "Full Screen=%1\n"
            "Video Mode=%2 x %3\n"
            "VSync=Yes\n")
            .arg(fullscreen ? QStringLiteral("Yes") : QStringLiteral("No"))
            .arg(width)
            .arg(height);

    const QString path = QDir(dir).absoluteFilePath(QStringLiteral("ogre.cfg"));
    if (!FileWrite::writeFileAtomic(path, cfg.toUtf8())) {
        if (error) *error = QStringLiteral("could not write %1").arg(path);
        return false;
    }
    return true;
}

bool isRunning(const QString &name)
{
    auto it = livePids().find(name);
    if (it == livePids().end()) return false;
    if (processAlive(it.value())) return true;
    livePids().erase(it);
    return false;
}

LaunchResult launch(const QString &name, const LaunchOptions &opts)
{
    LaunchResult r;
    if (!nameIsSane(name)) {
        r.reason = QStringLiteral("'%1' is not a sample name").arg(name);
        return r;
    }
    if (isHeadless()) {
        r.reason = QStringLiteral("headless session: an Ogre sample needs a display");
        return r;
    }
    const QString dir = samplesDir();
    if (dir.isEmpty()) {
        r.reason = QStringLiteral(
            "the Ogre samples are not built in this tree — "
            "OGRE_SAMPLES=1 ./irisgl/scripts/build-ogre.sh");
        return r;
    }
    r.path = binaryPath(name);
    if (r.path.isEmpty()) {
        r.reason = QStringLiteral("Sample_%1 is not in %2 (rebuild with OGRE_SAMPLES=1)")
                       .arg(name, dir);
        return r;
    }
    if (isRunning(name)) {
        r.pid = livePids().value(name);
        r.reason = QStringLiteral("Sample_%1 is already running (pid %2)").arg(name).arg(r.pid);
        return r;
    }

    QString cfgError;
    if (!writeConfig(dir, opts.width, opts.height, opts.fullscreen, &cfgError)) {
        r.reason = QStringLiteral("could not seed ogre.cfg: %1").arg(cfgError);
        return r;
    }

    QProcess proc;
    proc.setProgram(r.path);
    // THE WORKING DIRECTORY IS THE WHOLE CONFIGURATION: resources2.cfg,
    // plugins.cfg and the ogre.cfg we just wrote are all resolved from it, and
    // the paths inside resources2.cfg point at the Ogre SOURCE tree — which is
    // why no media is ever copied anywhere for this.
    proc.setWorkingDirectory(dir);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    // Ogre patch 0020. Without it mAlwaysAskForConfig short-circuits
    // restoreConfig() and the sample opens the raw GLX config dialog instead
    // of honouring the file we just wrote.
    env.insert(QStringLiteral("JAH_OGRE_SAMPLE_NO_CONFIG"), QStringLiteral("1"));
    proc.setProcessEnvironment(env);

    qint64 pid = 0;
    if (!proc.startDetached(&pid)) {
        r.reason = QStringLiteral("could not start %1").arg(r.path);
        return r;
    }
    livePids().insert(name, pid);
    r.launched = true;
    r.pid = pid;
    return r;
}

}  // namespace ogresamples
