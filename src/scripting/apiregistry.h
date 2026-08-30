/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef APIREGISTRY_H
#define APIREGISTRY_H

// ApiRegistry — the ONE registry of scripting modules and verbs (SCRIPTING_SPEC §2.2).
//
// Four consumers enumerate it: the JS context (install), the console's help() and
// autocomplete, the generated docs page (markdown), and the v2 MCP tool schemas
// (schema). A verb that isn't registered doesn't exist, anywhere.

#include <QJsonArray>
#include <QString>
#include <QStringList>
#include <QVector>

#include "scripting/apimodule.h"

class QJSEngine;

class ApiRegistry
{
public:
    /// Current API surface version — scripts read it as api.version. Bump on any
    /// verb rename/removal; freeze names when the MCP server ships (spec §8).
    static const char *apiVersion();

    /// Registers a module. The registry does not own it — parent modules to the
    /// QObject that owns the ScriptEngine (they must outlive the JS engine).
    void add(ApiModule *module);

    QVector<ApiModule *> modules() const { return mModules; }
    ApiModule *module(const QString &jsName) const;

    /// Installs every module as a JS global plus the `api` info object
    /// (api.version, api.help([name]), api.verbs()).
    void install(QJSEngine &engine);

    /// Metadata sanity: every verb must have a name, signature and doc string.
    /// Returns human-readable problems; empty list = valid. A test asserts this
    /// stays empty so new verbs cannot ship undocumented or unclassified (§2.1).
    QStringList validate() const;

    /// help() text: all modules+verbs, or one module / one verb when topic given.
    QString helpText(const QString &topic = QString()) const;

    /// The whole surface as JSON — the v2 MCP schema source.
    QJsonArray schema() const;

    /// docs/SCRIPTING.md — generated from the registry so it cannot drift.
    QString markdown() const;

    static QString needsName(Needs needs);

private:
    QVector<ApiModule *> mModules;
};

#endif // APIREGISTRY_H
