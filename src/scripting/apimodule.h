/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef APIMODULE_H
#define APIMODULE_H

// ApiModule — how a feature contributes verbs to the scripting API (SCRIPTING_SPEC §2.1).
//
// One QObject per namespace ("project", "scene", "assets", …), installed into the
// JS context as a global by ApiRegistry. Verbs are Q_INVOKABLE methods taking and
// returning QVariantMap / QString GUIDs — QJSEngine bridges them for free, and the
// v2 MCP mapping stays mechanical.
//
// verbs() is curated metadata, NOT reflection: it is the single source for the
// console's help(), the generated docs page, and the future MCP tool schemas.
// A verb that is not listed there does not exist, anywhere.

#include <QObject>
#include <QString>
#include <QVector>
#include "scripting/scripthost.h"

/// What a verb needs to run (SCRIPTING_SPEC §2.2). Drives the headless matrix:
/// Document verbs run under QT_QPA_PLATFORM=offscreen with no engine at all.
enum class Needs {
    Document,   // pure document/DB work
    Engine,     // the engine must be up (xcb + reachable DISPLAY; null-window OK)
    Window      // meaningful only with the visible editor (space/desktop switching)
};

struct VerbInfo
{
    QString name;        // "addPrimitive"
    QString signature;   // "scene.addPrimitive(name, {position, parent}) -> id"
    QString doc;         // one or two sentences; shown by help() and in the docs
    Needs needs = Needs::Document;
};

class ApiModule : public QObject
{
    Q_OBJECT
public:
    explicit ApiModule(ScriptHost &host, QObject *parent = nullptr);

    /// The JS global this module is installed as ("project", "scene", …).
    virtual QString jsName() const = 0;
    virtual QVector<VerbInfo> verbs() const = 0;

protected:
    /// Precondition guards (SCRIPTING_SPEC §1.6.1): throw a JS error the script
    /// can catch — never crash. Return false when the precondition failed (the
    /// verb must return immediately; its return value is discarded by the throw).
    bool requireProject() const;
    bool requireEngine() const;

    /// Throws `message` as a JS error in the engine this module is installed in.
    /// Always returns false so verbs can `return fail(...)` / `if (!...) return x;`.
    bool fail(const QString &message) const;

    ScriptHost &host;
};

#endif // APIMODULE_H
