/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef CLIOPTIONS_H
#define CLIOPTIONS_H

// The CLI surface (audit §4.2: app/cli/): argument parsing and the platform
// policy it implies, split out of main.cpp.

#include <QString>
#include <QStringList>

struct CliOptions
{
    /// --engine-preview: only the engine preview dialog, no MainWindow.
    bool enginePreviewOnly = false;
    /// --engine-selftest <out.png>: default scene, one screenshot, exit 0/1.
    QString selftestPng;
    /// --script <file.js> [--headless]: run a script and exit (SCRIPTING_SPEC §3.2).
    QString scriptPath;
    bool headlessScript = false;
    /// --dump-api-docs <file.md>: write the registry-generated verb reference.
    QString dumpDocsPath;
    /// --mcp-port=N: serve MCP on 127.0.0.1:N for this run (implies enabled;
    /// the session token is printed to stdout). With --headless: offscreen,
    /// document verbs only; otherwise windowed with the engine viewport up.
    quint16 mcpPort = 0;
    /// --data-root <dir>: THE hermetic-run flag (WINDOWS_BUILD_SPEC §6.2 W9,
    /// ENGINEERING_DEBT_SPEC ADDENDUM 6). Redirects the library database, the
    /// asset store, the shader cache AND the settings file under one directory.
    /// `HOME=` cannot do the last of those — under QT_DEBUG `jahsettings.ini`
    /// is derived from applicationDirPath(), so every run of a build tree
    /// shares one, which is how a suite came to rewrite the developer's
    /// `[assets] storeId`. `JAHSHAKA_DATA_ROOT` is the same override as an
    /// environment variable, and this flag wins over it (services/apppaths.h).
    QString dataRoot;
    /// --clear-shader-cache: deletes the persistent shader cache BEFORE the
    /// engine starts, then continues into a normal run (SHADER_CACHE_SPEC §4.5).
    /// Our `r.InvalidateCachedShaders`, and the flag every benchmark of a cold
    /// start has to use.
    bool clearShaderCache = false;

    // ---- the session log (SESSION_LOG_SPEC §3.5, layer 3 of the precedence
    // chain: compiled default -> ini -> COMMAND LINE -> runtime) -------------
    /// --log-level=<level> or --log-level=<cat>=<lvl>[,<cat>=<lvl>…], repeatable.
    QStringList logLevels;
    /// --log-file=<path>: the session file itself (its directory becomes the
    /// log dir, so the ogre sibling lands beside it).
    QString logFile;
    /// --log-dir=<path>: THE hermetic-test flag. `HOME=` does not isolate app
    /// data on macOS (CFFIXED_USER_HOME is what CoreFoundation honours), so a
    /// rotation test written with HOME= would write into the developer's real
    /// log directory there — this is the platform-independent isolation.
    QString logDir;
    /// --no-log: routing, levels and the in-memory ring all still work;
    /// nothing reaches disk.
    bool noLog = false;

    static CliOptions parse(int argc, char *argv[]);

    /// Chooses the QPA platform BEFORE QApplication exists: offscreen for
    /// headless runs, else xcb (the engine has no Wayland backend) unless the
    /// user chose a platform themselves.
    void applyPlatformPolicy() const;
};

#endif // CLIOPTIONS_H
