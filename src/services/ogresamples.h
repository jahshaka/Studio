/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef OGRESAMPLES_H
#define OGRESAMPLES_H

// OGRE-NEXT SAMPLES — the reference pictures beside our ports
// (SPECS/OGRE_SAMPLES_TAB_SPEC.md).
//
// The PORTS are the deliverable: Ogre's demo scenes re-authored as Jahshaka
// scenes, shipped in the sample browser's second tab. Their original binaries
// are the reference picture you put next to ours to judge it — a DEVELOPER
// CONVENIENCE, opt-in at engine-build time (`OGRE_SAMPLES=1
// ./irisgl/scripts/build-ogre.sh`), never a shipping dependency. Everything
// here degrades to `available:false` + a disabled button when the samples were
// not built, which is every ordinary tree, every CI run and every macOS box
// (SDL2 is absent from the no-Homebrew toolchain, so upstream's CMake skips
// the samples silently).
//
// THREE FACTS THIS FILE IS BUILT AROUND, each dearly won (§1.2, §1.3, §7.3):
//
//  1. The binaries run from THEIR OWN BUILD DIRECTORY. The generated
//     resources2.cfg / plugins.cfg beside them carry paths into the Ogre
//     SOURCE tree, and GraphicsSystem picks the first write-access folder for
//     Ogre.log — so the working directory is load-bearing, and no media copy
//     is needed anywhere.
//  2. A seeded ogre.cfg alone CANNOT suppress their config dialog:
//     GraphicsSystem::mAlwaysAskForConfig is initialised true and
//     short-circuits restoreConfig() before it is ever consulted. Ogre patch
//     0020 adds the missing env hook (JAH_OGRE_SAMPLE_NO_CONFIG), which is
//     why launch() exports it.
//  3. Their Vulkan xcb config defaults FULL SCREEN TO "Yes" and Video Mode to
//     the LARGEST RandR mode. Launching a sample without writing "Full
//     Screen=No" takes over the owner's whole display — which reads exactly
//     like the editor froze. writeConfig() always writes it.
//
// PURE + Qt only: no engine, no widgets. The verb (app.launchOgreSample /
// app.ogreSamples) and the sample browser's second tab are two callers of the
// same functions, per the API-first rule.

#include <QString>
#include <QVector>

namespace ogresamples {

/// One row of the curated inventory — the samples we ship (or will ship) a
/// port of. NOT a directory scan: the tab and the verb must be able to say
/// "this exists as a port, the original is not built here", and a tree with no
/// samples build would otherwise report an empty world.
struct Entry
{
    QString name;    ///< the sample's binary base name, e.g. "PbsMaterials" -> Sample_PbsMaterials
    QString title;   ///< display name for the tile, e.g. "PBS Materials"
    QString note;    ///< what the port CANNOT show (§3's tile label); may be empty
};

/// The eight ports of the committed program (spec §3, owner decision D2:
/// the 3-port slice first, then the rest in the same lane).
const QVector<Entry> &catalog();

/// The row for `name`, or nullptr. Names are the binary base names.
const Entry *entry(const QString &name);

/// Where the built sample binaries live, or an empty string when nothing
/// plausible was found. Resolution order (§6): the `samples/ogreSamplesDir`
/// preference, then <tree>/irisgl/thirdparty/ogre-next/build/bin found by
/// walking up from the application directory. NEVER a hard-coded absolute
/// path — a worktree and the main tree must resolve to their OWN samples, for
/// the same reason the engine install is per-tree.
QString samplesDir();

/// Absolute path of `name`'s binary, or an empty string when it is not built.
/// RelWithDebInfo (what build-ogre.sh uses) gets no _d suffix.
QString binaryPath(const QString &name);

/// Writes/refreshes ogre.cfg in `dir` so the sample starts WINDOWED at the
/// requested size with no config dialog. Shared, mutable state in a directory
/// we do not own (§9.3, the same class as jahsettings.ini being shared with
/// the owner's runs) — recorded, accepted, and always overwritten rather than
/// merged so a stale Full Screen=Yes cannot survive.
bool writeConfig(const QString &dir, int width, int height, bool fullscreen,
                 QString *error = nullptr);

/// What a launch attempt did. `launched` false is NEVER an exception: a tree
/// without the samples built is the normal case, and the caller (verb or
/// button) says so instead of throwing.
struct LaunchResult
{
    bool    launched = false;
    QString path;      ///< the binary we resolved (empty when none)
    qint64  pid = 0;
    QString reason;    ///< why not, when !launched
};

struct LaunchOptions
{
    int  width = 1920;
    int  height = 1080;
    bool fullscreen = false;   ///< NEVER default true — see fact 3 above
};

/// Spawns `name`'s original binary, detached, with its own build/bin as the
/// working directory. Refuses (without throwing) when: the process is
/// headless/offscreen, the name is not a plain identifier, the binary is not
/// built, or that sample is ALREADY RUNNING from this session (§9.2 — a second
/// copy stacked over the first is how "the editor froze" gets reported).
LaunchResult launch(const QString &name, const LaunchOptions &opts = LaunchOptions());

/// True when this session already spawned `name` and that process is alive.
bool isRunning(const QString &name);

}  // namespace ogresamples

#endif  // OGRESAMPLES_H
