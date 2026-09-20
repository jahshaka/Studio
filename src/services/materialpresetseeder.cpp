/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/materialpresetseeder.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QTimer>
#include <algorithm>
#include <QtConcurrent/QtConcurrent>

#include "data/database/database.h"
#include "data/materialpreset.h"
#include "io/materialpresets.h"
#include "services/assetcas.h"
#include "services/assetstorepaths.h"
#include "services/import/importbatchrunner.h"
#include "services/import/importtypes.h"
#include "services/shippedassets.h"
#include "services/jahlog.h"
#include "services/materialpresetassets.h"

MaterialPresetSeeder &MaterialPresetSeeder::instance()
{
    // Leaked on purpose: a queued completion from the worker must always find
    // a live receiver, and a static destructor racing a QtConcurrent thread at
    // exit is the shape of the zombie ImportBatchRunner's header describes.
    static MaterialPresetSeeder *seeder = new MaterialPresetSeeder();
    return *seeder;
}

bool MaterialPresetSeeder::start(Database *db)
{
    if (!db || mRunning.load()) return false;

    // ALREADY SEEDED = NOTHING TO DO, and this is the answer on every launch
    // after the first. `ensureSeeded`'s own idempotence test, asked of the
    // whole set before a thread is spawned or a file is hashed.
    QStringList pending;
    for (const MaterialPreset &preset : MaterialPresets::all()) {
        const QString guid = MaterialPresetAssets::guidFor(preset.name);
        if (guid.isEmpty()) continue;
        if (MaterialPresetAssets::isSeeded(guid, db)) continue;
        pending.append(preset.name);
    }
    if (pending.isEmpty()) return false;

    mDb = db;
    mPending = pending;
    mSeeded = 0;
    mAborted.store(false);
    mRunning.store(true);

    mTimer.start();
    // STEP 1, ON A WORKER: hash every map (a content id is how the library
    // answers "do I already have this picture") and build every tile. Both
    // are CPU on megabyte-sized PNGs, and measured they were 85 % of what the
    // seed used to spend on the thread that draws.
    const QStringList names = pending;
    (void)QtConcurrent::run([this, names]() { hashMapsOnWorker(names); });
    return true;
}

void MaterialPresetSeeder::requestAbort()
{
    mAborted.store(true);
    if (mRunner) mRunner->requestAbort();
}

void MaterialPresetSeeder::finishNow()
{
    if (!mRunning.load()) return;
    requestAbort();
    // The runner's own bounded join, which pumps the queued commits so the
    // batch drains cleanly rather than being left half-committed.
    if (mRunner) mRunner->waitForDone(5000);
    mPending.clear();
    mRunning.store(false);
}

void MaterialPresetSeeder::hashMapsOnWorker(const QStringList &presetNames)
{
    QElapsedTimer timer;
    timer.start();
    // EVERYTHING THE ROW PASS WOULD OTHERWISE DO ON THE UI THREAD: the maps'
    // content ids and the presets' tiles (a one-megabyte icon decoded and
    // scaled, per preset). Pure CPU and file reads — no database, no GUI.
    MaterialPresetAssets::Prepared prepared = MaterialPresetAssets::prepare(presetNames);
    const qint64 ms = timer.elapsed();

    // Back to the thread that owns the database. A plain queued invocation:
    // the worker never waits for the UI thread, so it cannot be the blocked
    // half of a shutdown deadlock (ImportBatchRunner's header names that one).
    QMetaObject::invokeMethod(this, [this, prepared, ms]() {
        mHashMs = ms;
        mPrepared = prepared;
        QVector<QPair<QString, QString>> hashed;
        for (auto it = prepared.mapOids.constBegin(); it != prepared.mapOids.constEnd(); ++it)
            hashed.append({ it.key(), it.value() });
        importMapsThenRows(hashed);
    }, Qt::QueuedConnection);
}

void MaterialPresetSeeder::importMapsThenRows(const QVector<QPair<QString, QString>> &hashed)
{
    if (mAborted.load() || !mDb) { mRunning.store(false); emit finished(mSeeded); return; }

    // STEP 2, ON THE UI THREAD BUT FREE: which maps does the library not have?
    // One indexed lookup per oid. The store dedups BYTES on its own, but the
    // import pipeline would still mint a second Texture ROW for content that
    // already has one, so this is the filter that keeps the seed from growing
    // duplicates in a library that already imported the same picture.
    QVector<ImportRequest> requests;
    for (const auto &entry : hashed) {
        if (!ShippedAssets::libraryTextureFor(entry.second).isEmpty()) continue;
        ImportRequest request;
        request.sourcePath = entry.first;
        request.typeHint = static_cast<int>(ModelTypes::Texture);
        requests.append(request);
    }
    if (requests.isEmpty()) { seedNextRow(); return; }

    // STEP 3: the import pipeline's OWN thread split (ImportBatchRunner) —
    // prepare (convert, hash, the store's byte writes AND the fsync) on a
    // worker, commit (the rows, one rename) on this thread, one file at a
    // time. It is the machinery the Assets page's drops already use; the
    // seed has exactly the same shape, so it uses exactly the same runner
    // instead of a second half-copy of it.
    mRunner = new ImportBatchRunner(mDb, nullptr, this);
    mRunner->setRequests(requests);
    connect(mRunner, &ImportBatchRunner::finished, this, [this](bool cancelled) {
        irisLog(QStringLiteral("preset seed: %1 map(s) imported through the pipeline%2")
                    .arg(mRunner->requests().size())
                    .arg(cancelled ? QStringLiteral(" (cancelled)") : QString()));
        mRunner->deleteLater();
        mRunner = nullptr;
        seedNextRow();
    });
    mRunner->start();
}

void MaterialPresetSeeder::seedNextRow()
{
    if (mAborted.load() || !mDb || mPending.isEmpty()) {
        mRunning.store(false);
        emit finished(mSeeded);
        return;
    }

    // ONE PRESET PER EVENT-LOOP TURN. The bytes are already in the store, so
    // this is hashing plus catalog rows plus a definition publish that
    // hardlinks (MaterialBundle::writeShipped stages inside the store root) —
    // no device wait at all. Spreading it anyway is what keeps the first
    // seconds of a launch free of one long block.
    const QString name = mPending.takeFirst();
    QElapsedTimer turn;
    turn.start();
    QString error;
    if (MaterialPresetAssets::ensureSeeded(name, mDb, &error, &mPrepared).isEmpty())
        irisLog("preset seed: '" + name + "' was not seeded - " + error);
    else
        ++mSeeded;
    mRowMs += turn.elapsed();
    mWorstTurnMs = std::max<qint64>(mWorstTurnMs, turn.elapsed());

    if (mPending.isEmpty())
        irisLog(QStringLiteral("preset seed: %1 bundle(s) seeded — %2 ms of UI thread in the row "
                               "pass, worst turn %3 ms; %4 ms hashing on the worker; %5 ms wall")
                    .arg(mSeeded).arg(mRowMs).arg(mWorstTurnMs).arg(mHashMs)
                    .arg(mTimer.elapsed()));
    QTimer::singleShot(0, this, [this]() { seedNextRow(); });
}
