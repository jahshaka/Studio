/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#ifndef APP_FIRSTRUN_H
#define APP_FIRSTRUN_H

// THE ONE ANSWER TO "is a PERSON using this editor, or is a machine driving it?"
// — and the first-launch greeting that is the only thing which asks (owner
// decision D3, 2026-09-12).
//
// The donate dialog used to run MODALLY inside MainWindow::closeEvent. That was
// wrong twice over:
//
//   * it was the last thing a user saw on the way out of the application, which
//     is the worst possible moment to ask anybody for anything; and
//   * a modal dialog inside closeEvent is a nested event loop inside the quit
//     path, so `app.quit()` could not complete until somebody clicked it. Four
//     app-spawning suites had to seed `ddialog_seen` in the settings file of the
//     binary they were about to spawn just to be able to quit it at all
//     (tests/support/seedsettings.h), and when that seeding missed the file a
//     RelWithDebInfo build actually read, the failure looked like a shutdown
//     ordering bug and had nothing to do with shutdown.
//
// It now runs ONCE, at FIRST LAUNCH, from main() — after the window is up and
// with no nested loop anywhere near the close path. The flag is the same
// `ddialog_seen` the dialog's own checkbox writes, so an existing user who
// already ticked it never sees it again.
//
// WHAT "DRIVEN" MEANS is the interesting half, and it is spelled out ONCE here
// rather than re-derived at the call site: a run that nobody is looking at must
// not put a modal window on screen, because there is nobody to dismiss it and
// the run then hangs on its own budget. Five signals say "driven", and each one
// is a real way this application is started by something other than a person.

#include <QByteArray>
#include <QtGlobal>

#include "app/cli/clioptions.h"
#include "services/apppaths.h"

namespace FirstRun {

/// True when this process is being DRIVEN (a test, a script, an agent, CI) and
/// must never show an unprompted modal window.
///
/// The five signals, each independently sufficient:
///   1. a data root was forced (`--data-root` / `JAHSHAKA_DATA_ROOT`) — the
///      hermetic-run flag; every app-spawning suite and every rig launch passes
///      it, and nothing else does.
///   2. `--script` / `--headless` — a script run.
///   3. `--dump-api-docs` — the docs generator.
///   4. `--mcp-port` — an MCP session; the client on the other end has no hands.
///   5. `--engine-selftest`, or `QT_QPA_PLATFORM=offscreen` — there is no screen
///      to show it on.
inline bool isDrivenSession(const CliOptions &cli)
{
    if (AppPaths::isOverridden())            return true;
    if (!cli.scriptPath.isEmpty())            return true;
    if (cli.headlessScript)                   return true;
    if (!cli.dumpDocsPath.isEmpty())          return true;
    if (cli.mcpServe)                         return true;
    if (!cli.selftestPng.isEmpty())           return true;
    if (qgetenv("QT_QPA_PLATFORM") == "offscreen") return true;
    return false;
}

/// True when the first-launch donate dialog should be shown for this run.
/// `alreadySeen` is the persisted `ddialog_seen` preference.
inline bool shouldGreet(const CliOptions &cli, bool alreadySeen)
{
    return !alreadySeen && !isDrivenSession(cli);
}

}   // namespace FirstRun

#endif // APP_FIRSTRUN_H
