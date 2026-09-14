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

namespace {

// THE PORT, RANGE-CHECKED AND SAID OUT LOUD (ledger 150). This was
// `quint16(QByteArray(arg).toUInt())`, which does two silent things: a
// non-numeric argument becomes 0 (= "pick an ephemeral port", the opposite of
// an error) and anything above 65535 is TRUNCATED mod 65536. That is not
// theoretical — `--mcp-port=8716336` became 48, the app tried to bind a
// PROTECTED port, failed, and quit down an unordered exit path that ended in a
// SIGSEGV on the way out (the exit path is fixed too; this is the trigger).
//
// 0 STAYS LEGAL and still means EPHEMERAL: several driver suites boot the app
// at once and cannot name a fixed port (TEST_GATE_AUDIT.md §4.1). Everything
// else must be 1..65535 and must be a number and nothing else.
void takePort(CliOptions &o, const char *text)
{
    o.mcpServe = true;
    // NO trimmed() (F4, round 2): it would have made " 80" and "80 " legal
    // while this comment said they were not, and a port with a space in it is a
    // quoting mistake in the caller's command line — the kind of thing that is
    // worth a message rather than a guess.
    const QByteArray raw(text);
    bool digits = !raw.isEmpty();
    for (char c : raw) if (c < '0' || c > '9') digits = false;   // no sign, no spaces, no suffix
    bool ok = false;
    const qulonglong value = digits ? raw.toULongLong(&ok) : 0ull;
    if (!digits || !ok || value > 65535ull) {
        o.errors << QStringLiteral(
            "--mcp-port: '%1' is not a port number (expected 0 for an ephemeral "
            "port, or 1-65535)").arg(QString::fromLocal8Bit(raw));
        return;
    }
    o.mcpPort = quint16(value);
}

}   // namespace

CliOptions CliOptions::parse(int argc, char *argv[])
{
    CliOptions o;
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--engine-preview") == 0) o.enginePreviewOnly = true;
        else if (qstrcmp(argv[i], "--engine-selftest") == 0 && i + 1 < argc) o.selftestPng = QString::fromLocal8Bit(argv[++i]);
        else if (qstrcmp(argv[i], "--script") == 0 && i + 1 < argc) o.scriptPath = QString::fromLocal8Bit(argv[++i]);
        else if (qstrcmp(argv[i], "--headless") == 0) o.headlessScript = true;
        else if (qstrcmp(argv[i], "--dump-api-docs") == 0 && i + 1 < argc) o.dumpDocsPath = QString::fromLocal8Bit(argv[++i]);
        // 0 is EPHEMERAL, not "off" — see CliOptions::mcpServe. A bare
        // `--mcp-port` with nothing after it is REFUSED rather than ignored
        // (F4, round 2): silently dropping it would start an app the caller
        // believes is serving MCP, and the caller then waits for a token line
        // that never comes.
        else if (qstrncmp(argv[i], "--mcp-port=", 11) == 0) takePort(o, argv[i] + 11);
        else if (qstrcmp(argv[i], "--mcp-port") == 0) {
            if (i + 1 < argc) takePort(o, argv[++i]);
            else { o.mcpServe = true;
                   o.errors << QStringLiteral("--mcp-port: needs a port number after it "
                                              "(0 for an ephemeral port, or 1-65535)"); }
        }
        else if (qstrcmp(argv[i], "--clear-shader-cache") == 0) o.clearShaderCache = true;
        else if (qstrncmp(argv[i], "--data-root=", 12) == 0) o.dataRoot = QString::fromLocal8Bit(argv[i] + 12);
        else if (qstrcmp(argv[i], "--data-root") == 0 && i + 1 < argc) o.dataRoot = QString::fromLocal8Bit(argv[++i]);
        // The session log (SESSION_LOG_SPEC §3.5). --log-level is REPEATABLE
        // and comma-separated, because retuning three categories for one
        // debugging run should not need three different spellings.
        else if (qstrncmp(argv[i], "--log-level=", 12) == 0) o.logLevels << QString::fromLocal8Bit(argv[i] + 12);
        else if (qstrcmp(argv[i], "--log-level") == 0 && i + 1 < argc) o.logLevels << QString::fromLocal8Bit(argv[++i]);
        else if (qstrncmp(argv[i], "--log-file=", 11) == 0) o.logFile = QString::fromLocal8Bit(argv[i] + 11);
        else if (qstrcmp(argv[i], "--log-file") == 0 && i + 1 < argc) o.logFile = QString::fromLocal8Bit(argv[++i]);
        else if (qstrncmp(argv[i], "--log-dir=", 10) == 0) o.logDir = QString::fromLocal8Bit(argv[i] + 10);
        else if (qstrcmp(argv[i], "--log-dir") == 0 && i + 1 < argc) o.logDir = QString::fromLocal8Bit(argv[++i]);
        else if (qstrcmp(argv[i], "--no-log") == 0) o.noLog = true;
        else if (qstrcmp(argv[i], "--no-ray-query") == 0) o.noRayQuery = true;
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

#ifdef Q_OS_LINUX
    // The engine (Ogre-Next) has no Wayland backend: xcb, always — unless the
    // user chose a platform themselves (or a headless run went offscreen above).
    // Linux-only: macOS (cocoa) and Windows (windows) must keep Qt's native
    // platform — forcing xcb there aborts at startup.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "xcb");
#endif
}
