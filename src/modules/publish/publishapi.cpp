/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "modules/publish/publishapi.h"

#include <QDir>
#include <QFileInfo>

#include "data/project.h"
#include "modules/publish/publishrecord.h"

QVector<VerbInfo> PublishApi::verbs() const
{
    return {
        { "state", "publish.state() -> {state, dir, index, when, exists}",
          "The open project's LAST WEB PUBLISH, from the record the Publish page keeps "
          "(.jah-publish.json in the project folder). `state` is \"none\" (never published), "
          "\"present\" (published and the export is still on disk) or \"missing\" (published "
          "once, the directory has since been deleted — the page's \"Process to regenerate\"). "
          "`dir` is the export directory and `index` its index.html (both empty in the \"none\" "
          "state), `when` the publish time as an ISO string, `exists` a convenience boolean for "
          "state == \"present\". Projects published before the record existed are BACKFILLED "
          "from the export's index.html timestamp, so an old publish reports honestly rather "
          "than as never-published.\n\n"
          "Read-only, on purpose: project.exportWeb writes an export and deliberately does NOT "
          "write this record — publishing stays a deliberate act performed on the Publish page "
          "— so a script can observe a publish but not fake one. With no project open every "
          "field is empty and `state` is \"none\".",
          Needs::Document },
    };
}

QString PublishApi::projectFolder() const
{
    if (!moduleHost.project || moduleHost.project->getProjectGuid().isEmpty()) return QString();
    return moduleHost.project->getProjectFolder();
}

QString PublishApi::exportDir() const
{
    const QString folder = projectFolder();
    if (folder.isEmpty()) return QString();
    // THE per-project publish path — the same one PublishPage::exportDir
    // derives, which is what makes the backfill below see the page's exports.
    return QDir(folder).filePath(QStringLiteral("exports/web"));
}

QVariantMap PublishApi::state()
{
    QVariantMap out;
    out[QStringLiteral("state")] = QStringLiteral("none");
    out[QStringLiteral("dir")] = QString();
    out[QStringLiteral("index")] = QString();
    out[QStringLiteral("when")] = QString();
    out[QStringLiteral("exists")] = false;

    const QString folder = projectFolder();
    if (folder.isEmpty()) return out;

    const PublishRecord record = PublishRecord::load(folder, exportDir());
    switch (record.state()) {
    case PublishRecord::State::None:
        return out;
    case PublishRecord::State::Present:
        out[QStringLiteral("state")] = QStringLiteral("present");
        out[QStringLiteral("exists")] = true;
        break;
    case PublishRecord::State::Missing:
        out[QStringLiteral("state")] = QStringLiteral("missing");
        break;
    }
    out[QStringLiteral("dir")] = QDir::toNativeSeparators(record.dir);
    out[QStringLiteral("index")] = QDir::toNativeSeparators(record.indexHtml());
    if (record.when.isValid()) out[QStringLiteral("when")] = record.when.toString(Qt::ISODate);
    return out;
}
