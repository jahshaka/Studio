/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/framemonitor.h"

#include <QAbstractEventDispatcher>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScreen>
#include <QTimer>

#include <chrono>

#include "bridge/enginehost.h"
#include "data/settingsmanager.h"
#include "services/jahlog.h"
#include "services/sessionheader.h"
#include "viewport/enginerenderdriver.h"

#include "jahshaka/engine/Engine.h"

using namespace jahshaka::engine;

// ---------------------------------------------------------------------------
// The one branch on the frame path, and the host stage stack
// ---------------------------------------------------------------------------
namespace {

/// THE flag lives in the header as an inline variable (framemonitor.h
/// explains why); this is the name the rest of this TU uses for it.
bool &gActive = framemonitor::detail::gActive;

/// The innermost open host stage, so a nested scope can subtract itself from
/// its parent and the stages of one frame stay EXCLUSIVE (they sum to the
/// frame instead of counting the same milliseconds twice).
framemonitor::Stage *gOpenStage = nullptr;

qint64 steadyNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

// ---- enum names (the bundle is read by humans and by scripts) -------------
const char *bucketName(PassBucket b)
{
    switch (b) {
    case PassBucket::Main:          return "main";
    case PassBucket::Post:          return "post";
    case PassBucket::ShadowView:    return "shadow.view";
    case PassBucket::ShadowReflect: return "shadow.reflect";
    case PassBucket::ShadowProbe:   return "shadow.probe";
    case PassBucket::Planar:        return "planar";
    case PassBucket::ProbeFace:     return "probe.face";
    case PassBucket::Other:         break;
    }
    return "other";
}

const char *cacheName(CacheKind c)
{
    switch (c) {
    case CacheKind::Probe:     return "probe";
    case CacheKind::ShadowMap: return "shadowMap";
    case CacheKind::Gi:        return "gi";
    case CacheKind::Planar:    return "planar";
    case CacheKind::Shader:    return "shader";
    case CacheKind::Texture:   return "texture";
    }
    return "?";
}

const char *reasonName(WorkReason r)
{
    switch (r) {
    case WorkReason::None:        return "none";
    case WorkReason::Build:       return "build";
    case WorkReason::Rebuild:     return "rebuild";
    case WorkReason::Refresh:     return "refresh";
    case WorkReason::Sweep:       return "sweep";
    case WorkReason::Moved:       return "moved";
    case WorkReason::Caster:      return "caster";
    case WorkReason::Light:       return "light";
    case WorkReason::Material:    return "material";
    case WorkReason::Sky:         return "sky";
    case WorkReason::Ambient:     return "ambient";
    case WorkReason::Fog:         return "fog";
    case WorkReason::Mobility:    return "mobility";
    case WorkReason::Added:       return "added";
    case WorkReason::Removed:     return "removed";
    case WorkReason::Bounds:      return "bounds";
    case WorkReason::Resolution:  return "resolution";
    case WorkReason::Camera:      return "camera";
    case WorkReason::Permutation: return "permutation";
    case WorkReason::Request:     return "request";
    }
    return "?";
}

const char *causeName(FrameCause c)
{
    switch (c) {
    case FrameCause::Driver:    return "driver";
    case FrameCause::Scripted:  return "scripted";
    case FrameCause::Offscreen: return "offscreen";
    case FrameCause::WarmUp:    return "warmup";
    case FrameCause::Player:    return "player";
    case FrameCause::Unknown:   break;
    }
    return "unknown";
}

const char *eventName(MonitorEventKind k)
{
    switch (k) {
    case MonitorEventKind::GiRebuild:        return "gi.rebuild";
    case MonitorEventKind::GiRefresh:        return "gi.refresh";
    case MonitorEventKind::ProbeGridBuild:   return "probe.grid";
    case MonitorEventKind::AtlasRebuild:     return "shadow.atlas";
    case MonitorEventKind::WorkspaceRebuild: return "workspace.rebuild";
    case MonitorEventKind::ShaderCompile:    return "shader.compile";
    case MonitorEventKind::TextureLoad:      return "texture.load";
    case MonitorEventKind::VramFlush:        return "vram.flush";
    case MonitorEventKind::DeviceLost:       return "device.lost";
    case MonitorEventKind::ViewDestroyed:    return "view.destroyed";
    case MonitorEventKind::Host:             return "host";
    }
    return "?";
}

const char *giModeName(GiMode m)
{
    switch (m) {
    case GiMode::Off:              return "off";
    case GiMode::InstantRadiosity: return "instant_radiosity";
    case GiMode::Vct:              return "vct";
    case GiMode::VctPccHybrid:     return "vct_pcc_hybrid";
    }
    return "?";
}

const char *giQualityName(GiQuality q)
{
    switch (q) {
    case GiQuality::Low:    return "low";
    case GiQuality::Medium: return "medium";
    case GiQuality::High:   return "high";
    }
    return "?";
}

const char *toggleName(GiToggle t)
{
    switch (t) {
    case GiToggle::Auto: return "auto";
    case GiToggle::Off:  return "off";
    case GiToggle::On:   return "on";
    }
    return "?";
}

const char *lightTypeName(LightType t)
{
    switch (t) {
    case LightType::Directional: return "directional";
    case LightType::Point:       return "point";
    case LightType::Spot:        return "spot";
    case LightType::Area:        return "area";
    }
    return "?";
}

QString qs(const std::string &s) { return QString::fromStdString(s); }

QJsonObject vec3(const Vec3 &v)
{
    return QJsonObject{ { "x", double(v.x) }, { "y", double(v.y) }, { "z", double(v.z) } };
}

/// One JSON line, compact — jsonl is read a line at a time by everything that
/// reads it, and pretty-printing would triple a 20 s capture.
QByteArray line(const QJsonObject &o)
{
    return QJsonDocument(o).toJson(QJsonDocument::Compact) + '\n';
}

/// A directory-name-safe version of a scene or scenario name.
QString slug(const QString &in)
{
    QString out;
    for (const QChar c : in) {
        if (c.isLetterOrNumber())      out += c.toLower();
        else if (!out.endsWith('-'))   out += QLatin1Char('-');
    }
    while (out.endsWith('-')) out.chop(1);
    while (out.startsWith('-')) out.remove(0, 1);
    return out.left(48);
}

/// The default size cap for a whole bundle. A 20 s Epic capture of a heavy
/// scene is a few hundred passes per frame at 60 fps; this is generous enough
/// to hold one and small enough that a session of captures cannot fill a disk.
/// Whatever it cuts is REPORTED (machine.json "truncation"), never silently
/// dropped — a capture that lies about being complete is worse than a short one.
constexpr qint64 kDefaultCapBytes = 192ll * 1024 * 1024;

}   // namespace

namespace framemonitor {

Stage::Stage(const char *name)
{
    if (!gActive) return;
    mName = name;
    mStart = steadyNs();
    mParent = gOpenStage;
    gOpenStage = this;
    mOpen = true;
}

void Stage::end()
{
    if (!mOpen) return;
    mOpen = false;
    gOpenStage = mParent;
    const qint64 elapsed = steadyNs() - mStart;
    // The parent counts our WHOLE span as a child, so its own number stays
    // exclusive; we report ours minus our own children, for the same reason.
    if (mParent) mParent->mChildrenNs += elapsed;
    const double ms = double(elapsed - mChildrenNs) / 1.0e6;
    if (auto engine = FrameMonitor::instance().engine())
        engine->noteHostStage(std::string(mName), float(ms));
}

Stage::~Stage() { end(); }

}   // namespace framemonitor

// ---------------------------------------------------------------------------
// FrameMonitor::Bundle — the capture bundle writer (§4.8)
// ---------------------------------------------------------------------------
//
// STREAMING, not buffering: frames.jsonl, events.jsonl and trace.json are
// written as the records arrive, so a capture's memory cost is flat however
// long it runs and a crash mid-capture leaves readable files behind. The size
// cap is enforced per file, and what it cut is counted and written into
// machine.json.
class FrameMonitor::Bundle
{
public:
    Bundle(const QString &dir, qint64 capBytes)
        : mDir(dir), mCap(capBytes > 0 ? capBytes : kDefaultCapBytes)
    {
        mFrames.setFileName(QDir(dir).filePath(QStringLiteral("frames.jsonl")));
        mEvents.setFileName(QDir(dir).filePath(QStringLiteral("events.jsonl")));
        mTrace.setFileName(QDir(dir).filePath(QStringLiteral("trace.json")));
        mOk = mFrames.open(QIODevice::WriteOnly | QIODevice::Truncate)
              && mEvents.open(QIODevice::WriteOnly | QIODevice::Truncate)
              && mTrace.open(QIODevice::WriteOnly | QIODevice::Truncate);
        if (mOk) {
            // Chrome Trace Event Format, array form — Perfetto and
            // chrome://tracing both open it as is.
            mTrace.write("[\n");
            mTraceBytes = 2;
            traceMeta();
        }
        mWall.start();
        mStartedAt = QDateTime::currentDateTime();
        mOgreLog = JahLog::ogreFilePath();
        mOgreLogStart = mOgreLog.isEmpty() ? -1 : QFileInfo(mOgreLog).size();
    }

    bool ok() const { return mOk; }
    QString dir() const { return mDir; }

    void setPlanned(double seconds) { mPlannedSeconds = seconds; }
    void setRequest(const QString &label, double seconds) { mLabel = label; mRequested = seconds; }

    unsigned long long frames() const { return mFrameCount; }
    unsigned long long events() const { return mEventCount; }

    void writeFrame(const FrameRecord &r);
    void writeEvent(const MonitorEvent &e);
    void writeSnapshot(const EngineSnapshot &s, const QString &file);
    /// The worst per-frame GPU-sample overflow this capture saw (see
    /// FrameMonitor::drainOnce) — reported in machine.json's truncation block,
    /// because incomplete GPU times are a truncation like any other.
    void noteGpuSamplesTruncated(unsigned n) { mGpuSamplesTruncated = qMax(mGpuSamplesTruncated, n); }
    /// What the ENGINE dropped: ring records nobody drained in time, and events
    /// past the event queue's cap. Both are counted on the engine's side of the
    /// boundary and neither is visible in the files — so a bundle that did not
    /// write them could certify itself complete while it had holes in it (the
    /// review's first item, lane MON-P1b). Monotonic counters: the last read
    /// before the monitor goes off is the capture's total.
    void noteEngineDrops(unsigned long long frames, unsigned long long events)
    { mEngineFramesDropped = frames; mEngineEventsDropped = events; }
    /// Everything that closes the bundle: the trace's tail, the ogre.log
    /// window, machine.json (which carries the truncation note).
    void close(bool early);

    qint64 bytes() const;

private:
    void traceMeta();
    void traceFrame(const FrameRecord &r);
    void traceEvent(const MonitorEvent &e);
    /// Appends to `f` unless the cap for it is already spent. Returns false
    /// when the write was refused, which is what the truncation counters count.
    bool append(QFile &f, qint64 &written, qint64 cap, const QByteArray &data);
    void writeOgreLogWindow();
    void writeMachine(bool early);

    QString mDir;
    qint64  mCap;
    QFile   mFrames, mEvents, mTrace;
    qint64  mFrameBytes = 0, mEventBytes = 0, mTraceBytes = 0, mOgreBytes = 0;
    bool    mOk = false;
    bool    mTraceFirst = true;
    /// Where each reconstructed trace track has been laid up to, µs (see
    /// traceFrame: the strips must not overlap on their own thread).
    double  mStageCursorUs = 0.0, mPassCursorUs = 0.0;
    int     mAsyncId = 0;        ///< ids for the events track's async spans
    unsigned long long mFrameCount = 0, mEventCount = 0;
    unsigned long long mFramesCut = 0, mEventsCut = 0, mTraceCut = 0;
    bool    mOgreCut = false;
    unsigned mGpuSamplesTruncated = 0;
    unsigned long long mEngineFramesDropped = 0, mEngineEventsDropped = 0;
    QElapsedTimer mWall;
    QDateTime mStartedAt;
    double  mPlannedSeconds = 0.0, mRequested = 0.0;
    QString mLabel;
    QString mOgreLog;
    qint64  mOgreLogStart = -1;
    QStringList mSnapshots;
    unsigned mWorkspacesAtStart = 0;
};

qint64 FrameMonitor::Bundle::bytes() const
{
    return mFrameBytes + mEventBytes + mTraceBytes + mOgreBytes;
}

bool FrameMonitor::Bundle::append(QFile &f, qint64 &written, qint64 cap, const QByteArray &data)
{
    if (written + data.size() > cap) return false;
    const qint64 before = written;
    const qint64 n = f.write(data);
    if (n == data.size()) { written += n; return true; }
    // A SHORT WRITE (a full disk is the realistic one) MUST NOT LEAVE HALF A
    // LINE BEHIND: a bundle whose last jsonl line does not parse is a bundle
    // every reader has to special-case. Roll the file back to the last complete
    // record and count this one as dropped, which is what the caller does with
    // a false. (Review item, lane MON-P1b: this used to advance `written` by
    // the partial count AND report the record dropped — the worst of both.)
    f.flush();
    f.resize(before);
    f.seek(before);
    written = before;
    return false;
}

void FrameMonitor::Bundle::writeFrame(const FrameRecord &r)
{
    QJsonArray stages;
    for (const FrameStage &s : r.stages)
        stages.append(QJsonObject{ { "name", qs(s.name) }, { "ms", double(s.ms) } });

    QJsonArray passes;
    unsigned passDraws = 0;
    for (const FramePass &p : r.passes) {
        passDraws += p.draws;
        QJsonObject o{
            { "workspace", qs(p.workspace) },
            { "node", qs(p.node) },
            { "pass", qs(p.pass) },
            { "bucket", QLatin1String(bucketName(p.bucket)) },
            { "draws", int(p.draws) },
            { "batches", int(p.batches) },
            { "instances", int(p.instances) },
            { "triangles", double(p.triangles) },
            { "cpuMs", double(p.cpuMs) },
            // A pass that never reported its end (a workspace boundary closed
            // it): its times and counts are UNKNOWN, not zero, and seeing one
            // is itself the finding (Types.h FramePass::orphaned).
            { "orphaned", p.orphaned },
            // NEGATIVE means NOT MEASURED, in both of these, and it is written
            // as the negative number rather than dropped: "no GPU timing" and
            // "0 ms on the GPU" are different facts (Types.h).
            { "shadowMs", double(p.shadowMs) },
            { "gpuMs", double(p.gpuMs) },
        };
        if (p.shadowMapIdx != FramePass::kNoShadowMap) o.insert("shadowMap", int(p.shadowMapIdx));
        passes.append(o);
    }

    QJsonArray work;
    for (const CacheWork &w : r.cacheWork) {
        work.append(QJsonObject{
            { "cache", QLatin1String(cacheName(w.cache)) },
            { "reason", QLatin1String(reasonName(w.reason)) },
            { "id", double(w.id) },
            { "detail", qs(w.detail) },
            { "units", int(w.units) },
            { "ms", double(w.ms) },
        });
    }

    // THE REPLAYED-FRAME MARKER (Types.h, FramePass). A frame that executed
    // passes and whose passes counted NO draws at all. Ogre replays a cached
    // command buffer when a render queue has not changed and only feeds its
    // metrics on the build path, which is the known reason a complete picture
    // can count zero — and lane MON-P1a measured a second, configuration-
    // dependent under-count it deliberately did not guess at. So this flag says
    // exactly one thing, and the bundle's readers must take it that way: THE
    // RENDERER COUNTED NOTHING IN THIS FRAME'S PASSES. It never means "nothing
    // was drawn", and `metricsRecording` below says whether the counters were
    // even live.
    const bool replayed = !r.passes.empty() && r.draws == 0;

    QJsonObject o{
        { "frame", double(r.frame) },
        { "tMs", r.startMs },
        { "totalMs", double(r.totalMs) },
        { "cause", QLatin1String(causeName(r.cause)) },
        { "onscreen", r.onscreen },
        { "replayed", replayed },
        { "scenesUpdated", int(r.scenesUpdated) },
        { "draws", int(r.draws) },
        { "batches", int(r.batches) },
        { "instances", int(r.instances) },
        { "triangles", double(r.triangles) },
        { "passDraws", int(passDraws) },
        { "probeCaptures", int(r.probeCaptures) },
        { "shadowPasses", int(r.shadowPasses) },
        { "shadowPassesReflect", int(r.shadowPassesReflect) },
        { "shadowPassesProbe", int(r.shadowPassesProbe) },
        { "planarRenders", int(r.planarRenders) },
        { "shaderCompiles", int(r.shaderCompiles) },
        // Was the render system COUNTING while this frame rendered? Recording
        // is off in Ogre until something asks for renderStats(), and a frame
        // rendered with it off reports zeros for every geometry counter.
        { "metricsRecording", r.metricsRecording },
        // Passes closed by a workspace boundary rather than by their own end
        // callback: non-zero means THIS FRAME'S PASS TREE IS INCOMPLETE.
        { "orphanedPasses", int(r.orphanedPasses) },
        { "textureWaitMs", double(r.textureWaitMs) },
        { "gpuMs", double(r.gpuMs) },
        { "overheadMs", double(r.overheadMs) },
        { "stages", stages },
        { "passes", passes },
        { "cacheWork", work },
    };
    if (append(mFrames, mFrameBytes, mCap / 2, line(o))) ++mFrameCount;
    else ++mFramesCut;
    traceFrame(r);
}

void FrameMonitor::Bundle::writeEvent(const MonitorEvent &e)
{
    QJsonObject o{
        { "kind", QLatin1String(eventName(e.kind)) },
        { "frame", double(e.frame) },
        { "tMs", e.startMs },
        { "ms", double(e.ms) },
        { "reason", QLatin1String(reasonName(e.reason)) },
        { "label", qs(e.label) },
        { "detail", qs(e.detail) },
        { "value", double(e.value) },
    };
    if (append(mEvents, mEventBytes, mCap / 10, line(o))) ++mEventCount;
    else ++mEventsCut;
    traceEvent(e);
}

void FrameMonitor::Bundle::traceMeta()
{
    const QJsonArray meta{
        QJsonObject{ { "name", "process_name" }, { "ph", "M" }, { "pid", 1 }, { "tid", 0 },
                     { "args", QJsonObject{ { "name", "Jahshaka" } } } },
        QJsonObject{ { "name", "thread_name" }, { "ph", "M" }, { "pid", 1 }, { "tid", 1 },
                     { "args", QJsonObject{ { "name", "frame" } } } },
        QJsonObject{ { "name", "thread_name" }, { "ph", "M" }, { "pid", 1 }, { "tid", 2 },
                     { "args", QJsonObject{ { "name", "stages" } } } },
        QJsonObject{ { "name", "thread_name" }, { "ph", "M" }, { "pid", 1 }, { "tid", 3 },
                     { "args", QJsonObject{ { "name", "passes" } } } },
        QJsonObject{ { "name", "thread_name" }, { "ph", "M" }, { "pid", 1 }, { "tid", 4 },
                     { "args", QJsonObject{ { "name", "events" } } } },
    };
    for (const QJsonValue &v : meta) {
        QByteArray data = (mTraceFirst ? QByteArray() : QByteArray(",\n"))
                          + QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact);
        if (append(mTrace, mTraceBytes, mCap * 35 / 100, data)) mTraceFirst = false;
    }
}

void FrameMonitor::Bundle::traceFrame(const FrameRecord &r)
{
    // A RECONSTRUCTION, and it says so here: the records carry EXCLUSIVE
    // milliseconds per stage and per pass, not timestamps, so the timeline lays
    // them end to end. Durations and order are real; the start time of a stage
    // inside its frame is not, and no analysis should read one as a timestamp.
    //
    // TWO TRACKS, EACH NON-OVERLAPPING. A trace's "X" events on one thread must
    // nest, never overlap, so each strip starts where the previous frame's
    // strip ended if laying it at the frame's own start would collide. The
    // stage strip also begins BEFORE the frame: a frame record's first stages
    // are the GAP since the previous tick, which happened before the frame
    // opened.
    const double frameUs = r.startMs * 1000.0;
    double gapUs = 0.0;
    for (const FrameStage &s : r.stages)
        if (s.name.rfind("gap.", 0) == 0) gapUs += double(s.ms) * 1000.0;
    QByteArray out;
    // ONE GUARD for every entry this function writes: the separator is emitted
    // only when something precedes it. An unconditional ",\n" on any single
    // entry writes `[\n,\n{...}` — invalid JSON — whenever everything before it
    // was refused by the cap (review item, lane MON-P1b).
    auto emitObj = [&](const QJsonObject &o) {
        out += (mTraceFirst && out.isEmpty() ? QByteArray() : QByteArray(",\n"))
               + QJsonDocument(o).toJson(QJsonDocument::Compact);
    };
    auto emitX = [&](int tid, const QString &name, double tsUs, double durUs,
                     const QJsonObject &args) {
        QJsonObject o{ { "name", name }, { "ph", "X" }, { "pid", 1 }, { "tid", tid },
                       { "ts", tsUs }, { "dur", durUs } };
        if (!args.isEmpty()) o.insert("args", args);
        emitObj(o);
    };

    emitX(1, QStringLiteral("frame %1 (%2)").arg(r.frame).arg(QLatin1String(causeName(r.cause))),
          frameUs, double(r.totalMs) * 1000.0,
          QJsonObject{ { "draws", int(r.draws) }, { "onscreen", r.onscreen },
                       { "gpuMs", double(r.gpuMs) },
                       { "probeCaptures", int(r.probeCaptures) } });
    double cursor = qMax(frameUs - gapUs, mStageCursorUs);
    for (const FrameStage &s : r.stages) {
        const double dur = qMax(0.0, double(s.ms) * 1000.0);
        emitX(2, qs(s.name), cursor, dur, QJsonObject{});
        cursor += dur;
    }
    mStageCursorUs = cursor;
    cursor = qMax(frameUs, mPassCursorUs);
    for (const FramePass &p : r.passes) {
        const double dur = qMax(0.0, double(p.cpuMs) * 1000.0);
        emitX(3, QStringLiteral("%1/%2").arg(qs(p.node), qs(p.pass)), cursor, dur,
              QJsonObject{ { "workspace", qs(p.workspace) },
                           { "bucket", QLatin1String(bucketName(p.bucket)) },
                           { "draws", int(p.draws) }, { "gpuMs", double(p.gpuMs) } });
        cursor += dur;
    }
    mPassCursorUs = cursor;
    // Counter tracks — what a timeline is actually read for.
    QJsonObject counters{ { "draws", int(r.draws) },
                          { "probeCaptures", int(r.probeCaptures) },
                          { "shadowPasses", int(r.shadowPasses + r.shadowPassesReflect
                                                 + r.shadowPassesProbe) } };
    emitObj(QJsonObject{ { "name", "counters" }, { "ph", "C" }, { "pid", 1 },
                         { "tid", 0 }, { "ts", frameUs }, { "args", counters } });

    if (append(mTrace, mTraceBytes, mCap * 35 / 100, out)) mTraceFirst = false;
    else ++mTraceCut;
}

void FrameMonitor::Bundle::traceEvent(const MonitorEvent &e)
{
    const QString name = QStringLiteral("%1 %2").arg(QLatin1String(eventName(e.kind)),
                                                     qs(e.label)).trimmed();
    const QJsonObject args{ { "reason", QLatin1String(reasonName(e.reason)) },
                            { "detail", qs(e.detail) },
                            { "value", double(e.value) } };
    const double ts = e.startMs * 1000.0;
    QByteArray data;
    auto emitObj = [&](const QJsonObject &o) {
        data += (mTraceFirst && data.isEmpty() ? QByteArray() : QByteArray(",\n"))
                + QJsonDocument(o).toJson(QJsonDocument::Compact);
    };
    if (e.ms >= 0.0f) {
        // AN ASYNC SPAN (ph b/e), not a complete "X" event, because these
        // OVERLAP by nature: the start toast lives for 1.65 s while GI
        // rebuilds, texture loads and UI gaps happen inside it, and overlapping
        // X events on one track are ill-formed in the Trace Event Format.
        const int id = ++mAsyncId;
        emitObj(QJsonObject{ { "name", name }, { "ph", "b" }, { "cat", "event" },
                             { "id", id }, { "pid", 1 }, { "tid", 4 },
                             { "ts", ts }, { "args", args } });
        emitObj(QJsonObject{ { "name", name }, { "ph", "e" }, { "cat", "event" },
                             { "id", id }, { "pid", 1 }, { "tid", 4 },
                             { "ts", ts + double(e.ms) * 1000.0 } });
    } else {
        emitObj(QJsonObject{ { "name", name }, { "ph", "i" }, { "s", "g" },
                             { "pid", 1 }, { "tid", 4 }, { "ts", ts },
                             { "args", args } });
    }
    if (append(mTrace, mTraceBytes, mCap * 35 / 100, data)) mTraceFirst = false;
    else ++mTraceCut;
}

void FrameMonitor::Bundle::writeSnapshot(const EngineSnapshot &s, const QString &file)
{
    QJsonObject o{
        { "live", s.live },
        { "label", qs(s.label) },
        { "scene", qs(s.scene) },
        { "frame", double(s.frame) },
        { "atMs", s.atMs },
        { "device", QJsonObject{
            { "renderSystem", qs(s.device.renderSystem) },
            { "vendor", qs(s.device.vendor) },
            { "deviceName", qs(s.device.deviceName) },
            { "driverVersion", qs(s.device.driverVersion) },
            { "apiVersion", qs(s.device.apiVersion) } } },
        { "giParams", QJsonObject{
            { "mode", QLatin1String(giModeName(s.giParams.mode)) },
            { "quality", QLatin1String(giQualityName(s.giParams.quality)) },
            { "boundsMin", vec3(s.giParams.boundsMin) },
            { "boundsMax", vec3(s.giParams.boundsMax) },
            { "autoBoundsMax", double(s.giParams.autoBoundsMax) },
            { "irLight", double(s.giParams.irLight) },
            { "numBounces", s.giParams.numBounces },
            { "pccProbes", QJsonArray{ s.giParams.pccProbesX, s.giParams.pccProbesY,
                                       s.giParams.pccProbesZ } },
            { "probeHdr", QLatin1String(toggleName(s.giParams.probeHdr)) },
            { "probeShadows", QLatin1String(toggleName(s.giParams.probeShadows)) },
            { "probeOverlap", double(s.giParams.probeOverlap) },
            { "updateBudget", s.giParams.updateBudget },
            { "rayMarchStepScale", double(s.giParams.rayMarchStepScale) },
            { "ddgi", QLatin1String(toggleName(s.giParams.ddgi)) },
            { "ddgiIntensity", double(s.giParams.ddgiIntensity) },
            { "ddgiAmbient", double(s.giParams.ddgiAmbient) } } },
        { "gi", QJsonObject{
            { "mode", QLatin1String(giModeName(s.gi.mode)) },
            { "probeCount", s.gi.probeCount },
            { "pccBound", s.gi.pccBound },
            { "vctBound", s.gi.vctBound },
            { "boundsMin", vec3(s.gi.boundsMin) },
            { "boundsMax", vec3(s.gi.boundsMax) },
            { "voxelMetres", double(s.gi.voxelMetres) },
            { "probeRegionMin", vec3(s.gi.probeRegionMin) },
            { "probeRegionMax", vec3(s.gi.probeRegionMax) },
            { "probeHdr", s.gi.probeHdr },
            { "probeShadows", s.gi.probeShadows },
            { "probeUpdatesPerFrame", s.gi.probeUpdatesPerFrame },
            { "probesExceedingCell", s.gi.probesExceedingCell },
            { "probesClampedToRegion", s.gi.probesClampedToRegion },
            { "cubemapProbeSlotsPerCell", s.gi.cubemapProbeSlotsPerCell },
            { "reusedLastRefresh", s.gi.reusedLastRefresh },
            { "ifdBound", s.gi.ifdBound },
            { "ifdProbes", s.gi.ifdProbes },
            { "ifdConverged", s.gi.ifdConverged } } },
        { "shaderCache", QJsonObject{
            { "enabled", s.shaderCache.enabled },
            { "dir", qs(s.shaderCache.dir) },
            { "fingerprint", qs(s.shaderCache.fingerprint) },
            { "sizeBytes", double(s.shaderCache.sizeBytes) },
            { "files", int(s.shaderCache.files) },
            { "pipelineCacheLoaded", s.shaderCache.pipelineCacheLoaded },
            { "pipelineCacheReason", qs(s.shaderCache.pipelineCacheReason) },
            { "microcodeEntries", int(s.shaderCache.microcodeEntries) },
            { "compiledThisRun", int(s.shaderCache.compiledThisRun) },
            { "loadedThisRun", int(s.shaderCache.loadedThisRun) } } },
        { "objects", QJsonObject{
            { "views", int(s.objects.views) },
            { "enabledViews", int(s.objects.enabledViews) },
            { "scenes", int(s.objects.scenes) },
            { "updatedScenes", int(s.objects.updatedScenes) },
            { "stagingScenes", int(s.objects.stagingScenes) },
            { "nodes", int(s.objects.nodes) },
            { "meshes", int(s.objects.meshes) },
            { "materials", int(s.objects.materials) },
            { "textures", int(s.objects.textures) },
            { "datablocks", int(s.objects.datablocks) } } },
        { "memory", QJsonObject{
            { "gpuPoolCapacityBytes", double(s.memory.gpuPoolCapacityBytes) },
            { "gpuPoolFreeBytes", double(s.memory.gpuPoolFreeBytes) },
            { "gpuPools", int(s.memory.gpuPools) },
            { "gpuPoolsIncludeTextures", s.memory.gpuPoolsIncludeTextures },
            { "sceneManagers", int(s.memory.sceneManagers) },
            { "simdNodes", int(s.memory.simdNodes) },
            { "simdObjects", int(s.memory.simdObjects) },
            { "residentBytes", double(s.memory.residentBytes) } } },
        { "threading", QJsonObject{
            { "multithreadedShaderCompilation", s.threading.multithreadedShaderCompilation },
            { "shaderThreadingMode", int(s.threading.shaderThreadingMode) },
            { "hlmsThreads", int(s.threading.hlmsThreads) } } },
        { "mobility", QJsonObject{
            { "movableItems", double(s.mobility.movableItems) },
            { "movableLights", double(s.mobility.movableLights) },
            { "movableNodes", double(s.mobility.movableNodes) },
            { "mobilityRebuilds", double(s.mobility.mobilityRebuilds) } } },
        { "render", QJsonObject{
            { "metricsRecording", s.render.metricsRecording },
            { "fps", s.render.fps },
            { "frameMs", s.render.frameMs },
            { "p95Ms", s.render.p95Ms },
            { "p99Ms", s.render.p99Ms },
            { "draws", double(s.render.draws) },
            { "batches", double(s.render.batches) },
            { "triangles", double(s.render.triangles) },
            { "instances", double(s.render.instances) },
            { "forwardPlusLights", int(s.render.forwardPlusLights) },
            { "forwardPlusBudget", int(s.render.forwardPlusBudget) },
            { "forwardPlusOverBudget", int(s.render.forwardPlusOverBudget) },
            { "incompletePsoRequests", int(s.render.incompletePsoRequests) } } },
        { "textures", QJsonObject{
            { "doneStreaming", s.texturesDoneStreaming },
            { "pending", int(s.texturesPending) } } },
    };

    QJsonObject shadow{
        { "live", s.shadow.live },
        { "resolution", int(s.shadow.resolution) },
        { "maps", int(s.shadow.maps) },
        { "pssmSplits", int(s.shadow.pssmSplits) },
        { "focusedMaps", int(s.shadow.focusedMaps) },
        { "lightSlots", int(s.shadow.lightSlots) },
        { "casters", int(s.shadow.casters) },
        { "budget", int(s.shadow.budget) },
        { "requestedBudget", int(s.shadow.requestedBudget) },
        { "atlasWidth", int(s.shadow.atlasWidth) },
        { "atlasHeight", int(s.shadow.atlasHeight) },
        { "atlasBytes", double(s.shadow.atlasBytes) },
        { "reflectAtlasBytes", double(s.shadow.reflectAtlasBytes) },
        { "probeAtlasBytes", double(s.shadow.probeAtlasBytes) },
        { "shadowPassesLastFrame", int(s.shadow.shadowPassesLastFrame) },
        { "cachedMapRendersLastFrame", int(s.shadow.cachedMapRendersLastFrame) },
        { "reflectPassesLastFrame", int(s.shadow.reflectPassesLastFrame) },
        { "probePassesLastFrame", int(s.shadow.probePassesLastFrame) },
        { "cachedInstances", int(s.shadow.cachedInstances) },
        { "uncachedInstances", int(s.shadow.uncachedInstances) },
        { "viewCached", s.shadow.viewCached },
        { "mapsDirtiedLastFrame", int(s.shadow.mapsDirtiedLastFrame) },
    };
    QJsonArray mapped;
    for (const ShadowMapInfo &m : s.shadow.mapped)
        mapped.append(QJsonObject{ { "slot", int(m.slot) }, { "node", double(m.node) },
                                   { "cached", m.isCached }, { "dirty", m.dirty },
                                   { "pssm", m.pssm },
                                   { "passesLastFrame", int(m.passesLastFrame) } });
    shadow.insert("mapped", mapped);
    QJsonArray unmapped;
    for (NodeId n : s.shadow.unmapped) unmapped.append(double(n));
    shadow.insert("unmapped", unmapped);
    o.insert("shadow", shadow);

    QJsonArray datablocks;
    for (const auto &kv : s.hlmsDatablocks)
        datablocks.append(QJsonObject{ { "block", qs(kv.first) }, { "count", int(kv.second) } });
    o.insert("hlmsDatablocks", datablocks);

    QJsonArray workers;
    for (const auto &kv : s.threading.sceneWorkerThreads)
        workers.append(QJsonObject{ { "scene", qs(kv.first) }, { "threads", int(kv.second) } });
    o.insert("sceneWorkerThreads", workers);

    QJsonArray textures;
    for (const TextureMemoryEntry &t : s.textures)
        textures.append(QJsonObject{ { "name", qs(t.name) }, { "resource", qs(t.resource) },
                                     { "width", int(t.width) }, { "height", int(t.height) },
                                     { "slices", int(t.slices) }, { "mipmaps", int(t.mipmaps) },
                                     { "msaa", int(t.msaa) }, { "format", qs(t.format) },
                                     { "bytes", double(t.bytes) },
                                     { "renderTarget", t.renderTarget }, { "uav", t.uav },
                                     { "manual", t.manual }, { "pooled", t.pooled },
                                     { "residency", qs(t.residency) } });
    o.insert("textureMemory", textures);
    // The list is capped (largest first) so a bundle is not dominated by it —
    // and it says how many there really were and how many were dropped, so a
    // reader is never silently handed a partial list.
    o.insert("textureCount", int(s.textureCount));
    o.insert("texturesTruncated", int(s.texturesTruncated));

    QJsonArray lights;
    for (const SnapshotLight &l : s.lights) {
        QJsonObject lo{ { "node", double(l.node) },
                        { "type", QLatin1String(lightTypeName(l.type)) },
                        { "castShadow", l.castShadow }, { "cached", l.cached },
                        { "dirty", l.dirty }, { "range", double(l.range) },
                        { "intensity", double(l.intensity) }, { "position", vec3(l.position) } };
        if (l.shadowSlot != 0xFFFFFFFFu) lo.insert("shadowSlot", int(l.shadowSlot));
        lights.append(lo);
    }
    o.insert("lights", lights);

    QJsonArray probes;
    for (const ProbeInfo &p : s.probes)
        probes.append(QJsonObject{ { "index", int(p.index) }, { "centre", vec3(p.centre) },
                                   { "halfSize", vec3(p.halfSize) },
                                   { "shapeMin", vec3(p.shapeMin) },
                                   { "shapeMax", vec3(p.shapeMax) },
                                   { "dirty", p.dirty }, { "static", p.isStatic },
                                   { "resolution", int(p.resolution) } });
    o.insert("probes", probes);

    // THE COMPOSITOR GRAPH — the one thing in a snapshot nothing else in the
    // app can see, and the line a capture is usually read for (which workspace
    // renders which scene, and which shadow node each scene pass recalculates).
    QJsonArray workspaces;
    for (const CompositorWorkspaceInfo &w : s.workspaces) {
        QJsonArray nodes;
        for (const CompositorNodeInfo &n : w.nodes) {
            QJsonArray passes;
            for (const CompositorPassInfo &p : n.passes) {
                QJsonObject po{ { "type", qs(p.type) }, { "profilingId", qs(p.profilingId) },
                                { "camera", qs(p.camera) }, { "shadowNode", qs(p.shadowNode) },
                                { "numInitialPasses", int(p.numInitialPasses) } };
                if (p.shadowMapIdx != FramePass::kNoShadowMap)
                    po.insert("shadowMap", int(p.shadowMapIdx));
                passes.append(po);
            }
            nodes.append(QJsonObject{ { "name", qs(n.name) }, { "passes", passes } });
        }
        workspaces.append(QJsonObject{ { "name", qs(w.name) }, { "owner", qs(w.owner) },
                                       { "scene", qs(w.scene) }, { "enabled", w.enabled },
                                       { "width", int(w.width) }, { "height", int(w.height) },
                                       { "listeners", int(w.listeners) },
                                       { "nodes", nodes } });
    }
    o.insert("workspaces", workspaces);
    if (mSnapshots.isEmpty()) mWorkspacesAtStart = unsigned(s.workspaces.size());

    QFile f(QDir(mDir).filePath(file));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        // PRETTY here, unlike the jsonl streams: a snapshot is read by a human
        // (and diffed between start and end) far more often than parsed.
        f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
        f.close();
        mSnapshots << file;
    }
}

void FrameMonitor::Bundle::writeOgreLogWindow()
{
    // THE WINDOW, not the file: the bytes the engine's log grew by while the
    // capture ran. Recording the start offset is what makes this exact — a
    // timestamp filter would depend on the log's format, which is Ogre's.
    QFile out(QDir(mDir).filePath(QStringLiteral("ogre.log")));
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    const qint64 cap = mCap / 20;
    if (mOgreLog.isEmpty() || mOgreLogStart < 0) {
        out.write("(no ogre log for this run)\n");
        out.close();
        return;
    }
    QFile in(mOgreLog);
    if (!in.open(QIODevice::ReadOnly)) {
        out.write("(the ogre log could not be read)\n");
        out.close();
        return;
    }
    in.seek(qMin(mOgreLogStart, in.size()));
    const QByteArray window = in.readAll();
    if (window.size() > cap) {
        // KEEP THE TAIL: the end of a capture's log window is where the thing
        // the owner pressed the key for usually is.
        out.write("(truncated: the head of this window was cut to fit the bundle's cap)\n");
        out.write(window.right(cap));
        mOgreBytes = cap;
        mOgreCut = true;
    } else if (window.isEmpty()) {
        // AN EMPTY WINDOW IS AN ANSWER — the engine logged nothing while the
        // capture ran — and a zero-byte file that does not say so reads like a
        // writer that failed.
        out.write("(the engine logged nothing during this capture's window)\n");
    } else {
        out.write(window);
        mOgreBytes = window.size();
    }
    out.close();
}

void FrameMonitor::Bundle::writeMachine(bool early)
{
    QJsonObject machine;
    // THE SESSION HEADER, whole: version, build id, build type, Qt, platform,
    // command line, data root, the log paths — plus every provider group the
    // app registered (the GPU block, the shader cache). One call, and it cannot
    // drift from what the session log says about the same run.
    QJsonObject session;
    for (const SessionHeader::Row &row : SessionHeader::allRows())
        session.insert(row.first, row.second);
    machine.insert("session", session);

    machine.insert("studioCommit", QStringLiteral(GIT_COMMIT_HASH));
    machine.insert("studioCommitDate", QStringLiteral(GIT_COMMIT_DATE));
    machine.insert("irisglCommit", QStringLiteral(IRISGL_COMMIT_HASH));
    machine.insert("ogrePin", QStringLiteral(OGRE_PIN_HASH));
    QJsonArray patches;
    for (const QString &p : QStringLiteral(OGRE_PATCH_STACK).split(QLatin1Char(','),
                                                                  Qt::SkipEmptyParts))
        patches.append(p);
    machine.insert("ogrePatchStack", patches);
#ifdef QT_DEBUG
    machine.insert("buildType", QStringLiteral("Debug"));
#else
    machine.insert("buildType", QStringLiteral("Release"));
#endif
#if defined(__SANITIZE_ADDRESS__)
    machine.insert("asan", true);
#elif defined(__has_feature)
#  if __has_feature(address_sanitizer)
    machine.insert("asan", true);
#  else
    machine.insert("asan", false);
#  endif
#else
    machine.insert("asan", false);
#endif

    // The display the owner was actually looking at, and how the loop was paced.
    QJsonObject display;
    if (auto *gui = qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        if (QScreen *s = gui->primaryScreen()) {
            display.insert("name", s->name());
            display.insert("width", s->geometry().width());
            display.insert("height", s->geometry().height());
            display.insert("refreshHz", s->refreshRate());
            display.insert("devicePixelRatio", s->devicePixelRatio());
        }
        display.insert("screens", gui->screens().size());
    }
    machine.insert("display", display);
    if (EngineRenderDriver *d = EngineHost::instance().driver()) {
        machine.insert("pacing", QJsonObject{
            { "mode", framepacing::modeName(d->pacingMode()) },
            { "intervalMs", d->intervalMs() },
            { "refreshHz", d->refreshHz() },
            { "running", d->isRunning() } });
    }

    QJsonObject capture{
        { "label", mLabel },
        { "requestedSeconds", mRequested },
        { "plannedSeconds", mPlannedSeconds },
        { "actualSeconds", double(mWall.elapsed()) / 1000.0 },
        { "stoppedEarly", early },
        { "framesWritten", double(mFrameCount) },
        { "eventsWritten", double(mEventCount) },
        // THE CAPTURE'S OWN TIMESTAMP IS WHEN IT STARTED, not when it was
        // written: this block is composed at the stop, and reading the clock
        // here dated every bundle by its end (review item, lane MON-P1b).
        { "startedAt", mStartedAt.toString(Qt::ISODate) },
        { "snapshots", QJsonArray::fromStringList(mSnapshots) },
        { "workspacesAtStart", int(mWorkspacesAtStart) },
    };
    machine.insert("capture", capture);

    // THE HONEST CAP (the brief's word): what the limit cut, per file, by name.
    // Zeroes everywhere mean the bundle is complete, and that is a statement
    // the bundle makes about itself rather than one a reader has to infer from
    // file sizes.
    machine.insert("truncation", QJsonObject{
        { "capBytes", double(mCap) },
        { "bytesWritten", double(bytes()) },
        { "frameRecordsDropped", double(mFramesCut) },
        { "eventRecordsDropped", double(mEventsCut) },
        { "traceRecordsDropped", double(mTraceCut) },
        { "ogreLogTruncated", mOgreCut },
        { "gpuSamplesTruncated", int(mGpuSamplesTruncated) },
        // THE ENGINE'S OWN LOSSES, which no file in the bundle could reveal:
        // frame records the ring overwrote before the host drained them, and
        // events past the engine's event-queue cap.
        { "engineFramesDropped", double(mEngineFramesDropped) },
        { "engineEventsDropped", double(mEngineEventsDropped) },
        // COMPLETE MEANS COMPLETE. Every way a record can be lost — the
        // writer's caps, the engine's ring, the engine's event queue, the GPU
        // query pool and the log window — is ANDed in here. A bundle must never
        // claim a completeness it cannot prove (review item, lane MON-P1b).
        { "complete", mFramesCut == 0 && mEventsCut == 0 && mTraceCut == 0 && !mOgreCut
                          && mGpuSamplesTruncated == 0
                          && mEngineFramesDropped == 0 && mEngineEventsDropped == 0 },
        { "note", QStringLiteral(
              "Per-file caps: frames.jsonl 50%, trace.json 35%, events.jsonl 10%, "
              "ogre.log 5% of capBytes. A dropped record is counted here and never "
              "half-written; ogre.log keeps its TAIL when it is cut.") },
    });

    QFile f(QDir(mDir).filePath(QStringLiteral("machine.json")));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(machine).toJson(QJsonDocument::Indented));
        f.close();
    }
}

void FrameMonitor::Bundle::close(bool early)
{
    if (mTrace.isOpen()) {
        mTrace.write("\n]\n");
        mTrace.close();
    }
    mFrames.close();
    mEvents.close();
    writeOgreLogWindow();
    writeMachine(early);
}

// ---------------------------------------------------------------------------
// FrameMonitor — the capture state machine
// ---------------------------------------------------------------------------

FrameMonitor &FrameMonitor::instance()
{
    static FrameMonitor monitor;
    return monitor;
}

FrameMonitor::FrameMonitor() = default;
FrameMonitor::~FrameMonitor() = default;

std::shared_ptr<Engine> FrameMonitor::engine() const
{
    return EngineHost::instance().engine();
}

double FrameMonitor::defaultSeconds() { return 20.0; }

double FrameMonitor::preferredSeconds()
{
    if (auto *settings = SettingsManager::getDefaultManager())
        return qBound(1.0, settings->getValue(QStringLiteral("perf/captureSeconds"),
                                              defaultSeconds()).toDouble(), 600.0);
    return defaultSeconds();
}

void FrameMonitor::setPreferredSeconds(double seconds)
{
    if (auto *settings = SettingsManager::getDefaultManager())
        settings->setValue(QStringLiteral("perf/captureSeconds"),
                           qBound(1.0, seconds, 600.0));
}

QString FrameMonitor::captureRoot()
{
    // REAL DISK, never /tmp (the scratch law, CLAUDE.md): a capture is evidence
    // the lead reads later, and /tmp here is RAM.
    const QByteArray env = qgetenv("JAHSHAKA_PERF_ROOT");
    if (!env.isEmpty()) return QString::fromLocal8Bit(env);
    if (auto *settings = SettingsManager::getDefaultManager()) {
        const QString stored =
            settings->getValue(QStringLiteral("perf/captureRoot"), QString()).toString();
        if (!stored.isEmpty()) return stored;
    }
    return QDir(QDir::homePath()).filePath(QStringLiteral("Developer/spikes/perf"));
}

bool FrameMonitor::start(const Request &request, QString *error)
{
    auto fail = [error](const QString &why) {
        if (error) *error = why;
        return false;
    };
    if (mPhase == Phase::Recording) return fail(QStringLiteral("a capture is already running"));
    if (mPhase == Phase::Writing)   return fail(QStringLiteral("the previous capture is still being written"));
    auto eng = engine();
    if (!eng) return fail(QStringLiteral("no engine is running"));

    const double seconds = request.seconds > 0.0 ? qBound(0.1, request.seconds, 600.0)
                                                 : preferredSeconds();

    // The START snapshot comes FIRST, because it is what names the bundle: the
    // engine knows which scene is on screen and the caller usually does not.
    EngineSnapshot startSnapshot;
    const bool haveSnapshot = eng->captureSnapshot(startSnapshot, "start", nullptr);
    QString label = request.label;
    if (label.isEmpty() && haveSnapshot) label = qs(startSnapshot.scene);
    if (slug(label).isEmpty()) label = QStringLiteral("scene");

    const QDir root(QDir::cleanPath(captureRoot()));
    QString dir = request.outDir;
    if (dir.isEmpty()) {
        const QDateTime now = QDateTime::currentDateTime();
        // MILLISECONDS in the name, because two scripted captures in the same
        // second used to land in the same directory and the second one
        // truncated the first (review item, lane MON-P1b). The uniqueness loop
        // below covers the rest.
        dir = root.filePath(QStringLiteral("%1-%2-%3")
                                .arg(now.toString(QStringLiteral("yyyyMMdd")),
                                     now.toString(QStringLiteral("HHmmsszzz")), slug(label)));
        for (int n = 2; QFile::exists(QDir(dir).filePath(QStringLiteral("machine.json")));
             ++n)
            dir = root.filePath(QStringLiteral("%1-%2-%3-%4")
                                    .arg(now.toString(QStringLiteral("yyyyMMdd")),
                                         now.toString(QStringLiteral("HHmmsszzz")),
                                         slug(label), QString::number(n)));
    } else {
        // A GIVEN PATH IS CONFINED TO THE CAPTURE ROOT. `out` reaches this from
        // a script AND from the MCP tool, and it creates directories and
        // TRUNCATES three files in whatever it is pointed at — a source tree, a
        // project, someone else's bundle (review item, lane MON-P1b). The root
        // itself is configurable (JAHSHAKA_PERF_ROOT, `perf/captureRoot`),
        // which is how a suite writes into its own scratch home.
        dir = QDir::cleanPath(QDir(dir).isAbsolute() ? dir : root.filePath(dir));
        const QString rootPath = root.absolutePath();
        if (!(dir == rootPath || dir.startsWith(rootPath + QLatin1Char('/'))))
            return fail(QStringLiteral("'out' must be inside the capture root (%1): %2")
                            .arg(rootPath, dir));
    }
    // NEVER OVERWRITE A BUNDLE. The three streams open with Truncate, so
    // pointing a capture at an existing bundle destroyed it silently.
    if (QFile::exists(QDir(dir).filePath(QStringLiteral("machine.json"))))
        return fail(QStringLiteral("a capture bundle already exists at %1").arg(dir));
    if (!QDir().mkpath(dir))
        return fail(QStringLiteral("could not create the bundle directory: %1").arg(dir));

    mBundle.reset(new Bundle(dir, request.maxBytes));
    if (!mBundle->ok()) {
        mBundle.reset();
        return fail(QStringLiteral("could not open the bundle's files in %1").arg(dir));
    }
    mBundle->setRequest(label, request.seconds);
    mBundle->setPlanned(seconds);
    if (haveSnapshot) mBundle->writeSnapshot(startSnapshot, QStringLiteral("snapshot_start.json"));

    mPlannedSeconds = seconds;
    mGpuSamplesTruncated = 0;
    mEngineFramesDropped = mEngineEventsDropped = 0;
    mPhase = Phase::Recording;
    // FORWARD ONLY, from this instant: the engine starts recording the frames
    // that come after this call, and there is no history behind it.
    eng->setFrameMonitor(MonitorLevel::Review);
    gActive = true;
    mBlockedNs = 0;
    mBlockedAt = 0;
    mSinceTickEnd.start();
    connectDispatcher();

    if (!mAutoStop) {
        mAutoStop = new QTimer(this);
        mAutoStop->setSingleShot(true);
        mAutoStop->setTimerType(Qt::PreciseTimer);
        connect(mAutoStop, &QTimer::timeout, this, [this] { finish(false); });
    }
    mAutoStop->start(int(seconds * 1000.0));
    if (!mDrainTimer) {
        mDrainTimer = new QTimer(this);
        // A SLOW SAFETY NET, not the drain path: the driver's tick end drains
        // every frame. This one exists so a capture in which the loop stalls
        // (or never ticks — a page with no viewport) still moves its records
        // out of the engine's ring before the ring overwrites them.
        connect(mDrainTimer, &QTimer::timeout, this, [this] { drain(); });
    }
    mDrainTimer->start(250);

    MonitorEvent startedEvent;
    startedEvent.kind = MonitorEventKind::Host;
    startedEvent.reason = WorkReason::Request;
    startedEvent.label = "capture.start";
    startedEvent.detail = dir.toStdString();
    eng->noteMonitorEvent(startedEvent);

    emit started(seconds);
    // THE FIRST OF THE TWO TOASTS, and the only thing drawn for a capture.
    emit toastRequested(tr("Render Monitor"),
                        tr("Recording %1 s…").arg(seconds, 0, 'g', 3), 0);
    JAH_LOG(JahLog::perf, Display,
            QStringLiteral("[perf] capture started: %1 s -> %2").arg(seconds).arg(dir));
    return true;
}

bool FrameMonitor::stop(QString *error)
{
    if (mPhase != Phase::Recording) {
        if (error) *error = QStringLiteral("no capture is running");
        return false;
    }
    finish(true);
    return true;
}

void FrameMonitor::finish(bool early)
{
    if (mPhase != Phase::Recording) return;
    auto eng = engine();
    if (mAutoStop) mAutoStop->stop();
    if (mDrainTimer) mDrainTimer->stop();
    disconnectDispatcher();
    mPhase = Phase::Writing;

    if (eng && mBundle) {
        EngineSnapshot endSnapshot;
        // Taken while the monitor is still ON, so it describes the engine the
        // capture recorded rather than the one left after it stopped.
        if (eng->captureSnapshot(endSnapshot, "end", nullptr))
            mBundle->writeSnapshot(endSnapshot, QStringLiteral("snapshot_end.json"));
    }
    drain();
    gActive = false;
    if (eng) eng->setFrameMonitor(MonitorLevel::Off);
    // THE TAIL. Stopping the monitor FLUSHES the engine's holding queue, and
    // the NEXT drain hands it back with the monitor already off (Engine.h
    // takeFrameRecords). Drain in a LOOP until it is empty — never "render one,
    // expect one" — or the last frames of every capture are lost.
    for (int i = 0; i < 8; ++i) {
        const unsigned moved = drainOnce();
        if (!moved) break;
    }

    QString path;
    if (mBundle) {
        mBundle->close(early);
        path = mBundle->dir();
        mLastBundle = path;
        JAH_LOG(JahLog::perf, Display,
                QStringLiteral("[perf] capture written: %1 (%2 frames, %3 events)")
                    .arg(path).arg(mBundle->frames()).arg(mBundle->events()));
        mLastFrames = double(mBundle->frames());
        mLastEvents = double(mBundle->events());
        mBundle.reset();
    }
    mPhase = Phase::Idle;

    emit stopped(path);
    // THE SECOND TOAST NAMES THE BUNDLE — the owner's whole hand-off ("press
    // the key, tell the lead where it is").
    emit toastRequested(tr("Render Monitor"),
                        path.isEmpty() ? tr("Capture failed")
                                       : tr("Capture written to %1").arg(path), 6000);
}

unsigned FrameMonitor::drainOnce()
{
    auto eng = engine();
    if (!eng || !mBundle) return 0;
    // GPU SAMPLES THE QUERY POOL COULD NOT HOLD (MonitorStatus, P1a's review
    // round). It is a per-frame number, so the capture keeps the worst it ever
    // saw and machine.json states it: a bundle whose GPU times are incomplete
    // has to say so rather than let analysis discover that some passes have no
    // time.
    const MonitorStatus st = eng->monitorStatus();
    if (st.gpuSamplesTruncated > mGpuSamplesTruncated) {
        mGpuSamplesTruncated = st.gpuSamplesTruncated;
        mBundle->noteGpuSamplesTruncated(st.gpuSamplesTruncated);
    }
    // The engine's own losses, sampled on every drain — they are monotonic, and
    // the LAST read while the monitor is still on is the capture's total (the
    // status reads zero once the monitor object is gone).
    if (st.framesDropped || st.eventsDropped) {
        mEngineFramesDropped = st.framesDropped;
        mEngineEventsDropped = st.eventsDropped;
    }
    mBundle->noteEngineDrops(mEngineFramesDropped, mEngineEventsDropped);
    unsigned moved = 0;
    std::vector<FrameRecord> frames;
    moved += eng->takeFrameRecords(frames);
    for (const FrameRecord &r : frames) mBundle->writeFrame(r);
    std::vector<MonitorEvent> events;
    moved += eng->takeMonitorEvents(events);
    for (const MonitorEvent &e : events) mBundle->writeEvent(e);
    return moved;
}

void FrameMonitor::drain()
{
    // Loop while records keep coming: one call can hand back a batch, and the
    // ring is the thing we are racing.
    for (int i = 0; i < 64; ++i)
        if (!drainOnce()) break;
}

void FrameMonitor::noteTickStart(bool willRender)
{
    if (!gActive) return;
    // NOTHING IS SHOWING: the driver is about to skip this tick. Push nothing,
    // and leave the gap clock running so the next rendered frame carries the
    // whole absence as one stage instead of a thousand (see the header).
    if (!willRender) return;
    auto eng = engine();
    if (!eng) return;
    if (mSinceTickEnd.isValid()) {
        const double gapMs = double(mSinceTickEnd.nsecsElapsed()) / 1.0e6;
        // The event loop's OWN split: blocked in the dispatcher = waiting for
        // work (idle), anything else in the gap = the UI thread doing something
        // that is not a frame (a panel rebuild, an offscreen render, a script).
        const double idleMs = qBound(0.0, double(mBlockedNs) / 1.0e6, gapMs);
        const double uiMs = gapMs - idleMs;
        eng->noteHostStage(std::string("gap.idle"), float(idleMs));
        eng->noteHostStage(std::string("gap.ui"), float(uiMs));
        // A LONG UI GAP IS AN EVENT, not only a stage: it is the "15 fps that
        // feels like 15" case the monitor exists to tell apart, and analysis
        // should find it without summing stages.
        if (uiMs >= 16.0) {
            MonitorEvent gap;
            gap.kind = MonitorEventKind::Host;
            gap.label = "gap.ui";
            gap.ms = float(uiMs);
            gap.reason = WorkReason::None;
            eng->noteMonitorEvent(gap);
        }
    }
    mBlockedNs = 0;
    mBlockedAt = 0;
}

void FrameMonitor::noteTickEnd(bool rendered)
{
    if (!gActive) return;
    // A SKIPPED TICK IS NOT THE END OF A FRAME: leave the gap clock where it
    // is, so the time spent on a page with no viewport lands as one stage on
    // the frame that comes back (see the header). There is also nothing to
    // drain — no frame was rendered — but the drain timer still runs.
    if (!rendered) return;
    drain();
    mSinceTickEnd.restart();
    mBlockedNs = 0;
    mBlockedAt = 0;
}

void FrameMonitor::noteToastShown(const QString &title, const QString &text, int holdMs)
{
    mLastToastTitle = title;
    mLastToastText = text;
    mLastToastHoldMs = holdMs;
    ++mToastsShown;
    if (!gActive) return;
    auto eng = engine();
    if (!eng) return;
    // THE START TOAST'S LIFETIME (owner, 2026-09-12): analysis has to know
    // which frames it overlapped, because it is the one thing on screen that
    // the capture itself put there.
    MonitorEvent toast;
    toast.kind = MonitorEventKind::Host;
    toast.label = "toast";
    toast.detail = (title + QStringLiteral(": ") + text).toStdString();
    toast.ms = float(holdMs);
    toast.reason = WorkReason::Request;
    eng->noteMonitorEvent(toast);
}

bool FrameMonitor::mark(const QString &label)
{
    if (mPhase != Phase::Recording) return false;
    auto eng = engine();
    if (!eng) return false;
    MonitorEvent e;
    e.kind = MonitorEventKind::Host;
    e.label = "mark";
    e.detail = label.toStdString();
    e.reason = WorkReason::Request;
    eng->noteMonitorEvent(e);
    return true;
}

void FrameMonitor::connectDispatcher()
{
    if (mDispatcherConnected) return;
    QAbstractEventDispatcher *d = QAbstractEventDispatcher::instance();
    if (!d) return;
    // CONNECTED ONLY WHILE A CAPTURE RUNS (§4.1's structural guarantee): at Off
    // the monitor is not on the event loop's signal list at all.
    mAboutToBlock = connect(d, &QAbstractEventDispatcher::aboutToBlock, this,
                            [this] { mBlockedAt = steadyNs(); });
    mAwake = connect(d, &QAbstractEventDispatcher::awake, this, [this] {
        if (mBlockedAt) mBlockedNs += steadyNs() - mBlockedAt;
        mBlockedAt = 0;
    });
    mDispatcherConnected = true;
}

void FrameMonitor::disconnectDispatcher()
{
    if (!mDispatcherConnected) return;
    disconnect(mAboutToBlock);
    disconnect(mAwake);
    mDispatcherConnected = false;
}

QVariantMap FrameMonitor::status() const
{
    QVariantMap out;
    const char *phase = mPhase == Phase::Recording ? "recording"
                        : mPhase == Phase::Writing ? "writing" : "idle";
    out["phase"] = QLatin1String(phase);
    out["recording"] = mPhase == Phase::Recording;
    out["seconds"] = mPlannedSeconds;
    out["captureSeconds"] = preferredSeconds();
    out["root"] = captureRoot();
    out["bundle"] = mBundle ? mBundle->dir() : QString();
    out["lastBundle"] = mLastBundle;
    // The RUNNING capture's counts while one runs, the last closed bundle's
    // otherwise — so `perf.stop()` can report what it just wrote.
    out["frames"] = mBundle ? double(mBundle->frames()) : mLastFrames;
    out["events"] = mBundle ? double(mBundle->events()) : mLastEvents;
    if (mPhase == Phase::Recording && mSinceTickEnd.isValid())
        out["remainingMs"] = mAutoStop ? mAutoStop->remainingTime() : -1;

    QVariantMap toast;
    toast["title"] = mLastToastTitle;
    toast["text"] = mLastToastText;
    toast["holdMs"] = mLastToastHoldMs;
    toast["shown"] = mToastsShown;
    out["toast"] = toast;

    // THE ENGINE'S OWN STATUS, verbatim — including the four "zero cost when
    // off" assertions a suite checks rather than trusts.
    QVariantMap eng;
    if (auto e = engine()) {
        const MonitorStatus s = e->monitorStatus();
        eng["level"] = s.level == MonitorLevel::Review ? QStringLiteral("review")
                                                       : QStringLiteral("off");
        eng["attachedListeners"] = s.attachedListeners;
        eng["ringCapacity"] = s.ringCapacity;
        eng["ringFrames"] = s.ringFrames;
        eng["pendingEvents"] = s.pendingEvents;
        eng["framesRecorded"] = double(s.framesRecorded);
        eng["framesDropped"] = double(s.framesDropped);
        eng["eventsDropped"] = double(s.eventsDropped);
        eng["overheadMs"] = double(s.overheadMs);
        QVariantMap gpu;
        gpu["compiled"] = s.gpuCompiled;
        gpu["supported"] = s.gpuSupported;
        gpu["active"] = s.gpuActive;
        gpu["queryPools"] = s.gpuQueryPools;
        gpu["reason"] = qs(s.gpuReason);
        gpu["samplesTruncated"] = s.gpuSamplesTruncated;
        eng["gpu"] = gpu;
    }
    out["gpuSamplesTruncated"] = mGpuSamplesTruncated;
    out["engineFramesDropped"] = double(mEngineFramesDropped);
    out["engineEventsDropped"] = double(mEngineEventsDropped);
    out["engine"] = eng;
    return out;
}
