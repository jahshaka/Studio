/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "scripting/modules/clipboardapi.h"

#include <QJsonObject>

#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"

#include "scripting/modules/moduleshared.h"
#include "services/clipboardservice.h"
#include "services/sceneeditservice.h"
#include "services/selectionservice.h"
#include "services/services.h"

using scriptmod::findNodeByGuid;

QVector<VerbInfo> ClipboardApi::verbs() const
{
    return {
        { "copy", "clipboard.copy(id | [id]) -> {items, assets, inlined, referenced, bytes}",
          "Copies scene objects — the given ids, or the selection when none are given — onto the "
          "SYSTEM clipboard as one versioned text payload (`jahshaka.clipboard`), offered under "
          "both `application/x-jahshaka-clipboard` and `text/plain`, so the copy survives a second "
          "instance, a text editor, a chat window and a different build. A member whose ancestor is "
          "also copied is dropped (it travels with its ancestor) and the World root is never "
          "copied. The ASSET CLOSURE travels too: every mesh, texture, IES profile, skeletal clip "
          "and avatar definition the objects reference, with its bytes inline while they fit "
          "`clipboard/inlineLimitBytes` (4 MB) and by content id above it. Not an undo entry. "
          "Copying nothing leaves the previous clipboard alone and returns items: 0.",
          Needs::Document },
        { "cut", "clipboard.cut(id | [id]) -> {items, removed: [id], skipped: [id]}",
          "clipboard.copy followed by a delete of the same set, as ONE undo step. Ctrl+Z brings "
          "the objects back and the clipboard still holds them, so a cut that was undone can still "
          "be pasted. The World root and non-removable nodes are reported in `skipped` rather than "
          "refusing the whole cut.",
          Needs::Document },
        { "copyAssets", "clipboard.copyAssets(guid | [guid]) -> {items, assets, inlined, referenced, bytes}",
          "Copies LIBRARY TILES onto the clipboard: each asset's catalog row (name, type, "
          "placement, its stored JSON) plus its dependency closure and, under the inline budget, "
          "its bytes. Pasting the payload in another instance — or another library entirely — "
          "registers the assets under the SAME guids, because an asset guid is identity.",
          Needs::Document },
        { "paste", "clipboard.paste({target, parent, index, allowMissing}) -> {pasted: [id], "
                   "imported: [guid], pinned: [guid], missing: [{guid, name, type, neededBy}], "
                   "skipped: [{kind, reason}]}",
          "Pastes what the clipboard holds. `target` is the surface asking: \"tree\" (the default "
          "— objects land beside the primary, same parent, sibling index + 1, or at the scene root "
          "when nothing is selected) or \"assets\" (library tiles land in this library). "
          "`parent`/`index` place the objects explicitly. Fresh node guids, the Unreal naming rule "
          "(`Cube` -> `Cube2`), ONE undo step for every document change; asset imports and pins sit "
          "OUTSIDE undo (they are idempotent, and no asset operation in this app is undoable). "
          "An asset this library does not have and cannot fetch is REPORTED in `missing` and the "
          "items needing it are refused — `allowMissing: true` pastes them anyway, with empty "
          "slots. An item kind this build has no paste for is listed in `skipped`, never a refusal "
          "of the whole payload.",
          Needs::Document },
        { "contents", "clipboard.contents() -> {format, version, app, source: {storeId, "
                      "sameLibrary}, items: [{kind, name}], assets: {total, inlined, referenced}}",
          "A DESCRIPTION of what the clipboard holds — never the payload itself, so a tool can "
          "decide what a paste would do without reading a megabyte of base64. Empty items when the "
          "clipboard holds something that is not a Jahshaka payload.",
          Needs::Document },
        { "text", "clipboard.text() -> string",
          "The raw payload, exactly as it sits on the system clipboard. Empty when the clipboard "
          "holds something else. This is what makes a copy inspectable, diffable and testable.",
          Needs::Document },
        { "setText", "clipboard.setText(payload) -> {valid, items, error}",
          "Puts a payload on the clipboard (Unreal's `SourceData`): validates it and reports what "
          "it holds. It does NOT paste — call clipboard.paste after it. An invalid payload is "
          "refused with `error` and the clipboard is left untouched.",
          Needs::Document },
        { "resolve", "clipboard.resolve() -> {known: [guid], importable: [guid], "
                     "missing: [{guid, name, type, neededBy}], sameLibrary}",
          "A DRY RUN of the paste's asset resolution: which referenced assets this library already "
          "has, which can be imported from the payload (inline bytes, or content this machine "
          "already stores), and which are missing. Writes nothing. `sameLibrary` compares STORE "
          "IDS and is false whenever either store has no identity yet (a library that has never "
          "imported an asset has no store.json) — resolution never depends on it, only the "
          "sibling-store fetch does.",
          Needs::Document },
    };
}

ClipboardService *ClipboardApi::service()
{
    if (!host.services || !host.services->clipboard) {
        fail("clipboard: not available in this session");
        return nullptr;
    }
    return host.services->clipboard;
}

bool ClipboardApi::resolveNodes(const QVariant &ids, const QString &verb,
                                QList<iris::SceneNodePtr> &out)
{
    auto scene = (host.services && host.services->sceneEdit) ? host.services->sceneEdit->scene()
                                                             : iris::ScenePtr();
    if (!scene) return fail(verb + QStringLiteral(": no scene is open"));

    if (!ids.isValid() || ids.isNull()) {
        if (host.services->selection) out = host.services->selection->selectedSet();
        return true;
    }
    // A JS array arrives as a QJSValue, not a QVariantList — normalizeJs is the
    // one place that difference is handled (the same call editor.select makes).
    const QVariant value = scriptmod::normalizeJs(ids);
    QStringList list;
    if (value.typeId() == QMetaType::QVariantList) {
        for (const QVariant &entry : value.toList()) {
            const QString guid = entry.toString();
            if (!guid.isEmpty()) list << guid;
        }
    } else {
        const QString guid = value.toString();
        if (!guid.isEmpty()) list << guid;
    }
    for (const QString &id : list) {
        auto node = findNodeByGuid(scene->getRootNode(), id);
        if (!node) return fail(QStringLiteral("%1: no node with id '%2'").arg(verb, id));
        out.append(node);
    }
    return true;
}

namespace {

QVariantMap copyToMap(const ClipboardCopyResult &result)
{
    return QVariantMap{ { "items", result.items },
                        { "assets", result.assets },
                        { "inlined", result.inlined },
                        { "referenced", result.referenced },
                        { "bytes", double(result.bytes) } };
}

QVariantList missingToList(const QVector<ClipboardMissing> &missing)
{
    QVariantList out;
    for (const auto &entry : missing)
        out.append(QVariantMap{ { "guid", entry.guid }, { "name", entry.name },
                                { "type", entry.type }, { "size", double(entry.size) },
                                { "neededBy", entry.neededBy } });
    return out;
}

} // namespace

QVariantMap ClipboardApi::copy(const QVariant &ids)
{
    auto *clipboard = service();
    if (!clipboard) return QVariantMap();
    QList<iris::SceneNodePtr> nodes;
    if (!resolveNodes(ids, QStringLiteral("clipboard.copy"), nodes)) return QVariantMap();
    if (nodes.isEmpty()) {
        // A documented outcome, not an error: the previous clipboard survives.
        refuse("clipboard.copy: nothing is selected");
        return copyToMap(ClipboardCopyResult());
    }
    const auto result = clipboard->copyNodes(nodes);
    if (!result.ok()) refuse(QStringLiteral("clipboard.copy: %1").arg(result.error));
    return copyToMap(result);
}

QVariantMap ClipboardApi::cut(const QVariant &ids)
{
    auto *clipboard = service();
    if (!clipboard) return QVariantMap();
    QList<iris::SceneNodePtr> nodes;
    if (!resolveNodes(ids, QStringLiteral("clipboard.cut"), nodes)) return QVariantMap();
    if (nodes.isEmpty()) {
        refuse("clipboard.cut: nothing is selected");
        return QVariantMap{ { "items", 0 }, { "removed", QVariantList() },
                            { "skipped", QVariantList() } };
    }
    const auto result = clipboard->cutNodes(nodes);
    if (!result.error.isEmpty()) refuse(QStringLiteral("clipboard.cut: %1").arg(result.error));
    return QVariantMap{ { "items", result.copy.items },
                        { "removed", QVariant(result.removed) },
                        { "skipped", QVariant(result.skipped) } };
}

QVariantMap ClipboardApi::copyAssets(const QVariant &guids)
{
    auto *clipboard = service();
    if (!clipboard) return QVariantMap();
    const QVariant value = scriptmod::normalizeJs(guids);
    QStringList list;
    if (value.typeId() == QMetaType::QVariantList) {
        for (const QVariant &entry : value.toList()) {
            const QString guid = entry.toString();
            if (!guid.isEmpty()) list << guid;
        }
    } else if (value.isValid()) {
        const QString guid = value.toString();
        if (!guid.isEmpty()) list << guid;
    }
    if (list.isEmpty()) {
        refuse("clipboard.copyAssets: no asset guids were given");
        return copyToMap(ClipboardCopyResult());
    }
    const auto result = clipboard->copyAssets(list);
    if (!result.ok()) refuse(QStringLiteral("clipboard.copyAssets: %1").arg(result.error));
    return copyToMap(result);
}

QVariantMap ClipboardApi::paste(const QVariantMap &options)
{
    QVariantMap out;
    auto *clipboard = service();
    if (!clipboard) return out;

    ClipboardPasteOptions pasteOptions;
    if (options.contains(QStringLiteral("target")))
        pasteOptions.target = options.value(QStringLiteral("target")).toString();
    pasteOptions.parentGuid = options.value(QStringLiteral("parent")).toString();
    pasteOptions.index = options.value(QStringLiteral("index"), -1).toInt();
    pasteOptions.allowMissing = options.value(QStringLiteral("allowMissing"), false).toBool();

    const auto result = clipboard->paste(pasteOptions);
    if (!result.error.isEmpty()) {
        refuse(QStringLiteral("clipboard.paste: %1").arg(result.error));
        out["pasted"] = QVariantList();
        out["error"] = result.error;
        return out;
    }
    out["pasted"] = QVariant(result.pasted);
    out["imported"] = QVariant(result.imported);
    out["pinned"] = QVariant(result.pinned);
    out["missing"] = missingToList(result.missing);
    QVariantList skipped;
    for (const auto &entry : result.skipped)
        skipped.append(QVariantMap{ { "kind", entry.kind }, { "reason", entry.reason } });
    out["skipped"] = skipped;
    return out;
}

QVariantMap ClipboardApi::contents()
{
    QVariantMap out;
    auto *clipboard = service();
    if (!clipboard) return out;

    const auto envelope = clipboard->contents();
    if (envelope.isNull()) {
        out["items"] = QVariantList();
        return out;
    }
    out["format"] = QString::fromLatin1(clipboardformat::kFormatId());
    out["version"] = envelope.version;
    out["app"] = envelope.app;
    out["sceneFormat"] = envelope.sceneFormat;
    out["source"] = QVariantMap{ { "storeId", envelope.source.storeId },
                                 { "projectGuid", envelope.source.projectGuid },
                                 { "sameLibrary", clipboard->sameLibrary(envelope) } };
    QVariantList items;
    for (const auto &item : envelope.items)
        items.append(QVariantMap{ { "kind", item.kind }, { "name", item.displayName() } });
    out["items"] = items;
    int inlined = 0, referenced = 0;
    for (const auto &asset : envelope.assets) {
        bool hasBytes = false;
        for (const auto &file : asset.files) if (file.hasBytes()) { hasBytes = true; break; }
        if (hasBytes) ++inlined; else ++referenced;
    }
    out["assets"] = QVariantMap{ { "total", envelope.assets.size() },
                                 { "inlined", inlined },
                                 { "referenced", referenced },
                                 { "bytes", double(envelope.inlineBytes()) } };
    return out;
}

QString ClipboardApi::text()
{
    auto *clipboard = service();
    if (!clipboard) return QString();
    return QString::fromUtf8(clipboard->text());
}

QVariantMap ClipboardApi::setText(const QString &payload)
{
    QVariantMap out;
    auto *clipboard = service();
    if (!clipboard) return out;
    QString error;
    const bool valid = clipboard->setText(payload.toUtf8(), &error);
    out["valid"] = valid;
    out["items"] = valid ? clipboard->contents().items.size() : 0;
    if (!valid) {
        out["error"] = error;
        refuse(QStringLiteral("clipboard.setText: %1").arg(error));
    }
    return out;
}

QVariantMap ClipboardApi::resolve()
{
    QVariantMap out;
    auto *clipboard = service();
    if (!clipboard) return out;
    const auto envelope = clipboard->contents();
    const auto report = clipboard->resolve();
    out["known"] = QVariant(report.known);
    out["importable"] = QVariant(report.importable);
    out["missing"] = missingToList(report.missing);
    out["sameLibrary"] = clipboard->sameLibrary(envelope);
    return out;
}
