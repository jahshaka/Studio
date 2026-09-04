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
//
// Per-verb MCP tools are explicitly rejected (spec): one tool per verb would
// mean one tool descriptor per registry verb in every model call — hundreds,
// growing with every feature. run_script + api_docs is stronger anyway (loops,
// math, batching under one undo macro). No count is written here on purpose:
// the last one said 76 and was three years of features out of date.

#include <QJsonArray>
#include <QJsonObject>

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
    QJsonObject runScript(const QJsonObject &args);
    QJsonObject apiDocs(const QJsonObject &args);
    QJsonObject describeScene();
    QJsonObject screenshot(const QJsonObject &args);
    QJsonObject browseAssets(const QJsonObject &args);
    QJsonObject undoRedo(const QJsonObject &args);

    ScriptEngine *mEngine;
};

#endif // MCPTOOLS_H
