/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "scripting/scriptworker.h"

#include <exception>

#include <QJSEngine>
#include <QJSValue>
#include <QJsonArray>
#include <QMetaMethod>
#include <QThread>

#include "scripting/apiregistry.h"
#include "scripting/scripthost.h"

namespace {

/// The most parameters any registered verb takes is four; ten is what
/// QMetaMethod::invoke can carry and what this refuses beyond.
constexpr int kMaxVerbArgs = 10;

/// The registry's own metadata object. It is NOT an ApiModule (it has no verbs
/// of its own to register), so the dispatcher answers it directly — but it
/// answers it HERE, on the UI thread, with everything else.
const char *kApiModuleName = "api";

/// One JS shim object per module. Written as JS because what it has to produce
/// is a closure per verb: a plain object whose every method funnels into the
/// bridge with the module and verb names baked in. A plain object, not a QObject
/// wrapper — which is also what retires the wrapper-shadowing trap (a verb named
/// `destroy` or `objectName` was silently uncallable through the wrapper).
const char *kInstallShim = R"JS(
(function(global, bridge, name, verbs) {
    var shim = {};
    for (var i = 0; i < verbs.length; ++i) {
        (function(verb) {
            shim[verb] = function() {
                return bridge.call(name, verb, Array.prototype.slice.call(arguments));
            };
        })(verbs[i]);
    }
    global[name] = shim;
})
)JS";

/// console.log and friends, and `help`. Unchanged from the UI-thread engine
/// except for where the line goes: __console_bridge now lives on the worker and
/// its signal is re-emitted on the UI thread by ScriptEngine.
const char *kBootstrap = R"JS(
(function() {
    function fmt(a) {
        if (a === undefined) return "undefined";
        if (a === null) return "null";
        if (typeof a === "object") {
            try { return JSON.stringify(a); } catch (e) { return String(a); }
        }
        return String(a);
    }
    function emit(args) {
        __console_bridge.emitLine(Array.prototype.map.call(args, fmt).join(" "));
    }
    console = {
        log:   function() { emit(arguments); },
        info:  function() { emit(arguments); },
        warn:  function() { emit(arguments); },
        error: function() { emit(arguments); }
    };
    print = console.log;
    help = function(topic) { console.log(api.help(topic === undefined ? "" : String(topic))); };
})();
)JS";

/// The host end of `console.log(...)`. Lives on the WORKER (it is created there
/// and wrapped there), so emitLine runs on the worker and the signal it raises
/// is the one ScriptEngine turns back into a UI-thread emission.
class ConsoleBridge : public QObject
{
    Q_OBJECT
public:
    explicit ConsoleBridge(ScriptWorker *owner) : QObject(owner), mOwner(owner) {}
    Q_INVOKABLE void emitLine(const QString &text) { emit mOwner->consoleLine(text); }
private:
    ScriptWorker *mOwner;
};

} // namespace

// ---------------------------------------------------------------------------
// VerbDispatcher — the UI thread's end
// ---------------------------------------------------------------------------

VerbDispatcher::VerbDispatcher(ApiRegistry &registry, ScriptHost &host, QObject *parent)
    : QObject(parent), mRegistry(registry), mHost(host)
{
}

VerbOutcome VerbDispatcher::dispatch(const QString &moduleName, const QString &verb,
                                     const QVariantList &argsIn)
{
    VerbOutcome out;

    // THE TRACE RECORDS HERE (MCP session logging). It used to be a JS shim
    // that replaced each module global; with a real central dispatch it is one
    // call at the one place every verb passes through, armed or not.
    if (mRegistry.tracing())
        mRegistry.noteVerbCall(moduleName + QLatin1Char('.') + verb);

    if (moduleName == QLatin1String(kApiModuleName)) {
        if (verb == QLatin1String("help"))
            out.value = mRegistry.helpText(argsIn.value(0).toString());
        else if (verb == QLatin1String("verbs"))
            out.value = QVariant::fromValue(mRegistry.schema());
        else {
            out.threw = true;
            out.error = QStringLiteral("api.%1 is not a verb").arg(verb);
        }
        return out;
    }

    ApiModule *module = mRegistry.module(moduleName);
    if (!module) {
        out.threw = true;
        out.error = QStringLiteral("no scripting module named '%1'").arg(moduleName);
        return out;
    }

    // JS has no arity: `editor.frame(5)` and `editor.frame(5, undefined)` mean
    // the same thing, and moc emits one method per default-argument prefix
    // (frame(int,double), frame(int), frame()). Trim the trailing nothings and
    // then match on the count.
    QVariantList args = argsIn;
    while (!args.isEmpty() && !args.constLast().isValid()) args.removeLast();

    QMetaMethod chosen;
    int chosenCount = -1;
    const QMetaObject *mo = module->metaObject();
    for (int i = 0; i < mo->methodCount(); ++i) {
        const QMetaMethod method = mo->method(i);
        if (method.access() != QMetaMethod::Public) continue;
        if (method.methodType() != QMetaMethod::Method
            && method.methodType() != QMetaMethod::Slot) continue;
        if (QLatin1String(method.name().constData()) != verb) continue;
        const int count = method.parameterCount();
        if (count == args.size()) { chosen = method; chosenCount = count; break; }
        // No exact match yet: prefer the smallest overload that can still take
        // every argument (the missing ones are defaulted by their metatype),
        // and fall back to the largest one, dropping the extra arguments — JS
        // discards surplus arguments too.
        if (chosenCount < 0) { chosen = method; chosenCount = count; continue; }
        const bool fits    = count >= args.size();
        const bool hadFits = chosenCount >= args.size();
        if ((fits && !hadFits) || (fits && hadFits && count < chosenCount)
            || (!fits && !hadFits && count > chosenCount)) {
            chosen = method;
            chosenCount = count;
        }
    }
    if (chosenCount < 0) {
        out.threw = true;
        out.error = QStringLiteral("%1.%2 is not a verb (api.help('%1') lists the module)")
                        .arg(moduleName, verb);
        return out;
    }
    if (chosenCount > kMaxVerbArgs) {
        out.threw = true;
        out.error = QStringLiteral("%1.%2 takes more arguments than the script bridge carries")
                        .arg(moduleName, verb);
        return out;
    }

    // CONVERT ONCE, INTO A STABLE VECTOR: the pointers below alias these
    // elements, so nothing may reallocate after this loop.
    std::vector<QVariant> boxed;
    boxed.reserve(size_t(chosenCount));
    for (int i = 0; i < chosenCount; ++i) {
        const QMetaType want = chosen.parameterMetaType(i);
        QVariant value = i < args.size() ? args.at(i) : QVariant();
        if (want.id() != QMetaType::QVariant) {
            // `undefined`/missing becomes the parameter type's default, which is
            // what a C++ default argument would have produced anyway.
            if (!value.isValid()) value = QVariant(want);
            else if (value.metaType() != want && !value.convert(want)) {
                out.threw = true;
                out.error = QStringLiteral("%1.%2: argument %3 cannot be read as %4")
                                .arg(moduleName, verb).arg(i + 1).arg(QLatin1String(want.name()));
                return out;
            }
        }
        boxed.push_back(value);
    }

    QGenericArgument a[kMaxVerbArgs];
    for (int i = 0; i < chosenCount; ++i) {
        const QMetaType want = chosen.parameterMetaType(i);
        a[i] = QGenericArgument(want.name(),
                                want.id() == QMetaType::QVariant
                                    ? static_cast<const void *>(&boxed[size_t(i)])
                                    : boxed[size_t(i)].constData());
    }

    // The pending slot is per CALL: a fail() left over from an earlier verb the
    // script caught and carried on from must not poison this one.
    mHost.pendingError.clear();

    const QMetaType ret = chosen.returnMetaType();
    bool called = false;
    QVariant returned;
    try {
        if (ret.id() == QMetaType::Void) {
            called = chosen.invoke(module, Qt::DirectConnection, QGenericReturnArgument(),
                                   a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9]);
        } else if (ret.id() == QMetaType::QVariant) {
            // A QVariant return cannot be boxed inside another QVariant without
            // a level of nesting nobody wants — take it straight.
            called = chosen.invoke(module, Qt::DirectConnection,
                                   QGenericReturnArgument("QVariant", &returned),
                                   a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9]);
        } else {
            QVariant box(ret);
            called = chosen.invoke(module, Qt::DirectConnection,
                                   QGenericReturnArgument(ret.name(), box.data()),
                                   a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9]);
            returned = box;
        }
    } catch (const std::exception &e) {
        // A C++ exception used to unwind through V4's frames, which is
        // undefined behaviour we happened never to hit. It stops here now.
        out.threw = true;
        out.error = QStringLiteral("%1.%2 threw: %3").arg(moduleName, verb, QString::fromUtf8(e.what()));
        return out;
    } catch (...) {
        out.threw = true;
        out.error = QStringLiteral("%1.%2 threw an unknown exception").arg(moduleName, verb);
        return out;
    }

    if (!called) {
        out.threw = true;
        out.error = QStringLiteral("%1.%2 could not be called with %3 argument%4")
                        .arg(moduleName, verb).arg(args.size()).arg(args.size() == 1 ? "" : "s");
        return out;
    }

    // ApiModule::fail() no longer has a wrapping engine to throw into (the
    // modules are not installed in one any more), so it records here and the
    // bridge rethrows on the worker — same message, same call site, and
    // refuse() still answers with a value instead of an exception.
    if (!mHost.pendingError.isEmpty()) {
        out.threw = true;
        out.error = mHost.pendingError;
        mHost.pendingError.clear();
        return out;
    }
    out.value = returned;
    return out;
}

// ---------------------------------------------------------------------------
// ScriptBridge — the worker's end
// ---------------------------------------------------------------------------

ScriptBridge::ScriptBridge(VerbDispatcher *dispatcher, QObject *parent)
    : QObject(parent), mDispatcher(dispatcher)
{
}

QVariant ScriptBridge::call(const QString &module, const QString &verb, const QVariant &args)
{
    // Flatten on THIS side: a QJSValue belongs to the engine that made it and
    // must never travel. toVariant() turns arrays into QVariantList and objects
    // into QVariantMap, recursively, which is exactly what the verbs' QVariant
    // parameters expect.
    QVariant flat = args;
    if (flat.userType() == qMetaTypeId<QJSValue>()) flat = flat.value<QJSValue>().toVariant();
    const QVariantList list = flat.toList();

    VerbOutcome out;
    if (!mDispatcher) {
        out.threw = true;
        out.error = QStringLiteral("the scripting bridge has no dispatcher");
    } else if (QThread::currentThread() == mDispatcher->thread()) {
        // Same thread: no hop to make. This is not a fallback for the app (the
        // worker is always a different thread) — it is what keeps the bridge
        // correct if anything ever drives it inline.
        out = mDispatcher->dispatch(module, verb, list);
    } else {
        // THE HOP. Blocking, so the verbs still execute in the script's order
        // and the script still reads its own writes; the UI thread is free for
        // everything that happens between two of them.
        QMetaObject::invokeMethod(mDispatcher,
                                  [this, &out, &module, &verb, &list]() {
                                      out = mDispatcher->dispatch(module, verb, list);
                                  },
                                  Qt::BlockingQueuedConnection);
    }

    if (out.threw) {
        if (mJs) mJs->throwError(out.error);
        return QVariant();
    }
    return out.value;
}

// ---------------------------------------------------------------------------
// ScriptWorker
// ---------------------------------------------------------------------------

ScriptWorker::ScriptWorker(VerbDispatcher *dispatcher)
    : QObject(nullptr), mDispatcher(dispatcher)
{
}

ScriptWorker::~ScriptWorker() = default;

void ScriptWorker::setup(const QVariantList &moduleSpecs, const QString &apiVersion)
{
    if (mJs) return;
    mJs = new QJSEngine(this);

    mBridge = new ScriptBridge(mDispatcher, this);
    mBridge->setEngine(mJs);
    mJs->globalObject().setProperty(QStringLiteral("__bridge"), mJs->newQObject(mBridge));

    auto *console = new ConsoleBridge(this);
    mJs->globalObject().setProperty(QStringLiteral("__console_bridge"), mJs->newQObject(console));

    QJSValue installer = mJs->evaluate(QString::fromLatin1(kInstallShim));
    for (const QVariant &entry : moduleSpecs) {
        const QVariantMap spec = entry.toMap();
        const QStringList verbs = spec.value(QStringLiteral("verbs")).toStringList();
        QJSValue names = mJs->newArray(uint(verbs.size()));
        for (int i = 0; i < verbs.size(); ++i) names.setProperty(uint(i), verbs.at(i));
        installer.call({ mJs->globalObject(), mJs->globalObject().property(QStringLiteral("__bridge")),
                         spec.value(QStringLiteral("name")).toString(), names });
    }

    // `api` is the registry's metadata, dispatched like any other module except
    // for `version`, which is a constant and does not need a hop to read.
    QJSValue apiObject = mJs->globalObject().property(QString::fromLatin1(kApiModuleName));
    if (apiObject.isObject()) apiObject.setProperty(QStringLiteral("version"), apiVersion);

    mJs->evaluate(QString::fromLatin1(kBootstrap), QStringLiteral("<bootstrap>"));
}

void ScriptWorker::runScript(const QString &source, const QString &fileName)
{
    mResult = ScriptResult();
    mResult.fileName = fileName;

    if (!mJs) {
        mResult.error = QStringLiteral("the script engine is not ready");
        emit finished();
        return;
    }

    QStringList stackTrace;
    const QJSValue value = mJs->evaluate(source, fileName, 1, &stackTrace);

    if (value.isError() || !stackTrace.isEmpty()) {
        mResult.ok = false;
        if (value.isError()) {
            const QString name = value.property(QStringLiteral("name")).toString();
            const QString message = value.property(QStringLiteral("message")).toString();
            mResult.error = name.isEmpty() ? message : name + QStringLiteral(": ") + message;
            mResult.line = value.property(QStringLiteral("lineNumber")).toInt();
            // Errors thrown in a different file (or the bootstrap) carry their own name.
            const QString errFile = value.property(QStringLiteral("fileName")).toString();
            if (!errFile.isEmpty()) mResult.fileName = errFile;
        } else {
            mResult.error = QStringLiteral("uncaught exception");
        }
        mResult.stack = stackTrace.join(QStringLiteral("\n"));
        if (mResult.line <= 0 && !stackTrace.isEmpty()) {
            // "func:file.js:12" — recover the line from the top stack frame.
            const QString top = stackTrace.first();
            const int colon = top.lastIndexOf(':');
            if (colon > 0) mResult.line = top.mid(colon + 1).toInt();
        }
    } else {
        mResult.ok = true;
        mResult.value = value.toVariant();
    }
    emit finished();
}

void ScriptWorker::teardown()
{
    delete mJs;     // a QJSEngine must be destroyed on the thread that made it
    mJs = nullptr;
    mBridge = nullptr;
}

#include "scriptworker.moc"
