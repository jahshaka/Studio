/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "export/exportmanifest.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

namespace exportformat {

namespace {

const QString kFormatTag = QStringLiteral("jah-export-manifest");

// The v1 vocabulary (assetview.cpp importJahModel) — anything else on a
// one-line manifest is rejected rather than guessed at.
const QStringList kV1Kinds = {
    QStringLiteral("object"), QStringLiteral("texture"), QStringLiteral("material"),
    QStringLiteral("shader"), QStringLiteral("sky"), QStringLiteral("particle_system"),
    QStringLiteral("bundle")
};

} // namespace

QJsonObject ExportManifest::toJson() const
{
    QJsonObject root;
    root["format"] = kFormatTag;
    root["version"] = 2;
    root["kind"] = kind;
    if (!generator.isEmpty()) root["generator"] = generator;
    if (!created.isEmpty()) root["created"] = created;

    QJsonArray assetArr;
    for (const ManifestAsset &a : assets) {
        QJsonObject ao;
        ao["guid"] = a.guid;
        ao["name"] = a.name;
        ao["type"] = a.type;
        if (a.typeId >= 0) ao["typeId"] = a.typeId;
        QJsonArray files;
        for (const ManifestFile &f : a.files) {
            QJsonObject fo;
            fo["role"] = f.role;
            fo["name"] = f.name;
            if (f.size >= 0) fo["size"] = double(f.size);
            if (!f.oid.isEmpty()) fo["oid"] = f.oid;
            files.append(fo);
        }
        ao["files"] = files;
        if (!a.dependencies.isEmpty()) {
            QJsonArray deps;
            for (const QString &d : a.dependencies) deps.append(d);
            ao["dependencies"] = deps;
        }
        assetArr.append(ao);
    }
    root["assets"] = assetArr;
    return root;
}

QByteArray ExportManifest::toBytes() const
{
    return QJsonDocument(toJson()).toJson(QJsonDocument::Indented);
}

bool ExportManifest::write(const QString &path, QString *error) const
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = QStringLiteral("cannot write %1: %2").arg(path, f.errorString());
        return false;
    }
    const QByteArray bytes = toBytes();
    if (f.write(bytes) != bytes.size()) {
        if (error) *error = QStringLiteral("short write to %1").arg(path);
        return false;
    }
    return true;
}

ExportManifest ExportManifest::fromBytes(const QByteArray &bytes, QString *error)
{
    ExportManifest m;
    m.version = 0;

    const QByteArray trimmed = bytes.trimmed();
    if (trimmed.isEmpty()) {
        if (error) *error = QStringLiteral("empty manifest");
        return m;
    }

    // v1: a single word on the first line, from the historical vocabulary.
    if (!trimmed.startsWith('{')) {
        const QString word = QString::fromUtf8(trimmed.split('\n').first()).trimmed();
        if (!kV1Kinds.contains(word)) {
            if (error) *error = QStringLiteral("unknown v1 manifest kind: %1").arg(word);
            return m;
        }
        m.version = 1;
        m.kind = word;
        return m;
    }

    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(trimmed, &perr);
    if (doc.isNull() || !doc.isObject()) {
        if (error) *error = QStringLiteral("manifest JSON parse error: %1").arg(perr.errorString());
        return m;
    }
    const QJsonObject root = doc.object();
    if (root["format"].toString() != kFormatTag) {
        if (error) *error = QStringLiteral("not a %1 document").arg(kFormatTag);
        return m;
    }
    const int version = root["version"].toInt();
    if (version < 2) {
        if (error) *error = QStringLiteral("bad manifest version: %1").arg(version);
        return m;
    }

    m.version = version;
    m.kind = root["kind"].toString();
    m.generator = root["generator"].toString();
    m.created = root["created"].toString();
    for (const auto &av : root["assets"].toArray()) {
        const QJsonObject ao = av.toObject();
        ManifestAsset a;
        a.guid = ao["guid"].toString();
        a.name = ao["name"].toString();
        a.type = ao["type"].toString();
        a.typeId = ao.contains("typeId") ? ao["typeId"].toInt() : -1;
        for (const auto &fv : ao["files"].toArray()) {
            const QJsonObject fo = fv.toObject();
            ManifestFile f;
            f.role = fo["role"].toString();
            f.name = fo["name"].toString();
            f.size = fo.contains("size") ? qint64(fo["size"].toDouble()) : -1;
            f.oid = fo["oid"].toString();
            a.files.append(f);
        }
        for (const auto &dv : ao["dependencies"].toArray())
            a.dependencies.append(dv.toString());
        m.assets.append(a);
    }
    return m;
}

ExportManifest ExportManifest::fromFile(const QString &path, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        ExportManifest m;
        m.version = 0;
        if (error) *error = QStringLiteral("cannot read %1: %2").arg(path, f.errorString());
        return m;
    }
    return fromBytes(f.readAll(), error);
}

} // namespace exportformat
