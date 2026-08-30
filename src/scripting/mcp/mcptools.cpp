/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "scripting/mcp/mcptools.h"

#include <QBuffer>
#include <QImage>
#include <QJsonDocument>
#include <QStringList>
#include <QUndoStack>

#include "scripting/scriptengine.h"
#include "viewport/ieditorviewport.h"

namespace {

QJsonObject textResult(const QString &text, bool isError = false)
{
    QJsonObject item{ { "type", "text" }, { "text", text } };
    QJsonObject result{ { "content", QJsonArray{ item } } };
    if (isError) result["isError"] = true;
    return result;
}

QJsonObject jsonResult(const QJsonObject &payload, bool isError = false)
{
    return textResult(QString::fromUtf8(
        QJsonDocument(payload).toJson(QJsonDocument::Compact)), isError);
}

} // namespace

McpTools::McpTools(ScriptEngine *engine) : mEngine(engine) {}

QJsonArray McpTools::listTools() const
{
    // Exactly five (CLAUDE_EDITOR_SPEC.md phase 1, owner decision 2): the
    // scripting engine is the ONLY capability surface. A missing capability
    // means a new registry verb, never a new tool.
    QJsonArray tools;

    tools.append(QJsonObject{
        { "name", "run_script" },
        { "description",
          "Execute JavaScript inside Jahshaka's scripting engine (the same API the "
          "script console uses). The whole run is ONE undo step. Call api_docs first "
          "to see the modules and verbs (project, scene, node, editor, world, assets, "
          "materials, material, graph, app). Returns the completion value, console "
          "output and any error as JSON." },
        { "inputSchema", QJsonObject{
            { "type", "object" },
            { "properties", QJsonObject{
                { "script", QJsonObject{
                    { "type", "string" },
                    { "description", "The JavaScript source to run." } } },
                { "label", QJsonObject{
                    { "type", "string" },
                    { "description",
                      "Short label for this run; names the undo macro shown in "
                      "Edit > Undo (e.g. the current task)." } } } } },
            { "required", QJsonArray{ "script" } } } } });

    tools.append(QJsonObject{
        { "name", "api_docs" },
        { "description",
          "The generated scripting API reference — always current, straight from the "
          "verb registry. Without arguments returns the whole reference; pass a module "
          "name (e.g. \"scene\") for just that module." },
        { "inputSchema", QJsonObject{
            { "type", "object" },
            { "properties", QJsonObject{
                { "module", QJsonObject{
                    { "type", "string" },
                    { "description", "Optional module name (\"project\", \"scene\", ...)." } } } } } } } });

    tools.append(QJsonObject{
        { "name", "describe_scene" },
        { "description",
          "The open scene as JSON: every node (id, name, type, transform) plus the "
          "current selection. The fast way to see what exists before and after edits." },
        { "inputSchema", QJsonObject{ { "type", "object" }, { "properties", QJsonObject{} } } } });

    tools.append(QJsonObject{
        { "name", "screenshot" },
        { "description",
          "Render the engine viewport and return the image (PNG) — see your work. "
          "Requires the engine viewport (open or create a project first)." },
        { "inputSchema", QJsonObject{
            { "type", "object" },
            { "properties", QJsonObject{
                { "view", QJsonObject{
                    { "type", "string" },
                    { "enum", QJsonArray{ "editor" } },
                    { "description", "Which view to capture (phase 1: editor only)." } } },
                { "width", QJsonObject{ { "type", "integer" }, { "description", "Pixels, 16-4096 (default 800)." } } },
                { "height", QJsonObject{ { "type", "integer" }, { "description", "Pixels, 16-4096 (default 600)." } } } } } } } });

    tools.append(QJsonObject{
        { "name", "undo_redo" },
        { "description",
          "Step the editor undo stack — the escape hatch when a script run should be "
          "reverted (each run_script call is one undo step)." },
        { "inputSchema", QJsonObject{
            { "type", "object" },
            { "properties", QJsonObject{
                { "action", QJsonObject{
                    { "type", "string" },
                    { "enum", QJsonArray{ "undo", "redo" } } } } } },
            { "required", QJsonArray{ "action" } } } } });

    return tools;
}

QJsonObject McpTools::call(const QString &name, const QJsonObject &args)
{
    if (name == QLatin1String("run_script"))     return runScript(args);
    if (name == QLatin1String("api_docs"))       return apiDocs(args);
    if (name == QLatin1String("describe_scene")) return describeScene();
    if (name == QLatin1String("screenshot"))     return screenshot(args);
    if (name == QLatin1String("undo_redo"))      return undoRedo(args);
    return textResult(QStringLiteral("unknown tool: %1").arg(name), true);
}

QJsonObject McpTools::runScript(const QJsonObject &args)
{
    const QString source = args.value(QLatin1String("script")).toString();
    if (source.isEmpty())
        return textResult(QStringLiteral("run_script: 'script' is required"), true);

    const QString label = args.value(QLatin1String("label")).toString();
    // ScriptEngine names the per-run undo macro "script: <fileName basename>",
    // so the label doubles as the macro name in Edit > Undo.
    const QString fileName = label.isEmpty() ? QStringLiteral("mcp") : label;

    QStringList consoleLines;
    QMetaObject::Connection tap = QObject::connect(
        mEngine, &ScriptEngine::consoleOutput,
        [&consoleLines](const QString &line) { consoleLines.append(line); });
    const ScriptResult result = mEngine->evaluate(source, fileName);
    QObject::disconnect(tap);

    QJsonObject payload;
    payload["ok"] = result.ok;
    if (result.ok) {
        payload["result"] = QJsonValue::fromVariant(result.value);
    } else {
        payload["error"] = result.error;
        if (result.line > 0) payload["line"] = result.line;
        if (!result.stack.isEmpty()) payload["stack"] = result.stack;
    }
    payload["console"] = QJsonArray::fromStringList(consoleLines);
    return jsonResult(payload, !result.ok);
}

QJsonObject McpTools::apiDocs(const QJsonObject &args)
{
    const QString moduleName = args.value(QLatin1String("module")).toString();
    ApiRegistry &registry = mEngine->registry();
    if (moduleName.isEmpty())
        return textResult(registry.markdown());
    if (!registry.module(moduleName)) {
        QStringList known;
        for (ApiModule *m : registry.modules()) known << m->jsName();
        return textResult(QStringLiteral("api_docs: unknown module '%1' (modules: %2)")
                              .arg(moduleName, known.join(QStringLiteral(", "))), true);
    }
    return textResult(registry.helpText(moduleName));
}

QJsonObject McpTools::describeScene()
{
    if (!mEngine->scriptHost().isProjectOpen())
        return jsonResult(QJsonObject{ { "projectOpen", false },
                                       { "hint", "run_script: project.create(name) or project.open(name)" } });

    // Bundle the existing verbs — the registry stays the single source of
    // scene truth; no macro (pure query, no undo entry).
    const ScriptResult result = mEngine->evaluate(
        QStringLiteral("({ projectOpen: true, root: scene.root(),"
                       "   selection: editor.selection(), nodes: scene.nodes() })"),
        QStringLiteral("<describe_scene>"), false);
    if (!result.ok)
        return textResult(QStringLiteral("describe_scene: %1").arg(result.error), true);
    return jsonResult(QJsonDocument::fromVariant(result.value).object());
}

QJsonObject McpTools::screenshot(const QJsonObject &args)
{
    ScriptHost &host = mEngine->scriptHost();
    if (!host.isEngineReady() || !host.viewport)
        return textResult(QStringLiteral(
            "screenshot: the engine viewport is not live yet — create or open a "
            "project first (run_script: project.create(name))"), true);

    const QString view = args.value(QLatin1String("view")).toString(QStringLiteral("editor"));
    if (view != QLatin1String("editor"))
        return textResult(QStringLiteral("screenshot: view '%1' is not available in "
                                         "phase 1 (only 'editor')").arg(view), true);

    const int width = qBound(16, args.value(QLatin1String("width")).toInt(800), 4096);
    const int height = qBound(16, args.value(QLatin1String("height")).toInt(600), 4096);

    // Deterministic frame: document -> engine sync + render before reading pixels
    // (the editor.frame(n) pattern of the headless suites).
    host.viewport->renderFrames(2);
    const QImage img = host.viewport->takeScreenshot(width, height);
    if (img.isNull())
        return textResult(QStringLiteral("screenshot: the viewport returned no image"), true);

    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    img.save(&buffer, "PNG");

    QJsonObject item{ { "type", "image" },
                      { "data", QString::fromLatin1(png.toBase64()) },
                      { "mimeType", "image/png" } };
    return QJsonObject{ { "content", QJsonArray{ item } } };
}

QJsonObject McpTools::undoRedo(const QJsonObject &args)
{
    QUndoStack *stack = mEngine->scriptHost().undoStack;
    if (!stack)
        return textResult(QStringLiteral("undo_redo: no undo stack in this session"), true);

    const QString action = args.value(QLatin1String("action")).toString();
    bool applied = false;
    if (action == QLatin1String("undo")) {
        applied = stack->canUndo();
        if (applied) stack->undo();
    } else if (action == QLatin1String("redo")) {
        applied = stack->canRedo();
        if (applied) stack->redo();
    } else {
        return textResult(QStringLiteral("undo_redo: action must be 'undo' or 'redo'"), true);
    }

    return jsonResult(QJsonObject{
        { "action", action },
        { "applied", applied },
        { "undoText", stack->undoText() },
        { "redoText", stack->redoText() },
        { "index", stack->index() },
        { "count", stack->count() } });
}
