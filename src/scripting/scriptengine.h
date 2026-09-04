/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCRIPTENGINE_H
#define SCRIPTENGINE_H

// ScriptEngine — owns the QJSEngine and the ApiRegistry (SCRIPTING_SPEC §2, §4).
//
// Main thread only: evaluate() is called from the console dock, the --script CLI
// after boot, or (v2) the socket bridge via a queued connection — never a worker.
//
// Each top-level evaluate() is wrapped in one undo macro ("script: <name>") so a
// single Ctrl+Z reverts the whole script run; editor.beginBatch()/endBatch() is
// the finer-grained escape hatch (the editor module manages nesting).

#include <QJSEngine>
#include <QObject>
#include <QString>
#include <QVariant>

#include "scripting/apiregistry.h"
#include "scripting/scripthost.h"

struct ScriptResult
{
    bool ok = false;
    QVariant value;          // the completion value, JSON-converted
    QString fileName;
    QString error;           // error message (empty when ok)
    int line = 0;            // 1-based line of the error, 0 if unknown
    QString stack;           // JS stack trace, newline-joined
    bool timedOut = false;   // the run was cut short by evaluate()'s watchdog

    /// "file.js:12: TypeError: …" — the console/CLI display form.
    QString toString() const;
};

class ScriptEngine : public QObject
{
    Q_OBJECT
public:
    explicit ScriptEngine(ScriptHost &host, QObject *parent = nullptr);

    ScriptHost &scriptHost() { return mHost; }
    ApiRegistry &registry() { return mRegistry; }
    QJSEngine &jsEngine() { return mJs; }

    /// Registers a module: parents it to this ScriptEngine (so the JS engine
    /// never garbage-collects it) and adds it to the registry.
    void addModule(ApiModule *module);

    /// Installs all registered modules + `api` + `console` into the JS global
    /// object. Idempotent; called automatically by the first evaluate().
    void installApi();

    /// Runs a script. wrapUndoMacro=true (the default) makes the whole run one
    /// undo step when the host has an undo stack; pass false for REPL fragments
    /// that should not create empty undo entries (e.g. pure queries).
    ///
    /// `timeoutMs` > 0 arms a watchdog (AI_SURFACE_AUDIT F6): a worker thread
    /// calls QJSEngine::setInterrupted(true) when the budget expires, the run
    /// aborts, and ScriptResult::timedOut is set. The flag is always reset
    /// before returning — leaving it set would kill every later script
    /// instantly. LIMIT, and it is a hard one: QJSEngine checks the flag at
    /// JAVASCRIPT bytecode boundaries only, so a run parked inside a native
    /// verb (editor.frame, graph.bake, warmUpShaders, a synchronous import) is
    /// not interruptible and the timeout fires only when control returns to JS.
    ScriptResult evaluate(const QString &source,
                          const QString &fileName = QStringLiteral("<console>"),
                          bool wrapUndoMacro = true,
                          int timeoutMs = 0);

signals:
    /// console.log/info/warn/error output, one line per call.
    void consoleOutput(const QString &text);

private:
    ScriptHost &mHost;
    ApiRegistry mRegistry;
    QJSEngine mJs;
    bool mInstalled = false;
};

#endif // SCRIPTENGINE_H
