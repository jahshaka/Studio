/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCRIPTRUNNER_H
#define SCRIPTRUNNER_H

// --script <file.js> [--headless] and --dump-api-docs <file.md>
// (audit §4.2: app/cli/; SCRIPTING_SPEC §3.2).

#include <QtContainerFwd>   // QStringList

class MainWindow;
class QApplication;
class QString;

/// Runs the script with the full verb surface (engine mode) or document-only
/// (--headless). Exit code: 1 on script error, else the script's numeric
/// completion value (clamped 0-255) or 0.
int runScriptFile(MainWindow &window, QApplication &app, const QString &path, bool headless,
                  bool live = false);

/// --scripts <dir-or-list> [--pool <name>] [--arms <a,b>] [--headless]: THE
/// POOL RUNNER (lane SUITE-POOL-1; jahshaka/docs/TESTING.md). Every arm runs
/// in this ONE process, in a fresh JavaScript realm. The first arm starts on
/// the boot; after EVERY arm, green or red, the pool's baseline runs: the
/// pool's own script (`--pool-baseline`, when it has one), then the runner's —
/// the window back to the boot's size and out of full screen, whatever project
/// the arm left open closed through the verb a script would call
/// (`project.close`, which lands on the desktop page), and the deferred deletes
/// delivered. So a later arm starts with no project open and opens or creates
/// what it needs itself. The protocol, one line each on stdout, flushed:
///     ARM-BEGIN <pool>.<arm>
///     ARM <pool>.<arm> PASS <ms>
///     ARM <pool>.<arm> FAIL <ms> <first failure>
///     POOL-BASELINE-LOST <pool>.<arm> <why>     (then the process exits; the
///                                               driver restarts for the rest)
/// A CRASH is an ARM-BEGIN with no ARM line — the driver's verdict, because a
/// dead process cannot print one. Exit code: the number of failed arms.
int runScriptPool(MainWindow &window, QApplication &app, const QString &scripts,
                  const QString &pool, const QStringList &arms, const QString &poolBaseline,
                  bool headless, bool live);

/// Writes the registry-generated verb reference (docs/SCRIPTING.md is this
/// output — generated, never hand-edited). Returns the process exit code.
int runDumpApiDocs(MainWindow &window, const QString &outPath);

/// --mcp-port=N [--headless] (CLAUDE_EDITOR_SPEC.md phase 1): boot like a
/// script run (windowed = engine viewport up; headless = document verbs only),
/// start the MCP server on 127.0.0.1:port, print the session token + connect
/// line to stdout, and serve until the app quits.
int runMcpServe(MainWindow &window, QApplication &app, unsigned short port, bool headless);

/// The shared exec tail: releases the engine (EngineHost::shutdown) and then
/// guarantees the process actually exits — if the global thread pool still
/// holds a worker after a bounded wait (a stuck import/decode future), it
/// logs and force-exits instead of hanging in QThreadPool's destructor (the
/// owner-reported headless zombie). Returns rc for the normal path.
int finalizeAppExit(int rc);

#endif // SCRIPTRUNNER_H
