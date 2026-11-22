/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "scripting/modules/previewsapi.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <functional>
#include <memory>

#include "data/database/database.h"
#include "data/project.h"
#include "scripting/modules/moduleshared.h"
#include "services/previewservice.h"
#include "services/previewwriter.h"
#include "services/services.h"

using namespace scriptmod;

namespace {

// THE VERBS' WAIT FOR A LANDED PREVIEW (PREVIEWS-1, the lead's A1 and G2) - in these synchronous
// verbs' door ONLY: PreviewService::request never waits (PICTURE-REQUEST-1 stage 3 replaces this by
// a ticket). The run's database batch is stood down WHEN THE VERB STARTS (the commit a top-level
// DbTransaction makes; it begins again at its next write), or every write would wait behind it.
// The event loop turns with user input held back (assetshare.cpp's import wait). BOUNDED: the wait
// ends when every write has landed, or when none has landed for kVerbWaitMs - one write ahead of it
// under its budget, then its own - with a named failure.
constexpr int kVerbWaitMs = 2 * previews::Writer::kBudgetMs;

struct PreviewWait
{
    int requested = 0;
    int completed = 0;
    QEventLoop *loop = nullptr;
};

/// Waits for `wait->completed` to reach `wait->requested`; false when the bound passed first.
bool waitForPreviews(const std::shared_ptr<PreviewWait> &wait)
{
    if (wait->completed >= wait->requested) return true;
    QEventLoop loop;
    QTimer stall;
    stall.setSingleShot(true);
    bool stalled = false;
    QObject::connect(&stall, &QTimer::timeout, &loop, [&] { stalled = true; loop.quit(); });
    wait->loop = &loop;
    while (wait->completed < wait->requested && !stalled) {
        stall.start(kVerbWaitMs);   // restarted at every landing: no landing for kVerbWaitMs = stalled
        loop.exec(QEventLoop::ExcludeUserInputEvents);
    }
    wait->loop = nullptr;
    return wait->completed >= wait->requested;
}

/// THE WAIT FOR THE WRITER ITSELF: until `enough()` holds, waking at EVERY write the writer completes
/// (whoever asked: an import's store as much as this verb's own requests - PreviewService's landing
/// notice), bounded by kVerbWaitMs WITHOUT A LANDING. True when `enough()` held.
bool waitForWriter(PreviewService *previews, const std::function<bool()> &enough)
{
    if (enough()) return true;
    QEventLoop loop;
    QTimer stall;
    stall.setSingleShot(true);
    bool stalled = false;
    QObject::connect(&stall, &QTimer::timeout, &loop, [&] { stalled = true; loop.quit(); });
    const PreviewService::LandingFn previous = previews->setLandingListener([&loop] { loop.quit(); });
    while (!enough() && !stalled) {
        stall.start(kVerbWaitMs);   // restarted at every landing: no landing for kVerbWaitMs = stalled
        loop.exec(QEventLoop::ExcludeUserInputEvents);
    }
    previews->setLandingListener(previous);
    return enough();
}

/// The completion a waited request reports to.
void notePreview(const std::shared_ptr<PreviewWait> &wait)
{
    ++wait->completed;
    if (wait->loop) wait->loop->quit();
}

QString notLanded(int count)
{
    return QStringLiteral("%1 preview(s) were drawn but their write did not land within %2 ms (the library "
                          "stayed locked)").arg(count).arg(kVerbWaitMs);
}

}   // namespace

QVector<VerbInfo> PreviewsApi::verbs() const
{
    return {
        { "state", "previews.state({scope, type}) -> [{guid, type, state, key, storedKey}]",
          "Every library row's stored preview against what it depicts, by keys - no render, no decode, one "
          "catalog statement. `scope` 'library' (default: every row) or 'project' (the open project's pinned "
          "rows); `type` one assets.list type name. `state`: 'ok' (stored, drawn from the key it has now), "
          "'stale' (stored from another key, or with none), 'missing' (no picture), 'none' (nothing to draw: a "
          "type with no preview, a row with no definition, platform furniture). `key` is what the preview "
          "depicts now (the row's content oids, the open project's pin of it, its members' oids, the preset's "
          "version and the studio's for a studio-lit preset - never its name); `storedKey` what the stored one "
          "was drawn from. A Mesh row draws its bake (no bake: 'none'); Sky rows read 'none' until their preset arrives "
          "(PREVIEWS-1 stage 4). A "
          "preview a door "
          "asked for (an import's picture included) lands through the one writer: until previews.settle, "
          "previews.render or the run's end lands it, its row reads 'missing' or 'stale'.",
          Needs::Document },
        { "counts", "previews.counts({scope}) -> {ok, missing, stale, none}",
          "How many rows' previews are in each state - previews.state's rows tallied, from the SAME one "
          "catalog read (no render, no decode, no statement per row). `scope` 'library' (default) or "
          "'project' (the open project's pinned rows). `missing` + `stale` is what previews.renderAll "
          "draws (\"N previews to refresh\").",
          Needs::Document },
        { "render", "previews.render(guid) -> {ok, reason}",
          "Draws one row's preview through the preview service and waits for it to be STORED with its key. "
          "Textures, video frames, pose strips, photometric lobes and icons are drawn on the CPU (headless too); "
          "materials, models, meshes (a Mesh row's bake in one neutral material), particle systems and avatars on the engine (with none: {ok:false, reason}). A "
          "failure changes NOTHING stored and says why; so does a write that does not land within the bound "
          "(the library stayed locked). Throws for a guid that names no asset.",
          Needs::Document },
        { "settle", "previews.settle() -> {ok, reason}",
          "Waits until the preview writer has NOTHING PENDING - every preview a door asked for (an import's "
          "pictures, a mint, a request) stored with its key or failed - so a script reads previews.state after "
          "it as the library holds it. The run's database batch is stood down when the verb starts (a write "
          "never waits behind it). BOUNDED: when no write lands for 2 x the writer's budget (4000 ms) it "
          "answers {ok:false, reason} naming how many are still pending; nothing is cancelled.",
          Needs::Document },
        { "coverage", "previews.coverage(guid) -> {ok, reason, coverage, controlCoverage, width, height}",
          "A MEASURE of the row's STORED preview: its SUBJECT coverage - the fraction of its pixels that differ "
          "(any channel, by any code) from THE EMPTY STUDIO rendered at the same view and size (the material "
          "view, or the asset view of a model, particle system, avatar or mesh: both aim a fixed way at a sky "
          "at infinity, so one empty picture serves every tile of its view; rendered once and kept). "
          "`controlCoverage` compares a FRESH empty studio with the kept one: 0 when the studio is "
          "reproducible. Needs the engine; a CPU preset (a texture's own pixels) has no studio and is "
          "refused by name, so is a row with no stored picture. Throws for a guid that names no asset.",
          Needs::Document },
        { "renderAll", "previews.renderAll({staleOnly, projectOnly, limit}) -> {considered, rendered, skipped, cancelled, peakQueued, failed: [{guid, reason}]}",
          "Draws many previews, one row a turn with the window painting between them, and waits for them to be "
          "stored. `staleOnly` (default true) takes the 'missing' and 'stale' rows of previews.state; false "
          "redraws every drawable row. `projectOnly` (default false): the open project's pinned rows. `limit` "
          "(default 0 = no limit) caps the requests. `skipped` counts rows with nothing to draw; `cancelled` is "
          "true when a stop arrived (the app quitting, the script stopped) - what was stored stays stored. "
          "`peakQueued` is the most previews waiting in the writer at once: never above its cap (8); at the cap "
          "the sweep waits for a landing.",
          Needs::Document },
    };
}

QVariantList PreviewsApi::state(const QVariantMap &options)
{
    QVariantList out;
    PreviewService *previews = host.services ? host.services->previews : nullptr;
    if (!previews) { fail("previews: there is no preview service in this session"); return out; }
    const QString refusal = refuseUnknownKeys(QStringLiteral("previews.state"), options,
                                              { QStringLiteral("scope"), QStringLiteral("type") });
    if (!refusal.isEmpty()) { fail(refusal); return out; }
    const QString scope = options.value(QStringLiteral("scope"), QStringLiteral("library")).toString();
    if (scope != QLatin1String("library") && scope != QLatin1String("project")) {
        fail(QStringLiteral("previews.state: unknown scope '%1' (library | project)").arg(scope));
        return out;
    }
    const bool projectOnly = scope == QLatin1String("project");
    if (projectOnly && !host.isProjectOpen()) {
        fail("previews.state: scope 'project' needs an open project");
        return out;
    }
    int type = -1;
    if (options.contains(QStringLiteral("type"))) {
        type = assetTypeFromName(options.value(QStringLiteral("type")).toString());
        if (type < 0) {
            fail(QStringLiteral("previews.state: unknown type '%1'").arg(options.value(QStringLiteral("type")).toString()));
            return out;
        }
    }
    for (const PreviewService::Row &row : previews->state(host.project, projectOnly, type)) {
        out.append(QVariantMap{ { "guid", row.guid },
                                { "type", assetTypeName(row.type) },
                                { "state", QString::fromLatin1(PreviewService::stateName(row.state)) },
                                { "key", row.key },
                                { "storedKey", row.storedKey } });
    }
    return out;
}

QVariantMap PreviewsApi::counts(const QVariantMap &options)
{
    QVariantMap out;
    PreviewService *previews = host.services ? host.services->previews : nullptr;
    if (!previews) { fail("previews: there is no preview service in this session"); return out; }
    const QString refusal = refuseUnknownKeys(QStringLiteral("previews.counts"), options, { QStringLiteral("scope") });
    if (!refusal.isEmpty()) { fail(refusal); return out; }
    const QString scope = options.value(QStringLiteral("scope"), QStringLiteral("library")).toString();
    if (scope != QLatin1String("library") && scope != QLatin1String("project")) {
        fail(QStringLiteral("previews.counts: unknown scope '%1' (library | project)").arg(scope));
        return out;
    }
    const bool projectOnly = scope == QLatin1String("project");
    if (projectOnly && !host.isProjectOpen()) {
        fail("previews.counts: scope 'project' needs an open project");
        return out;
    }
    const PreviewService::Counts c = previews->counts(host.project, projectOnly);
    out["ok"] = c.ok;
    out["missing"] = c.missing;
    out["stale"] = c.stale;
    out["none"] = c.none;
    return out;
}

QVariantMap PreviewsApi::render(const QString &guid)
{
    QVariantMap out;
    out["ok"] = false;
    PreviewService *previews = host.services ? host.services->previews : nullptr;
    if (!host.db || !previews) { fail("previews: there is no preview service in this session"); return out; }
    // MISUSE THROWS (SCRIPTING_SPEC §1.6.1): a guid that names no asset is a precondition failure;
    // why a REAL row's preview could not be made is the answer.
    if (host.db->fetchAsset(guid).guid.isEmpty()) {
        fail(QStringLiteral("previews.render: no asset with guid '%1'").arg(guid));
        return out;
    }
    host.db->commitBatchNow();   // the run's batch stands down before the request: the write never waits behind it
    auto wait = std::make_shared<PreviewWait>();
    wait->requested = 1;
    auto outcome = std::make_shared<PreviewService::Outcome>();
    previews->request(host.project, guid, [wait, outcome](const PreviewService::Outcome &o) {
        *outcome = o;
        notePreview(wait);
    });
    if (!waitForPreviews(wait)) {
        outcome->ok = false;
        outcome->reason = notLanded(1);
    }
    out["ok"] = outcome->ok;
    if (!outcome->ok) {
        out["reason"] = outcome->reason;
        // A REFUSAL, not a throw: the answer IS the return value (app.lastError() has it too).
        refuse(QStringLiteral("previews.render: %1").arg(outcome->reason));
    }
    return out;
}

QVariantMap PreviewsApi::renderAll(const QVariantMap &options)
{
    QVariantMap out;
    out["considered"] = 0;
    out["rendered"] = 0;
    out["skipped"] = 0;
    out["cancelled"] = false;
    out["failed"] = QVariantList();
    PreviewService *previews = host.services ? host.services->previews : nullptr;
    if (!previews) { fail("previews: there is no preview service in this session"); return out; }
    const QString refusal = refuseUnknownKeys(QStringLiteral("previews.renderAll"), options,
                                              { QStringLiteral("staleOnly"), QStringLiteral("projectOnly"),
                                                QStringLiteral("limit") });
    if (!refusal.isEmpty()) { fail(refusal); return out; }

    PreviewService::SweepOptions sweep;
    if (options.contains("staleOnly")) sweep.staleOnly = options.value("staleOnly").toBool();
    if (options.contains("projectOnly")) sweep.projectOnly = options.value("projectOnly").toBool();
    if (options.contains("limit")) sweep.limit = options.value("limit").toInt();
    if (host.db) host.db->commitBatchNow();   // stood down BEFORE the sweep: its writes land as it goes

    // THE YIELD (THUMBS-1 item 4): one render a row, and a library can hold hundreds. USER INPUT IS
    // EXCLUDED - a click that re-entered the sweep through a menu would be a second sweep over the
    // same rows. AND IT IS STOPPABLE: a yield can deliver the window's close, a script can be
    // stopped; ScriptEngine::stop and the shutdown set the service's stop, checked a row.
    auto wait = std::make_shared<PreviewWait>();
    // AT THE CAP: until the writer is below it again, whoever's writes are ahead (an import's stores
    // land in the same queue as this sweep's requests).
    sweep.waitForLanding = [previews] {
        return waitForWriter(previews, [previews] { return previews->pendingWrites() < previews::Writer::kQueueCap; });
    };
    auto rendered = std::make_shared<int>(0);
    auto failures = std::make_shared<QVariantList>();
    const auto result = previews->sweep(
        host.project, sweep,
        [wait, rendered, failures](const QString &guid, const PreviewService::Outcome &o) {
            if (o.ok) ++*rendered;
            else if (!o.nothingToDraw && !o.superseded) failures->append(QVariantMap{ { "guid", guid }, { "reason", o.reason } });
            notePreview(wait);
        },
        [] { QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents); });
    wait->requested = result.requested;
    if (result.stalled)
        failures->append(QVariantMap{ { "guid", QString() },
                                      { "reason", QStringLiteral("the preview writer stayed at its cap of %1 for %2 ms: "
                                                                 "the sweep stopped").arg(previews::Writer::kQueueCap)
                                                      .arg(kVerbWaitMs) } });
    if (!result.cancelled && !waitForPreviews(wait))
        failures->append(QVariantMap{ { "guid", QString() },
                                      { "reason", notLanded(wait->requested - wait->completed) } });

    out["considered"] = result.considered;
    out["rendered"] = *rendered;
    out["skipped"] = result.skipped;
    out["cancelled"] = result.cancelled;
    out["peakQueued"] = result.peakPending;
    out["failed"] = *failures;
    return out;
}

QVariantMap PreviewsApi::settle()
{
    QVariantMap out;
    out["ok"] = false;
    PreviewService *previews = host.services ? host.services->previews : nullptr;
    if (!previews) { fail("previews: there is no preview service in this session"); return out; }
    if (host.db) host.db->commitBatchNow();   // the run's batch stands down: no write waits behind it
    if (!waitForWriter(previews, [previews] { return previews->pendingWrites() == 0; })) {
        const QString reason = QStringLiteral("%1 preview write(s) still pending: none landed within %2 ms (the "
                                              "library stayed locked)").arg(previews->pendingWrites()).arg(kVerbWaitMs);
        out["reason"] = reason;
        refuse(QStringLiteral("previews.settle: %1").arg(reason));
        return out;
    }
    out["ok"] = true;
    return out;
}

namespace {
/// The fraction of `tile`'s pixels whose colour differs (any channel, by any code) from `control`'s.
double differingFraction(const QImage &tile, const QImage &control)
{
    const QImage a = tile.convertToFormat(QImage::Format_RGB32);
    const QImage b = control.convertToFormat(QImage::Format_RGB32);
    qint64 differ = 0;
    for (int y = 0; y < a.height(); ++y) {
        const QRgb *pa = reinterpret_cast<const QRgb *>(a.constScanLine(y));
        const QRgb *pb = reinterpret_cast<const QRgb *>(b.constScanLine(y));
        for (int x = 0; x < a.width(); ++x)
            if (qRed(pa[x]) != qRed(pb[x]) || qGreen(pa[x]) != qGreen(pb[x]) || qBlue(pa[x]) != qBlue(pb[x]))
                ++differ;
    }
    return double(differ) / double(qMax<qint64>(1, qint64(a.width()) * a.height()));
}
}   // namespace

QVariantMap PreviewsApi::coverage(const QString &guid)
{
    QVariantMap out;
    out["ok"] = false;
    PreviewService *previews = host.services ? host.services->previews : nullptr;
    if (!host.db || !previews) { fail("previews: there is no preview service in this session"); return out; }
    const AssetRecord record = host.db->fetchAsset(guid);
    if (record.guid.isEmpty()) {
        fail(QStringLiteral("previews.coverage: no asset with guid '%1'").arg(guid));
        return out;
    }
    const auto refused = [&](const QString &why) {
        out["reason"] = why;
        refuse(QStringLiteral("previews.coverage: %1").arg(why));
        return out;
    };
    QImage tile;
    if (record.thumbnail.isEmpty() || !tile.loadFromData(record.thumbnail, "PNG"))
        return refused(QStringLiteral("'%1' has no stored picture to measure").arg(record.name));
    QString why;
    const QImage empty = previews->emptyStudio(record.type, tile.size(), false, &why);
    if (empty.isNull()) return refused(why);
    const QImage again = previews->emptyStudio(record.type, tile.size(), true, &why);
    if (again.isNull()) return refused(why);
    out["ok"] = true;
    out["coverage"] = differingFraction(tile, empty);
    out["controlCoverage"] = differingFraction(again, empty);
    out["width"] = tile.width();
    out["height"] = tile.height();
    return out;
}
