/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef PUBLISHAPI_H
#define PUBLISHAPI_H

// publish.* — the publish module's verbs (2026-09-06 verb-coverage audit F14).
//
// READ-ONLY, deliberately. The publish RECORD (.jah-publish.json, one publish
// per project) was invisible to scripts: `project.exportWeb` writes an export
// wherever it is told and does NOT touch the record, because publishing is a
// deliberate act performed on the Publish page — that stays true. What was
// missing is the other half: a script, a test or an assistant could not ask
// "has this project ever been published, where, and is the export still
// there?", which is the question every publish workflow starts with.
//
// The module owns these verbs (StudioModule::registerApi) rather than the core
// registry: publish is a module, and its interface is its verbs.

#include <QVariantMap>

#include "modules/studiomodule.h"
#include "scripting/apimodule.h"

class PublishApi : public ApiModule
{
    Q_OBJECT
public:
    PublishApi(ScriptHost &host, const ModuleHost &moduleHost)
        : ApiModule(host), moduleHost(moduleHost) {}

    QString jsName() const override { return QStringLiteral("publish"); }
    QVector<VerbInfo> verbs() const override;

    Q_INVOKABLE QVariantMap state();

private:
    /// The open project's folder, or empty when no real project is open (the
    /// same rule the page uses: a guid AND a folder — the selftest's
    /// project-less scene must not look publishable).
    QString projectFolder() const;
    QString exportDir() const;

    ModuleHost moduleHost;
};

#endif // PUBLISHAPI_H
