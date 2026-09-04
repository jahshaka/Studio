// claude.chat — CLAUDE_EDITOR_SPEC phases 2+3, the honestly-headless parts:
//   1. ClaudeLaunchConfig: the generated .claude/ MCP wiring + the versioned
//      skills install + the TOTAL-LOCKDOWN argv (owner decision 2) + the
//      per-project session persistence (owner decision 3).
//   2. ClaudeStreamParser: fixture transcripts — a REAL captured
//      `claude -p --output-format stream-json --include-partial-messages`
//      turn (turn_simple.jsonl, CLI 2.1.251), a handcrafted tool-use loop,
//      and an error turn with a garbage line.
//   3. ClaudeCliProbe: found / not-found / broken CLI via JAHSHAKA_CLAUDE_CLI.
//   4. ClaudeChatHost against a FAKE claude CLI (a bash script speaking
//      stream-json): full send→stream→result round-trip, argv lockdown at the
//      spawn boundary, session-id persistence, Clear semantics, --resume.
//   5. ClaudeChatWindow smoke (offscreen): flags, states, geometry.
// The real-CLI end-to-end (a live chat turn driving the MCP server) is a
// manual test — it needs a logged-in Claude Code.

#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <cstdio>
#include <functional>

#include "scripting/claude/claudechathost.h"
#include "scripting/claude/claudecliprobe.h"
#include "scripting/claude/claudelaunchconfig.h"
#include "scripting/claude/claudestreamparser.h"
#include "ui/windows/claudechatwindow.h"

static int failures = 0;
#define CHECK(cond, name)                                                  \
    do {                                                                   \
        if (cond) { std::printf("ok: %s\n", name); }                       \
        else { std::printf("FAIL: %s\n", name); ++failures; }              \
    } while (0)

static QByteArray readFile(const QString &path)
{
    QFile file(path);
    file.open(QIODevice::ReadOnly);
    return file.readAll();
}

static bool writeFile(const QString &path, const QByteArray &content)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    file.write(content);
    return true;
}

/// Pumps the event loop until predicate() or timeout. True = predicate held.
static bool waitFor(const std::function<bool()> &predicate, int timeoutMs = 8000)
{
    QEventLoop loop;
    QTimer poll, deadline;
    bool ok = false;
    poll.setInterval(20);
    QObject::connect(&poll, &QTimer::timeout, [&]() {
        if (predicate()) { ok = true; loop.quit(); }
    });
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    poll.start();
    deadline.start(timeoutMs);
    loop.exec();
    return ok;
}

// ---------------------------------------------------------------- config ----
static void testLaunchConfig()
{
    QTemporaryDir projectDir;
    const QString project = projectDir.path();

    // MCP wiring file.
    QString error;
    CHECK(ClaudeLaunchConfig::writeProjectConfig(project, 8639, "sekrit-token", &error),
          "config: writeProjectConfig succeeds");
    const QByteArray mcpJson = readFile(ClaudeLaunchConfig::mcpConfigPath(project));
    CHECK(mcpJson.contains("\"http://127.0.0.1:8639/mcp\""), "config: mcp url carries the live port");
    CHECK(mcpJson.contains("Bearer sekrit-token"), "config: mcp header carries the live token");
    CHECK(mcpJson.contains("\"jahshaka\""), "config: server is named jahshaka");

    // Skills installed, versioned.
    for (const QString &name : ClaudeLaunchConfig::skillNames()) {
        const QString path = project + "/.claude/skills/" + name + "/SKILL.md";
        const QString markdown = QString::fromUtf8(readFile(path));
        CHECK(!markdown.isEmpty(), qPrintable("config: skill installed: " + name));
        CHECK(markdown.contains("name: " + name), qPrintable("config: frontmatter name: " + name));
        CHECK(ClaudeLaunchConfig::skillVersion(markdown) >= 1,
              qPrintable("config: frontmatter version: " + name));
    }
    // Skills mention only real verbs — spot-check a few signatures.
    const QString sceneSkill = QString::fromUtf8(
        readFile(project + "/.claude/skills/jahshaka-scene-building/SKILL.md"));
    CHECK(sceneSkill.contains("scene.addPrimitive") && sceneSkill.contains("world.gi")
              && sceneSkill.contains("run_script"),
          "config: scene skill grounded in real verbs");

    // Version upgrade: an OLD copy is refreshed…
    const QString target = project + "/.claude/skills/jahshaka-materials/SKILL.md";
    writeFile(target, "---\nname: jahshaka-materials\nversion: 0\n---\nstale\n");
    ClaudeLaunchConfig::writeProjectConfig(project, 8639, "sekrit-token");
    CHECK(!QString::fromUtf8(readFile(target)).contains("stale"),
          "config: older installed skill upgraded");
    // …a NEWER (user-modified) copy is left alone.
    writeFile(target, "---\nname: jahshaka-materials\nversion: 999\n---\nuser edit\n");
    ClaudeLaunchConfig::writeProjectConfig(project, 8639, "sekrit-token");
    CHECK(QString::fromUtf8(readFile(target)).contains("user edit"),
          "config: newer user skill kept");

    // MCP off removes the wiring (no stale token lingers).
    ClaudeLaunchConfig::writeProjectConfig(project, 0, QString());
    CHECK(!QFile::exists(ClaudeLaunchConfig::mcpConfigPath(project)),
          "config: mcp-off removes the wiring file");

    // ---- the lockdown argv (owner decision 2) ----
    const QStringList args = ClaudeLaunchConfig::arguments(project, true);
    const QString joined = args.join(' ');
    CHECK(args.contains("-p") && joined.contains("--output-format stream-json")
              && joined.contains("--input-format stream-json"),
          "argv: stream-json in and out");
    CHECK(args.contains("--include-partial-messages"), "argv: partial messages on");
    const int toolsAt = args.indexOf("--tools");
    CHECK(toolsAt >= 0 && args.value(toolsAt + 1) == "Skill",
          "argv: --tools Skill (every other built-in tool removed)");
    CHECK(args.contains("--strict-mcp-config"), "argv: strict mcp config");
    const int mcpAt = args.indexOf("--mcp-config");
    CHECK(mcpAt >= 0 && args.value(mcpAt + 1) == ClaudeLaunchConfig::mcpConfigPath(project),
          "argv: only OUR mcp config");
    // The allow list is the SERVER GLOB, never a hand-typed tool list: a tool
    // added to McpTools::listTools must be usable in the dock the same day
    // (CLAUDE_EDITOR_SPEC §C a-glob). A name-by-name list is the regression.
    const int allowAt = args.indexOf("--allowedTools");
    const QStringList allowed = args.value(allowAt + 1).split(',');
    CHECK(allowAt >= 0 && allowed.size() == 2 && allowed.contains("Skill")
              && allowed.contains("mcp__jahshaka__*"),
          "argv: allow list is Skill + the jahshaka server glob");
    CHECK(!args.value(allowAt + 1).contains("mcp__jahshaka__run_script"),
          "argv: no per-tool names in the allow list");
    const int denyAt = args.indexOf("--disallowedTools");
    CHECK(denyAt >= 0 && args.value(denyAt + 1).contains("Bash")
              && args.value(denyAt + 1).contains("WebFetch")
              && args.value(denyAt + 1).contains("Write"),
          "argv: shell/file/network denied outright");
    CHECK(!joined.contains("dangerously-skip-permissions"), "argv: no permission bypass");
    CHECK(!args.contains("--resume"), "argv: no resume without a session");

    // MCP off: no mcp config, no mcp tools in the allow list.
    const QStringList offArgs = ClaudeLaunchConfig::arguments(project, false);
    CHECK(!offArgs.contains("--mcp-config"), "argv: mcp-off drops --mcp-config");
    CHECK(offArgs.value(offArgs.indexOf("--allowedTools") + 1) == "Skill",
          "argv: mcp-off allow list is Skill only");

    // Resume.
    const QStringList resumeArgs = ClaudeLaunchConfig::arguments(project, true, "sess-42");
    const int resumeAt = resumeArgs.indexOf("--resume");
    CHECK(resumeAt >= 0 && resumeArgs.value(resumeAt + 1) == "sess-42",
          "argv: --resume carries the stored session id");

    // ---- model (owner decision: the dock pins the BIG model) ----
    CHECK(!args.contains("--model"), "argv: no --model when none is chosen");
    CHECK(!ClaudeLaunchConfig::defaultModel().isEmpty(),
          "config: a default model is defined (the dock no longer inherits silently)");
    const QStringList modelArgs = ClaudeLaunchConfig::arguments(
        project, true, QString(), ClaudeLaunchConfig::defaultModel());
    const int modelAt = modelArgs.indexOf("--model");
    CHECK(modelAt >= 0 && modelArgs.value(modelAt + 1) == ClaudeLaunchConfig::defaultModel(),
          "argv: --model carries the chosen model");

    // ---- session persistence (owner decision 3) ----
    CHECK(ClaudeLaunchConfig::readSessionId(project).isEmpty(), "session: none initially");
    CHECK(ClaudeLaunchConfig::writeSessionId(project, "sess-42"), "session: persisted");
    CHECK(ClaudeLaunchConfig::readSessionId(project) == "sess-42", "session: read back");
    ClaudeLaunchConfig::clearSessionId(project);
    CHECK(ClaudeLaunchConfig::readSessionId(project).isEmpty(), "session: cleared");
}

// ---------------------------------------------------------------- parser ----
struct ParserCapture {
    QString sessionId;
    QStringList tools, servers;
    bool mcpConnected = false;
    QString streamed;                 // concatenated deltas
    QStringList finals;               // assistantText payloads
    QStringList toolNames, toolInputs;
    QStringList toolResults;
    int completions = 0;
    bool lastOk = false;
    QString lastResult;
    int parseErrors = 0;

    void attach(ClaudeStreamParser &parser) {
        QObject::connect(&parser, &ClaudeStreamParser::sessionStarted,
                         [this](const QString &id, const QStringList &t, const QStringList &s, bool c) {
                             sessionId = id; tools = t; servers = s; mcpConnected = c;
                         });
        QObject::connect(&parser, &ClaudeStreamParser::textDelta,
                         [this](const QString &text) { streamed += text; });
        QObject::connect(&parser, &ClaudeStreamParser::assistantText,
                         [this](const QString &text) { finals << text; });
        QObject::connect(&parser, &ClaudeStreamParser::toolUseStarted,
                         [this](const QString &name, const QString &input) {
                             toolNames << name; toolInputs << input;
                         });
        QObject::connect(&parser, &ClaudeStreamParser::toolResult,
                         [this](const QString &snippet, bool) { toolResults << snippet; });
        QObject::connect(&parser, &ClaudeStreamParser::turnCompleted,
                         [this](bool ok, const QString &result, const QString &, double) {
                             ++completions; lastOk = ok; lastResult = result;
                         });
        QObject::connect(&parser, &ClaudeStreamParser::parseError,
                         [this](const QString &) { ++parseErrors; });
    }
};

static void testParser(const QString &fixtureDir)
{
    // A REAL captured transcript.
    {
        ClaudeStreamParser parser;
        ParserCapture capture;
        capture.attach(parser);
        parser.feed(readFile(fixtureDir + "/turn_simple.jsonl"));
        CHECK(capture.sessionId == "7c2c89ba-5082-4a04-b9b9-540e42cf7f06",
              "parser: real transcript session id");
        CHECK(capture.streamed == "Hello from Jahshaka", "parser: real deltas concatenate");
        CHECK(capture.finals.size() == 1 && capture.finals[0] == "Hello from Jahshaka",
              "parser: real final assistant text");
        CHECK(capture.completions == 1 && capture.lastOk
                  && capture.lastResult == "Hello from Jahshaka",
              "parser: real turn completes ok");
        CHECK(capture.parseErrors == 0, "parser: real transcript parses clean");
    }
    // The same bytes fed one chunk at a time (line-split robustness).
    {
        ClaudeStreamParser parser;
        ParserCapture capture;
        capture.attach(parser);
        const QByteArray all = readFile(fixtureDir + "/turn_simple.jsonl");
        for (int i = 0; i < all.size(); i += 7) parser.feed(all.mid(i, 7));
        CHECK(capture.streamed == "Hello from Jahshaka" && capture.completions == 1,
              "parser: 7-byte chunked feed gives identical output");
    }
    // Tool-use loop.
    {
        ClaudeStreamParser parser;
        ParserCapture capture;
        capture.attach(parser);
        parser.feed(readFile(fixtureDir + "/turn_tooluse.jsonl"));
        CHECK(capture.mcpConnected, "parser: init reports jahshaka connected");
        CHECK(capture.tools.contains("mcp__jahshaka__run_script"),
              "parser: init lists the mcp tools");
        CHECK(capture.toolNames == QStringList{"mcp__jahshaka__run_script"},
              "parser: tool use surfaced once with its name");
        CHECK(capture.toolInputs.value(0).contains("scene.addPrimitive('cube')"),
              "parser: tool input json is complete");
        CHECK(capture.toolResults.size() == 1
                  && capture.toolResults[0].contains("node_7"),
              "parser: tool result snippet surfaced");
        CHECK(capture.finals.size() == 2
                  && capture.finals.last() == QString::fromUtf8("Done — the cube is in the scene."),
              "parser: both assistant messages finalized");
        CHECK(capture.completions == 1 && capture.lastOk, "parser: tool turn completes ok");
    }
    // Error turn + garbage line.
    {
        ClaudeStreamParser parser;
        ParserCapture capture;
        capture.attach(parser);
        parser.feed(readFile(fixtureDir + "/turn_error.jsonl"));
        CHECK(capture.parseErrors == 1, "parser: garbage line raises parseError, not a stop");
        CHECK(capture.completions == 1 && !capture.lastOk
                  && capture.lastResult.contains("401"),
              "parser: error result surfaces is_error + text");
        CHECK(!capture.mcpConnected, "parser: failed mcp server not reported connected");
    }
}

// ----------------------------------------------------------------- probe ----
static void testProbe(const QString &scratch)
{
    // Found: a fake CLI that answers --version.
    const QString good = scratch + "/fake-claude-good";
    writeFile(good, "#!/bin/bash\nif [ \"$1\" = \"--version\" ]; then echo \"9.9.9 (Fake Claude)\"; exit 0; fi\nexit 0\n");
    QFile::setPermissions(good, QFile::permissions(good) | QFile::ExeOwner);
    qputenv("JAHSHAKA_CLAUDE_CLI", good.toUtf8());
    auto found = ClaudeCliProbe::probe();
    CHECK(found.status == ClaudeCliProbe::Status::Found && found.version == "9.9.9",
          "probe: fake CLI found with parsed version");

    // NotFound: nonexistent program.
    qputenv("JAHSHAKA_CLAUDE_CLI", (scratch + "/does-not-exist").toUtf8());
    CHECK(ClaudeCliProbe::probe().status == ClaudeCliProbe::Status::NotFound,
          "probe: missing CLI = NotFound");

    // Error: runs but no version.
    const QString bad = scratch + "/fake-claude-bad";
    writeFile(bad, "#!/bin/bash\necho \"boom\"\nexit 1\n");
    QFile::setPermissions(bad, QFile::permissions(bad) | QFile::ExeOwner);
    qputenv("JAHSHAKA_CLAUDE_CLI", bad.toUtf8());
    CHECK(ClaudeCliProbe::probe().status == ClaudeCliProbe::Status::Error,
          "probe: broken CLI = Error");
    qunsetenv("JAHSHAKA_CLAUDE_CLI");
}

// ------------------------------------------------------------------ host ----
static void testHost(const QString &scratch)
{
    // The fake claude: records argv, then answers each stdin line with a
    // canned stream-json turn (session id fake-session-1).
    const QString fake = scratch + "/fake-claude";
    const QString argvLog = scratch + "/fake-claude-argv.txt";
    const QString script = QString(
        "#!/bin/bash\n"
        "echo \"$@\" > '%1'\n"
        "first=1\n"
        "while IFS= read -r line; do\n"
        "  case \"$line\" in *control_request*) continue;; esac\n"
        "  if [ $first = 1 ]; then\n"
        "    printf '{\"type\":\"system\",\"subtype\":\"init\",\"session_id\":\"fake-session-1\",\"tools\":[\"Skill\"],\"mcp_servers\":[{\"name\":\"jahshaka\",\"status\":\"connected\"}]}\\n'\n"
        "    first=0\n"
        "  fi\n"
        "  printf '{\"type\":\"stream_event\",\"event\":{\"type\":\"content_block_start\",\"index\":0,\"content_block\":{\"type\":\"text\",\"text\":\"\"}},\"session_id\":\"fake-session-1\"}\\n'\n"
        "  printf '{\"type\":\"stream_event\",\"event\":{\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"text_delta\",\"text\":\"pong\"}},\"session_id\":\"fake-session-1\"}\\n'\n"
        "  printf '{\"type\":\"assistant\",\"message\":{\"role\":\"assistant\",\"content\":[{\"type\":\"text\",\"text\":\"pong\"}]},\"session_id\":\"fake-session-1\"}\\n'\n"
        "  printf '{\"type\":\"result\",\"subtype\":\"success\",\"is_error\":false,\"result\":\"pong\",\"session_id\":\"fake-session-1\",\"total_cost_usd\":0.01}\\n'\n"
        "done\n").arg(argvLog);
    writeFile(fake, script.toUtf8());
    QFile::setPermissions(fake, QFile::permissions(fake) | QFile::ExeOwner);
    qputenv("JAHSHAKA_CLAUDE_CLI", fake.toUtf8());

    QTemporaryDir projectDir;
    const QString project = projectDir.path();

    ClaudeChatHost host;
    QString streamed;
    int completions = 0;
    QObject::connect(host.parser(), &ClaudeStreamParser::textDelta,
                     [&](const QString &text) { streamed += text; });
    QObject::connect(host.parser(), &ClaudeStreamParser::turnCompleted,
                     [&](bool, const QString &, const QString &, double) { ++completions; });

    CHECK(host.configure(project, true, 8639, "tok"), "host: configure writes the project config");
    CHECK(QFile::exists(ClaudeLaunchConfig::mcpConfigPath(project)),
          "host: configure produced the mcp wiring");

    host.sendMessage("ping");
    CHECK(host.isBusy(), "host: busy during the turn");
    CHECK(waitFor([&]() { return completions == 1; }), "host: turn completed via the fake CLI");
    CHECK(streamed == "pong", "host: streamed text arrived");
    CHECK(!host.isBusy(), "host: idle after the turn");
    CHECK(host.sessionId() == "fake-session-1", "host: session id captured");
    CHECK(ClaudeLaunchConfig::readSessionId(project) == "fake-session-1",
          "host: session id persisted per project");

    // The spawn boundary got the lockdown argv — with the SERVER GLOB, and the
    // pinned model (the dock no longer inherits the user's terminal default).
    const QString argv = QString::fromUtf8(readFile(argvLog));
    CHECK(argv.contains("--tools Skill") && argv.contains("--strict-mcp-config")
              && argv.contains("mcp__jahshaka__*"),
          "host: fake CLI spawned with the lockdown argv");
    CHECK(argv.contains("--model " + ClaudeLaunchConfig::defaultModel()),
          "host: the spawn carries the pinned model");
    CHECK(!argv.contains("--resume"), "host: first run has no --resume");

    // Second process resumes the persisted session.
    host.shutdown();
    host.sendMessage("ping again");
    CHECK(waitFor([&]() { return completions == 2; }), "host: second turn after shutdown");
    const QString argv2 = QString::fromUtf8(readFile(argvLog));
    CHECK(argv2.contains("--resume fake-session-1"), "host: restart resumes the session");

    // Clear forgets the session (owner decision 3).
    host.clearSession();
    CHECK(host.sessionId().isEmpty() && ClaudeLaunchConfig::readSessionId(project).isEmpty(),
          "host: Clear forgets the session");
    host.sendMessage("fresh");
    CHECK(waitFor([&]() { return completions == 3; }), "host: fresh turn after Clear");
    CHECK(!QString::fromUtf8(readFile(argvLog)).contains("--resume"),
          "host: post-Clear run starts without --resume");
    host.shutdown();

    // ---- D1: a PROJECT SWITCH ends the conversation ----------------------
    // configure() already stopped the process on a folder change; nothing told
    // the window, so a chat left open across a switch showed the old project's
    // transcript beside the new project's session.
    {
        host.sendMessage("hello project one");
        CHECK(waitFor([&]() { return completions == 4; }), "host: a turn in project one");
        QTemporaryDir otherDir;
        int switches = 0;
        QString switchedTo;
        QObject::connect(&host, &ClaudeChatHost::projectChanged,
                         [&](const QString &folder) { ++switches; switchedTo = folder; });
        CHECK(host.configure(otherDir.path(), true, 8639, "tok"),
              "host: configure to a second project");
        CHECK(switches == 1 && switchedTo == otherDir.path(),
              "D1: the project switch is announced once, with the new folder");
        CHECK(!host.isProcessRunning(), "D1: the old conversation's process is gone");
        CHECK(host.sessionId().isEmpty(),
              "D1: and its session id does not follow into the new project");
        // Switching BACK does not re-announce when there was no conversation.
        host.configure(project, true, 8639, "tok");
    }

    // ---- D3: a STALE session id does not brick the chat -------------------
    // `--resume <unknown-id>` is a hard exit 1. The id used to survive it, so
    // every later send rebuilt the same doomed argv — any ~/.claude prune or
    // copied project folder produced a chat that failed forever until Clear.
    {
        const QString picky = scratch + "/fake-claude-stale-resume";
        const QString pickyScript = QString(
            "#!/bin/bash\n"
            "echo \"$@\" > '%1'\n"
            "case \"$@\" in\n"
            "  *--resume*)\n"
            "    echo 'No conversation found with session ID: stale-session' >&2\n"
            "    exit 1;;\n"
            "esac\n"
            "while IFS= read -r line; do\n"
            "  case \"$line\" in *control_request*) continue;; esac\n"
            "  printf '{\"type\":\"system\",\"subtype\":\"init\",\"session_id\":\"fresh-session\",\"tools\":[\"Skill\"],\"mcp_servers\":[]}\\n'\n"
            "  printf '{\"type\":\"assistant\",\"message\":{\"role\":\"assistant\",\"content\":[{\"type\":\"text\",\"text\":\"recovered\"}]},\"session_id\":\"fresh-session\"}\\n'\n"
            "  printf '{\"type\":\"result\",\"subtype\":\"success\",\"is_error\":false,\"result\":\"recovered\",\"session_id\":\"fresh-session\"}\\n'\n"
            "done\n").arg(argvLog);
        writeFile(picky, pickyScript.toUtf8());
        QFile::setPermissions(picky, QFile::permissions(picky) | QFile::ExeOwner);
        qputenv("JAHSHAKA_CLAUDE_CLI", picky.toUtf8());

        QTemporaryDir staleDir;
        ClaudeLaunchConfig::writeSessionId(staleDir.path(), "stale-session");

        ClaudeChatHost stale;
        int recoveries = 0, staleCompletions = 0, hardFailures = 0;
        QString finalText;
        QObject::connect(&stale, &ClaudeChatHost::sessionResumeFailed,
                         [&]() { ++recoveries; });
        QObject::connect(&stale, &ClaudeChatHost::processFailed,
                         [&](const QString &) { ++hardFailures; });
        QObject::connect(stale.parser(), &ClaudeStreamParser::assistantText,
                         [&](const QString &t) { finalText = t; });
        QObject::connect(stale.parser(), &ClaudeStreamParser::turnCompleted,
                         [&](bool, const QString &, const QString &, double) { ++staleCompletions; });

        stale.configure(staleDir.path(), false, 0, QString());
        CHECK(stale.sessionId() == "stale-session", "D3: the stale id is loaded from disk");
        stale.sendMessage("are you there?");
        CHECK(waitFor([&]() { return staleCompletions == 1; }, 15000),
              "D3: the turn COMPLETES — the host restarted after the failed resume");
        CHECK(recoveries == 1, "D3: the recovery is announced once");
        CHECK(hardFailures == 0, "D3: and it is not reported as a hard failure");
        CHECK(finalText == "recovered", "D3: the user's message was re-queued, not lost");
        CHECK(stale.sessionId() == "fresh-session", "D3: a fresh session id took its place");
        CHECK(ClaudeLaunchConfig::readSessionId(staleDir.path()) == "fresh-session",
              "D3: and the stale id is gone from disk (it cannot come back next launch)");
        stale.shutdown();
        qputenv("JAHSHAKA_CLAUDE_CLI", fake.toUtf8());
    }

    // Failure surfaces (missing CLI).
    qputenv("JAHSHAKA_CLAUDE_CLI", (scratch + "/really-not-there").toUtf8());
    ClaudeChatHost broken;
    broken.configure(project, false, 0, QString());
    QString failure;
    QObject::connect(&broken, &ClaudeChatHost::processFailed,
                     [&](const QString &detail) { failure = detail; });
    broken.sendMessage("hello?");
    CHECK(waitFor([&]() { return !failure.isEmpty(); }), "host: missing CLI reports processFailed");
    CHECK(!broken.isBusy(), "host: not stuck busy after a failed start");
    qunsetenv("JAHSHAKA_CLAUDE_CLI");
}

// ---------------------------------------------------------------- window ----
static void testWindow(const QString &scratch)
{
    const QString iniPath = scratch + "/jahsettings.ini";

    ClaudeCliProbe::Result missing;
    missing.status = ClaudeCliProbe::Status::NotFound;
    missing.program = "claude";
    ClaudeCliProbe::Result present;
    present.status = ClaudeCliProbe::Status::Found;
    present.version = "9.9.9";

    {
        QSettings ini(iniPath, QSettings::IniFormat);
        ClaudeChatWindow window(&ini);
        CHECK((window.windowFlags() & Qt::WindowType_Mask) == Qt::Tool,
              "window: Qt::Tool (floats over the app, not a dock)");
        CHECK(bool(window.windowFlags() & Qt::FramelessWindowHint), "window: frameless");

        // Install state when the CLI is absent (simulated probe result).
        window.setCliState(missing);
        CHECK(window.isInstallStateVisible(), "window: install state when CLI missing");

        // Found + no project -> hint page; + project + mcp off -> banner.
        window.setCliState(present);
        CHECK(!window.isInstallStateVisible(), "window: chat states when CLI found");
        window.setProjectOpen(true);
        window.setMcpRunning(false);
        CHECK(window.isMcpBannerVisible(), "window: MCP-off banner offered");
        window.setMcpRunning(true);
        CHECK(!window.isMcpBannerVisible(), "window: banner gone once MCP runs");

        // Streamed content renders through an attached host's parser.
        ClaudeChatHost host;
        window.setHost(&host);
        const int before = window.messageCount();
        host.parser()->feed(readFile(qEnvironmentVariable("CLAUDECHAT_FIXTURES")
                                     + "/turn_tooluse.jsonl"));
        CHECK(window.messageCount() > before, "window: fixture turn renders bubbles");

        window.resize(444, 555);
        window.show();
        window.close();          // saves geometry
    }
    {
        QSettings ini(iniPath, QSettings::IniFormat);
        CHECK(!ini.value("claude_chat/geometry").toByteArray().isEmpty(),
              "window: geometry persisted under claude_chat/geometry");
        ClaudeChatWindow window(&ini);
        CHECK(window.size() == QSize(444, 555), "window: geometry restored on reopen");
    }
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QTemporaryDir scratch;

    const QString fixtures = qEnvironmentVariable("CLAUDECHAT_FIXTURES");
    if (fixtures.isEmpty() || !QFile::exists(fixtures + "/turn_simple.jsonl")) {
        std::printf("FAIL: CLAUDECHAT_FIXTURES not set or fixtures missing\n");
        return 1;
    }

    testLaunchConfig();
    testParser(fixtures);
    testProbe(scratch.path());
    testHost(scratch.path());
    testWindow(scratch.path());

    std::printf(failures ? "claude.chat: %d FAILURES\n" : "claude.chat: all checks passed\n",
                failures);
    return failures ? 1 : 0;
}
