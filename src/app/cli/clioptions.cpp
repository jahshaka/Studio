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

#include <cstring>

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

// QT'S OWN OPTIONS, in their double-dash spelling (QGuiApplication strips one
// dash, so `--platform offscreen` is `-platform offscreen`). QApplication reads
// and removes them after this parser has run; they are not ours to refuse. The
// single-dash spellings never reach the unknown-flag check at all.
bool isQtOption(const char *arg, bool *takesValue)
{
    static const char *const withValue[] = {
        "platform", "platformpluginpath", "platformtheme", "plugin", "qwindowgeometry",
        "qwindowicon", "qwindowtitle", "session", "style", "stylesheet", "display",
        "geometry", "name", "title", "visual", "qmljsdebugger" };
    static const char *const bare[] = { "reverse", "widgetcount", "nograb", "dograb", "sync" };
    const char *name = arg + 2;
    const char *eq = std::strchr(name, '=');
    const size_t len = eq ? size_t(eq - name) : std::strlen(name);
    for (const char *w : withValue)
        if (std::strlen(w) == len && std::strncmp(w, name, len) == 0) { *takesValue = !eq; return true; }
    for (const char *b : bare)
        if (!eq && std::strcmp(b, name) == 0) { *takesValue = false; return true; }
    return false;
}

}   // namespace

QString CliOptions::usageText()
{
    return QStringLiteral(
        "Usage: Jahshaka [options]\n"
        "\n"
        "With no options the editor starts. Every option below is the whole list: an\n"
        "unknown --option is refused (exit 2) and nothing starts.\n"
        "\n"
        "  -h, --help                  print this text and exit\n"
        "  --version                   print the version and exit\n"
        "  --data-root <dir>           keep settings, library, asset store and shader cache in <dir>\n"
        "                              (JAHSHAKA_DATA_ROOT is the same override)\n"
        "  --engine-selftest <out.png> render the self-test scene, print the hash lines, exit 0/1\n"
        "  --script <file.js>          run one script and exit\n"
        "  --scripts <dir-or-list>     run a test pool's scripts in one process\n"
        "  --pool <name>               the pool's name (with --scripts)\n"
        "  --arms <a,b>                run only these arms (with --scripts)\n"
        "  --pool-baseline <file.js>   the pool's baseline script (with --scripts)\n"
        "  --headless                  a script/MCP run with no window (offscreen)\n"
        "  --script-live               keep rendering between a script's verbs\n"
        "  --dump-api-docs <file.md>   write the generated scripting reference and exit\n"
        "  --mcp-port <n>              serve MCP on 127.0.0.1:<n> (0 = an ephemeral port)\n"
        "  --test-tier <mode>          put every scene on a World Mode (low, medium, high, epic)\n"
        "  --clear-shader-cache        delete the shader cache before the engine starts\n"
        "  --no-ray-query              boot with the hardware ray-query tier off\n"
        "  --vr                        VR with any OpenXR runtime, the headset checked at startup\n"
        "  --no-vr                     no VR (no OpenXR call) in this run\n"
        "                              (JAHSHAKA_VR=1 / JAHSHAKA_VR=0 are the same two switches)\n"
        "  --log-level <lvl|cat=lvl>   session-log levels (repeatable, comma-separated)\n"
        "  --log-file <path>           the session log file\n"
        "  --log-dir <dir>             the session log directory\n"
        "  --no-log                    write no session log to disk\n"
        "  --watchdog=on|off           the UI-thread watchdog (development builds)\n"
        "  --watchdog-stall=<ms>       the watchdog's stall threshold\n"
        "\n"
        "Qt's own options (-platform, -style, ...) are passed through.\n");
}

CliOptions CliOptions::parse(int argc, char *argv[])
{
    CliOptions o;
    // A value-taking flag at the END of the line is refused, never dropped: a
    // dropped `--engine-selftest` boots the whole editor instead of a self-test.
    auto needsValue = [&o](const char *flag) {
        o.errors << QStringLiteral("%1: needs a value after it").arg(QString::fromLocal8Bit(flag));
    };
    for (int i = 1; i < argc; ++i) {
        const bool hasNext = i + 1 < argc;
        if (qstrcmp(argv[i], "--help") == 0 || qstrcmp(argv[i], "-h") == 0) o.help = true;
        else if (qstrcmp(argv[i], "--version") == 0) o.version = true;
        else if (qstrcmp(argv[i], "--engine-selftest") == 0) {
            if (hasNext) o.selftestPng = QString::fromLocal8Bit(argv[++i]); else needsValue(argv[i]); }
        else if (qstrcmp(argv[i], "--script") == 0) {
            if (hasNext) o.scriptPath = QString::fromLocal8Bit(argv[++i]); else needsValue(argv[i]); }
        else if (qstrcmp(argv[i], "--scripts") == 0) {
            if (hasNext) o.poolScripts = QString::fromLocal8Bit(argv[++i]); else needsValue(argv[i]); }
        else if (qstrcmp(argv[i], "--pool-baseline") == 0) {
            if (hasNext) o.poolBaseline = QString::fromLocal8Bit(argv[++i]); else needsValue(argv[i]); }
        else if (qstrcmp(argv[i], "--pool") == 0) {
            if (hasNext) o.poolName = QString::fromLocal8Bit(argv[++i]); else needsValue(argv[i]); }
        else if (qstrcmp(argv[i], "--arms") == 0) {
            if (hasNext) o.poolArms = QString::fromLocal8Bit(argv[++i]).split(QLatin1Char(','), Qt::SkipEmptyParts);
            else needsValue(argv[i]); }
        else if (qstrcmp(argv[i], "--headless") == 0) o.headlessScript = true;
        else if (qstrcmp(argv[i], "--script-live") == 0) o.liveScript = true;
        else if (qstrcmp(argv[i], "--dump-api-docs") == 0) {
            if (hasNext) o.dumpDocsPath = QString::fromLocal8Bit(argv[++i]); else needsValue(argv[i]); }
        // 0 is EPHEMERAL, not "off" — see CliOptions::mcpServe. A bare
        // `--mcp-port` with nothing after it is REFUSED rather than ignored
        // (F4, round 2): silently dropping it would start an app the caller
        // believes is serving MCP, and the caller then waits for a token line
        // that never comes.
        else if (qstrncmp(argv[i], "--mcp-port=", 11) == 0) takePort(o, argv[i] + 11);
        else if (qstrcmp(argv[i], "--mcp-port") == 0) {
            if (hasNext) takePort(o, argv[++i]);
            else { o.mcpServe = true;
                   o.errors << QStringLiteral("--mcp-port: needs a port number after it "
                                              "(0 for an ephemeral port, or 1-65535)"); }
        }
        else if (qstrcmp(argv[i], "--clear-shader-cache") == 0) o.clearShaderCache = true;
        else if (qstrncmp(argv[i], "--data-root=", 12) == 0) o.dataRoot = QString::fromLocal8Bit(argv[i] + 12);
        else if (qstrcmp(argv[i], "--data-root") == 0) {
            if (hasNext) o.dataRoot = QString::fromLocal8Bit(argv[++i]); else needsValue(argv[i]); }
        // The session log (SESSION_LOG_SPEC §3.5). --log-level is REPEATABLE
        // and comma-separated, because retuning three categories for one
        // debugging run should not need three different spellings.
        else if (qstrncmp(argv[i], "--log-level=", 12) == 0) o.logLevels << QString::fromLocal8Bit(argv[i] + 12);
        else if (qstrcmp(argv[i], "--log-level") == 0) {
            if (hasNext) o.logLevels << QString::fromLocal8Bit(argv[++i]); else needsValue(argv[i]); }
        else if (qstrncmp(argv[i], "--log-file=", 11) == 0) o.logFile = QString::fromLocal8Bit(argv[i] + 11);
        else if (qstrcmp(argv[i], "--log-file") == 0) {
            if (hasNext) o.logFile = QString::fromLocal8Bit(argv[++i]); else needsValue(argv[i]); }
        else if (qstrncmp(argv[i], "--log-dir=", 10) == 0) o.logDir = QString::fromLocal8Bit(argv[i] + 10);
        else if (qstrcmp(argv[i], "--log-dir") == 0) {
            if (hasNext) o.logDir = QString::fromLocal8Bit(argv[++i]); else needsValue(argv[i]); }
        else if (qstrcmp(argv[i], "--no-log") == 0) o.noLog = true;
        else if (qstrcmp(argv[i], "--no-ray-query") == 0) o.noRayQuery = true;
        else if (qstrcmp(argv[i], "--vr") == 0) o.vr = true;
        else if (qstrcmp(argv[i], "--no-vr") == 0) o.noVr = true;
        else if (qstrncmp(argv[i], "--test-tier=", 12) == 0) o.testTier = QString::fromLocal8Bit(argv[i] + 12);
        else if (qstrcmp(argv[i], "--test-tier") == 0) {
            // A bare flag is refused, like --mcp-port: dropping it would boot the
            // Epic chain the caller asked to be spared.
            if (hasNext) o.testTier = QString::fromLocal8Bit(argv[++i]);
            else o.errors << QStringLiteral("--test-tier: needs a World Mode after it (low, medium, high, epic)");
        }
        // The watchdog's two switches are READ by services/mainthreadwatchdog.cpp
        // (MainWindow never sees CliOptions); they are accepted here so the
        // unknown-flag refusal below does not take them.
        else if (qstrcmp(argv[i], "--watchdog=on") == 0 || qstrcmp(argv[i], "--watchdog=off") == 0) {}
        else if (qstrncmp(argv[i], "--watchdog-stall=", 17) == 0) {}
        else if (qstrncmp(argv[i], "--", 2) == 0 && argv[i][2] != '\0') {
            bool takesValue = false;
            if (isQtOption(argv[i], &takesValue)) { if (takesValue && hasNext) ++i; }
            // AN UNKNOWN FLAG NEVER LAUNCHES THE EDITOR (HELP-FLAG-1): it is a
            // typo or a flag of another program, and the answer is one line and
            // exit 2 — not a full boot on whatever display and data root the
            // shell happens to have.
            else o.errors << QStringLiteral("unknown option '%1' (Jahshaka --help lists the options)")
                                 .arg(QString::fromLocal8Bit(argv[i]));
        }
    }
    // ONE SCRIPT RUN AT A TIME: `--script` and `--scripts` are two shapes of
    // the same run, and a command line naming both has no one meaning.
    if (!o.scriptPath.isEmpty() && !o.poolScripts.isEmpty())
        o.errors << QStringLiteral("--script and --scripts: pass one or the other, not both");
    if ((!o.poolName.isEmpty() || !o.poolArms.isEmpty() || !o.poolBaseline.isEmpty()) &&
        o.poolScripts.isEmpty())
        o.errors << QStringLiteral("--pool/--arms/--pool-baseline: need --scripts <dir-or-list>");
    if (o.vr && o.noVr)
        o.errors << QStringLiteral("--vr and --no-vr: pass one or the other, not both");
    return o;
}

CliOptions::VrChoice CliOptions::resolveVr(bool cliVr, bool cliNoVr, const QByteArray &env,
                                           bool startInVr)
{
    if (cliVr)      return { true,  true,  true,  QStringLiteral("--vr") };
    if (cliNoVr)    return { false, false, false, QStringLiteral("--no-vr") };
    if (env == "1") return { true,  true,  true,  QStringLiteral("JAHSHAKA_VR=1") };
    if (env == "0") return { false, false, false, QStringLiteral("JAHSHAKA_VR=0") };
    return { true, false, startInVr, QStringLiteral("setting") };
}

void CliOptions::applyPlatformPolicy() const
{
    if ((headlessScript && (isScriptRun() || mcpPort > 0)) || !dumpDocsPath.isEmpty())
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
