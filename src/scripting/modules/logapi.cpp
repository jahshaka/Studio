/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "scripting/modules/logapi.h"

#include "data/settingsmanager.h"
#include "services/services.h"
#include "services/jahlog.h"

QVector<VerbInfo> LogApi::verbs() const
{
    return {
        { "path", "log.path() -> {session, ogre, dir, latest, enabled}",
          "Where this run's log artifacts are. The first thing a bug report or an agent needs: "
          "'session' is the per-run session log, 'ogre' the engine's own sibling file for the "
          "SAME run, 'dir' the directory both live in (a logs/ subdirectory of the working "
          "directory in a development build, of the app data location in a release build) and "
          "'latest' the symlink that always points at the current session. 'enabled' is false "
          "when the run was started with --no-log or the file could not be opened — routing, "
          "levels and log.tail all still work in that case, nothing just reaches disk.",
          Needs::Document },
        { "level", "log.level(category?, level?, persist?) -> level | {category: level}",
          "Gets or sets logging verbosity. With no arguments it returns the whole table, "
          "including the pseudo-category 'global'. With one argument it returns that category's "
          "level. With two it SETS it, at runtime, immediately — the last and strongest layer of "
          "the precedence chain (compiled default, then jahsettings.ini, then the --log-level "
          "command line, then this). Levels are the Unreal set: off, fatal, error, warning, "
          "display, log, verbose, veryverbose. 'global' is the default for every category nobody "
          "has set explicitly — deliberately NOT a ceiling, so raising or lowering it never "
          "silently disables a category someone tuned on purpose. Pass persist=true to write the "
          "matching log/<category> key into the settings file so tomorrow's launch keeps it; "
          "without it a debugging session cannot accidentally reconfigure the next one. Note that "
          "qDebug() output arrives as 'verbose' under the 'qt' category, so "
          "log.level('qt','verbose') is what turns the tree's existing qDebug tracing into log "
          "records.",
          Needs::Document },
        { "categories", "log.categories() -> [{name, level, defaultLevel, explicit, records}]",
          "Every registered category, in registration order — Unreal's `Log list`. 'level' is "
          "what it is set to now, 'defaultLevel' what the compiled table gave it, 'explicit' "
          "whether something (ini, command line or a verb) has retuned it, and 'records' how many "
          "records it has actually emitted this session. A category with a high level and zero "
          "records is either quiet or misspelled; a category that is not in this list cannot be "
          "set, and asking for one reports the typo into the log instead of failing silently.",
          Needs::Document },
        { "write", "log.write(category, level, message) -> bool",
          "Writes one record into the session log as if the app had emitted it. This is how a "
          "script or an agent ANNOTATES the record — 'about to open the heavy scene', 'the drag "
          "starts here' — which is what turns a flat file into a timeline you can correlate "
          "against. Returns false only if the category name is unknown (which is reported into "
          "the log). A record below its category's level is dropped, exactly like any other, and "
          "still returns true.",
          Needs::Document },
        { "mark", "log.mark(label?) -> markerId",
          "Writes a named MARK record and returns an opaque id for log.since(). The anchor of the "
          "'what happened while I did that' query: mark, do the thing, then read everything since "
          "the mark at whatever severity you care about.",
          Needs::Document },
        { "tail", "log.tail(n=50, {category, minLevel}?) -> [string]",
          "The last n formatted records from the in-memory ring (the last 2,000), newest last. "
          "No file is re-read, so this is safe to call in a loop. 'category' filters to one "
          "category; 'minLevel' keeps records at least that severe ('warning' gives warnings, "
          "errors and fatals). The ring is bounded and drops oldest, so a very chatty session can "
          "have written more than tail can show — log.counts().dropped says how many.",
          Needs::Document },
        { "since", "log.since(markerId, {minLevel}?) -> [string]",
          "Every record written after the given log.mark(), oldest first. Pass "
          "{minLevel:'warning'} for the question this verb exists to answer: what went wrong "
          "while I was doing that. Records that have already fallen out of the ring cannot be "
          "returned; pass 0 for 'everything the ring still holds'.",
          Needs::Document },
        { "counts", "log.counts() -> {byCategory, byLevel, topRepeats, records, dropped}",
          "The session summary, on demand — the same roll-up the log writes at close. "
          "'topRepeats' is the five most repeated warning-and-above messages with their counts, "
          "which is usually where a session's real problem is hiding. 'records' is everything "
          "written this session and 'dropped' how many have aged out of the ring.",
          Needs::Document },
        { "flush", "log.flush() -> bool",
          "Pushes the buffered records to disk now. The log is BUFFERED by design (warnings and "
          "above flush immediately, everything else rides a record count and a one-second idle "
          "timer), so a test or an agent that is about to READ the file has to call this first.",
          Needs::Document },
    };
}

QVariantMap LogApi::path()
{
    return JahLog::paths();
}

QVariant LogApi::level(const QString &category, const QString &levelName, bool persist)
{
    if (category.isEmpty()) {
        QVariantMap table;
        table.insert(QStringLiteral("global"), JahLog::levelName(JahLog::globalLevel()));
        for (JahLog::Category *c : JahLog::categories())
            table.insert(QString::fromLatin1(c->name()), JahLog::levelName(c->level()));
        return table;
    }

    if (levelName.isEmpty()) {
        if (category.compare(QLatin1String("global"), Qt::CaseInsensitive) == 0)
            return JahLog::levelName(JahLog::globalLevel());
        for (JahLog::Category *c : JahLog::categories())
            if (category == QLatin1String(c->name())) return JahLog::levelName(c->level());
        fail(QStringLiteral("log.level: no category '%1' — log.categories() lists them")
                 .arg(category));
        return QVariant();
    }

    JahLog::Level l;
    if (!JahLog::parseLevel(levelName, &l))
        return fail(QStringLiteral("log.level: '%1' is not a level (off, fatal, error, warning, "
                                   "display, log, verbose, veryverbose)").arg(levelName));
    if (!JahLog::setCategoryLevel(category, l))
        return fail(QStringLiteral("log.level: no category '%1' — log.categories() lists them")
                        .arg(category));
    // NOT persisted unless asked: a debugging session must not silently
    // reconfigure tomorrow's launch (spec §3.5).
    if (persist)
        SettingsManager::getDefaultManager()->setValue(QStringLiteral("log/") + category,
                                                       JahLog::levelName(l).toLower());
    return JahLog::levelName(l);
}

QVariantList LogApi::categories()
{
    QVariantList out;
    for (JahLog::Category *c : JahLog::categories()) {
        QVariantMap m;
        m["name"] = QString::fromLatin1(c->name());
        m["level"] = JahLog::levelName(c->level());
        m["defaultLevel"] = JahLog::levelName(c->defaultLevel());
        m["explicit"] = c->isExplicit();
        m["records"] = qulonglong(c->records());
        out.append(m);
    }
    return out;
}

bool LogApi::write(const QString &category, const QString &levelName, const QString &message)
{
    JahLog::Level l = JahLog::Level::Display;
    if (!levelName.isEmpty() && !JahLog::parseLevel(levelName, &l))
        return fail(QStringLiteral("log.write: '%1' is not a level").arg(levelName));

    // Deliberately NOT auto-creating: a typo in a script would otherwise mint a
    // category nobody can find again. Unknown names are reported, as everywhere
    // else in this subsystem.
    for (JahLog::Category *c : JahLog::categories()) {
        if (category == QLatin1String(c->name())) {
            JahLog::write(*c, l, message);
            return true;
        }
    }
    return fail(QStringLiteral("log.write: no category '%1' — log.categories() lists them")
                    .arg(category));
}

QVariant LogApi::mark(const QString &label)
{
    return QVariant::fromValue(qlonglong(
        JahLog::mark(label.isEmpty() ? QStringLiteral("(unnamed)") : label)));
}

QStringList LogApi::tail(int n, const QVariantMap &filter)
{
    JahLog::Level min = JahLog::Level::VeryVerbose;
    if (filter.contains(QStringLiteral("minLevel")))
        JahLog::parseLevel(filter.value(QStringLiteral("minLevel")).toString(), &min);
    return JahLog::tail(n, filter.value(QStringLiteral("category")).toString(), min);
}

QStringList LogApi::since(qint64 marker, const QVariantMap &filter)
{
    JahLog::Level min = JahLog::Level::VeryVerbose;
    if (filter.contains(QStringLiteral("minLevel")))
        JahLog::parseLevel(filter.value(QStringLiteral("minLevel")).toString(), &min);
    return JahLog::since(quint64(qMax<qint64>(0, marker)), min);
}

QVariantMap LogApi::counts()
{
    return JahLog::counts();
}

bool LogApi::flush()
{
    JahLog::flush();
    return true;
}
