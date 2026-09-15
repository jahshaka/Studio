/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef MCPTOOLS_H
#define MCPTOOLS_H

// The MCP tools (CLAUDE_EDITOR_SPEC.md phase 1; the five-tool cap was lifted by
// the owner 2026-09-05). Deliberately FEW: the
// scripting engine is the whole capability surface — Claude writes JavaScript
// against the registry verbs, and these tools are only the bridge:
//
//   run_script     execute JS in the ScriptHost; one undo macro per call
//   api_docs       the registry-generated verb reference (whole, one module,
//                  one verb, or a search over names + doc text)
//   describe_scene scene graph + selection as JSON (existing verbs bundled)
//   screenshot     engine viewport render as PNG (MCP image content)
//   browse_assets  the asset library as rows + thumbnail images (MCP image
//                  content) — the byte-carrying VIEW of assets.list, which is
//                  where the capability lives
//   undo_redo      escape hatch onto the editor undo stack
//   capture_perf   the render-loop monitor: record the next N seconds of the
//                  frame into a capture bundle, stop one early, ask what it is
//                  doing, or drop a named mark into a running capture. One tool
//                  for the four perf.* verbs, because they are one workflow
//
// Per-verb MCP tools are explicitly rejected (spec): one tool per verb would
// mean one tool descriptor per registry verb in every model call — hundreds,
// growing with every feature. run_script + api_docs is stronger anyway (loops,
// math, batching under one undo macro). No count is written here on purpose:
// the last one said 76 and was three years of features out of date.

#include <QJsonArray>
#include <QJsonObject>

#include "scripting/mcp/mcplog.h"

class ScriptEngine;

class McpTools
{
public:
    explicit McpTools(ScriptEngine *engine);

    /// The tools/list payload: one descriptor with a JSON input schema per
    /// tool. run_script's description is GENERATED from the live registry (its
    /// module list used to be hand-typed and went stale — audit F16).
    QJsonArray listTools() const;

    /// Executes one tools/call. Returns the MCP result object
    /// ({content: [...], isError?: bool}); unknown tool names yield an
    /// isError result, not a protocol error (per MCP spec).
    QJsonObject call(const QString &name, const QJsonObject &args);

private:
    /// A TOOL THAT ONLY READS WAITS FOR A RUN IN FLIGHT instead of failing on
    /// it (round 2, M4). Before the script engine moved off the UI thread these
    /// tools could not arrive mid-run at all — the thread was blocked, and the
    /// request queued behind it — so "a console script is running" was never an
    /// answer an agent had to handle. It is now, and for a describe_scene or a
    /// screenshot the honest answer is the one it was always going to get, a
    /// moment later. Pumps this thread's event loop (the run's verb hops ARE
    /// events on it; a blocking wait would deadlock), bounded by the same
    /// budget a run gets. True when the engine is idle and the caller may
    /// proceed; false means the run outlasted the budget and the tool refuses.
    bool waitForScriptIdle();

    QJsonObject runScript(const QJsonObject &args);
    QJsonObject apiDocs(const QJsonObject &args);
    QJsonObject describeScene(const QJsonObject &args);
    QJsonObject screenshot(const QJsonObject &args);
    QJsonObject browseAssets(const QJsonObject &args);
    QJsonObject undoRedo(const QJsonObject &args);
    QJsonObject capturePerf(const QJsonObject &args);

    /// What the tools/call now in flight will tell McpLog. call() sets the
    /// parts it can see from outside (tool, arguments, duration, outcome) and
    /// the handlers enrich it with what only they know — run_script's failing
    /// line, the timeout flag, the verbs the script called.
    McpCallRecord mRecord;

    ScriptEngine *mEngine;
};

#endif // MCPTOOLS_H
