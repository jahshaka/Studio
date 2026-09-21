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
#include <QSet>
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
#include "services/memberstamp.h"

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
    mStamped = 0;
    mStampMs = 0;
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
        importMapsThenRows();
    }, Qt::QueuedConnection);
}

void MaterialPresetSeeder::importMapsThenRows()
{
    if (mAborted.load() || !mDb) { mRunning.store(false); emit finished(mSeeded); return; }

    // STEP 2, ON THE UI THREAD BUT FREE: which maps does the library not have?
    // One indexed lookup per oid. The store dedups BYTES on its own, but the
    // import pipeline would still mint a second Texture ROW for content that
    // already has one, so this is the filter that keeps the seed from growing
    // duplicates in a library that already imported the same picture.
    QVector<ImportRequest> requests;
    QSet<QString> queued;
    mStampOrigin.clear();
    for (const auto &entry : mPrepared.mapOwners) {
        const QString oid = mPrepared.mapOids.value(entry.first);
        if (oid.isEmpty()) continue;
        if (!ShippedAssets::libraryTextureFor(oid).isEmpty()) continue;
        // ONE PICTURE, ONE ROW — INSIDE THIS BATCH TOO (SEED-STAMP-1,
        // measured). The library test above is asked BEFORE anything is
        // imported, so two shipped files with the same BYTES both answered
        // "not here" and both minted a row: the pipeline dedups objects, not
        // rows. Three of the shipped maps are that case today — the same
        // picture is named by a preset's map slot and by its graph in two
        // spellings of one path ('…/materials/../../shadergraph/wood.jpg'
        // against the cleaned '…/shadergraph/wood.jpg', AssetIOBase::
        // getAbsolutePath does not clean) — and the second row of each pair
        // was named by no definition, so nothing could ever stamp it and it
        // stood in the tray for ever. Keyed by CONTENT because content is
        // what a duplicate row means; `definitionFor` resolves both spellings
        // to the one row by the same key.
        if (queued.contains(oid)) continue;
        queued.insert(oid);
        ImportRequest request;
        request.sourcePath = entry.first;
        request.typeHint = static_cast<int>(ModelTypes::Texture);
        // THE MATERIAL ASKED, not the user (IMPORT-INTENT-1): these maps are
        // arriving INSIDE the shipped presets. A seed may never take a stamp
        // off — and a User-intent import of the same bytes later is exactly
        // the gesture that does.
        request.intent = ImportRequest::Intent::Material;
        requests.append(request);
        // AND WHO IT CAME IN THROUGH, for the stamp this pass now writes
        // itself (below).
        mStampOrigin.insert(entry.first, MaterialPresetAssets::guidFor(entry.second));
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
    // THE MAP IS STAMPED THE MOMENT ITS ROW EXISTS (SEED-STAMP-1; V-2, owner
    // review R10.2: "a preset's map is not one of the user's tiles").
    //
    // The stamp used to be written in ONE place — `definitionFor`, on a row
    // IT minted — and that is exactly why the route a PERSON takes never got
    // one: this pass mints the rows first, so the row pass behind it finds
    // them by content, hears minted=false, and skips the stamp. Twenty
    // presets' maps stood in the tray and on the Assets page as if the user
    // had imported them (measured on the launch route: 31 members, 0
    // stamped). The seed that mints is the seed that must stamp.
    //
    // ONLY THE ROWS THIS BATCH MINTS, which is the other half of V-2: a
    // picture the library already held is the USER'S and a seed may not
    // quietly hide it. That is not an assumption here, it is the batch's own
    // filter — every request above exists BECAUSE no Texture row held those
    // bytes (`libraryTextureFor`), and the pipeline's two by-content answers
    // cannot fire on such a request: re-listing needs an unlisted row with
    // this source oid (that query would have found it) and the member claim
    // is User-intent only.
    //
    // ON THE UI THREAD, where fileFinished is emitted and the database lives:
    // one small UPDATE per map, interleaved with the commits — see the log line
    // below for what it costs.
    connect(mRunner, &ImportBatchRunner::fileFinished, this,
            [this](int, const ImportRequest &request, const ImportResult &result) {
                if (!mDb || !result.ok()) return;
                const QString origin = mStampOrigin.value(request.sourcePath);
                if (origin.isEmpty()) return;
                QElapsedTimer stampTimer;
                stampTimer.start();
                if (memberstamp::stamp(mDb, result.assetGuid, origin)) ++mStamped;
                mStampMs += stampTimer.elapsed();
            });
    connect(mRunner, &ImportBatchRunner::finished, this, [this](bool cancelled) {
        // THE PREPARED COUNT IS PART OF THE LINE (PATHCLEAN-1): the files the
        // worker hashed against the requests this batch actually made. They
        // differed by three until `AssetIOBase::getAbsolutePath` learned to
        // clean — the same picture named '…/materials/../../shadergraph/x.jpg'
        // by a map slot and '…/shadergraph/x.jpg' by the graph hashed twice —
        // and a gap between them is the symptom of that class returning.
        irisLog(QStringLiteral("preset seed: %1 map file(s) prepared, %2 imported through "
                               "the pipeline, %3 stamped as members in %4 ms%5")
                    .arg(mPrepared.mapOwners.size()).arg(mRunner->requests().size())
                    .arg(mStamped).arg(mStampMs)
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
