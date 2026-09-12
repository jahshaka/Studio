// app.first_launch — WHO IS ALLOWED TO SEE A MODAL WINDOW (owner decision D3,
// 2026-09-12; the predicate is src/app/firstrun.h).
//
// The donate greeting moved off the QUIT path and onto FIRST LAUNCH. What makes
// that safe is one predicate: a run that is being DRIVEN — a suite, a script,
// an MCP client, the selftest, an offscreen boot — never gets an unprompted
// modal window, because there is nobody on the other end to dismiss it and the
// run would then hang on its own budget. That failure mode is not theoretical:
// four app-spawning suites used to seed `ddialog_seen` into the settings file of
// the binary they were about to spawn precisely so they could quit it, and when
// the seeding missed the file a RelWithDebInfo build actually read, the app hung
// in closeEvent and the suite blamed shutdown ordering.
//
// So this suite asserts the TABLE, one row per way of starting the application.
// No window, no engine, no display: the predicate is the unit.
#include "app/cli/clioptions.h"
#include "app/firstrun.h"
#include "services/apppaths.h"

#include <QCoreApplication>
#include <cstdio>
#include <vector>

static int failures = 0;
#define CHECK(cond, ...) do { \
    if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } \
    else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++failures; } \
} while (0)

/// Parses a command line exactly as main() does, so the rows below are the
/// strings a user or a suite really types.
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
    qunsetenv("QT_QPA_PLATFORM");
    qunsetenv("JAHSHAKA_DATA_ROOT");
    AppPaths::initialize();   // no override: the ordinary windowed run

    // ---- the ONE row that greets ------------------------------------------
    CHECK(FirstRun::shouldGreet(parsed({}), false),
          "a plain first launch greets");
    CHECK(!FirstRun::shouldGreet(parsed({}), true),
          "...and never again once the flag is set");
    CHECK(!FirstRun::isDrivenSession(parsed({})),
          "a plain launch is not a driven session");

    // ---- every driven way of starting the application ---------------------
    struct Row { const char *what; std::vector<const char *> args; };
    const Row driven[] = {
        { "--script",          { "--script", "x.js" } },
        { "--headless",        { "--headless" } },
        { "--dump-api-docs",   { "--dump-api-docs", "out.md" } },
        { "--mcp-port",        { "--mcp-port=0" } },
        { "--engine-selftest", { "--engine-selftest", "out.png" } },
        { "--data-root",       { "--data-root", "." } },
    };
    for (const Row &r : driven) {
        // --data-root only counts once AppPaths has resolved it, exactly as
        // main() resolves it: the flag is not read twice in two places.
        const CliOptions cli = parsed(r.args);
        AppPaths::initialize(cli.dataRoot);
        CHECK(FirstRun::isDrivenSession(cli), "%s is a driven session", r.what);
        CHECK(!FirstRun::shouldGreet(cli, false),
              "%s never greets, flag unset or not", r.what);
        AppPaths::initialize();   // back to no override for the next row
    }

    // ---- the environment forms, which is how the rig starts the app -------
    qputenv("JAHSHAKA_DATA_ROOT", ".");
    AppPaths::initialize();
    CHECK(FirstRun::isDrivenSession(parsed({})),
          "JAHSHAKA_DATA_ROOT alone is enough — the rig launches a WINDOWED app "
          "with it and nothing else");
    qunsetenv("JAHSHAKA_DATA_ROOT");
    AppPaths::initialize();

    qputenv("QT_QPA_PLATFORM", "offscreen");
    CHECK(FirstRun::isDrivenSession(parsed({})),
          "QT_QPA_PLATFORM=offscreen is enough — there is no screen to show it on");
    qputenv("QT_QPA_PLATFORM", "xcb");
    CHECK(!FirstRun::isDrivenSession(parsed({})),
          "...and xcb is not (the ordinary Linux launch)");
    qunsetenv("QT_QPA_PLATFORM");

    std::printf(failures ? "FAILED: %d check(s)\n" : "ALL CHECKS PASSED\n", failures);
    return failures ? 1 : 0;
}
