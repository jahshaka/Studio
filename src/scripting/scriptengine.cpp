/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "scripting/scriptengine.h"

#include <QFileInfo>
#include <QJSValueIterator>
#include <QJsonDocument>
#include <QUndoStack>

namespace {

/// The host end of `console.log(...)`: the JS bootstrap below stringifies its
/// arguments and calls emitLine(). A QObject so QJSEngine bridges it for free.
class ConsoleBridge : public QObject
{
    Q_OBJECT
public:
    explicit ConsoleBridge(ScriptEngine *owner) : QObject(owner), mOwner(owner) {}
    Q_INVOKABLE void emitLine(const QString &text) { emit mOwner->consoleOutput(text); }
private:
    ScriptEngine *mOwner;
};

} // namespace

QString ScriptResult::toString() const
{
    if (ok) {
        if (value.isValid() && !value.isNull())
            return QString::fromUtf8(QJsonDocument::fromVariant(value).toJson(QJsonDocument::Compact));
        return QString();
    }
    QString where = fileName;
    if (line > 0) where += QStringLiteral(":%1").arg(line);
    return where.isEmpty() ? error : where + QStringLiteral(": ") + error;
}

ScriptEngine::ScriptEngine(ScriptHost &host, QObject *parent)
    : QObject(parent), mHost(host)
{
}

void ScriptEngine::addModule(ApiModule *module)
{
    if (!module) return;
    module->setParent(this);   // C++ ownership: the JS engine must never GC a module
    mRegistry.add(module);
}

void ScriptEngine::installApi()
{
    if (mInstalled) return;
    mInstalled = true;

    mRegistry.install(mJs);

    // console.log and friends: stringify like a browser console (objects as
    // JSON), then hand the line to the host bridge. Replaces the default
    // ConsoleExtension so the console dock and the CLI capture output.
    auto *bridge = new ConsoleBridge(this);
    mJs.globalObject().setProperty(QStringLiteral("__console_bridge"), mJs.newQObject(bridge));
    mJs.evaluate(QStringLiteral(R"JS(
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
    )JS"), QStringLiteral("<bootstrap>"));
}

ScriptResult ScriptEngine::evaluate(const QString &source, const QString &fileName, bool wrapUndoMacro)
{
    installApi();

    ScriptResult result;
    result.fileName = fileName;

    const bool useMacro = wrapUndoMacro && mHost.undoStack != nullptr;
    if (useMacro) {
        mHost.undoStack->beginMacro(QStringLiteral("script: %1").arg(QFileInfo(fileName).fileName()));
        if (mHost.macroOpenChanged) mHost.macroOpenChanged(true);
    }

    QStringList stackTrace;
    QJSValue value = mJs.evaluate(source, fileName, 1, &stackTrace);

    if (useMacro) {
        if (mHost.macroOpenChanged) mHost.macroOpenChanged(false);
        mHost.undoStack->endMacro();
    }

    if (value.isError() || !stackTrace.isEmpty()) {
        result.ok = false;
        if (value.isError()) {
            const QString name = value.property(QStringLiteral("name")).toString();
            const QString message = value.property(QStringLiteral("message")).toString();
            result.error = name.isEmpty() ? message : name + QStringLiteral(": ") + message;
            result.line = value.property(QStringLiteral("lineNumber")).toInt();
            // Errors thrown in a different file (or the bootstrap) carry their own name.
            const QString errFile = value.property(QStringLiteral("fileName")).toString();
            if (!errFile.isEmpty()) result.fileName = errFile;
        } else {
            result.error = QStringLiteral("uncaught exception");
        }
        result.stack = stackTrace.join(QStringLiteral("\n"));
        if (result.line <= 0 && !stackTrace.isEmpty()) {
            // "func:file.js:12" — recover the line from the top stack frame.
            const QString top = stackTrace.first();
            const int colon = top.lastIndexOf(':');
            if (colon > 0) result.line = top.mid(colon + 1).toInt();
        }
    } else {
        result.ok = true;
        result.value = value.toVariant();
    }
    return result;
}

#include "scriptengine.moc"
