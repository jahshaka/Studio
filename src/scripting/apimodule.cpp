/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "scripting/apimodule.h"

ApiModule::ApiModule(ScriptHost &host, QObject *parent)
    : QObject(parent), host(host)
{
}

bool ApiModule::fail(const QString &message) const
{
    host.lastError = message;
    // NO ENGINE WRAPS A MODULE ANY MORE (SCRIPTING_LIVE_SPEC): the JavaScript
    // runs on a worker thread and the modules stay here, reached through the
    // bridge. So a failure is RECORDED and the bridge rethrows it on the worker
    // engine the instant the verb returns — a normal JS exception, at this
    // verb's own call site, catchable exactly as before. Recording rather than
    // throwing also means a verb that calls fail() and keeps going (they all
    // `return fail(...)`) cannot corrupt an engine it does not own.
    host.pendingError = message;
    return false;
}

bool ApiModule::refuse(const QString &message) const
{
    // No throw, on purpose (see the header): the answer IS the return value.
    // Recording it is what keeps a refusal debuggable — app.lastError() reads
    // this back, and the console prints it for an interactive user.
    host.lastError = message;
    return false;
}

QVariant ApiModule::jsNull()
{
    // QMetaType::Nullptr is the one QVariant QJSEngine turns into JS null;
    // a default-constructed QVariant becomes undefined.
    return QVariant::fromValue(nullptr);
}

bool ApiModule::requireProject() const
{
    if (host.isProjectOpen()) return true;
    return fail(QStringLiteral("%1: no project is open — call project.open(guidOrName) or project.create(name) first")
                    .arg(jsName()));
}

bool ApiModule::requireEngine() const
{
    if (host.isEngineReady()) return true;
    return fail(QStringLiteral("%1: this verb renders, and no rendering engine is available "
                               "(a --headless run boots the NULL render system: document verbs "
                               "only). Run without --headless, on a machine with a display.")
                    .arg(jsName()));
}
