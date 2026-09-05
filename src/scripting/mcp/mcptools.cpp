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
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QStringList>
#include <QUndoStack>

#include <utility>

#include "data/database/database.h"
#include "data/project.h"
#include "scripting/scriptengine.h"
#include "services/engineerrorpump.h"
#include "viewport/ieditorviewport.h"

namespace {

/// F6 default budget. Long enough for a real scene-building script, short
/// enough that a runaway loop does not look like a hung editor.
constexpr int kDefaultScriptTimeoutMs = 30000;

// ---- browse_assets image budget (AI_SURFACE_PROGRAM_SPEC lane C #6, D6) ----
// The audit measured the stored thumbnails across 37 test libraries: 72x72
// (import time), 256x256 and 512x512 (engine renders), mean 1.5-16 KB, max
// 60,102 B. Three bounds keep a browse to roughly a quarter of one screenshot:
//   * a row cap, so a big library cannot be dumped in one call;
//   * a downscale, so ONE oversized thumbnail cannot blow the turn; and
//   * a total byte ceiling, the backstop for a library of 60 KB thumbnails.
// A row that loses its image to the ceiling still appears in the text rows —
// silently dropping it would be the F7/F8 class of defect (a swallowed refusal).
constexpr int kDefaultBrowseLimit = 12;
constexpr int kMaxBrowseLimit     = 24;
constexpr int kThumbnailLongEdge  = 128;
constexpr int kBrowseImageBudget  = 256 * 1024;   ///< PNG bytes, before base64

/// describe_scene's default bound (AI_SURFACE_PROGRAM_SPEC owner row D2, option
/// c). The dock pays this on most turns, and an unbounded depth-first dump of a
/// large scene is the single largest avoidable cost on the surface. Two levels
/// covers a flat scene whole; anything deeper says so per row (truncated).
constexpr int kDefaultDescribeDepth = 2;

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
    // Deliberately few (CLAUDE_EDITOR_SPEC.md phase 1, owner decision 2): the
    // scripting engine is the ONLY capability surface. A missing capability
    // means a new registry verb, never a new tool. The two tools that are not
    // run_script exist because a script result is JSON and cannot carry BYTES:
    // screenshot and browse_assets return MCP image content.
    QJsonArray tools;

    // F16: the module list is GENERATED from the registry. It was a hand-typed
    // ten of the thirteen modules, so three whole domains were invisible to
    // anything that read only the tool description.
    QStringList moduleNames;
    for (ApiModule *m : mEngine->registry().modules()) moduleNames << m->jsName();

    tools.append(QJsonObject{
        { "name", "run_script" },
        { "description",
          QStringLiteral(
              "Execute JavaScript inside Jahshaka's scripting engine (the same API the "
              "script console uses). The whole run is ONE undo step. Call api_docs first "
              "to see the verbs; the modules are: %1. Returns the completion value, "
              "console output and any error as JSON.\n"
              "timeoutMs caps the run (default %2 ms) and aborts it with an error rather "
              "than wedging the editor. It cannot be a verb — a verb would have to run "
              "inside the script it must interrupt — and there is no cancel TOOL either: "
              "this transport is POST-only and serves one request at a time, so a second "
              "request cannot reach the editor while the first is running. The interrupt "
              "lands at JavaScript statement boundaries ONLY: a run parked inside a native "
              "verb (editor.frame, graph.bake, editor.warmUpShaders, an import) finishes "
              "that call first.\n"
              "engineErrors, when present, lists the RENDERER refusals recorded while this "
              "run was executing — a texture that would not decode, a mesh the backend "
              "rejected. The engine refuses rather than throws, so those never reach the "
              "script's return value or its error. They are recorded when a frame runs, so "
              "a run that changes the scene without calling editor.frame(n) usually reports "
              "them on the NEXT run that does. app.engineErrors() has the cumulative record.")
              .arg(moduleNames.join(QStringLiteral(", ")))
              .arg(kDefaultScriptTimeoutMs) },
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
                      "Edit > Undo (e.g. the current task)." } } },
                { "timeoutMs", QJsonObject{
                    { "type", "integer" },
                    { "description",
                      QStringLiteral("Milliseconds before the run is interrupted "
                                     "(default %1, minimum 50, maximum 600000).")
                          .arg(kDefaultScriptTimeoutMs) } } } } },
            { "required", QJsonArray{ "script" } } } } });

    tools.append(QJsonObject{
        { "name", "api_docs" },
        { "description",
          QStringLiteral(
              "The generated scripting API reference — always current, straight from the "
              "verb registry. The whole reference is large (hundreds of verbs), so ask "
              "narrowly first:\n"
              "  verb:\"scene.addPrimitive\"  one verb's signature + doc (a bare verb name "
              "works too and reports every module that has one)\n"
              "  search:\"light\"             every verb whose name OR doc text matches\n"
              "  module:\"scene\"             one module's verbs\n"
              "  (no arguments)              the whole reference\n"
              "The modules are: %1.")
              .arg(moduleNames.join(QStringLiteral(", "))) },
        { "inputSchema", QJsonObject{
            { "type", "object" },
            { "properties", QJsonObject{
                { "module", QJsonObject{
                    { "type", "string" },
                    { "description", "Optional module name (\"project\", \"scene\", ...)." } } },
                { "verb", QJsonObject{
                    { "type", "string" },
                    { "description", "One verb, \"module.verb\" (or a bare verb name)." } } },
                { "search", QJsonObject{
                    { "type", "string" },
                    { "description", "Substring matched against verb names, signatures "
                                     "and doc text (case-insensitive)." } } } } } } } });

    tools.append(QJsonObject{
        { "name", "describe_scene" },
        { "description",
          QStringLiteral(
              "The open scene as JSON: the nodes (id, name, type, transform) plus the current "
              "selection. The fast way to see what exists before and after edits.\n"
              "It is BOUNDED by default: only %1 levels below the root are listed, and any node "
              "whose children were cut off carries childCount and truncated:true — so nothing is "
              "silently missing and a 500-node scene is not re-sent every turn. Raise `depth` (-1 "
              "= the whole tree) or point `subtree` at a node id to go deeper in one place. "
              "`include` adds blocks: \"materials\" (a material summary on mesh nodes), \"lights\" "
              "(the parameters of each light), \"visibility\" (each node's own flag plus whether "
              "an ancestor hides it) and \"world\" (the scene-level settings — ambient, fog, sky, "
              "shadows, GI, post-fx — as one object beside the nodes).\n"
              "Every field comes from a registry verb: scene.nodes({subtree, depth, include}) and "
              "world.get(). run_script can call them directly.")
              .arg(kDefaultDescribeDepth) },
        { "inputSchema", QJsonObject{
            { "type", "object" },
            { "properties", QJsonObject{
                { "include", QJsonObject{
                    { "type", "array" },
                    { "items", QJsonObject{ { "type", "string" },
                        { "enum", QJsonArray{ "materials", "lights", "visibility", "world" } } } },
                    { "description", "Extra blocks to attach (default: none)." } } },
                { "subtree", QJsonObject{
                    { "type", "string" },
                    { "description", "A node id to describe instead of the whole scene "
                                     "(that node and its descendants)." } } },
                { "depth", QJsonObject{
                    { "type", "integer" },
                    { "description", QStringLiteral("Levels below the start node to list; 0 is "
                                                    "the start node alone, -1 the whole tree "
                                                    "(default %1).").arg(kDefaultDescribeDepth) } } } } } } } });

    tools.append(QJsonObject{
        { "name", "screenshot" },
        { "description",
          "Render the engine viewport and return the image (PNG) — see your work. "
          "Requires the engine viewport (open or create a project first). By "
          "default the scene's post-processing chain (HDR/tonemap, bloom, ambient "
          "occlusion, SMAA) is applied so the image matches what the user sees; "
          "pass postFx:false for a neutral render.\n"
          "The EDITOR view is the only view there is: the player view has no offscreen "
          "render path and no verb behind it, so it is not a parameter here.\n"
          "`camera` and `frameNode` move the editor camera before the shot — they are "
          "editor.setCamera / editor.frameNode, called for you, and they MOVE THE USER'S "
          "VIEWPORT (there is no separate screenshot camera). The resulting pose is "
          "always echoed in a text block beside the image, so you can see where the shot "
          "was taken from and take the next one relative to it.\n"
          "`camera` AS A STRING is the opposite: it is the id of a real SCENE CAMERA "
          "(describe_scene lists them, scene.addCamera makes them) and the shot is taken "
          "THROUGH that camera without moving the user's viewport at all. That is how you "
          "see what an avatar sees — attach a camera to a rig's `head` socket "
          "(node.attachToSocket) and shoot through it." },
        { "inputSchema", QJsonObject{
            { "type", "object" },
            { "properties", QJsonObject{
                { "view", QJsonObject{
                    { "type", "string" },
                    { "enum", QJsonArray{ "editor" } },
                    { "description", "Which view to capture — editor only." } } },
                { "width", QJsonObject{ { "type", "integer" }, { "description", "Pixels, 16-4096 (default 800)." } } },
                { "height", QJsonObject{ { "type", "integer" }, { "description", "Pixels, 16-4096 (default 600)." } } },
                { "camera", QJsonObject{
                    { "description", "EITHER an object — editor.setCamera's argument, which places "
                                     "the USER'S viewport camera first: {position?:{x,y,z}, "
                                     "lookAt?:{x,y,z} | rotation?:{x,y,z,scalar}, fov?} — OR a "
                                     "STRING, the id of a scene camera to shoot through, which "
                                     "leaves the user's viewport exactly where it was. Mutually "
                                     "exclusive with frameNode." } } },
                { "frameNode", QJsonObject{
                    { "type", "object" },
                    { "description", "Frame a node first — editor.frameNode: {id (required), yaw?, "
                                     "pitch?, distance?}, angles in degrees (pitch is clamped to "
                                     "-89..89). A bare node id string is accepted too. Mutually "
                                     "exclusive with camera." } } },
                { "postFx", QJsonObject{ { "type", "boolean" },
                    { "description", "Apply the scene's post-processing chain so the image "
                                     "matches the viewport (default true); false renders "
                                     "neutrally." } } } } } } } });

    tools.append(QJsonObject{
        { "name", "browse_assets" },
        { "description",
          QStringLiteral(
              "SEE the asset library: one JSON summary of the matching store assets "
              "(guid, name, type, drawer) followed by each one's stored thumbnail as an "
              "image, captioned with its name and guid so rows and pictures line up.\n"
              "This is the byte-carrying view of the assets.list verb — everything it "
              "filters on, a script can ask for too (run_script: assets.list({query, "
              "type, drawer, limit})); only the PIXELS need a tool, because a script "
              "result is JSON.\n"
              "Images are budgeted: at most %1 rows per call (default %2), each thumbnail "
              "downscaled to a %3 px long edge, and a %4 KB ceiling on the PNG bytes in "
              "one response. An asset with no stored thumbnail — or one the ceiling cut — "
              "still appears in the rows, flagged, never as a placeholder picture. Use "
              "assets.refreshThumbnail(guid) to (re)render one.")
              .arg(kMaxBrowseLimit).arg(kDefaultBrowseLimit)
              .arg(kThumbnailLongEdge).arg(kBrowseImageBudget / 1024) },
        { "inputSchema", QJsonObject{
            { "type", "object" },
            { "properties", QJsonObject{
                { "query", QJsonObject{
                    { "type", "string" },
                    { "description", "Case-insensitive substring of the asset name." } } },
                { "type", QJsonObject{
                    { "type", "string" },
                    { "description", "Asset type: object, texture, material, shader, "
                                     "music, video, sky, particles, lightprofile, file." } } },
                { "drawer", QJsonObject{
                    { "type", "integer" },
                    { "description", "Restrict to one drawer id (0 = Uncategorized); "
                                     "run_script: assets.drawers() lists them." } } },
                { "limit", QJsonObject{
                    { "type", "integer" },
                    { "description", QStringLiteral("Rows to return (default %1, maximum %2).")
                                         .arg(kDefaultBrowseLimit).arg(kMaxBrowseLimit) } } } } } } } });

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
    if (name == QLatin1String("describe_scene")) return describeScene(args);
    if (name == QLatin1String("screenshot"))     return screenshot(args);
    if (name == QLatin1String("browse_assets"))  return browseAssets(args);
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
    const int timeoutMs = qBound(50, args.value(QLatin1String("timeoutMs"))
                                         .toInt(kDefaultScriptTimeoutMs), 600000);

    // The renderer refuses rather than throws (irisgl/engine EnginePrivate.h
    // JAH_CATCH), so a failed texture or a rejected mesh never reaches the
    // script's return value OR its error — it lands in EngineErrorPump. Echo
    // what THIS run produced (audit #5).
    //
    // A DIFF, not a snapshot, and deliberately DRAIN-NEUTRAL: report() is
    // const and cumulative, so taking it twice and subtracting the per-message
    // counts steals nothing the pump would have logged and does not disturb
    // app.engineErrors(reset) for anyone else. (A `reset` here would be the
    // bug: the next reader would find the failure gone.)
    const QVariantMap errorsBefore = EngineErrorPump::instance().report();
    QHash<QString, qulonglong> countBefore;
    for (const QVariant &e : errorsBefore.value(QStringLiteral("entries")).toList()) {
        const QVariantMap entry = e.toMap();
        countBefore.insert(entry.value(QStringLiteral("message")).toString(),
                           entry.value(QStringLiteral("count")).toULongLong());
    }

    QStringList consoleLines;
    QMetaObject::Connection tap = QObject::connect(
        mEngine, &ScriptEngine::consoleOutput,
        [&consoleLines](const QString &line) { consoleLines.append(line); });
    const ScriptResult result = mEngine->evaluate(source, fileName, true, timeoutMs);
    QObject::disconnect(tap);

    QJsonArray engineErrors;
    for (const QVariant &e : EngineErrorPump::instance()
                                 .report().value(QStringLiteral("entries")).toList()) {
        const QVariantMap entry = e.toMap();
        const QString message = entry.value(QStringLiteral("message")).toString();
        const qulonglong now = entry.value(QStringLiteral("count")).toULongLong();
        const qulonglong was = countBefore.value(message, 0);
        // `<=` covers the pump's LRU eviction and a reset from elsewhere: a
        // message whose count went DOWN did not happen during this run.
        if (now <= was) continue;
        engineErrors.append(QJsonObject{
            { "message", message },
            { "count", static_cast<qint64>(now - was) } });
    }

    QJsonObject payload;
    payload["ok"] = result.ok;
    if (result.ok) {
        payload["result"] = QJsonValue::fromVariant(result.value);
    } else {
        payload["error"] = result.error;
        if (result.timedOut) payload["timedOut"] = true;
        if (result.line > 0) payload["line"] = result.line;
        if (!result.stack.isEmpty()) payload["stack"] = result.stack;
    }
    payload["console"] = QJsonArray::fromStringList(consoleLines);
    // Only when non-empty: an always-present empty array on every response
    // would train a reader to skip the field.
    if (!engineErrors.isEmpty()) payload["engineErrors"] = engineErrors;
    return jsonResult(payload, !result.ok);
}

QJsonObject McpTools::apiDocs(const QJsonObject &args)
{
    const QString moduleName = args.value(QLatin1String("module")).toString();
    ApiRegistry &registry = mEngine->registry();

    // Narrowest first (audit #2). The full dump is tens of kilobytes and grows
    // with every feature; a reader that knows the verb's name, or half of it,
    // should get hundreds of bytes instead.
    //
    // No new capability, so no new verb (API-first, SCRIPTING_SPEC §2.3): a
    // script already has the whole surface as data through api.verbs() and one
    // verb through api.help("module.verb"). These two parameters only spend
    // FEWER of the model's tokens on the same registry.
    const QString verb = args.value(QLatin1String("verb")).toString().trimmed();
    if (!verb.isEmpty()) {
        const QString text = registry.verbText(verb);
        if (text.isEmpty())
            return textResult(
                QStringLiteral("api_docs: no verb '%1' — try api_docs({search:\"%2\"}), "
                               "or a module name")
                    .arg(verb, verb.section(QLatin1Char('.'), -1)), true);
        return textResult(text);
    }

    const QString search = args.value(QLatin1String("search")).toString().trimmed();
    if (!search.isEmpty())
        return textResult(registry.searchText(search, 40));

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

QJsonObject McpTools::describeScene(const QJsonObject &args)
{
    if (!mEngine->scriptHost().isProjectOpen())
        return jsonResult(QJsonObject{ { "projectOpen", false },
                                       { "hint", "run_script: project.create(name) or project.open(name)" } });

    // The include names split two ways: three of them are per-NODE blocks that
    // scene.nodes understands, and "world" is a scene-level read of its own.
    QJsonArray nodeIncludes;
    bool wantWorld = false;
    for (const QJsonValue &v : args.value(QLatin1String("include")).toArray()) {
        const QString name = v.toString();
        if (name == QLatin1String("world")) { wantWorld = true; continue; }
        if (name != QLatin1String("materials") && name != QLatin1String("lights")
            && name != QLatin1String("visibility"))
            return textResult(QStringLiteral(
                "describe_scene: unknown include '%1' (materials, lights, visibility, world)")
                    .arg(name), true);
        nodeIncludes.append(name);
    }

    QJsonObject nodeOptions;
    nodeOptions["depth"] = args.contains(QLatin1String("depth"))
                               ? args.value(QLatin1String("depth")).toInt(kDefaultDescribeDepth)
                               : kDefaultDescribeDepth;
    const QString subtree = args.value(QLatin1String("subtree")).toString();
    if (!subtree.isEmpty()) nodeOptions["subtree"] = subtree;
    if (!nodeIncludes.isEmpty()) nodeOptions["include"] = nodeIncludes;

    // Bundle the existing verbs — the registry stays the single source of
    // scene truth; no macro (pure query, no undo entry). A compact JSON object
    // is a valid JS object literal, which is how the options travel.
    const QString options = QString::fromUtf8(
        QJsonDocument(nodeOptions).toJson(QJsonDocument::Compact));
    QString expr = QStringLiteral(
        "({ projectOpen: true, root: scene.root(), selection: editor.selection(),"
        "   depth: %1, nodes: scene.nodes(%2)")
        .arg(nodeOptions.value("depth").toInt()).arg(options);
    if (wantWorld) expr += QStringLiteral(", world: world.get()");
    expr += QStringLiteral(" })");

    const ScriptResult result = mEngine->evaluate(expr, QStringLiteral("<describe_scene>"), false);
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
        return textResult(QStringLiteral("screenshot: view '%1' does not exist — the editor "
                                         "view is the only one that can be captured").arg(view), true);

    const int width = qBound(16, args.value(QLatin1String("width")).toInt(800), 4096);
    const int height = qBound(16, args.value(QLatin1String("height")).toInt(600), 4096);

    // Optional camera placement, done by CALLING THE VERBS: editor.setCamera /
    // editor.frameNode carry the capability and their tests gate it; this tool
    // is only the thing that can carry the resulting bytes back. The pose the
    // verb returns is echoed beside the image — without it the model has an
    // image and no idea where it was taken from.
    const bool hasCamera = args.contains(QLatin1String("camera"));
    const bool hasFrameNode = args.contains(QLatin1String("frameNode"));
    if (hasCamera && hasFrameNode)
        return textResult(QStringLiteral("screenshot: pass EITHER camera or frameNode, not both"), true);

    // A STRING `camera` is a SCENE camera id (CAMERAS_SPEC §5's AI hook), which
    // is a different verb and a different promise: camera.screenshot renders
    // through that node's offscreen view and never touches the user's viewport.
    // It writes a file (that is what the verb does), so the bytes come back off
    // disk from a scratch dir that dies with this call.
    if (hasCamera && args.value(QLatin1String("camera")).isString()) {
        const QString cameraId = args.value(QLatin1String("camera")).toString();
        QTemporaryDir scratch;
        if (!scratch.isValid())
            return textResult(QStringLiteral("screenshot: could not create a scratch directory"), true);
        const QString file = scratch.filePath(QStringLiteral("camera.png"));
        const bool postFxShot = args.value(QLatin1String("postFx")).toBool(true);
        const QByteArray quoted =
            QJsonDocument(QJsonArray{ cameraId, file }).toJson(QJsonDocument::Compact);
        const QString expr = QStringLiteral("camera.screenshot(%1[0], %1[1], {width: %2, height: %3, postFx: %4})")
                                 .arg(QString::fromUtf8(quoted))
                                 .arg(width).arg(height)
                                 .arg(postFxShot ? QStringLiteral("true") : QStringLiteral("false"));
        // The deterministic frame FIRST: a socketed camera is placed by the
        // mirror's socket pass, which only runs inside a sync.
        host.viewport->renderFrames(2);
        const ScriptResult shot = mEngine->evaluate(expr, QStringLiteral("<screenshot>"), false);
        if (!shot.ok)
            return textResult(QStringLiteral("screenshot: %1").arg(shot.error), true);
        QFile png(file);
        if (!png.open(QIODevice::ReadOnly))
            return textResult(QStringLiteral("screenshot: the camera render produced no file"), true);
        const QByteArray bytes = png.readAll();
        QJsonObject echo = QJsonDocument::fromVariant(shot.value).object();
        echo.remove(QStringLiteral("path"));    // a scratch path is noise to the caller
        echo["camera"] = cameraId;
        return QJsonObject{ { "content", QJsonArray{
            QJsonObject{ { "type", "image" }, { "data", QString::fromLatin1(bytes.toBase64()) },
                         { "mimeType", "image/png" } },
            QJsonObject{ { "type", "text" },
                         { "text", QStringLiteral("shot through scene camera %1: %2")
                                       .arg(cameraId, QString::fromUtf8(
                                           QJsonDocument(echo).toJson(QJsonDocument::Compact))) } } } } };
    }

    QString poseExpr = QStringLiteral("editor.camera()");
    if (hasCamera) {
        poseExpr = QStringLiteral("editor.setCamera(%1)").arg(QString::fromUtf8(
            QJsonDocument(args.value(QLatin1String("camera")).toObject())
                .toJson(QJsonDocument::Compact)));
    } else if (hasFrameNode) {
        const QJsonValue raw = args.value(QLatin1String("frameNode"));
        QJsonObject framing = raw.toObject();
        QString nodeId = framing.take(QStringLiteral("id")).toString();
        if (raw.isString()) nodeId = raw.toString();   // a bare id is accepted
        if (nodeId.isEmpty())
            return textResult(QStringLiteral(
                "screenshot: frameNode needs a node id — {id: \"…\", yaw?, pitch?, distance?} "
                "(describe_scene lists the ids)"), true);
        // QJsonDocument only serializes objects/arrays, so the id rides in a
        // throwaway array to get it correctly quoted and escaped.
        const QByteArray quotedId =
            QJsonDocument(QJsonArray{ nodeId }).toJson(QJsonDocument::Compact);
        poseExpr = QStringLiteral("editor.frameNode(%1, %2)")
                       .arg(QString::fromUtf8(quotedId.mid(1, quotedId.size() - 2)),
                            QString::fromUtf8(QJsonDocument(framing).toJson(QJsonDocument::Compact)));
    }
    const ScriptResult poseResult =
        mEngine->evaluate(poseExpr, QStringLiteral("<screenshot>"), false);
    if (!poseResult.ok)
        return textResult(QStringLiteral("screenshot: %1").arg(poseResult.error), true);
    const QJsonObject pose = QJsonDocument::fromVariant(poseResult.value).object();

    // Deterministic frame: document -> engine sync + render before reading pixels
    // (the editor.frame(n) pattern of the headless suites).
    // postFx: with the scene's post chain on, the shot looks like the viewport
    // (tonemapped, bloomed, anti-aliased) instead of like a neutral thumbnail.
    // Default TRUE here, unlike editor.screenshot: this tool exists so a human
    // or an assistant can SEE the work, not to assert exact pixels.
    const bool postFx = args.value(QLatin1String("postFx")).toBool(true);
    host.viewport->renderFrames(2);
    const QImage img = host.viewport->takeScreenshot(width, height, postFx);
    if (img.isNull())
        return textResult(QStringLiteral("screenshot: the viewport returned no image"), true);

    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    img.save(&buffer, "PNG");

    QJsonObject item{ { "type", "image" },
                      { "data", QString::fromLatin1(png.toBase64()) },
                      { "mimeType", "image/png" } };
    // The image FIRST (clients that read only content[0] keep working), the
    // pose echo beside it.
    QJsonObject echo = pose;
    echo["view"] = QStringLiteral("editor");
    echo["width"] = img.width();
    echo["height"] = img.height();
    const QJsonObject poseItem{
        { "type", "text" },
        { "text", QStringLiteral("camera pose for this shot: %1")
                      .arg(QString::fromUtf8(QJsonDocument(echo).toJson(QJsonDocument::Compact))) } };
    return QJsonObject{ { "content", QJsonArray{ item, poseItem } } };
}

QJsonObject McpTools::browseAssets(const QJsonObject &args)
{
    const int limit = qBound(1, args.value(QLatin1String("limit")).toInt(kDefaultBrowseLimit),
                             kMaxBrowseLimit);

    // The VERB is the source of truth (API-first, SCRIPTING_SPEC §2.3): this
    // tool adds no filtering, no ordering and no scope of its own — it asks
    // assets.list exactly what a script would, then carries the bytes a JSON
    // return value cannot. One extra row is requested so `more` can be honest
    // without a second, unbounded scan of the library.
    QJsonObject listArgs{ { "scope", "store" }, { "limit", limit + 1 } };
    const QString query = args.value(QLatin1String("query")).toString().trimmed();
    if (!query.isEmpty()) listArgs["query"] = query;
    const QString type = args.value(QLatin1String("type")).toString().trimmed();
    if (!type.isEmpty()) listArgs["type"] = type;
    if (args.contains(QLatin1String("drawer")))
        listArgs["drawer"] = args.value(QLatin1String("drawer")).toInt();

    const ScriptResult listed = mEngine->evaluate(
        QStringLiteral("assets.list(%1)").arg(QString::fromUtf8(
            QJsonDocument(listArgs).toJson(QJsonDocument::Compact))),
        QStringLiteral("<browse_assets>"), false);
    if (!listed.ok)
        return textResult(QStringLiteral("browse_assets: %1").arg(listed.error), true);

    QJsonArray rows = QJsonDocument::fromVariant(listed.value).array();
    const bool more = rows.size() > limit;
    while (rows.size() > limit) rows.removeLast();

    Database *db = mEngine->scriptHost().db;
    QJsonArray content;
    QJsonArray rowsOut;
    QJsonArray images;          // built alongside, appended after the summary
    int imageBytes = 0, pictured = 0, missing = 0, overBudget = 0;

    for (const QJsonValue &v : std::as_const(rows)) {
        const QJsonObject row = v.toObject();
        const QString guid = row.value(QLatin1String("guid")).toString();
        QJsonObject out = row;

        QByteArray png = db ? db->fetchAsset(guid).thumbnail : QByteArray();
        QImage thumb;
        if (!png.isEmpty()) thumb.loadFromData(png, "PNG");

        if (thumb.isNull()) {
            out["image"] = false;
            // Say WHY there is no picture: "no stored thumbnail" is actionable
            // (assets.refreshThumbnail), "over budget" is not the same thing.
            out["imageNote"] = db ? QStringLiteral("no stored thumbnail")
                                  : QStringLiteral("no library in this session");
            ++missing;
            rowsOut.append(out);
            continue;
        }

        // Downscale FIRST (one 512x512 engine render is ~60 KB; at 128 px it is
        // a few), then re-encode. Qt keeps the aspect ratio.
        if (thumb.width() > kThumbnailLongEdge || thumb.height() > kThumbnailLongEdge) {
            thumb = thumb.scaled(kThumbnailLongEdge, kThumbnailLongEdge,
                                 Qt::KeepAspectRatio, Qt::SmoothTransformation);
            QByteArray scaled;
            QBuffer buffer(&scaled);
            buffer.open(QIODevice::WriteOnly);
            if (thumb.save(&buffer, "PNG")) png = scaled;
        }

        if (imageBytes + png.size() > kBrowseImageBudget) {
            out["image"] = false;
            out["imageNote"] = QStringLiteral("omitted: the response image budget is full");
            ++overBudget;
            rowsOut.append(out);
            continue;
        }

        out["image"] = true;
        out["imageWidth"] = thumb.width();
        out["imageHeight"] = thumb.height();
        imageBytes += png.size();
        ++pictured;
        rowsOut.append(out);

        // Caption THEN image, in row order: MCP image blocks carry no name, so
        // without the caption a reader has to count blocks to tell which
        // picture belongs to which guid.
        images.append(QJsonObject{
            { "type", "text" },
            { "text", QStringLiteral("%1 — %2 — %3")
                          .arg(row.value(QLatin1String("name")).toString(),
                               row.value(QLatin1String("type")).toString(), guid) } });
        images.append(QJsonObject{ { "type", "image" },
                                   { "data", QString::fromLatin1(png.toBase64()) },
                                   { "mimeType", "image/png" } });
    }

    QJsonObject summary{
        { "scope", "store" },
        { "count", rowsOut.size() },
        { "limit", limit },
        { "more", more },
        { "images", pictured },
        { "imageBytes", imageBytes },
        { "imageBudgetBytes", kBrowseImageBudget },
        { "assets", rowsOut } };
    if (missing) summary["withoutThumbnail"] = missing;
    if (overBudget) summary["imagesOmittedForBudget"] = overBudget;
    if (more)
        summary["hint"] = QStringLiteral(
            "more assets match — narrow with query/type/drawer, or raise limit (max %1)")
            .arg(kMaxBrowseLimit);
    if (rowsOut.isEmpty())
        summary["hint"] = QStringLiteral(
            "no store asset matches — run_script: assets.list({scope:'store'}) to see the "
            "whole library, assets.importFile(path) to add one");

    content.append(QJsonObject{
        { "type", "text" },
        { "text", QString::fromUtf8(QJsonDocument(summary).toJson(QJsonDocument::Compact)) } });
    for (const QJsonValue &item : std::as_const(images)) content.append(item);
    return QJsonObject{ { "content", content } };
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
