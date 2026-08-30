/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "app/cli/clioptions.h"

#include <QByteArray>
#include <QtGlobal>

CliOptions CliOptions::parse(int argc, char *argv[])
{
    CliOptions o;
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--engine-preview") == 0) o.enginePreviewOnly = true;
        else if (qstrcmp(argv[i], "--engine-selftest") == 0 && i + 1 < argc) o.selftestPng = QString::fromLocal8Bit(argv[++i]);
        else if (qstrcmp(argv[i], "--script") == 0 && i + 1 < argc) o.scriptPath = QString::fromLocal8Bit(argv[++i]);
        else if (qstrcmp(argv[i], "--headless") == 0) o.headlessScript = true;
        else if (qstrcmp(argv[i], "--dump-api-docs") == 0 && i + 1 < argc) o.dumpDocsPath = QString::fromLocal8Bit(argv[++i]);
        else if (qstrncmp(argv[i], "--mcp-port=", 11) == 0) o.mcpPort = quint16(QByteArray(argv[i] + 11).toUInt());
        else if (qstrcmp(argv[i], "--mcp-port") == 0 && i + 1 < argc) o.mcpPort = quint16(QByteArray(argv[++i]).toUInt());
        else if (qstrncmp(argv[i], "--viewport", 10) == 0) {
            // Accepted for compatibility; the engine viewport is the only
            // renderer since the legacy GL viewport was deleted (step 14).
            if (qstrcmp(argv[i], "--viewport=legacy") == 0)
                qWarning("--viewport=legacy: the legacy GL viewport was removed; using the engine viewport.");
        }
    }
    return o;
}

void CliOptions::applyPlatformPolicy() const
{
    if ((headlessScript && (!scriptPath.isEmpty() || mcpPort > 0)) || !dumpDocsPath.isEmpty())
        qputenv("QT_QPA_PLATFORM", "offscreen");

    // The engine (Ogre-Next) has no Wayland backend: xcb, always — unless the
    // user chose a platform themselves (or a headless run went offscreen above).
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "xcb");
}
