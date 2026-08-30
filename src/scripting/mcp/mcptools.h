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

// The five MCP tools (CLAUDE_EDITOR_SPEC.md phase 1). Deliberately FEW: the
// scripting engine is the whole capability surface — Claude writes JavaScript
// against the registry verbs, and these tools are only the bridge:
//
//   run_script     execute JS in the ScriptHost; one undo macro per call
//   api_docs       the registry-generated verb reference (whole or one module)
//   describe_scene scene graph + selection as JSON (existing verbs bundled)
//   screenshot     engine viewport render as PNG (MCP image content)
//   undo_redo      escape hatch onto the editor undo stack
//
// Per-verb MCP tools are explicitly rejected (spec): 76 tools bloat every
// model call; run_script + api_docs is stronger (loops, math, batching under
// one undo macro).

#include <QJsonArray>
#include <QJsonObject>

class ScriptEngine;

class McpTools
{
public:
    explicit McpTools(ScriptEngine *engine);

    /// The tools/list payload: five tool descriptors with JSON input schemas.
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
    QJsonObject undoRedo(const QJsonObject &args);

    ScriptEngine *mEngine;
};

#endif // MCPTOOLS_H
