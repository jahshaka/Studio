/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef MATERIALPRESETSEEDER_H
#define MATERIALPRESETSEEDER_H

// THE SHIPPED PRESETS, SEEDED AT FIRST RUN, WITH NO FSYNC ON THE THREAD THAT
// DRAWS (MATERIAL_BUNDLE_SPEC §8 phase 3: "seeded at first run beside the
// other shipped assets"; CLAUDE.md, FSYNC-2: "an fsync on the UI thread is
// not slow code, it is the whole device's queue — no durable write belongs on
// the thread that draws").
//
// THE DEFECT THIS EXISTS TO PREVENT, measured rather than assumed. Seeding a
// preset imports its maps through the one import pipeline, and an import ends
// in `AssetCas::storeObject`: hardlink when the source and the store share a
// filesystem, otherwise COPY + fsync. On the owner's box the store is ext4 on
// a USB stick and the shipped PNGs are in the app tree, so `::link()` fails
// and every map is copied and fsynced — 220-1,153 ms per file write there
// (FSYNC-2's measurements on that device). Eighteen of the twenty presets
// carry maps, up to three each. Done inside a drop, that is a frozen second
// or more at the exact moment the user is dragging.
//
// THE SPLIT, along the line the store already draws:
//
//   worker   every map's HASH — its content id, and therefore the answer to
//            "does the library already have this picture" — then the import
//            pipeline's own worker half (ImportBatchRunner: convert, the
//            store's byte writes and the FSYNC). No database on that side of
//            the line, which is the rule that makes the split possible.
//   UI       the rows. The import's commit (one rename each) and then the
//            bundles, ONE PRESET PER EVENT-LOOP TURN: by then every map is
//            an existing library row found by content, so the row pass has
//            no import and no device wait left in it — hashing, catalog
//            rows, and a definition publish that hardlinks inside the store
//            (MaterialBundle::writeShipped stages there for exactly this
//            reason).
//
// IT IS NOT ON THE APPLY'S CRITICAL PATH. `SceneEditService::applyMaterial`
// still calls `MaterialPresetAssets::ensureSeeded`, which is synchronous and
// correct on its own — a preset applied before the seeder reached it simply
// seeds itself there, exactly as it did before this class existed. What the
// seeder changes is that by the time a user can drop anything, the bytes are
// already in the store and that call has no device wait left in it. The
// fallback is the OLD behaviour, never a worse one, and never a wrong
// catalog: this is a warm-up, not a dependency.
//
// IDEMPOTENT AND CHEAP ON EVERY LAUNCH AFTER THE FIRST: one query answers
// "are all twenty rows already there with a definition", and a seeded
// library starts no worker at all.

#include <QElapsedTimer>
#include <QObject>
#include <QPair>
#include <QStringList>
#include <QVector>
#include <atomic>

#include "services/materialpresetassets.h"

class Database;
class ImportBatchRunner;

class MaterialPresetSeeder : public QObject
{
    Q_OBJECT

public:
    /// The app's one seeder. Leaked on purpose (the house pattern for a
    /// service a worker may outlive): a queued completion must always have a
    /// live receiver, and this object owns nothing a shutdown needs back.
    static MaterialPresetSeeder &instance();

    /// Start the first-run seed for `db`. Returns false when there is nothing
    /// to do (every preset already seeded) or a run is already in flight.
    /// Call it on the UI thread, once the library is open.
    bool start(Database *db);

    /// True while the worker or the row pass is still going.
    bool isRunning() const { return mRunning.load(); }

    /// Stop as soon as the current file finishes. The worker checks this
    /// between files, so an abort costs at most one copy. Safe from any
    /// thread; the app's shutdown calls it.
    void requestAbort();

    /// STAND DOWN SO THE CALLER CAN SEED (the UI thread, bounded).
    ///
    /// Two importers of the same files is how a content-addressed store still
    /// ends up with TWO ROWS for one picture: each asks "does the library
    /// have these bytes", both hear no, and both import. The pipeline does
    /// not dedup rows — `ShippedAssets` exists because it does not — so the
    /// rule here is that only ONE of them ever runs: anything that seeds
    /// synchronously (the apply that beats the seeder, `materials.seedPresets`,
    /// Customise) calls this first, and by the time it returns the seeder is
    /// idle and will not start again in this session. What it had not reached
    /// the caller now does itself, and the next launch finishes whatever the
    /// abort skipped (`isSeeded` is the test).
    void finishNow();

signals:
    /// Every shipped preset that has a row now (once per run, UI thread).
    void finished(int seeded);

private:
    MaterialPresetSeeder() = default;

    void hashMapsOnWorker(const QStringList &presetNames);
    void importMapsThenRows(const QVector<QPair<QString, QString>> &hashed);
    void seedNextRow();

    Database *mDb = nullptr;
    QStringList mPending;              ///< preset names still to seed (UI pass)
    int mSeeded = 0;
    qint64 mRowMs = 0;         ///< UI-thread cost of the row pass, total
    qint64 mWorstTurnMs = 0;   ///< …and its worst single turn
    qint64 mHashMs = 0;        ///< the worker's hashing pass
    QElapsedTimer mTimer;      ///< wall time of the whole seed
    ImportBatchRunner *mRunner = nullptr;
    MaterialPresetAssets::Prepared mPrepared;   ///< what the worker did ahead
    std::atomic<bool> mRunning{false};
    std::atomic<bool> mAborted{false};
};

#endif // MATERIALPRESETSEEDER_H
