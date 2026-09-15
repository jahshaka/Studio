/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCRIPTWORKER_H
#define SCRIPTWORKER_H

// THE SCRIPT ENGINE RUNS OFF THE UI THREAD (SCRIPTING_LIVE_SPEC §3, option A).
//
// The JavaScript lives on a worker thread; every verb is a BLOCKING HOP back to
// the UI thread, executed in order, so the UI thread is free between verbs — the
// viewport paints, input works, a loop no longer freezes the app — while nothing
// a verb touches (the document, Ogre, the undo stack, widgets) ever leaves the
// thread it belongs to. Measured on this box: 5.6-9 us per hop, against verb
// bodies that cost hundreds of microseconds to milliseconds.
//
// WHY A BRIDGE AND NOT `newQObject(module)` ON THE WORKER ENGINE. A QJSEngine
// created on the worker will happily wrap a UI-thread QObject and then call its
// invokable ON THE WORKER — silently, with no warning (probed, spec §1). So the
// real modules are NEVER installed into the worker engine. What is installed is
// a plain JS object per module, generated from the registry, whose every method
// funnels into ScriptBridge::call(module, verb, args) -> one hop -> the real
// QMetaMethod on the UI thread. That bridge is also the central verb dispatch
// the API surface never had: tracing, error routing and re-entry all live in it.
//
// Three objects, three thread roles:
//   VerbDispatcher  UI thread     — finds the verb by name+arity, converts the
//                                   arguments, calls it, reports what happened.
//   ScriptBridge    worker thread — what the JS shims call; does the hop.
//   ScriptWorker    worker thread — owns the QJSEngine, installs the shims,
//                                   evaluates, reports the result.

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>

#include "scripting/scriptengine.h"

class QJSEngine;
class ApiRegistry;
struct ScriptHost;

/// What a verb call answered with. `threw` carries a JS error the shim rethrows
/// on the worker engine — which is how ApiModule::fail() still lands as a normal
/// JS exception at the script's own call site now that no engine wraps a module.
struct VerbOutcome
{
    QVariant value;
    bool     threw = false;
    QString  error;
};

/// THE UI-THREAD END OF EVERY VERB CALL. Lives on the UI thread; its dispatch()
/// only ever runs there (the bridge reaches it with a BlockingQueuedConnection).
class VerbDispatcher : public QObject
{
    Q_OBJECT
public:
    VerbDispatcher(ApiRegistry &registry, ScriptHost &host, QObject *parent = nullptr);

    /// UI THREAD ONLY. Looks the verb up by name and arity, converts each
    /// argument to the parameter's metatype, invokes it, and turns a pending
    /// fail() into `threw`.
    ///
    /// ARGUMENT CONVERSION SPEAKS JAVASCRIPT, not QVariant. V4 used to marshal
    /// these arguments and QVariant::convert disagrees with it in three places:
    /// a double into an integer parameter TRUNCATES toward zero here (convert
    /// would round: editor.frame(3.7) means three frames, not four), and JS
    /// `null` is read as "no argument" — the parameter's default, 0/false/empty
    /// — because convert refuses it outright and an agent passes null for "no
    /// opinion" all the time. THE ONE DELIBERATE DIFFERENCE that remains: a
    /// non-empty STRING into a bool parameter follows Qt, so "false" and "0"
    /// are false, where JavaScript's truthiness would make every non-empty
    /// string true. Qt's reading is the one a person typing
    /// world.photon({enabled: "false"}) means, and no verb documents a string
    /// where it wants a bool. `module` == "api" is the registry's own metadata
    /// object (api.help / api.verbs), which is dispatched here for one reason:
    /// so that EVERY C++ call a script makes happens on the UI thread, with no
    /// second rule to remember.
    VerbOutcome dispatch(const QString &module, const QString &verb, const QVariantList &args);

private:
    ApiRegistry &mRegistry;
    ScriptHost  &mHost;
};

/// WHAT THE SHIMS CALL. Created on the worker thread and wrapped into the worker
/// engine; `call` therefore runs on the worker, which is exactly where it has to
/// be to do a blocking hop to the UI thread.
class ScriptBridge : public QObject
{
    Q_OBJECT
public:
    ScriptBridge(VerbDispatcher *dispatcher, QObject *parent = nullptr);

    /// The one entry point for all 452 verbs. `args` arrives as the shim's
    /// `Array.prototype.slice.call(arguments)`, i.e. a QJSValue array; it is
    /// flattened to plain QVariants HERE, on the worker, so no QJSValue ever
    /// crosses a thread and the 39 bare-QVariant parameters receive the plain
    /// values scriptmod::normalizeJs already expects.
    Q_INVOKABLE QVariant call(const QString &module, const QString &verb, const QVariant &args);

    /// The engine whose scripts this bridge serves — the one it rethrows into.
    /// Set once, on the worker, before any script runs.
    void setEngine(QJSEngine *js) { mJs = js; }

private:
    VerbDispatcher *mDispatcher;
    QJSEngine      *mJs = nullptr;
};

/// The worker thread's script host: one QJSEngine for the life of the app (REPL
/// globals persist between console lines exactly as they did on the UI thread).
class ScriptWorker : public QObject
{
    Q_OBJECT
public:
    /// Constructed on the UI thread, then moved to the worker; `dispatcher`
    /// stays behind on the UI thread.
    explicit ScriptWorker(VerbDispatcher *dispatcher);
    ~ScriptWorker() override;

    /// The worker's engine. Read from the UI thread ONLY to call
    /// setInterrupted(), which Qt documents as thread safe. Null until setup().
    QJSEngine *engine() const { return mJs; }
    /// Valid after finished(). The queued signal that announces it is the
    /// synchronisation point, so a plain read is correct here.
    const ScriptResult &result() const { return mResult; }

public slots:
    /// ON THE WORKER. Creates the engine, the bridge and the console bridge and
    /// installs one JS shim object per module. `moduleSpecs` is built on the UI
    /// thread from the registry (a list of {name, verbs:[…]} maps) so the worker
    /// never reads the registry itself.
    void setup(const QVariantList &moduleSpecs, const QString &apiVersion);
    /// ON THE WORKER. Evaluates and then emits finished(); the caller reads
    /// result().
    void runScript(const QString &source, const QString &fileName);
    /// ON THE WORKER. Destroys the engine (a QJSEngine must die on its own
    /// thread) before the thread's loop ends.
    void teardown();

signals:
    /// console.log/print, from the worker; ScriptEngine re-emits it on the UI
    /// thread so every existing receiver stays a UI-thread receiver.
    void consoleLine(const QString &text);
    /// The run is over and result() is readable.
    void finished();

private:
    VerbDispatcher *mDispatcher;
    QJSEngine      *mJs = nullptr;
    ScriptBridge   *mBridge = nullptr;
    ScriptResult    mResult;
};

#endif // SCRIPTWORKER_H
