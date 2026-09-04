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
//   6. B2/B3 (AI_SURFACE_PROGRAM_SPEC lane E): the rich transcript — inline
//      images, per-turn cost, permission denials, thinking, rate limits and
//      parse errors, all from a fixture; friendly tool rows; the model picker;
//      the orphan reaper; and interrupt-then-resume, which CLAUDE_EDITOR_SPEC
//      §J listed as unverified.
// The real-CLI end-to-end (a live chat turn driving the MCP server) is a
// manual test — it needs a logged-in Claude Code.

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
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

    // ---- F14: the skills teach the CURRENT surface ------------------------
    // installSkills only replaces a target whose installed version is OLDER
    // than the shipped one, so a content refresh that forgets the version bump
    // ships to nobody. These minimums are the delivery mechanism, asserted.
    struct SkillFloor { const char *name; int version; };
    const SkillFloor floors[] = {
        {"jahshaka-scene-building", 2}, {"jahshaka-materials", 2},
        {"jahshaka-assets", 4},         {"jahshaka-particles", 1},
        {"jahshaka-decals", 1},         {"jahshaka-world", 1}};
    CHECK(ClaudeLaunchConfig::skillNames().size() == 6,
          "skills: six pages ship (particles, decals and world modes joined the three)");
    for (const auto &floor : floors) {
        const QString name = QString::fromLatin1(floor.name);
        CHECK(ClaudeLaunchConfig::skillNames().contains(name),
              qPrintable("skills: shipped: " + name));
        const QString markdown = QString::fromUtf8(
            readFile(project + "/.claude/skills/" + name + "/SKILL.md"));
        CHECK(ClaudeLaunchConfig::skillVersion(markdown) >= floor.version,
              qPrintable(QString("skills: %1 is at least version %2 (the refresh is deliverable)")
                             .arg(name).arg(floor.version)));
    }

    // The refreshed content, spot-checked where it carries a promise: the
    // debugging loop, the look-act-look loop and the discovery-first rule are
    // the three things the audit says the surface was missing.
    const QString sceneMd = QString::fromUtf8(
        readFile(project + "/.claude/skills/jahshaka-scene-building/SKILL.md"));
    CHECK(sceneMd.contains("node.properties"), "skills: scene teaches node.properties discovery");
    CHECK(sceneMd.contains("editor.setCamera") && sceneMd.contains("editor.frameNode")
              && sceneMd.contains("frameNode:"),
          "skills: scene teaches the camera verbs AND the framed screenshot");
    CHECK(sceneMd.contains("app.engineErrors") && sceneMd.contains("editor.viewportState")
              && sceneMd.contains("editor.frame(") && sceneMd.contains("engineErrors` block"),
          "skills: scene has the DEBUGGING section (engineErrors, viewportState, frame(n,dt))");
    CHECK(sceneMd.contains("node.physics") && sceneMd.contains("scene.addViewer")
              && sceneMd.contains("editor.setOverlays"),
          "skills: scene teaches physics, the viewer and the overlay switches");
    CHECK(sceneMd.contains("api.help") && sceneMd.contains("api_docs({search"),
          "skills: scene teaches the cold-start lookups");
    CHECK(sceneMd.contains("ground") && sceneMd.contains("count:"),
          "skills: scene knows the ground primitive and the count option");
    const QString particlesMd = QString::fromUtf8(
        readFile(project + "/.claude/skills/jahshaka-particles/SKILL.md"));
    CHECK(particlesMd.contains("particles.setColourKeys")
              && particlesMd.contains("scene.addParticles")
              && particlesMd.contains("editor.frame(60, 1/60)"),
          "skills: particles page teaches ramps, creation and deterministic stepping");
    const QString decalsMd = QString::fromUtf8(
        readFile(project + "/.claude/skills/jahshaka-decals/SKILL.md"));
    CHECK(decalsMd.contains("scene.addDecal") && decalsMd.contains("scene.addImagePlane")
              && decalsMd.contains("node.setDecalTexture"),
          "skills: decals page covers both decals and image planes");
    const QString worldMd = QString::fromUtf8(
        readFile(project + "/.claude/skills/jahshaka-world/SKILL.md"));
    CHECK(worldMd.contains("world.override") && worldMd.contains("world.modeTable")
              && worldMd.contains("giMode"),
          "skills: world page teaches modes, overrides and the registry");
    // Skills mention only real verbs — spot-check a few signatures.
    const QString sceneSkill = QString::fromUtf8(
        readFile(project + "/.claude/skills/jahshaka-scene-building/SKILL.md"));
    CHECK(sceneSkill.contains("scene.addPrimitive") && sceneSkill.contains("world.gi")
              && sceneSkill.contains("run_script"),
          "config: scene skill grounded in real verbs");
    // The assets skill teaches the ONE-CALL import (lane D #7). It taught
    // scene.addMesh as a shortcut once (audit F2), and that is what made the
    // F1 data loss reachable from the dock — so what it names here is a gate,
    // not prose.
    const QString assetSkill = QString::fromUtf8(
        readFile(project + "/.claude/skills/jahshaka-assets/SKILL.md"));
    CHECK(assetSkill.contains("assets.importAndPlace"),
          "config: assets skill teaches assets.importAndPlace");
    CHECK(assetSkill.contains("scene.addMesh") && assetSkill.contains("FAILS"),
          "config: ...and still says scene.addMesh fails");
    CHECK(assetSkill.contains("browse_assets"),
          "config: assets skill points at browse_assets for visual browsing");

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
    // The picker's list, and the owner's pick leading it.
    const auto choices = ClaudeLaunchConfig::modelChoices();
    CHECK(choices.size() >= 3 && choices.first().id == ClaudeLaunchConfig::defaultModel(),
          "config: the picker offers >= 3 models, the shipped default first");
    QStringList choiceIds;
    for (const auto &c : choices) choiceIds << c.id;
    CHECK(choiceIds.contains("sonnet") && choiceIds.contains("opus"),
          "config: sonnet and opus are offered beside the default");
    for (const auto &c : choices) {
        const QStringList a = ClaudeLaunchConfig::arguments(project, true, QString(), c.id);
        CHECK(a.value(a.indexOf("--model") + 1) == c.id,
              qPrintable("argv: every offered model reaches --model: " + c.id));
    }

    // D2: the reaper identifies OUR orphan by the system prompt in its argv.
    // If that sentence ever leaves the prompt, the reaper silently stops
    // recognising anything — so the two are pinned together here.
    CHECK(joined.contains(ClaudeLaunchConfig::launchSignature()),
          "argv: carries the launch signature the orphan reaper matches on");

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
    QList<QPair<QByteArray, QString>> images;
    QStringList denials;
    QStringList rateLimits;
    int thinkings = 0;
    int completions = 0;
    bool lastOk = false;
    QString lastResult;
    double lastCost = 0.0;
    int parseErrors = 0;

    void attach(ClaudeStreamParser &parser) {
        QObject::connect(&parser, &ClaudeStreamParser::toolResultImage,
                         [this](const QByteArray &data, const QString &mime) {
                             images.append({data, mime});
                         });
        QObject::connect(&parser, &ClaudeStreamParser::thinkingBlock,
                         [this]() { ++thinkings; });
        QObject::connect(&parser, &ClaudeStreamParser::rateLimitEvent,
                         [this](const QString &status, const QString &resets) {
                             rateLimits << (status + "|" + resets);
                         });
        QObject::connect(&parser, &ClaudeStreamParser::permissionsDenied,
                         [this](const QStringList &tools) { denials << tools; });
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
                         [this](bool ok, const QString &result, const QString &, double cost) {
                             ++completions; lastOk = ok; lastResult = result; lastCost = cost;
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
    // B3: everything the parser used to drop on the floor. One fixture turn
    // carrying a thinking block, a screenshot tool_result (caption + a real
    // PNG), a rate_limit_event, a cost and a permission denial.
    {
        ClaudeStreamParser parser;
        ParserCapture capture;
        capture.attach(parser);
        parser.feed(readFile(fixtureDir + "/turn_rich.jsonl"));
        CHECK(capture.parseErrors == 0, "rich: the transcript parses clean");
        CHECK(capture.thinkings == 1, "rich: a thinking block is announced (content stays elided)");
        CHECK(capture.toolNames == QStringList{"mcp__jahshaka__screenshot"},
              "rich: the tool call surfaced");
        CHECK(capture.toolResults.value(0).startsWith("camera: position"),
              "rich: the caption text is the tool-result preview, not \"(image)\"");
        CHECK(capture.images.size() == 1, "rich: exactly one image block surfaced");
        QImage decoded;
        CHECK(!capture.images.isEmpty()
                  && decoded.loadFromData(capture.images[0].first)
                  && decoded.size() == QSize(48, 32),
              "rich: the image content DECODES to a real 48x32 QImage");
        CHECK(!capture.images.isEmpty() && capture.images[0].second == "image/png",
              "rich: with its media type");
        CHECK(capture.rateLimits.size() == 1
                  && capture.rateLimits[0].startsWith("allowed_warning|"),
              "rich: rate_limit_event surfaces its status");
        CHECK(capture.denials == QStringList{"Write"},
              "rich: result.permission_denials names the refused tool");
        CHECK(capture.completions == 1 && capture.lastOk
                  && qAbs(capture.lastCost - 0.0431) < 1e-9,
              "rich: the turn's total_cost_usd reaches turnCompleted");
    }
    // A rate_limit_event in a shape we do not recognise is silence, not noise.
    {
        ClaudeStreamParser parser;
        ParserCapture capture;
        capture.attach(parser);
        parser.feed("{\"type\":\"rate_limit_event\",\"something\":\"else\"}\n");
        CHECK(capture.rateLimits.isEmpty() && capture.parseErrors == 0,
              "rich: an unknown rate_limit shape produces no row and no error");
    }
    // An image whose base64 is junk must not fabricate an empty picture.
    {
        ClaudeStreamParser parser;
        ParserCapture capture;
        capture.attach(parser);
        parser.feed("{\"type\":\"user\",\"message\":{\"role\":\"user\",\"content\":[{\"type\":"
                    "\"tool_result\",\"content\":[{\"type\":\"image\",\"source\":{\"type\":"
                    "\"base64\",\"media_type\":\"image/png\",\"data\":\"\"}}]}]}}\n");
        CHECK(capture.images.isEmpty() && capture.toolResults.size() == 1,
              "rich: an empty image payload is dropped, the tool result still lands");
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

// ------------------------------------------------------- D2 orphan reaper ----
// The pid file is the portable half of the PDEATHSIG hardening: a child the
// app never got to kill (because the app crashed) must die at the NEXT launch.
// The dangerous half is the kill, so the negative case matters more than the
// positive one — pids are recycled and a bare number proves nothing.
static void testOrphanReaper(const QString &scratch)
{
    QTemporaryDir projectDir;
    const QString project = projectDir.path();
    QDir(project).mkpath(QStringLiteral(".claude"));
    Q_UNUSED(scratch);

    CHECK(ClaudeChatHost::reapStaleChild(project) == 0, "D2: nothing recorded, nothing reaped");
    CHECK(ClaudeLaunchConfig::readPid(project) == 0, "D2: and no pid to read");

    // A stand-in orphan: a process whose command line carries our launch
    // signature, exactly as a real spawned `claude` would.
    QProcess orphan;
    orphan.start(QStringLiteral("/bin/sh"),
                 {QStringLiteral("-c"), QStringLiteral("sleep 30"),
                  ClaudeLaunchConfig::launchSignature()});
    CHECK(orphan.waitForStarted(3000), "D2: the stand-in orphan started");
    CHECK(ClaudeLaunchConfig::writePid(project, orphan.processId()), "D2: its pid is recorded");
    CHECK(ClaudeLaunchConfig::readPid(project) == orphan.processId(), "D2: and reads back");

    const qint64 reaped = ClaudeChatHost::reapStaleChild(project);
    CHECK(reaped == orphan.processId(), "D2: the recorded orphan is reaped");
    CHECK(orphan.waitForFinished(3000) || orphan.state() == QProcess::NotRunning,
          "D2: ...and is really gone");
    CHECK(ClaudeLaunchConfig::readPid(project) == 0, "D2: the record is cleared after reaping");

    // A pid that is alive but is NOT ours: never killed.
    QProcess stranger;
    stranger.start(QStringLiteral("/bin/sh"),
                   {QStringLiteral("-c"), QStringLiteral("sleep 30"),
                    QStringLiteral("somebody-elses-process")});
    CHECK(stranger.waitForStarted(3000), "D2: a stranger process started");
    ClaudeLaunchConfig::writePid(project, stranger.processId());
    CHECK(ClaudeChatHost::reapStaleChild(project) == 0,
          "D2: a pid without our launch signature is NOT killed");
    CHECK(stranger.state() != QProcess::NotRunning, "D2: ...it is still running");
    CHECK(ClaudeLaunchConfig::readPid(project) == 0,
          "D2: but the stale record is dropped either way");
    stranger.kill();
    stranger.waitForFinished(3000);

    // A pid that no longer exists at all.
    ClaudeLaunchConfig::writePid(project, 2147483646);
    CHECK(ClaudeChatHost::reapStaleChild(project) == 0, "D2: a dead pid reaps nothing");
    CHECK(ClaudeLaunchConfig::readPid(project) == 0, "D2: and its record is gone");
}

// The Linux half: PR_SET_PDEATHSIG on the spawned child, set through
// QProcess::setChildProcessModifier. Proven by asking the child itself
// (PR_GET_PDEATHSIG), which needs the child to be the exec'd process — hence a
// python "claude" rather than a shell one. Skipped where python3 is absent.
static void testPdeathsig(const QString &scratch)
{
#ifndef __linux__
    std::printf("skip: PDEATHSIG is Linux-only\n");
    Q_UNUSED(scratch);
    return;
#else
    const QString python = QStandardPaths::findExecutable(QStringLiteral("python3"));
    if (python.isEmpty()) {
        std::printf("skip: PDEATHSIG probe needs python3\n");
        return;
    }
    const QString reportPath = scratch + "/pdeathsig.txt";
    const QString fake = scratch + "/fake-claude-pdeathsig";
    const QString script = QStringLiteral(
        "#!%1\n"
        "import ctypes, os, sys\n"
        "v = ctypes.c_int(-1)\n"
        "ctypes.CDLL('libc.so.6').prctl(2, ctypes.byref(v))\n"   // PR_GET_PDEATHSIG
        "open(os.environ['JAHSHAKA_PDEATHSIG_OUT'], 'w').write(str(v.value))\n"
        "for line in sys.stdin:\n"
        "    if 'control_request' in line: continue\n"
        "    sys.stdout.write('{\"type\":\"system\",\"subtype\":\"init\",\"session_id\":"
        "\"pd-session\",\"tools\":[\"Skill\"],\"mcp_servers\":[]}\\n')\n"
        "    sys.stdout.write('{\"type\":\"result\",\"subtype\":\"success\",\"is_error\":false,"
        "\"result\":\"ok\",\"session_id\":\"pd-session\"}\\n')\n"
        "    sys.stdout.flush()\n").arg(python);
    writeFile(fake, script.toUtf8());
    QFile::setPermissions(fake, QFile::permissions(fake) | QFile::ExeOwner);
    qputenv("JAHSHAKA_CLAUDE_CLI", fake.toUtf8());
    qputenv("JAHSHAKA_PDEATHSIG_OUT", reportPath.toUtf8());

    QTemporaryDir projectDir;
    ClaudeChatHost host;
    int completions = 0;
    QObject::connect(host.parser(), &ClaudeStreamParser::turnCompleted,
                     [&](bool, const QString &, const QString &, double) { ++completions; });
    host.configure(projectDir.path(), false, 0, QString());
    host.sendMessage("ping");
    CHECK(waitFor([&]() { return completions == 1; }), "D2: the python fake CLI answered");
    // The pid file exists WHILE the child lives, and is gone once it does not.
    CHECK(ClaudeLaunchConfig::readPid(projectDir.path()) == host.childProcessId()
              && host.childProcessId() > 0,
          "D2: the live child's pid is on disk for the next launch to find");
    host.shutdown();
    CHECK(ClaudeLaunchConfig::readPid(projectDir.path()) == 0,
          "D2: a clean shutdown removes the pid record");

    CHECK(QString::fromUtf8(readFile(reportPath)).trimmed() == "9",
          "D2: the spawned CLI runs with PR_SET_PDEATHSIG = SIGKILL");
    qunsetenv("JAHSHAKA_PDEATHSIG_OUT");
    qunsetenv("JAHSHAKA_CLAUDE_CLI");
#endif
}

// ------------------------------------------------ interrupt, then resume ----
// CLAUDE_EDITOR_SPEC §J: "whether --resume after an interrupted turn resumes
// cleanly is unproven, as is whether the 3 s kill fallback ever fires". Both,
// against a fake CLI that speaks the real interrupt protocol.
static void testInterruptResume(const QString &scratch)
{
    const QString argvLog = scratch + "/interrupt-argv.txt";
    auto makeFake = [&](const QString &path, bool cooperative) {
        const QString script = QStringLiteral(
            "#!/bin/bash\n"
            "echo \"$@\" > '%1'\n"
            "init=0\n"
            "while IFS= read -r line; do\n"
            "  case \"$line\" in\n"
            "    *control_request*)\n"
            "      %2\n"
            "      continue;;\n"
            "  esac\n"
            "  if [ $init = 0 ]; then\n"
            "    printf '{\"type\":\"system\",\"subtype\":\"init\",\"session_id\":\"slow-session\","
            "\"tools\":[\"Skill\"],\"mcp_servers\":[]}\\n'\n"
            "    init=1\n"
            "  fi\n"
            "  case \"$line\" in\n"
            "    *slow*)\n"
            "      printf '{\"type\":\"stream_event\",\"event\":{\"type\":\"content_block_start\","
            "\"index\":0,\"content_block\":{\"type\":\"text\",\"text\":\"\"}}}\\n'\n"
            "      printf '{\"type\":\"stream_event\",\"event\":{\"type\":\"content_block_delta\","
            "\"index\":0,\"delta\":{\"type\":\"text_delta\",\"text\":\"working\"}}}\\n'\n"
            "      ;;\n"
            "    *)\n"
            "      printf '{\"type\":\"assistant\",\"message\":{\"role\":\"assistant\",\"content\":"
            "[{\"type\":\"text\",\"text\":\"done\"}]},\"session_id\":\"slow-session\"}\\n'\n"
            "      printf '{\"type\":\"result\",\"subtype\":\"success\",\"is_error\":false,"
            "\"result\":\"done\",\"session_id\":\"slow-session\"}\\n'\n"
            "      ;;\n"
            "  esac\n"
            "done\n")
            .arg(argvLog,
                 cooperative
                     ? QStringLiteral(
                           "printf '{\"type\":\"result\",\"subtype\":\"error_during_execution\","
                           "\"is_error\":true,\"result\":\"Interrupted by user\","
                           "\"session_id\":\"slow-session\"}\\n'")
                     : QStringLiteral(": # deliberately deaf to the interrupt"));
        writeFile(path, script.toUtf8());
        QFile::setPermissions(path, QFile::permissions(path) | QFile::ExeOwner);
    };

    // ---- the CLI answers the interrupt: the SESSION SURVIVES in place -----
    {
        const QString fake = scratch + "/fake-claude-interrupt";
        makeFake(fake, true);
        qputenv("JAHSHAKA_CLAUDE_CLI", fake.toUtf8());
        QTemporaryDir projectDir;
        ClaudeChatHost host;
        QString streamed, finalText;
        int completions = 0, failures = 0;
        QObject::connect(host.parser(), &ClaudeStreamParser::textDelta,
                         [&](const QString &t) { streamed += t; });
        QObject::connect(host.parser(), &ClaudeStreamParser::assistantText,
                         [&](const QString &t) { finalText = t; });
        QObject::connect(host.parser(), &ClaudeStreamParser::turnCompleted,
                         [&](bool, const QString &, const QString &, double) { ++completions; });
        QObject::connect(&host, &ClaudeChatHost::processFailed,
                         [&](const QString &) { ++failures; });
        host.configure(projectDir.path(), false, 0, QString());

        host.sendMessage("this one is slow");
        CHECK(waitFor([&]() { return streamed.contains("working"); }),
              "interrupt: a long turn is streaming");
        const qint64 pidDuring = host.childProcessId();
        CHECK(pidDuring > 0 && host.isBusy(), "interrupt: busy, with a live child");

        host.stopTurn();
        CHECK(waitFor([&]() { return completions == 1; }),
              "interrupt: the CLI ends the turn on control_request");
        CHECK(!host.isBusy(), "interrupt: and the dock is idle again");
        CHECK(host.isProcessRunning() && host.childProcessId() == pidDuring,
              "interrupt: the process is NOT killed when the interrupt is answered");
        CHECK(host.sessionId() == "slow-session", "interrupt: the session id survives");

        // The §J question: send again — does the conversation continue?
        host.sendMessage("and now a quick one");
        CHECK(waitFor([&]() { return completions == 2; }),
              "interrupt: the NEXT message is answered after an interrupt");
        CHECK(finalText == "done", "interrupt: ...with a real reply");
        CHECK(host.childProcessId() == pidDuring,
              "interrupt: in the SAME session — no restart, so nothing but the "
              "interrupted turn was lost");
        CHECK(failures == 0, "interrupt: none of it is reported as a failure");
        host.shutdown();
    }

    // ---- the CLI ignores the interrupt: the 3 s kill fallback fires -------
    {
        const QString fake = scratch + "/fake-claude-deaf";
        makeFake(fake, false);
        qputenv("JAHSHAKA_CLAUDE_CLI", fake.toUtf8());
        QTemporaryDir projectDir;
        ClaudeChatHost host;
        QString streamed;
        int completions = 0, aborts = 0, failures = 0;
        QObject::connect(host.parser(), &ClaudeStreamParser::textDelta,
                         [&](const QString &t) { streamed += t; });
        QObject::connect(host.parser(), &ClaudeStreamParser::turnCompleted,
                         [&](bool, const QString &, const QString &, double) { ++completions; });
        QObject::connect(&host, &ClaudeChatHost::turnAborted, [&]() { ++aborts; });
        QObject::connect(&host, &ClaudeChatHost::processFailed,
                         [&](const QString &) { ++failures; });
        host.configure(projectDir.path(), false, 0, QString());

        host.sendMessage("this one is slow");
        CHECK(waitFor([&]() { return streamed.contains("working"); }),
              "kill fallback: a long turn is streaming");
        host.stopTurn();
        CHECK(waitFor([&]() { return aborts == 1; }, 10000),
              "kill fallback: an unanswered interrupt kills the process");
        CHECK(!host.isProcessRunning() && !host.isBusy(),
              "kill fallback: the dock is idle with no child");
        CHECK(failures == 0, "kill fallback: a deliberate kill is not a failure");

        // And the next send resumes the session the killed process created.
        host.sendMessage("still there?");
        CHECK(waitFor([&]() { return completions == 1; }),
              "kill fallback: the next message restarts the CLI and completes");
        CHECK(QString::fromUtf8(readFile(argvLog)).contains("--resume slow-session"),
              "kill fallback: the restart RESUMES the interrupted conversation");
        host.shutdown();
    }
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

        // ---- B3: the tool row reads as work, not as protocol --------------
        CHECK(ClaudeChatWindow::toolLabel("mcp__jahshaka__screenshot") == "taking a screenshot"
                  && ClaudeChatWindow::toolLabel("mcp__jahshaka__run_script") == "running a script"
                  && ClaudeChatWindow::toolLabel("mcp__jahshaka__browse_assets") == "browsing assets",
              "window: tool names become friendly labels");
        CHECK(ClaudeChatWindow::toolLabel("mcp__jahshaka__future_tool") == "future_tool",
              "window: a tool this build has never heard of keeps its own name");
        const QString compact = ClaudeChatWindow::compactArgs(
            "{\n \"width\": 800,\n \"frameNode\": {\"id\": \"node_7\"}\n}");
        CHECK(compact.contains("width: 800") && compact.contains("frameNode")
                  && !compact.contains('\n'),
              "window: arguments compact to one line");
        CHECK(ClaudeChatWindow::compactArgs("{\"script\": \"scene.addPrimitive('cube')\"}")
                  == "scene.addPrimitive('cube')",
              "window: a script argument shows bare, without its key");
        const QString longArgs = ClaudeChatWindow::compactArgs(
            QStringLiteral("{\"script\": \"%1\"}").arg(QString(400, QLatin1Char('x'))));
        CHECK(longArgs.size() <= 91, "window: a long argument is bounded, not pasted whole");
        CHECK(!window.toolLines().isEmpty()
                  && window.toolLines().first().startsWith("running a script"),
              "window: the rendered row starts with the friendly label");
        CHECK(window.toolLines().first().contains("scene.addPrimitive('cube')"),
              "window: ...and carries the argument digest");

        // ---- B3: images, cost, denials, thinking, rate limits, parse errors -
        const int imagesBefore = window.imageCount();
        host.parser()->feed(readFile(qEnvironmentVariable("CLAUDECHAT_FIXTURES")
                                     + "/turn_rich.jsonl"));
        CHECK(window.imageCount() == imagesBefore + 1,
              "window: the screenshot the tool returned is rendered INLINE");
        CHECK(window.costText() == "$0.043",
              "window: the turn's cost is surfaced in the header");
        CHECK(window.infoLines().filter("blocked: Write").size() == 1,
              "window: a permission denial becomes a row that explains the lockdown");
        CHECK(window.infoLines().filter("thought for a moment").size() == 1,
              "window: the thinking block gets its affordance");
        CHECK(window.infoLines().filter("rate limit").size() == 1,
              "window: a rate-limit event is a row, not a mystery stall");
        host.parser()->feed("this is not json\n");
        CHECK(window.infoLines().filter("unreadable line").size() == 1,
              "window: parseError is finally connected (garbage used to vanish)");

        // ---- B3: the model picker ----------------------------------------
        CHECK(window.selectedModel() == ClaudeLaunchConfig::defaultModel(),
              "window: the picker starts on the shipped default (the owner's big model)");
        CHECK(host.model() == ClaudeLaunchConfig::defaultModel(),
              "window: ...and the host launches with it");
        auto *combo = window.findChild<QComboBox *>("claudeModel");
        CHECK(combo && combo->count() == ClaudeLaunchConfig::modelChoices().size(),
              "window: every offered model is in the combo");
        if (combo) combo->setCurrentIndex(combo->findData("sonnet"));
        CHECK(window.selectedModel() == "sonnet" && host.model() == "sonnet",
              "window: choosing a model retargets the host");
        CHECK(ini.value("claude_model").toString() == "sonnet",
              "window: ...and persists under the key MainWindow reads");
        CHECK(!window.infoLines().filter("model: sonnet").isEmpty(),
              "window: the change is announced (it applies to the next conversation)");
        if (combo) combo->setCurrentIndex(combo->findData(ClaudeLaunchConfig::defaultModel()));

        // ---- Stop is not an error ----------------------------------------
        // A cooperative interrupt comes back as is_error, which used to paint
        // a red "The turn failed." bubble at the user who pressed Stop.
        auto *stop = window.findChild<QPushButton *>("claudeStop");
        CHECK(stop != nullptr, "window: the Stop button exists");
        const int bubblesBefore = window.messageCount();
        if (stop) stop->click();
        host.parser()->feed("{\"type\":\"result\",\"subtype\":\"error_during_execution\","
                            "\"is_error\":true,\"result\":\"Interrupted by user\"}\n");
        CHECK(!window.infoLines().filter("stopped").isEmpty(),
              "window: the user's own interrupt reads as \"stopped\"");
        CHECK(window.messageCount() == bubblesBefore,
              "window: ...and does not add a red failure bubble");

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
    testOrphanReaper(scratch.path());
    testPdeathsig(scratch.path());
    testInterruptResume(scratch.path());
    testWindow(scratch.path());

    std::printf(failures ? "claude.chat: %d FAILURES\n" : "claude.chat: all checks passed\n",
                failures);
    return failures ? 1 : 0;
}
