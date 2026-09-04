/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "scripting/apiregistry.h"

#include <QJSEngine>
#include <QJsonObject>
#include <QMetaMethod>

namespace {

/// The `api` JS global: version, help, verbs. Defined here so the registry stays
/// a plain class; the object holds a non-owning pointer to its registry.
class ApiInfoObject : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString version READ version CONSTANT)
public:
    explicit ApiInfoObject(ApiRegistry *registry, QObject *parent = nullptr)
        : QObject(parent), mRegistry(registry) {}

    QString version() const { return ApiRegistry::apiVersion(); }

    Q_INVOKABLE QString help(const QString &topic = QString()) const
    {
        return mRegistry->helpText(topic);
    }

    Q_INVOKABLE QJsonArray verbs() const
    {
        return mRegistry->schema();
    }

private:
    ApiRegistry *mRegistry;
};

} // namespace

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

void ApiRegistry::install(QJSEngine &engine)
{
    for (auto *m : mModules)
        engine.globalObject().setProperty(m->jsName(), engine.newQObject(m));

    // `api` — owned by the JS engine via QObject ownership rules? No: parent it
    // to the module list's world by giving it the engine as parent is not
    // possible (QJSEngine is not its parent by default). Parent it to the first
    // module's parent when available so it dies with the ScriptEngine.
    QObject *owner = mModules.isEmpty() ? nullptr : mModules.first()->parent();
    auto *info = new ApiInfoObject(this, owner);
    engine.globalObject().setProperty(QStringLiteral("api"), engine.newQObject(info));
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
        "headless matrix: *document* verbs run with no engine at all (`--headless`),\n"
        "*engine* verbs need the engine viewport up (a reachable DISPLAY is enough —\n"
        "no visible window), *window* verbs are only meaningful with the editor UI.\n\n"
        "Each script run is one undo step (Ctrl+Z reverts the whole script) unless\n"
        "wrapped differently with `editor.beginBatch()`/`editor.endBatch()`.\n"
        "Asset/store operations are NOT undoable — asset mutations are permanent.\n\n");
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

#include "apiregistry.moc"
