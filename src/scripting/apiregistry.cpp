/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "scripting/apiregistry.h"

#include <QJsonObject>
#include <QMetaMethod>


const char *ApiRegistry::apiVersion() { return "0.1.0"; }

QString ApiRegistry::needsName(Needs needs)
{
    switch (needs) {
    case Needs::Document: return QStringLiteral("document");
    case Needs::Engine:   return QStringLiteral("engine");
    case Needs::Window:   return QStringLiteral("window");
    }
    return QStringLiteral("unknown");
}

void ApiRegistry::add(ApiModule *module)
{
    if (module) mModules.append(module);
}

ApiModule *ApiRegistry::module(const QString &jsName) const
{
    for (auto *m : mModules)
        if (m->jsName() == jsName) return m;
    return nullptr;
}

void ApiRegistry::noteVerbCall(const QString &qualifiedName)
{
    if (!mTracing) return;
    for (auto &entry : mTrace) {
        if (entry.first == qualifiedName) { ++entry.second; return; }
    }
    // Bounded: a script that calls a thousand DIFFERENT verbs is not a thing,
    // but a runaway that builds names is, and this list goes into a log line.
    if (mTrace.size() < 256) mTrace.append({ qualifiedName, 1 });
}

QStringList ApiRegistry::takeTrace()
{
    QStringList out;
    out.reserve(mTrace.size());
    for (const auto &entry : mTrace)
        out << (entry.second == 1 ? entry.first
                                  : QStringLiteral("%1 x%2").arg(entry.first).arg(entry.second));
    mTrace.clear();
    return out;
}

QStringList ApiRegistry::validate() const
{
    QStringList problems;
    QStringList seenModules;
    for (auto *m : mModules) {
        const QString name = m->jsName();
        if (name.isEmpty()) problems << QStringLiteral("a module has an empty jsName");
        if (seenModules.contains(name))
            problems << QStringLiteral("duplicate module name '%1'").arg(name);
        seenModules << name;

        QStringList seenVerbs;
        const auto verbList = m->verbs();
        if (verbList.isEmpty())
            problems << QStringLiteral("module '%1' registers no verbs").arg(name);
        for (const auto &v : verbList) {
            const QString id = name + "." + v.name;
            if (v.name.isEmpty())
                problems << QStringLiteral("module '%1' has a verb with no name").arg(name);
            if (v.signature.isEmpty())
                problems << QStringLiteral("%1 has no signature").arg(id);
            if (v.doc.isEmpty())
                problems << QStringLiteral("%1 has no doc string").arg(id);
            if (seenVerbs.contains(v.name))
                problems << QStringLiteral("duplicate verb '%1'").arg(id);
            seenVerbs << v.name;

            // NAMES JAVASCRIPT ITSELF OWNS. Modules reach a script as plain JS
            // objects now (the QObject wrappers died with the UI-thread engine,
            // SCRIPTING_LIVE_SPEC), so `destroy` and `objectName` are ordinary
            // names again — the 2026-09-10 trap where texture.destroy returned
            // undefined and destroyed nothing is closed at the cause. What is
            // still not a verb name is `toString`: every JS object inherits one
            // and string coercion would call the verb.
            static const QStringList kShadowed = { QStringLiteral("toString") };
            if (kShadowed.contains(v.name))
                problems << QStringLiteral("%1 collides with JavaScript's own "
                                           "'%2' and would be called by string coercion — "
                                           "rename the verb")
                                .arg(id, v.name);

            // The metadata must describe a method that actually exists on the
            // QObject — the registry is curated, but a typo'd name would make
            // help() advertise a verb scripts cannot call.
            bool found = false;
            const QMetaObject *mo = m->metaObject();
            for (int i = 0; i < mo->methodCount() && !found; ++i)
                if (QString::fromLatin1(mo->method(i).name()) == v.name) found = true;
            if (!found)
                problems << QStringLiteral("%1 is registered but no such invokable method exists").arg(id);
        }
    }
    return problems;
}

QString ApiRegistry::helpText(const QString &topic) const
{
    QString out;
    const bool all = topic.isEmpty();

    if (all) {
        out += QStringLiteral("Jahshaka scripting API v%1 — help(\"module\") or help(\"module.verb\") for detail\n")
                   .arg(apiVersion());
    }

    QString moduleTopic = topic, verbTopic;
    const int dot = topic.indexOf('.');
    if (dot > 0) {
        moduleTopic = topic.left(dot);
        verbTopic = topic.mid(dot + 1);
    }

    bool found = false;
    for (auto *m : mModules) {
        if (!all && m->jsName() != moduleTopic) continue;
        found = true;
        if (all) {
            QStringList names;
            for (const auto &v : m->verbs()) names << v.name;
            out += QStringLiteral("  %1: %2\n").arg(m->jsName(), names.join(QStringLiteral(", ")));
        } else {
            for (const auto &v : m->verbs()) {
                if (!verbTopic.isEmpty() && v.name != verbTopic) continue;
                out += QStringLiteral("%1\n    %2 [%3]\n").arg(v.signature, v.doc, needsName(v.needs));
            }
        }
    }
    if (!found)
        out += QStringLiteral("no module named '%1' — try help() for the full list\n").arg(moduleTopic);
    return out;
}

QString ApiRegistry::verbText(const QString &name) const
{
    const QString wanted = name.trimmed();
    if (wanted.isEmpty()) return QString();

    QString moduleTopic, verbTopic = wanted;
    const int dot = wanted.indexOf('.');
    if (dot > 0) {
        moduleTopic = wanted.left(dot);
        verbTopic = wanted.mid(dot + 1);
    }

    QString out;
    for (auto *m : mModules) {
        if (!moduleTopic.isEmpty() && m->jsName().compare(moduleTopic, Qt::CaseInsensitive) != 0)
            continue;
        for (const auto &v : m->verbs()) {
            if (v.name.compare(verbTopic, Qt::CaseInsensitive) != 0) continue;
            out += QStringLiteral("%1\n    %2 [%3]\n")
                       .arg(v.signature, v.doc, needsName(v.needs));
        }
    }
    return out;
}

QString ApiRegistry::searchText(const QString &needle, int limit) const
{
    const QString q = needle.trimmed();
    if (q.isEmpty()) return QString();

    QStringList rows;
    int matches = 0;
    for (auto *m : mModules) {
        for (const auto &v : m->verbs()) {
            const QString qualified = m->jsName() + QLatin1Char('.') + v.name;
            // Name first, then the doc TEXT: "light" must find world.ambient's
            // prose as well as scene.addLight's name, which is the whole point
            // of a search over a curated registry.
            if (!qualified.contains(q, Qt::CaseInsensitive)
                && !v.signature.contains(q, Qt::CaseInsensitive)
                && !v.doc.contains(q, Qt::CaseInsensitive))
                continue;
            ++matches;
            if (limit > 0 && rows.size() >= limit) continue;
            rows << QStringLiteral("%1\n    %2 [%3]")
                        .arg(v.signature, v.doc, needsName(v.needs));
        }
    }

    if (matches == 0)
        return QStringLiteral("no verb matches '%1' — api_docs with no arguments "
                              "returns the whole reference\n").arg(q);

    QString out = QStringLiteral("%1 verb%2 match '%3'")
                      .arg(matches)
                      .arg(matches == 1 ? QString() : QStringLiteral("s"), q);
    if (rows.size() < matches)
        out += QStringLiteral(" — showing the first %1; narrow the search or pass "
                              "a module to api_docs for the rest").arg(rows.size());
    out += QStringLiteral("\n\n") + rows.join(QStringLiteral("\n")) + QStringLiteral("\n");
    return out;
}

QJsonArray ApiRegistry::schema() const
{
    QJsonArray modulesJson;
    for (auto *m : mModules) {
        QJsonArray verbsJson;
        for (const auto &v : m->verbs()) {
            QJsonObject verb;
            verb["name"] = v.name;
            verb["signature"] = v.signature;
            verb["doc"] = v.doc;
            verb["needs"] = needsName(v.needs);
            verbsJson.append(verb);
        }
        QJsonObject mod;
        mod["module"] = m->jsName();
        mod["verbs"] = verbsJson;
        modulesJson.append(mod);
    }
    return modulesJson;
}

QString ApiRegistry::markdown() const
{
    QString out;
    out += QStringLiteral("# Jahshaka scripting — verb reference\n\n");
    out += QStringLiteral("API version %1. GENERATED from the ApiRegistry (`--dump-api-docs`) — do not edit by hand.\n\n")
               .arg(apiVersion());
    out += QStringLiteral(
        "Every verb is callable from the script console (Editor, bottom dock), from\n"
        "`./Jahshaka --script file.js`, and headless. The **needs** column is the\n"
        "headless matrix: *document* verbs need no VIEWPORT (`--headless`), *engine*\n"
        "verbs need the engine viewport up, *window* verbs are only meaningful with\n"
        "the editor UI. A `--headless` run boots the engine on the NULL render\n"
        "system and needs NO DISPLAY AT ALL (eb4320b4; 35 suites run displayless\n"
        "on this rig). A run WITH a viewport — `--script` without `--headless`, and\n"
        "therefore every *engine* verb — does need a reachable DISPLAY, because Ogre\n"
        "has no Wayland backend and its XCB support object connects at plugin load.\n\n"
        "Each script run is one undo step (Ctrl+Z reverts the whole script) unless\n"
        "wrapped differently with `editor.beginBatch()`/`editor.endBatch()`.\n"
        "Asset/store operations are NOT undoable — asset mutations are permanent.\n\n"
        "THE JAVASCRIPT RUNS ON ITS OWN THREAD and every verb hops to the UI\n"
        "thread, in order, so the editor keeps answering while a script works and\n"
        "the console's Run button becomes Stop. Whether the VIEWPORT keeps drawing\n"
        "between two verbs is the `app.scriptPolicy` setting: live (the console and\n"
        "Claude's run_script, by default — you watch the script build the scene) or\n"
        "off (command-line runs, always: the picture holds still and nothing at all\n"
        "happens between two verbs, which is what a deterministic script needs).\n"
        "Scripts do not NEST: a run started while one is running is refused.\n\n");
    for (auto *m : mModules) {
        out += QStringLiteral("## %1\n\n").arg(m->jsName());
        out += QStringLiteral("| verb | needs | description |\n|---|---|---|\n");
        for (const auto &v : m->verbs()) {
            QString sig = v.signature;
            sig.replace('|', QStringLiteral("\\|"));
            QString doc = v.doc;
            doc.replace('|', QStringLiteral("\\|"));
            out += QStringLiteral("| `%1` | %2 | %3 |\n").arg(sig, needsName(v.needs), doc);
        }
        out += QStringLiteral("\n");
    }
    return out;
}
