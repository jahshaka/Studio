/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef CLAUDELAUNCHCONFIG_H
#define CLAUDELAUNCHCONFIG_H

// ClaudeLaunchConfig — generates the per-project `.claude/` config the chat
// window launches Claude Code with (CLAUDE_EDITOR_SPEC phases 2+3):
//
//   <project>/.claude/jahshaka-mcp.json   MCP wiring: the LIVE McpServer's
//                                         port + per-session bearer token
//                                         (rewritten every launch — the token
//                                         changes per app run).
//   <project>/.claude/skills/<name>/SKILL.md
//                                         the three curated skills, embedded
//                                         in the binary as versioned
//                                         resources; installed when missing
//                                         or older than the shipped version
//                                         (a user copy with a HIGHER version
//                                         is left alone).
//   <project>/.claude/jahshaka-chat.json  the persisted chat session id
//                                         (owner decision 3: resume per
//                                         project; Clear forgets it).
//
// arguments() builds the TOTAL-LOCKDOWN argv (owner decision 2): the
// scripting engine is the only capability surface. `--tools "Skill"` removes
// every built-in tool except Skill outright (verified against CLI 2.1.251:
// the session's tool list is exactly ["Skill"] + the MCP tools);
// `--strict-mcp-config` limits MCP to our config file; the allow list
// auto-approves our whole MCP server (+Skill) so no permission prompt can ever
// appear; the deny list is belt-and-braces on top.
//
// The allow list is the server-scoped GLOB `mcp__jahshaka__*`, not a hand-typed
// list of tool names (CLAUDE_EDITOR_SPEC §C "a-glob", verified live at CLI
// 2.1.258). The hardcoded five names meant any tool added to McpTools::listTools
// was visible to the session but NOT allow-listed, and under a growing toolset
// came back `is_error: true` "permission ... denied" — a bug that appears in
// the dock only, since external Claude Code users control their own allow list.
// The glob must stay server-anchored: bare `mcp__*` is ignored with a warning.

#include <QList>
#include <QString>
#include <QStringList>

class ClaudeLaunchConfig
{
public:
    /// One entry of the chat header's model picker.
    struct ModelChoice
    {
        QString id;      ///< what goes after --model
        QString label;   ///< what the combo shows
    };

    /// The permission pattern that covers EVERY tool on the jahshaka MCP
    /// server, present and future (`mcp__jahshaka__*`).
    static QString jahshakaMcpToolPattern();

    /// The default model for dock sessions. The shipped argv passed no
    /// `--model` at all, so every turn silently inherited whatever the user's
    /// terminal was tuned for. Owner decision 2026-09-05 (AI_SURFACE_PROGRAM_SPEC
    /// §Owner decisions): pin the BIG model — capability over cost, chosen over
    /// the spec's Sonnet recommendation at a measured ~$0.15 vs ~$0.04 per short
    /// turn. That measurement was taken on `claude-fable-5-1`, so `fable` is the
    /// alias that reproduces exactly the model the owner priced and approved.
    static QString defaultModel();

    /// The models the chat header offers, defaultModel() first. Deliberately
    /// short: these are aliases the CLI resolves itself, so they keep working
    /// as the underlying model ids move.
    static QList<ModelChoice> modelChoices();

    /// <project>/.claude/jahshaka-mcp.json (the path, whether or not written).
    static QString mcpConfigPath(const QString &projectFolder);

    /// <project>/.claude/jahshaka-chat.pid — the child pid this app spawned,
    /// so a LATER launch can reap one that outlived a crashing app
    /// (CLAUDE_EDITOR_SPEC D2; the portable half of the PDEATHSIG hardening).
    static QString pidFilePath(const QString &projectFolder);
    static bool writePid(const QString &projectFolder, qint64 pid);
    /// The recorded pid, or 0 when there is none.
    static qint64 readPid(const QString &projectFolder);
    static void clearPid(const QString &projectFolder);

    /// A stable sentence out of the appended system prompt. It is in the argv
    /// of every session THIS APP spawns and of no other claude session, so a
    /// reaper can prove a recorded pid is really our orphan before killing it
    /// (pids are recycled; killing on a number alone is not acceptable).
    static QString launchSignature();

    /// Writes jahshaka-mcp.json for the live server and installs/updates the
    /// skills. Pass mcpPort 0 for "MCP off" — the MCP file is then removed so
    /// a stale token can never linger. Returns false with *errorOut set.
    static bool writeProjectConfig(const QString &projectFolder, quint16 mcpPort,
                                   const QString &token, QString *errorOut = nullptr);

    /// The full argv for `claude` (program not included). Stream-json in/out,
    /// partial messages, and the lockdown flags; --mcp-config only when
    /// mcpEnabled; --resume only when resumeSessionId is non-empty; --model
    /// only when `model` is non-empty (the seam a header picker will drive —
    /// pass an empty string to inherit the user's default again).
    static QStringList arguments(const QString &projectFolder, bool mcpEnabled,
                                 const QString &resumeSessionId = QString(),
                                 const QString &model = QString());

    /// Session persistence (jahshaka-chat.json).
    static QString readSessionId(const QString &projectFolder);
    static bool writeSessionId(const QString &projectFolder, const QString &sessionId);
    static void clearSessionId(const QString &projectFolder);

    /// The shipped skills: {"jahshaka-scene-building", ...}.
    static QStringList skillNames();
    /// Version stamped in a skill's frontmatter ("version: N"); -1 = none.
    static int skillVersion(const QString &markdown);

private:
    static bool installSkills(const QString &claudeDir, QString *errorOut);
};

#endif // CLAUDELAUNCHCONFIG_H
