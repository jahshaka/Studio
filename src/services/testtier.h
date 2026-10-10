/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef TESTTIER_H
#define TESTTIER_H

// THE PROCESS'S TEST TIER (lane TEST-TIER-1, SPECS/audits/GPU_LOSS_AUDIT_2026-09-27.md §A2).
//
// A process with no test tier boots at the document's default tier (Epic: the full voxel chain,
// ~1.5-3 GB of VRAM per process). EVERY ctest row that starts the app declares what it boots
// instead (TEST-NEEDS-1: `TIER` + `NEEDS`, or `TIER document` for a claim about the document's
// own World state; tests/support/vram_tokens.cmake, docs/TESTING_GATE.md §4b).
// `--test-tier <low|medium|high|epic>` (or JAHSHAKA_TEST_TIER for a runner that cannot pass an
// argument; the flag wins) is the tier, JAHSHAKA_TEST_NEEDS the features it keeps: EVERY scene
// the process binds to the editor — a new one or an opened one — is put on that World Mode
// through the same call `world.mode` makes (worldmodes::setMode, rows the scene pinned with
// world.override survive), AFTER the reader has run, so the reader's absent-key defaults stay
// the constructor's. A windowed script run boots at kWindowWidth x kWindowHeight when its row
// also declares the test window (JAHSHAKA_TEST_WINDOW; a separate declaration since TEST-NEEDS-1).
//
// It is a PROCESS setting, applied IN MEMORY: nothing here changes the document default
// (iris::Scene::giTier), and a process with no test tier (every pixel pool, the owner's app)
// honours each opened scene's own tier. `app.testTier()` reads it.
//
// A SAVE FROM A TEST-TIER PROCESS WRITES THE TIER (TESTING-DEBTS-1 T6): setMode writes the
// scene's worldMode and every unpinned World row in memory (MainWindow::setScene ->
// worldmodes::setMode), and SceneWriter saves what the scene holds (`worldMode`, `giTier` and
// the rows) — no guard, and the scene is not marked dirty by it. So a test that asserts a SAVED
// World row (a tier, a GI / shadow / probe row read back after save -> open) must run at the
// document's tier (a `TIER epic` pool or a row without JAHSHAKA_TEST_TIER); a Low process can
// only assert what the test tier itself puts there (<pool>.tier does, on purpose). Measured on
// 2026-09-28: no Low arm or Low row saves and asserts a World row but <pool>.tier.
//
// Header-only on purpose: a process latch, set once in main() before any window exists and
// read by the shell and the scripting layer — no translation unit of its own to link into the
// test binaries that compile those files standalone.

#include <QRegularExpression>
#include <QString>
#include <QStringList>

namespace testtier {

namespace detail {
inline QString &slot()
{
    static QString name;
    return name;
}
}   // namespace detail

/// Latch the process's test tier ("" = none). main() validates the name first
/// (worldmodes::modeFromName; "custom" is not a tier).
inline void set(const QString &worldModeName) { detail::slot() = worldModeName; }
/// The World Mode name every scene of this process is put on, or "" when the
/// process has none (the document's own tier rules).
inline QString name() { return detail::slot(); }
inline bool active() { return !detail::slot().isEmpty(); }

/// The window a windowed script run boots at when its row DECLARES the test window: 1280x720 —
/// the chain's render targets scale with it, a 16:9 aspect like the rig's 1920x1080 screen, so
/// an arm's framing is the same picture, smaller. THE WINDOW IS ITS OWN DECLARATION
/// (TEST-NEEDS-1 fix round, F6): a tier does not imply it — JAHSHAKA_TEST_WINDOW=1280x720 (the
/// ctest helpers' `TEST_WINDOW`) asks for it, and a row that does not keeps the screen-sized
/// boot window its bars were measured at (no silent bar move when a row gained a tier).
constexpr int kWindowWidth = 1280;
constexpr int kWindowHeight = 720;
constexpr const char *kWindowEnvVar = "JAHSHAKA_TEST_WINDOW";
inline bool testWindow()
{
    return qgetenv(kWindowEnvVar).trimmed() == QByteArrayLiteral("1280x720");
}

/// The environment form of `--test-tier`, for a runner that cannot pass an argument.
constexpr const char *kEnvVar = "JAHSHAKA_TEST_TIER";

/// WHAT A TEST-TIER PROCESS NEEDS beyond the bare tier (WORLD-MODE-1; owner: "a test suite
/// passes variables for what it needs"): JAHSHAKA_TEST_NEEDS, space-separated (`photon`,
/// `bloom`). Each World Mode runs Photon at its own name, so a test-tier scene is booted with
/// Photon and bloom OFF unless named here (worldmodes::applyTestTier). Read only while a test
/// tier is active; a process with none honours the document.
constexpr const char *kNeedsEnvVar = "JAHSHAKA_TEST_NEEDS";
inline QStringList needs()
{
    return QString::fromLocal8Bit(qgetenv(kNeedsEnvVar)).toLower()
        .split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
}
inline bool needs(const QString &what) { return needs().contains(what.toLower()); }

/// THE FIVE SWITCHABLE FEATURES (TEST-NEEDS-1): every one a test-tier process boots OFF unless
/// JAHSHAKA_TEST_NEEDS names it — Photon, bloom, SSAO, SMAA and the planar mirrors
/// (worldmodes::applyTestTier holds the row each word switches). Shadows, the sun and Atom are
/// never switchable: they are the scene, not a feature of the chain. `none` is the explicit
/// empty list — "this process reads no picture" — and stands alone. Every app-binary ctest row
/// declares the list (jah_gpu_row / jah_add_pool `NEEDS`, tests/support/vram_tokens.cmake);
/// main() refuses a word outside this list, `none` beside another word, and a list with no
/// test tier, like any other argument the app cannot honour.
inline QStringList switchable()
{
    return { QStringLiteral("photon"), QStringLiteral("bloom"), QStringLiteral("ssao"),
             QStringLiteral("smaa"), QStringLiteral("planar") };
}
constexpr const char *kNoNeeds = "none";
/// "" when `words` is a valid declaration, else what is wrong with it.
inline QString needsError(const QStringList &words)
{
    if (words.contains(QLatin1String(kNoNeeds)))
        return words.size() == 1 ? QString()
                                 : QStringLiteral("'none' stands alone (got '%1')").arg(words.join(QLatin1Char(' ')));
    for (const QString &w : words)
        if (!switchable().contains(w))
            return QStringLiteral("'%1' is not a switchable feature (%2, or none)")
                .arg(w, switchable().join(QStringLiteral(", ")));
    return QString();
}

}   // namespace testtier

#endif // TESTTIER_H
