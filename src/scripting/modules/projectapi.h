/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCRIPTING_PROJECTAPI_H
#define SCRIPTING_PROJECTAPI_H

// project.* — project lifecycle and desktops (SCRIPTING_SPEC §1.1).
//
// DB verbs (rename/remove/list/moveToDesktop/setPosition) go straight to the
// guid-parameterised Database methods; create/open/save/close delegate to the
// MainWindow/ProjectManager halves, with the blob-only save and the synchronous
// asset preload replacing the two headless traps (silent no-op save, modal
// concurrent preload).

#include <QVariantList>
#include <QVariantMap>

#include "scripting/apimodule.h"

class ProjectApi : public ApiModule
{
    Q_OBJECT
public:
    using ApiModule::ApiModule;

    QString jsName() const override { return QStringLiteral("project"); }
    QVector<VerbInfo> verbs() const override;

    Q_INVOKABLE QString create(const QString &name);
    Q_INVOKABLE bool open(const QString &guidOrName);
    Q_INVOKABLE bool save();
    Q_INVOKABLE bool close();
    Q_INVOKABLE bool rename(const QString &guid, const QString &newName);
    Q_INVOKABLE bool remove(const QString &guid);
    Q_INVOKABLE QVariantList list(const QVariantMap &options = QVariantMap());
    Q_INVOKABLE bool moveToDesktop(const QString &guid, int desktop);
    Q_INVOKABLE bool setPosition(const QString &guid, double x, double y);
    Q_INVOKABLE QVariant current();
    Q_INVOKABLE QVariantMap exportWeb(const QString &dir = QString());
    Q_INVOKABLE QVariantMap previewWeb(const QString &dir = QString());
    Q_INVOKABLE QVariantMap exportManifest(const QString &dir = QString());

private:
    QString resolveGuid(const QString &guidOrName, QString *nameOut = nullptr);
};

#endif // SCRIPTING_PROJECTAPI_H
