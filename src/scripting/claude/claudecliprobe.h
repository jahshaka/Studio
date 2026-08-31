/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef CLAUDECLIPROBE_H
#define CLAUDECLIPROBE_H

// ClaudeCliProbe — is the Claude Code CLI installed? (`claude --version`).
// The chat window renders a friendly install state when it isn't
// (CLAUDE_EDITOR_SPEC: "Prereq check at build time ... dock shows a friendly
// 'install Claude Code' state when absent").
//
// The program is "claude" from PATH unless the JAHSHAKA_CLAUDE_CLI
// environment variable overrides it (also how the tests inject a fake).

#include <QString>

class ClaudeCliProbe
{
public:
    enum class Status {
        Found,     // ran, version parsed
        NotFound,  // executable missing / not startable
        Error      // started but did not report a version
    };

    struct Result {
        Status status = Status::NotFound;
        QString version;  // "2.1.251" when Found
        QString program;  // what was probed
        QString detail;   // raw output / error text for diagnostics
    };

    /// The CLI program to spawn: $JAHSHAKA_CLAUDE_CLI or "claude".
    static QString program();

    /// Blocking probe (bounded by timeoutMs) — call it off the UI hot path
    /// or accept the one-time cost on window open.
    static Result probe(int timeoutMs = 6000);
};

#endif // CLAUDECLIPROBE_H
