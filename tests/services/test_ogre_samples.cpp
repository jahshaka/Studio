/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// services.ogre_samples — the seeded ogre.cfg and the launch guards
// (SPECS/OGRE_SAMPLES_TAB_SPEC.md §7.3/§9.2).
//
// scripting.e2e.ogre_samples proves the VERBS refuse honestly in a headless
// run. It cannot reach the config seeding, because the refusal happens first —
// and the config file is the one piece of this feature that a wrong value
// makes actively hostile: their Vulkan xcb support defaults Full Screen to
// "Yes" and Video Mode to the LARGEST RandR mode, so a sample launched with a
// missing or stale ogre.cfg takes over the owner's whole display. That value
// is asserted here, on a scratch directory, with nothing launched.
//
// Offscreen and DISPLAY-free (jah_no_display): pure file and string work.

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QSet>
#include <QTemporaryDir>

#include <cstdio>

#include "services/ogresamples.h"

static int failures = 0;
#define CHECK(cond, name) do { \
    if (cond) { std::printf("PASS %s\n", name); } \
    else { std::printf("FAIL %s\n", name); ++failures; } \
} while (0)

namespace {

QString readAll(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QString();
    return QString::fromUtf8(f.readAll());
}

void testCatalog()
{
    const QVector<ogresamples::Entry> &cat = ogresamples::catalog();
    CHECK(cat.size() >= 8, "catalog: the eight committed ports are listed");

    QSet<QString> names;
    bool everyRowComplete = true;
    for (const ogresamples::Entry &e : cat) {
        if (e.name.isEmpty() || e.title.isEmpty()) everyRowComplete = false;
        names.insert(e.name);
    }
    CHECK(everyRowComplete, "catalog: every row has a binary name and a title");
    CHECK(names.size() == cat.size(), "catalog: names are unique");
    CHECK(ogresamples::entry("PbsMaterials") != nullptr, "catalog: lookup by name finds a row");
    CHECK(ogresamples::entry("NoSuchSample") == nullptr, "catalog: an unknown name is not a row");
}

void testWriteConfig()
{
    QTemporaryDir tmp;
    CHECK(tmp.isValid(), "config: scratch directory");

    // A PRE-EXISTING fullscreen config is the dangerous case: this is a shared,
    // mutable file in a directory we do not own (§9.3), so the write REPLACES
    // it rather than merging into it.
    const QString cfgPath = QDir(tmp.path()).filePath(QStringLiteral("ogre.cfg"));
    {
        QFile stale(cfgPath);
        stale.open(QIODevice::WriteOnly);
        stale.write("Render System=OpenGL 3+ Rendering Subsystem\n\n"
                    "[OpenGL 3+ Rendering Subsystem]\nFull Screen=Yes\n");
        stale.close();
    }

    QString err;
    CHECK(ogresamples::writeConfig(tmp.path(), 1280, 720, false, &err),
          "config: written into the samples directory");
    const QString cfg = readAll(cfgPath);
    CHECK(cfg.contains(QLatin1String("Render System=Vulkan Rendering Subsystem")),
          "config: names the Vulkan render system");
    CHECK(cfg.contains(QLatin1String("[Vulkan Rendering Subsystem]")),
          "config: carries the render system's section");
    // THE ASSERTION THIS SUITE EXISTS FOR.
    CHECK(cfg.contains(QLatin1String("Full Screen=No")), "config: NOT full screen");
    CHECK(!cfg.contains(QLatin1String("Full Screen=Yes")), "config: the stale fullscreen line is gone");
    CHECK(cfg.contains(QLatin1String("Video Mode=1280 x 720")),
          "config: the video mode is the requested one, in their spelling");
    CHECK(cfg.contains(QLatin1String("Interface=xcb")), "config: the xcb window interface");

    // Every option NAME must be one the Vulkan render system actually has:
    // an unknown one makes restoreConfig() throw ERR_INVALIDPARAMS and the
    // sample dies before it draws anything.
    static const char *known[] = { "Render System", "Device", "Interface", "FSAA",
                                   "sRGB Gamma Conversion", "Full Screen", "Video Mode",
                                   "VSync" };
    bool allKnown = true;
    for (const QString &line : cfg.split(QLatin1Char('\n'))) {
        const QString t = line.trimmed();
        if (t.isEmpty() || t.startsWith(QLatin1Char('['))) continue;
        const QString key = t.section(QLatin1Char('='), 0, 0);
        bool found = false;
        for (const char *k : known) if (key == QLatin1String(k)) { found = true; break; }
        if (!found) { std::printf("  unknown config key: %s\n", qPrintable(key)); allKnown = false; }
    }
    CHECK(allKnown, "config: every key is an option the Vulkan render system has");

    // Nonsense sizes fall back rather than writing a mode nothing can parse.
    CHECK(ogresamples::writeConfig(tmp.path(), 0, -4, false, &err), "config: degenerate size accepted");
    CHECK(readAll(cfgPath).contains(QLatin1String("Video Mode=1920 x 1080")),
          "config: degenerate size falls back to 1920 x 1080");

    // A directory that does not exist is a refusal, with a reason.
    err.clear();
    CHECK(!ogresamples::writeConfig(QDir(tmp.path()).filePath(QStringLiteral("nope")),
                                    1920, 1080, false, &err),
          "config: refuses a directory that does not exist");
    CHECK(!err.isEmpty(), "config: the refusal carries a reason");
}

void testLaunchGuards()
{
    // Offscreen: this whole suite is headless, so EVERY launch must refuse —
    // including one naming a sample that really is built in this tree.
    const ogresamples::LaunchResult r = ogresamples::launch(QStringLiteral("PbsMaterials"));
    CHECK(!r.launched && r.pid == 0, "launch: refused in a headless process");
    CHECK(!r.reason.isEmpty(), "launch: the refusal carries a reason");
    CHECK(!ogresamples::isRunning(QStringLiteral("PbsMaterials")),
          "launch: nothing is recorded as running");

    // A name is a FILE NAME under a directory handed to QProcess. Traversal,
    // shell punctuation and emptiness are all refused before anything is
    // resolved, which is also why binaryPath() screens them.
    const char *bad[] = { "", "../../../bin/sh", "PbsMaterials;id", "Pbs Materials", "." };
    bool allRefused = true;
    for (const char *b : bad) {
        const QString name = QString::fromLatin1(b);
        if (ogresamples::launch(name).launched) allRefused = false;
        if (!ogresamples::binaryPath(name).isEmpty()) allRefused = false;
    }
    CHECK(allRefused, "launch: junk and traversal names are refused");
}

void testSamplesDir()
{
    // No SettingsManager has been created in this process, so the preference
    // branch is inert and the answer comes from the walk up from the binary —
    // which, for a test binary in tests/services, finds nothing. "No samples
    // here" must be an empty string and an unavailable sample, never a guess.
    const QString dir = ogresamples::samplesDir();
    if (dir.isEmpty()) {
        CHECK(ogresamples::binaryPath(QStringLiteral("PbsMaterials")).isEmpty(),
              "samplesDir: no directory -> no binary");
    } else {
        // A tree that opted in (OGRE_SAMPLES=1) resolves its OWN build/bin.
        CHECK(dir.endsWith(QLatin1String("ogre-next/build/bin")),
              "samplesDir: resolves this tree's own samples directory");
    }
}

}  // namespace

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    testCatalog();
    testWriteConfig();
    testLaunchGuards();
    testSamplesDir();
    std::printf(failures ? "FAILURES: %d\n" : "all ok (%d failures)\n", failures);
    return failures == 0 ? 0 : 1;
}
