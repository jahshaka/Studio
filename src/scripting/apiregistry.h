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

    /// ONE verb's signature + doc + needs, by "module.verb" or by a bare verb
    /// name (which may match in several modules — all matches come back).
    /// Empty when nothing matches. The `api_docs({verb})` source
    /// (AI_SURFACE_PROGRAM_SPEC lane C #2): the whole reference is ~55 KB and
    /// an agent that knows the verb's name should not have to read all of it.
    QString verbText(const QString &name) const;

    /// Every verb whose qualified name OR doc string contains `needle`
    /// (case-insensitive), newest-registered last, capped at `limit` rows
    /// (<= 0 = uncapped). The header names the match count so a truncated
    /// result says so instead of looking complete.
    QString searchText(const QString &needle, int limit = 0) const;

    /// The whole surface as JSON — the v2 MCP schema source.
    QJsonArray schema() const;

    /// docs/SCRIPTING.md — generated from the registry so it cannot drift.
    QString markdown() const;

    static QString needsName(Needs needs);

    // ---- verb tracing (MCP session logging, ledger §361) -------------------
    // "which verbs did that script actually call" has no central dispatch to
    // hook: install() hands each module's QObject wrapper straight to the JS
    // engine and every call goes engine -> qt_metacall, with nothing of ours
    // in between. So tracing REPLACES each module global with a thin JS shim
    // that records `module.verb` and forwards to the real wrapper, and puts
    // the wrappers back when it is disarmed. It is off by default and costs
    // nothing while it is: the plain wrappers are exactly what they were.
    /// Arms/disarms the trace in `engine`. Idempotent; safe before install().
    void setTracing(QJSEngine &engine, bool on);
    bool tracing() const { return mTracing; }
    /// The verbs called since the last take, in call order, deduplicated with
    /// a count: "scene.addPrimitive x64". Clears the record.
    QStringList takeTrace();
    /// Recorded by the shim; not for callers.
    void noteVerbCall(const QString &qualifiedName);

private:
    QVector<ApiModule *> mModules;
    bool mTracing = false;
    QVector<QPair<QString, int>> mTrace;   // qualified name -> calls, in first-call order
};

#endif // APIREGISTRY_H
