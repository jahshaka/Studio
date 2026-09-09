/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "io/clipboardformat.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>

namespace clipboardformat {

QString ClipItem::displayName() const
{
    if (kind == QLatin1String("node"))
        return nodeObject().value(QStringLiteral("name")).toString();
    if (kind == QLatin1String("asset"))
        return data.value(QStringLiteral("name")).toString();
    if (kind == QLatin1String("material"))
        return data.value(QStringLiteral("material")).toObject()
                   .value(QStringLiteral("name")).toString();
    if (kind == QLatin1String("graph")) {
        const int n = data.value(QStringLiteral("graph")).toObject()
                          .value(QStringLiteral("nodes")).toArray().size();
        return QStringLiteral("%1 graph node(s)").arg(n);
    }
    return data.value(QStringLiteral("name")).toString();
}

QVector<ClipItem> Envelope::itemsOfKind(const QString &wanted) const
{
    QVector<ClipItem> out;
    for (const ClipItem &item : items) if (item.kind == wanted) out.append(item);
    return out;
}

qint64 Envelope::inlineBytes() const
{
    qint64 total = 0;
    for (const ClipAsset &asset : assets)
        for (const ClipFile &file : asset.files) total += file.inlineData.size();
    return total;
}

namespace {

QJsonObject fileToJson(const ClipFile &file)
{
    QJsonObject obj;
    obj[QStringLiteral("role")] = file.role;
    obj[QStringLiteral("name")] = file.name;
    if (!file.ext.isEmpty()) obj[QStringLiteral("ext")] = file.ext;
    if (file.size >= 0) obj[QStringLiteral("size")] = double(file.size);
    if (!file.oid.isEmpty()) obj[QStringLiteral("oid")] = file.oid;
    // base64 ONLY when the bytes fit the budget; above it the oid is the whole
    // reference (§2.3) and the resolver finds the content by it.
    if (!file.inlineData.isEmpty())
        obj[QStringLiteral("inline")] = QString::fromLatin1(file.inlineData.toBase64());
    return obj;
}

ClipFile fileFromJson(const QJsonObject &obj)
{
    ClipFile file;
    file.role = obj.value(QStringLiteral("role")).toString();
    file.name = obj.value(QStringLiteral("name")).toString();
    file.ext  = obj.value(QStringLiteral("ext")).toString();
    file.size = obj.contains(QStringLiteral("size"))
                    ? qint64(obj.value(QStringLiteral("size")).toDouble(-1)) : -1;
    file.oid  = obj.value(QStringLiteral("oid")).toString();
    const QString encoded = obj.value(QStringLiteral("inline")).toString();
    if (!encoded.isEmpty())
        file.inlineData = QByteArray::fromBase64(encoded.toLatin1());
    return file;
}

} // namespace

QByteArray Envelope::toText() const
{
    // THE MARKER GOES FIRST, and QJsonDocument cannot put it there: it writes
    // an object's keys in sorted order, so `assets` — the megabyte of base64 —
    // would lead and `format` would sit somewhere in the middle. A payload
    // whose identity is not in its first bytes is not self-identifying: the
    // sniff would have to scan (or parse) every clipboard the user ever fills,
    // and a human pasting it into a text editor would see base64 rather than
    // what the thing is. So the two identity keys are written by hand around
    // the body (which carries neither), and the parser stays order-agnostic.
    QJsonObject root;
    if (!app.isEmpty()) root[QStringLiteral("app")] = app;
    if (sceneFormat > 0) root[QStringLiteral("sceneFormat")] = sceneFormat;

    QJsonObject sourceObj;
    if (!source.storeId.isEmpty())     sourceObj[QStringLiteral("storeId")] = source.storeId;
    if (!source.projectGuid.isEmpty()) sourceObj[QStringLiteral("projectGuid")] = source.projectGuid;
    if (!source.storeRoot.isEmpty())   sourceObj[QStringLiteral("storeRoot")] = source.storeRoot;
    if (!sourceObj.isEmpty()) root[QStringLiteral("source")] = sourceObj;
    if (!created.isEmpty()) root[QStringLiteral("created")] = created;

    QJsonArray itemsArray;
    for (const ClipItem &item : items) {
        QJsonObject obj = item.data;
        obj[QStringLiteral("kind")] = item.kind;
        itemsArray.append(obj);
    }
    root[QStringLiteral("items")] = itemsArray;

    if (!assets.isEmpty()) {
        QJsonObject assetsObj;
        for (auto it = assets.constBegin(); it != assets.constEnd(); ++it) {
            const ClipAsset &asset = it.value();
            QJsonObject obj;
            obj[QStringLiteral("name")] = asset.name;
            obj[QStringLiteral("type")] = asset.type;
            if (asset.typeId >= 0) obj[QStringLiteral("typeId")] = asset.typeId;
            if (!asset.parent.isEmpty()) obj[QStringLiteral("parent")] = asset.parent;
            if (asset.viewFilter >= 0) obj[QStringLiteral("viewFilter")] = asset.viewFilter;
            if (!asset.dependencies.isEmpty()) {
                QJsonArray deps;
                for (const QString &dep : asset.dependencies) deps.append(dep);
                obj[QStringLiteral("dependencies")] = deps;
            }
            QJsonArray files;
            for (const ClipFile &file : asset.files) files.append(fileToJson(file));
            if (!files.isEmpty()) obj[QStringLiteral("files")] = files;
            // The row's own JSON columns. An Object row's `asset` blob IS a
            // node object (sceneformat.h "WHAT DID NOT CHANGE"), which is what
            // makes "copy a library tile, paste it into another instance's
            // library" produce a usable row rather than an empty one.
            if (!asset.blob.isEmpty())
                obj[QStringLiteral("blob")] = QString::fromUtf8(asset.blob.toBase64());
            if (!asset.properties.isEmpty())
                obj[QStringLiteral("properties")] = QString::fromUtf8(asset.properties.toBase64());
            assetsObj[it.key()] = obj;
        }
        root[QStringLiteral("assets")] = assetsObj;
    }

    QByteArray body = QJsonDocument(root).toJson(QJsonDocument::Compact);
    QByteArray out = "{\"format\":\"";
    out += kFormatId();
    out += "\",\"version\":";
    out += QByteArray::number(version);
    if (body.size() > 2) {          // "{...}" with something in it
        out += ',';
        out += body.mid(1, body.size() - 2);
    }
    out += '}';
    return out;
}

Envelope Envelope::fromText(const QByteArray &text, QString *error)
{
    Envelope envelope;
    const auto fail = [&](const QString &why) {
        if (error) *error = why;
        return Envelope();
    };
    if (text.trimmed().isEmpty()) return fail(QStringLiteral("the clipboard is empty"));

    // PARSE, then decide what to say. The cheap prefix sniff is the BACKEND's
    // job (it decides whether a `text/plain` clipboard is worth handing over at
    // all); here the payload is already ours-or-not and the answer must not
    // depend on key ORDER — a payload that went through a pretty-printer, a
    // chat window or any tool that rewrote the JSON is still ours, and refusing
    // it would break the one property this format exists for.
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(text, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return fail(looksLikeEnvelope(text)
                        ? QStringLiteral("the clipboard payload is damaged (%1)")
                              .arg(parseError.errorString())
                        : QStringLiteral("not a Jahshaka clipboard payload"));

    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("format")).toString() != QLatin1String(kFormatId()))
        return fail(QStringLiteral("not a Jahshaka clipboard payload (no format marker)"));

    envelope.version = root.value(QStringLiteral("version")).toInt(0);
    if (envelope.version <= 0)
        return fail(QStringLiteral("clipboard payload carries no version"));
    // A NEWER envelope is read, not refused: every field this build knows is
    // read and the rest is ignored, which is the same forward tolerance the
    // scene format has. A reader that refuses by version cannot be the reader
    // of a format whose whole point is crossing between builds.
    envelope.app = root.value(QStringLiteral("app")).toString();
    envelope.sceneFormat = root.value(QStringLiteral("sceneFormat")).toInt(0);
    envelope.created = root.value(QStringLiteral("created")).toString();

    const QJsonObject sourceObj = root.value(QStringLiteral("source")).toObject();
    envelope.source.storeId     = sourceObj.value(QStringLiteral("storeId")).toString();
    envelope.source.projectGuid = sourceObj.value(QStringLiteral("projectGuid")).toString();
    envelope.source.storeRoot   = sourceObj.value(QStringLiteral("storeRoot")).toString();

    const QJsonArray itemsArray = root.value(QStringLiteral("items")).toArray();
    for (const QJsonValue &value : itemsArray) {
        if (!value.isObject()) continue;
        QJsonObject obj = value.toObject();
        ClipItem item;
        item.kind = obj.value(QStringLiteral("kind")).toString();
        obj.remove(QStringLiteral("kind"));
        item.data = obj;
        if (item.kind.isEmpty()) continue;      // an item with no kind is noise
        envelope.items.append(item);
    }
    if (envelope.items.isEmpty())
        return fail(QStringLiteral("clipboard payload carries no items"));

    const QJsonObject assetsObj = root.value(QStringLiteral("assets")).toObject();
    for (auto it = assetsObj.constBegin(); it != assetsObj.constEnd(); ++it) {
        const QJsonObject obj = it.value().toObject();
        ClipAsset asset;
        asset.guid = it.key();
        asset.name = obj.value(QStringLiteral("name")).toString();
        asset.type = obj.value(QStringLiteral("type")).toString();
        asset.typeId = obj.value(QStringLiteral("typeId")).toInt(-1);
        asset.parent = obj.value(QStringLiteral("parent")).toString();
        asset.viewFilter = obj.value(QStringLiteral("viewFilter")).toInt(-1);
        for (const QJsonValue &dep : obj.value(QStringLiteral("dependencies")).toArray())
            asset.dependencies << dep.toString();
        for (const QJsonValue &file : obj.value(QStringLiteral("files")).toArray())
            asset.files.append(fileFromJson(file.toObject()));
        const QString blob = obj.value(QStringLiteral("blob")).toString();
        if (!blob.isEmpty()) asset.blob = QByteArray::fromBase64(blob.toUtf8());
        const QString properties = obj.value(QStringLiteral("properties")).toString();
        if (!properties.isEmpty()) asset.properties = QByteArray::fromBase64(properties.toUtf8());
        envelope.assets.insert(asset.guid, asset);
    }

    if (error) error->clear();
    return envelope;
}

bool Envelope::looksLikeEnvelope(const QByteArray &text)
{
    // The first non-space bytes must be the object opener and the format
    // marker. Deliberately a PREFIX test on the raw bytes: a 30 MB clipboard
    // from another application must cost a strncmp, not a JSON parse.
    const QByteArray head = text.left(96).trimmed();
    if (!head.startsWith('{')) return false;
    return head.contains(QByteArray("\"format\"")) &&
           head.contains(QByteArray(kFormatId()));
}

} // namespace clipboardformat
