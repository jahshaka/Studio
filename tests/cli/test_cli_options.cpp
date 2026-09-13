// app.cli_options — THE COMMAND LINE IS AN INTERFACE, so it has a gate
// (ledger 150).
//
// WHY THIS SUITE EXISTS. `--mcp-port=8716336` did not fail. It was parsed as
// `quint16(QByteArray("8716336").toUInt())`, which TRUNCATES mod 65536: the app
// booted the whole editor on port 48, a protected port, failed to bind it and
// quit again — down a CLI exit path that skipped EngineHost::shutdown() and
// ended in a SIGSEGV inside TextureCache::save on the way out. Three defects in
// one line, and the first of them was a silent argument.
//
// So the parser now REFUSES what it cannot honour, and main() prints the
// refusals and exits 2 before QApplication exists. This suite is that contract:
// no window, no engine, no display — the parser is the unit.
#include "app/cli/clioptions.h"

#include <QCoreApplication>
#include <cstdio>
#include <vector>

static int failures = 0;
#define CHECK(cond, ...) do { \
    if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } \
    else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++failures; } \
} while (0)

/// Parses a command line exactly as main() does, so every row below is a
/// string a user or a suite really types.
static CliOptions parsed(std::vector<const char *> args)
{
    args.insert(args.begin(), "Jahshaka");
    std::vector<char *> argv;
    for (const char *a : args) argv.push_back(const_cast<char *>(a));
    return CliOptions::parse(int(argv.size()), argv.data());
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // ---- the good ports ----------------------------------------------------
    {
        const CliOptions o = parsed({ "--mcp-port=8639" });
        CHECK(o.errors.isEmpty() && o.mcpServe && o.mcpPort == 8639,
              "--mcp-port=8639 parses (serve=%d port=%u errors=%d)",
              int(o.mcpServe), unsigned(o.mcpPort), int(o.errors.size()));
    }
    {
        const CliOptions o = parsed({ "--mcp-port", "8639" });
        CHECK(o.errors.isEmpty() && o.mcpServe && o.mcpPort == 8639,
              "the two-token spelling parses the same");
    }
    {
        // 0 IS LEGAL AND MEANS EPHEMERAL, not "off" — several driver suites boot
        // the app at once and cannot name a fixed port (TEST_GATE_AUDIT §4.1).
        // A range check that rejected it would break every one of them.
        const CliOptions o = parsed({ "--mcp-port=0" });
        CHECK(o.errors.isEmpty() && o.mcpServe && o.mcpPort == 0,
              "--mcp-port=0 stays legal and still means EPHEMERAL");
    }
    {
        const CliOptions o = parsed({ "--mcp-port=65535" });
        CHECK(o.errors.isEmpty() && o.mcpPort == 65535, "65535 is the top of the range");
    }

    // ---- the refusals ------------------------------------------------------
    {
        // THE ONE THAT HAPPENED. 8716336 mod 65536 = 48.
        const CliOptions o = parsed({ "--mcp-port=8716336" });
        CHECK(o.errors.size() == 1, "--mcp-port=8716336 is REFUSED (%d error(s))",
              int(o.errors.size()));
        CHECK(o.mcpPort != 48, "...and is NOT silently truncated to 48 (port=%u)",
              unsigned(o.mcpPort));
        if (!o.errors.isEmpty())
            std::printf("info: %s\n", qPrintable(o.errors.first()));
    }
    {
        const CliOptions o = parsed({ "--mcp-port=65536" });
        CHECK(o.errors.size() == 1 && o.mcpPort == 0, "65536 is one past the range and is refused");
    }
    {
        const CliOptions o = parsed({ "--mcp-port=-1" });
        CHECK(o.errors.size() == 1, "a negative port is refused, not wrapped");
    }
    {
        const CliOptions o = parsed({ "--mcp-port=abc" });
        CHECK(o.errors.size() == 1, "a non-numeric port is refused, not read as 0 (= ephemeral)");
    }
    {
        const CliOptions o = parsed({ "--mcp-port=" });
        CHECK(o.errors.size() == 1, "an empty port is refused, not read as 0 (= ephemeral)");
    }
    {
        const CliOptions o = parsed({ "--mcp-port=8639x" });
        CHECK(o.errors.size() == 1, "a trailing-garbage port is refused whole");
    }
    {
        // A SPACE IS A QUOTING MISTAKE, not a value to be salvaged (F4). The
        // parser used to trim first, so these two were accepted while the code
        // beside them said "no spaces".
        const CliOptions o = parsed({ "--mcp-port= 80" });
        CHECK(o.errors.size() == 1, "a leading space is refused, not trimmed away");
    }
    {
        const CliOptions o = parsed({ "--mcp-port=80 " });
        CHECK(o.errors.size() == 1, "a trailing space is refused, not trimmed away");
    }
    {
        // A BARE --mcp-port used to be dropped on the floor: the app started,
        // served nothing, and the caller waited for a token line that never
        // came (F4).
        const CliOptions o = parsed({ "--mcp-port" });
        CHECK(o.errors.size() == 1 && o.mcpServe,
              "a bare --mcp-port with no value is refused, not ignored (%d error(s), serve=%d)",
              int(o.errors.size()), int(o.mcpServe));
        if (!o.errors.isEmpty()) std::printf("info: %s\n", qPrintable(o.errors.first()));
    }
    {
        // ...but a bare --mcp-port as the LAST argument is the only ambiguous
        // spelling; with a value after it nothing changed.
        const CliOptions o = parsed({ "--mcp-port", "8639", "--headless" });
        CHECK(o.errors.isEmpty() && o.mcpPort == 8639 && o.headlessScript,
              "the two-token spelling still consumes exactly one token");
    }
    {
        // THE SEEN-FLAG SURVIVES A REFUSAL on purpose: main() exits on the
        // error, so nothing downstream reads mcpPort — but a future caller that
        // looks at mcpServe first must not be told "MCP was never asked for".
        const CliOptions o = parsed({ "--mcp-port=99999" });
        CHECK(o.mcpServe, "a refused --mcp-port still records that MCP was asked for");
    }

    // ---- a clean command line has no errors --------------------------------
    {
        const CliOptions o = parsed({ "--headless", "--script", "x.js", "--data-root", "/tmp/x" });
        CHECK(o.errors.isEmpty() && o.headlessScript && o.scriptPath == QStringLiteral("x.js") &&
                  o.dataRoot == QStringLiteral("/tmp/x"),
              "an ordinary command line parses with no errors");
    }

    std::printf(failures ? "FAILED: %d check(s)\n" : "ALL CHECKS PASSED\n", failures);
    return failures ? 1 : 0;
}
