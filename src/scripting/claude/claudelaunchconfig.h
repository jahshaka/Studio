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
// auto-approves exactly the five jahshaka tools (+Skill) so no permission
// prompt can ever appear; the deny list is belt-and-braces on top.

#include <QString>
#include <QStringList>

class ClaudeLaunchConfig
{
public:
    /// The five MCP tools the jahshaka server exposes, as Claude Code
    /// permission names (mcp__<server>__<tool>).
    static QStringList jahshakaMcpTools();

    /// <project>/.claude/jahshaka-mcp.json (the path, whether or not written).
    static QString mcpConfigPath(const QString &projectFolder);

    /// Writes jahshaka-mcp.json for the live server and installs/updates the
    /// skills. Pass mcpPort 0 for "MCP off" — the MCP file is then removed so
    /// a stale token can never linger. Returns false with *errorOut set.
    static bool writeProjectConfig(const QString &projectFolder, quint16 mcpPort,
                                   const QString &token, QString *errorOut = nullptr);

    /// The full argv for `claude` (program not included). Stream-json in/out,
    /// partial messages, and the lockdown flags; --mcp-config only when
    /// mcpEnabled; --resume only when resumeSessionId is non-empty.
    static QStringList arguments(const QString &projectFolder, bool mcpEnabled,
                                 const QString &resumeSessionId = QString());

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
