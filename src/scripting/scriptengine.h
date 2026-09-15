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

// ScriptEngine — the app's one script host (SCRIPTING_SPEC §2, §4).
//
// THE JAVASCRIPT RUNS ON A WORKER THREAD (SCRIPTING_LIVE_SPEC, 2026-09-15).
// evaluate() is still SYNCHRONOUS from every caller's point of view — it returns
// the run's result, as it always did — but it no longer holds the UI thread
// while the script thinks: the JS lives on ScriptWorker's thread and every verb
// is a blocking hop back here, so between two verbs the event loop runs. That is
// the whole feature: the viewport paints while a loop builds a scene, input
// works, and Stop is a button rather than a wish.
//
// evaluate() must therefore be called from the UI thread (the thread the modules
// and the document live on) and it PUMPS that thread's event loop while it
// waits — a mutex wait here would deadlock on the first hop. Two consequences
// callers must know:
//   * a run started while one is running is REFUSED (re-entrancy), which is
//     also what keeps the undo macro's bracket balanced: a verb that spins the
//     event loop itself (project.open, an import) can no longer let a second
//     run close the first one's macro;
//   * anything that can be reached from the event loop can happen mid-run, by
//     design — see ScriptRunPolicy for the one thing that must not.
//
// Each top-level evaluate() is wrapped in one undo macro ("script: <name>") so a
// single Ctrl+Z reverts the whole script run; editor.beginBatch()/endBatch() is
// the finer-grained escape hatch (the editor module manages nesting).

#include <functional>
#include <vector>

#include <QObject>
#include <QString>
#include <QVariant>

#include "scripting/apiregistry.h"
#include "scripting/scripthost.h"

class QThread;
class ScriptWorker;
class VerbDispatcher;

/// What a run does to the render loop while it runs (SCRIPTING_LIVE_SPEC §3.1).
/// The UI thread being free between verbs means the render driver's timer FIRES
/// between them — the feature for a person watching, a defect for a test, since
/// 54 e2e scripts step frames deterministically and 18 read frame counters.
enum class ScriptRunPolicy
{
    /// The driver ticks as normal: the script's effect appears as it happens.
    /// The console and the MCP default to this (the owner watches both).
    Live,
    /// The driver skips its ticks for the whole run — not stop(), a per-tick
    /// flag — so the viewport holds its last picture and NOTHING advances
    /// between verbs, which is exactly what a blocked UI thread used to give.
    /// `editor.frame(n, dt)` still renders exactly n frames: it is a verb.
    /// `--script`, `--headless` and every ctest suite run here.
    Off
};

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
    ~ScriptEngine() override;

    ScriptHost &scriptHost() { return mHost; }
    ApiRegistry &registry() { return mRegistry; }

    /// Registers a module: parents it to this ScriptEngine (so nothing else
    /// owns its lifetime) and adds it to the registry. Modules are NEVER
    /// installed into the worker's QJSEngine — see scriptworker.h.
    void addModule(ApiModule *module);

    /// Starts the worker thread and installs the API shims into its engine.
    /// Idempotent; called automatically by the first evaluate().
    void installApi();

    /// Runs a script and returns when it is done. wrapUndoMacro=true (the
    /// default) makes the whole run one undo step when the host has an undo
    /// stack; pass false for REPL fragments that should not create empty undo
    /// entries (e.g. pure queries).
    ///
    /// `timeoutMs` > 0 arms a watchdog: a timer on THIS thread (which is free
    /// now) calls QJSEngine::setInterrupted(true) when the budget expires, the
    /// run aborts, and ScriptResult::timedOut is set. LIMIT, and it is a hard
    /// one: QJSEngine checks the flag at JAVASCRIPT bytecode boundaries only,
    /// so a run parked inside a native verb (editor.frame, graph.bake, a
    /// synchronous import) is not interruptible and the interrupt lands only
    /// when control returns to JS.
    ///
    /// `policy` decides whether the render loop runs between the verbs; the
    /// default is the safe one, because every non-interactive caller wants it.
    ScriptResult evaluate(const QString &source,
                          const QString &fileName = QStringLiteral("<console>"),
                          bool wrapUndoMacro = true,
                          int timeoutMs = 0,
                          ScriptRunPolicy policy = ScriptRunPolicy::Off);

    /// True between the start and the end of a run. A second run is refused.
    bool isRunning() const { return mRunning; }
    /// The policy THIS run was started with (meaningless while nothing runs;
    /// app.scriptPolicy reports it so a script can tell what it is living in).
    ScriptRunPolicy currentRunPolicy() const { return mRunPolicy; }

    /// Stop: interrupts the running script at its next bytecode boundary. The
    /// run ends with an error, the macro closes, and the edits it already made
    /// stay as the one undo step they were always going to be. A run parked
    /// inside a verb ends when that verb returns.
    void stop();

    /// THE INTERACTIVE POLICY — what the console and the MCP use, persisted as
    /// the "Live script feedback" preference and readable/writable from a
    /// script through app.scriptPolicy(). Live by default: a person (or an
    /// agent the owner is watching) should see the script work.
    ScriptRunPolicy interactivePolicy() const { return mInteractivePolicy; }
    void setInteractivePolicy(ScriptRunPolicy policy) { mInteractivePolicy = policy; }
    static QString policyName(ScriptRunPolicy policy);
    /// "live"/"off", case-insensitive; anything else leaves `ok` false.
    static ScriptRunPolicy policyFromName(const QString &name, bool &ok);

    /// VERB TRACING (MCP session logging): while on, every `module.verb` call
    /// a script makes is recorded — at the bridge's dispatch, which is the one
    /// place every verb passes through. Off by default and free when off.
    void setVerbTracing(bool on);
    bool verbTracing() const { return mRegistry.tracing(); }
    /// The verbs called since the last take ("scene.addPrimitive x64"), cleared.
    QStringList takeVerbTrace() { return mRegistry.takeTrace(); }

signals:
    /// console.log/info/warn/error output, one line per call. ALWAYS emitted on
    /// the UI thread, whatever thread the script ran on, so every receiver —
    /// including the context-less lambdas the CLI and the MCP tap install —
    /// stays exactly as safe as it was.
    void consoleOutput(const QString &text);
    /// A run started / finished. The console dock swaps Run for Stop on it.
    void runningChanged(bool running);

private:
    ScriptHost &mHost;
    ApiRegistry mRegistry;
    QThread *mThread = nullptr;
    ScriptWorker *mWorker = nullptr;
    VerbDispatcher *mDispatcher = nullptr;
    bool mInstalled = false;
    bool mRunning = false;
    bool mStopped = false;
    ScriptRunPolicy mInteractivePolicy = ScriptRunPolicy::Live;
    ScriptRunPolicy mRunPolicy = ScriptRunPolicy::Off;
    /// What verbs asked to have done AFTER the run (ScriptHost::afterRun).
    std::vector<std::function<void()>> mAfterRun;
};

#endif // SCRIPTENGINE_H
