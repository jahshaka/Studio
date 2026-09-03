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
    /// --clear-shader-cache: deletes the persistent shader cache BEFORE the
    /// engine starts, then continues into a normal run (SHADER_CACHE_SPEC §4.5).
    /// Our `r.InvalidateCachedShaders`, and the flag every benchmark of a cold
    /// start has to use.
    bool clearShaderCache = false;

    static CliOptions parse(int argc, char *argv[]);

    /// Chooses the QPA platform BEFORE QApplication exists: offscreen for
    /// headless runs, else xcb (the engine has no Wayland backend) unless the
    /// user chose a platform themselves.
    void applyPlatformPolicy() const;
};

#endif // CLIOPTIONS_H
