/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "scripting/apimodule.h"

#include <QJSEngine>

ApiModule::ApiModule(ScriptHost &host, QObject *parent)
    : QObject(parent), host(host)
{
}

bool ApiModule::fail(const QString &message) const
{
    // qjsEngine() finds the QJSEngine that owns this QObject wrapper; the throw
    // becomes a normal JS exception with this verb's call site in the stack.
    if (QJSEngine *engine = qjsEngine(this))
        engine->throwError(message);
    else
        qWarning("script API error (no JS engine attached): %s", qPrintable(message));
    return false;
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
