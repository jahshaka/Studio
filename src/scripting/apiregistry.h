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
// Four consumers enumerate it: the SCRIPT BRIDGE (the shim objects the worker
// engine gets, one function per registered verb — so "a verb that isn't
// registered doesn't exist" is now enforced by construction, not by convention),
// the console's help() and autocomplete, the generated docs page (markdown), and
// the MCP tool schemas (schema).

#include <QJsonArray>
#include <QString>
#include <QStringList>
#include <QVector>

#include "scripting/apimodule.h"

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
    // "which verbs did that script actually call" HAS a central dispatch to
    // hook since the script engine moved off the UI thread: every verb call
    // arrives at VerbDispatcher::dispatch, which records one line here when the
    // trace is armed. (It used to swap each module global for a forwarding JS
    // shim, because the modules were QObject wrappers with nothing of ours in
    // between; that machinery is gone with them.) Off by default, and when it
    // is off the dispatcher reads one bool per call.
    /// Arms/disarms the trace. Idempotent.
    void setTracing(bool on) { mTracing = on; }
    bool tracing() const { return mTracing; }
    /// The verbs called since the last take, in call order, deduplicated with
    /// a count: "scene.addPrimitive x64". Clears the record.
    QStringList takeTrace();
    /// Recorded by the dispatcher; not for callers.
    ///
    /// THE CONSOLE CANNOT BE CHARGED TO THE AGENT ANY MORE, so nothing pauses
    /// this. The trace is armed only around an MCP run_script, and a script run
    /// started inside that window — by the console, by anything — is REFUSED
    /// (ScriptEngine::evaluate, SCRIPTING_LIVE_SPEC): there is no longer a way
    /// for a second run's verbs to reach this record. The pause pair that used
    /// to guard it is deleted.
    void noteVerbCall(const QString &qualifiedName);

private:
    QVector<ApiModule *> mModules;
    bool mTracing = false;
    QVector<QPair<QString, int>> mTrace;   // qualified name -> calls, in first-call order
};

#endif // APIREGISTRY_H
