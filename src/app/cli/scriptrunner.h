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

class MainWindow;
class QApplication;
class QString;

/// Runs the script with the full verb surface (engine mode) or document-only
/// (--headless). Exit code: 1 on script error, else the script's numeric
/// completion value (clamped 0-255) or 0.
int runScriptFile(MainWindow &window, QApplication &app, const QString &path, bool headless);

/// Writes the registry-generated verb reference (docs/SCRIPTING.md is this
/// output — generated, never hand-edited). Returns the process exit code.
int runDumpApiDocs(MainWindow &window, const QString &outPath);

/// --mcp-port=N [--headless] (CLAUDE_EDITOR_SPEC.md phase 1): boot like a
/// script run (windowed = engine viewport up; headless = document verbs only),
/// start the MCP server on 127.0.0.1:port, print the session token + connect
/// line to stdout, and serve until the app quits.
int runMcpServe(MainWindow &window, QApplication &app, unsigned short port, bool headless);

#endif // SCRIPTRUNNER_H
