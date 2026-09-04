/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#include "services/engineerrorpump.h"

#include <algorithm>

#include "irisgl/core/logger.h"
#include "jahshaka/engine/Engine.h"

EngineErrorPump &EngineErrorPump::instance()
{
    static EngineErrorPump pump;
    return pump;
}

EngineErrorPump::EngineErrorPump()
{
    mClock.start();
    mEntries.reserve(kMaxKeys);
}

void EngineErrorPump::drain(jahshaka::engine::Engine *engine)
{
    ++mDrains;
    if (!engine) return;
    // Returns AND clears (Engine::takeLastError): the reason is consumed here
    // or nowhere. In the overwhelmingly common case this is an empty string.
    const std::string taken = engine->takeLastError();
    if (taken.empty()) return;
    record(QString::fromStdString(taken));
}

std::vector<EngineErrorPump::Entry>::iterator EngineErrorPump::find(const QString &message)
{
    return std::find_if(mEntries.begin(), mEntries.end(),
                        [&message](const Entry &e) { return e.message == message; });
}

bool EngineErrorPump::takeLogBudget(qint64 now)
{
    if (now - mBudgetWindowMs >= mWindowMs) {
        if (mFloodedInWindow) {
            // One line saying what was swallowed, so a flood is never invisible.
            iris::Logger::getSingleton()->warn(
                QStringLiteral("engine: %1 further error(s) in the last %2 ms were not logged "
                               "(flood budget) — app.engineErrors() has them")
                    .arg(mFloodedInWindow).arg(mWindowMs));
        }
        mBudgetWindowMs  = now;
        mLoggedInWindow  = 0;
        mFloodedInWindow = 0;
    }
    if (mLoggedInWindow < kMaxLogsPerWindow) { ++mLoggedInWindow; return true; }
    ++mFloodedInWindow;
    ++mSuppressed;
    return false;
}

void EngineErrorPump::record(const QString &message)
{
    if (message.isEmpty()) return;
    ++mRecorded;

    const qint64 now = mClock.elapsed();
    auto it = find(message);

    if (it == mEntries.end()) {
        // First time: always logged, immediately. A brand new failure is never
        // suppressed by a pending one — that is the whole point of keying on
        // the message.
        if (int(mEntries.size()) >= kMaxKeys) {
            // Least recently SEEN, which with the append-on-touch discipline
            // below is simply the front.
            mEntries.erase(mEntries.begin());
        }
        Entry fresh;
        fresh.message  = message;
        fresh.firstMs  = now;
        fresh.lastMs   = now;
        fresh.loggedMs = now;
        fresh.count    = 1;
        mEntries.push_back(fresh);
        if (takeLogBudget(now))
            iris::Logger::getSingleton()->warn(QStringLiteral("engine: ") + message);
        return;
    }

    Entry entry = *it;
    entry.lastMs = now;
    ++entry.count;

    if (now - entry.loggedMs >= mWindowMs) {
        const quint64 swallowed = entry.suppressed;
        entry.suppressed = 0;
        entry.loggedMs   = now;
        QString line = QStringLiteral("engine: ") + message;
        if (swallowed)
            line += QStringLiteral(" (repeated %1x)").arg(swallowed + 1);
        if (takeLogBudget(now))
            iris::Logger::getSingleton()->warn(line);
    } else {
        ++entry.suppressed;
        ++mSuppressed;
    }

    // Move-to-back keeps the vector ordered least-recently-seen first, so the
    // eviction above is O(1)-ish and never throws away a live message while a
    // dead one lingers.
    mEntries.erase(it);
    mEntries.push_back(entry);
}

QVariantMap EngineErrorPump::report() const
{
    QVariantList entries;
    for (auto it = mEntries.rbegin(); it != mEntries.rend(); ++it) {
        QVariantMap e;
        e["message"]    = it->message;
        e["count"]      = qulonglong(it->count);
        e["suppressed"] = qulonglong(it->suppressed);
        e["firstMs"]    = qlonglong(it->firstMs);
        e["lastMs"]     = qlonglong(it->lastMs);
        entries.append(e);
    }
    QVariantMap out;
    out["drains"]     = qulonglong(mDrains);
    out["recorded"]   = qulonglong(mRecorded);
    out["suppressed"] = qulonglong(mSuppressed);
    out["entries"]    = entries;
    return out;
}

QStringList EngineErrorPump::messages() const
{
    QStringList out;
    for (auto it = mEntries.rbegin(); it != mEntries.rend(); ++it)
        out << it->message;
    return out;
}

void EngineErrorPump::reset()
{
    mEntries.clear();
    mDrains = mRecorded = mSuppressed = 0;
    mBudgetWindowMs = 0;
    mLoggedInWindow = 0;
    mFloodedInWindow = 0;
    mClock.restart();
}
