/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "modules/publish/publishrecord.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace {
const char kRecordFileName[] = ".jah-publish.json";
} // namespace

QString PublishRecord::indexHtml() const
{
    return dir.isEmpty() ? QString() : QDir(dir).filePath(QStringLiteral("index.html"));
}

PublishRecord::State PublishRecord::state() const
{
    if (!isValid()) return State::None;
    return QFileInfo::exists(indexHtml()) ? State::Present : State::Missing;
}

QString PublishRecord::filePath(const QString &projectFolder)
{
    if (projectFolder.isEmpty()) return QString();
    return QDir(projectFolder).filePath(QString::fromLatin1(kRecordFileName));
}

PublishRecord PublishRecord::load(const QString &projectFolder,
                                  const QString &conventionalDir)
{
    PublishRecord rec;
    const QString path = filePath(projectFolder);
    if (!path.isEmpty()) {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly)) {
            const QJsonObject web =
                QJsonDocument::fromJson(f.readAll()).object()
                    .value(QStringLiteral("web")).toObject();
            rec.dir = web.value(QStringLiteral("dir")).toString();
            rec.when = QDateTime::fromString(
                web.value(QStringLiteral("publishedAt")).toString(), Qt::ISODate);
        }
    }
    if (!rec.isValid() && !conventionalDir.isEmpty()) {
        // Backfill: projects published before the record existed.
        const QFileInfo index(QDir(conventionalDir).filePath(QStringLiteral("index.html")));
        if (index.exists()) {
            rec.dir = conventionalDir;
            rec.when = index.lastModified();
        }
    }
    return rec;
}

bool PublishRecord::save(const QString &projectFolder, const QString &dir,
                         const QDateTime &when)
{
    const QString path = filePath(projectFolder);
    if (path.isEmpty() || dir.isEmpty() || !when.isValid()) return false;
    QJsonObject web;
    web.insert(QStringLiteral("dir"), dir);
    web.insert(QStringLiteral("publishedAt"), when.toString(Qt::ISODate));
    QJsonObject root;
    root.insert(QStringLiteral("web"), web);
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return f.commit();
}
