/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// vr.frame_budget — THE 90 Hz BAR'S RIG ARMS (lane V1-RIG item 5;
// SPECS/PHOTON_SPEC.md §11 item 8, MASTER_QUEUE §681-C).
//
// The READER half. `vr_frame_budget.js` renders the default scene and Showroom 2
// in an OpenXR session with the eye render forced to the Quest Pro's size
// (2160x2376 per eye — 10.26 Mpx of stereo target, five times a desktop 1080p)
// and records the monitor's per-frame rows; this reads them.
//
// WHAT IT REPORTS, and what it ASSERTS — the two are deliberately different,
// and the split was CORRECTED on 2026-09-18 (the lead's read of the first
// version, which asserted a ratio the defect it was written for walks through).
//
// THE BUDGET IS A REPORT LINE. The owner's targets are 90 Hz in a headset
// (11.1 ms) and 60 on the desktop (16.7). This suite prints where each arm sits
// against them and RED-LINES NEITHER, because on the gate's rig the number is
// not the product's: the Xvfb present copy adds a constant 13-21 ms to every
// frame (CLAUDE.md's rig facts) and the GPU sits at whatever clock nobody
// locked — measured on this lane, the identical pass read 0.46 ms in one run and
// 10.3 in another with the clocks free. A red against 11.1 ms here would be a
// red about the rig. The numbers that MEAN something are taken with
// `sudo nvidia-smi --lock-gpu-clocks`, by hand, and go in the ledger; the
// headset half of V1 measures the real thing on the owner's Quest Pro.
//
// THE RUN'S OWN WORST/MEDIAN RATIO IS ALSO ONLY A REPORT LINE, and arithmetic is
// why. The defect this suite was written for — cascade 0's rebuild and the
// irradiance field's whole re-integration landing on ONE frame — measured 11.7 ms
// against a 5.8 ms quiet frame: a ratio of 2.0, which sits UNDER any 3x bar, so
// the pre-fix engine would have passed. A percentile cannot rescue it either:
// p99 of 180 frames is about the second worst, so a class that lands on one or
// two frames of a window barely moves it (a 2-frame class is 1.1 % of the window
// against p99's top 1.8 % — caught only by luck). A ratio is the wrong
// instrument for a rare bounded burst; it sees only a hitch already several
// times the frame.
//
// WHAT IS ASSERTED IS STRUCTURAL, and structure reads the same on any rig, at
// any clock, under any load:
//
//   (1) NO FRAME PAYS FOR BOTH HALVES OF A STEP — no recorded frame carries a
//       cascade rebuild AND an `ifd.follow`. That is the invariant the fix
//       installed (the follow owns the next frame's one GI slot), it is exactly
//       false on the pre-fix engine, and it is a count of frames, not a cost.
//   (2) NO WHOLE-CHAIN REBUILD INSIDE A RECORDED WINDOW — `cascadeFullRebuilds`
//       equal in the bundle's start and end snapshots, and the arm's GI
//       `rebuilds` count unmoved. This is the net under the driver-election
//       defect (V1-RIG fix round item 1): a no-picture frame used to flip the
//       GI driver and cost two from-scratch chain builds.
//   (3) THE WALK ARM REALLY WALKED — at least two of cascade 0's steps inside
//       the window, re-derived from the snapshots so a bundle cannot claim a
//       walk it did not take. A STILL arm can never take one, which is why a
//       still-only suite cannot see the class in (1) at all.
//
// The MAX is reported with its worst pass named and that pass's nominal cost,
// because on this rig a single frame's first compute pass after the big opaque
// pass absorbs a queue wait that is not work: on Showroom 2 the "HDR meter
// clear" — a 256-bin clear with no draws, median 0.172 ms — read 21.851 ms on
// exactly one frame of 180 while the opaque pass beside it never moved from 7.2
// (p50 7.217, max 7.363), with 0 cascade rebuilds, 0 probe captures and 0 shader
// compiles on that frame. A reader can tell that from a real hitch; a threshold
// cannot.
//
// The frame's cost here is GPU + GI: `gpuMs` (every pass's timestamps) plus the
// GI dispatches, which are the one GPU cost in a capture that is not inside a
// pass (Types.h CacheWork::gpuMs). `engine.swap` is never in it.
#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

int gFailures = 0;

void ok(bool cond, const QString &what)
{
    QTextStream(stdout) << (cond ? "ok: " : "FAIL: ") << what << "\n";
    if (!cond) ++gFailures;
}

void report(const QString &what) { QTextStream(stdout) << "report: " << what << "\n"; }

double percentile(std::vector<double> v, double p)
{
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const double k = (double(v.size()) - 1.0) * p / 100.0;
    const size_t f = size_t(std::floor(k));
    const size_t c = std::min(f + 1, v.size() - 1);
    return v[f] + (v[c] - v[f]) * (k - double(f));
}

struct Arm {
    QString name;
    int     frames = 0;
    std::vector<double> gpu;       // passes + GI dispatches, per frame
    std::vector<double> work;      // the host's frame minus engine.swap
    double  swapMean = 0.0;
    int     rebuildFrames = 0;
    int     followFrames = 0;
    /// FRAMES THAT PAID FOR BOTH HALVES OF A STEP — a cascade rebuild and the
    /// field's re-placement on one frame. The invariant is that this is 0.
    int     sharedFrames = 0;
    double  worstRebuildGpu = 0.0;
    /// From the bundle's own start/end snapshots: what the GI arm did ACROSS the
    /// recorded window, which the per-frame rows cannot say.
    long long fullRebuildsDelta = -1;   // -1 = a snapshot was missing
    long long armRebuildsDelta  = -1;
    long long cascade0Steps     = -1;
    /// The worst frame's most expensive pass, its cost there, and what that
    /// pass costs on a median frame — so the reader can say whether the worst
    /// frame was WORK or a queue wait charged to the first pass after the
    /// opaque one (see the header).
    QString worstPass;
    double  worstPassMs = 0.0;
    double  worstPassMedian = 0.0;
};

/// One capture bundle's frames.jsonl, folded into an arm.
bool readArm(const QString &dir, Arm &out)
{
    QFile f(dir + "/frames.jsonl");
    if (!f.open(QIODevice::ReadOnly)) {
        ok(false, QStringLiteral("no frames.jsonl in %1").arg(dir));
        return false;
    }
    // THE FIRST FRAMES OF A CAPTURE ARE NOT THE SUBJECT: the script settles
    // before it records, but the capture's own first frames still carry the
    // monitor's own warm-up (its listener list is built on the first frame).
    const int kSkip = 20;
    int index = 0;
    double swapSum = 0.0;
    // Per-pass GPU series, kept so the worst frame's worst pass can be compared
    // against what that pass costs on an ordinary frame.
    QHash<QString, std::vector<double>> passSeries;
    std::vector<QHash<QString, double>> perFrame;
    while (!f.atEnd()) {
        const QByteArray line = f.readLine().trimmed();
        if (line.isEmpty()) continue;
        const QJsonObject o = QJsonDocument::fromJson(line).object();
        if (o.isEmpty()) continue;
        if (index++ < kSkip) continue;
        double swap = 0.0;
        for (const QJsonValue &sv : o.value(QStringLiteral("stages")).toArray()) {
            const QJsonObject s = sv.toObject();
            if (s.value(QStringLiteral("name")).toString() == QStringLiteral("engine.swap"))
                swap = s.value(QStringLiteral("ms")).toDouble();
        }
        swapSum += swap;
        out.work.push_back(o.value(QStringLiteral("totalMs")).toDouble() - swap);

        const double passGpu = o.value(QStringLiteral("gpuMs")).toDouble();
        double giGpu = 0.0;
        bool follow = false;
        for (const QJsonValue &wv : o.value(QStringLiteral("cacheWork")).toArray()) {
            const QJsonObject w = wv.toObject();
            if (w.value(QStringLiteral("cache")).toString().compare(
                    QStringLiteral("gi"), Qt::CaseInsensitive) != 0)
                continue;
            const double g = w.value(QStringLiteral("gpuMs")).toDouble();
            if (g >= 0.0) giGpu += g;
            if (w.value(QStringLiteral("detail")).toString() == QStringLiteral("ifd.follow"))
                follow = true;
        }
        const double frameGpu = (passGpu >= 0.0 ? passGpu : 0.0) + giGpu;
        out.gpu.push_back(frameGpu);
        QHash<QString, double> framePasses;
        for (const QJsonValue &pv : o.value(QStringLiteral("passes")).toArray()) {
            const QJsonObject q = pv.toObject();
            if (q.value(QStringLiteral("orphaned")).toBool()) continue;
            const double g = q.value(QStringLiteral("gpuMs")).toDouble();
            if (g < 0.0) continue;
            const QString name = q.value(QStringLiteral("pass")).toString();
            framePasses[name] += g;
            passSeries[name].push_back(g);
        }
        perFrame.push_back(framePasses);
        const int rebuilds = o.value(QStringLiteral("cascadeRebuilds")).toInt();
        if (rebuilds > 0 || follow) {
            ++out.rebuildFrames;
            out.worstRebuildGpu = std::max(out.worstRebuildGpu, frameGpu);
        }
        if (follow) ++out.followFrames;
        // (1) THE STRUCTURAL INVARIANT, per frame.
        if (follow && rebuilds > 0) ++out.sharedFrames;
        ++out.frames;
    }
    out.swapMean = out.frames ? swapSum / double(out.frames) : 0.0;

    // THE WORST FRAME'S WORST PASS.
    if (!out.gpu.empty()) {
        const size_t w = size_t(std::max_element(out.gpu.begin(), out.gpu.end()) -
                                out.gpu.begin());
        if (w < perFrame.size()) {
            for (auto it = perFrame[w].constBegin(); it != perFrame[w].constEnd(); ++it)
                if (it.value() > out.worstPassMs) {
                    out.worstPassMs = it.value();
                    out.worstPass   = it.key();
                }
            if (!out.worstPass.isEmpty())
                out.worstPassMedian = percentile(passSeries.value(out.worstPass), 50);
        }
    }
    return out.frames > 0;
}

/// The bundle's start/end snapshots, for the three things the per-frame rows
/// cannot say: whether a WHOLE-CHAIN rebuild happened inside the window, whether
/// the arm rebuilt its GI at all, and how many cascade-0 steps the window really
/// contained.
void readSnapshots(const QString &dir, Arm &out)
{
    const auto giOf = [&](const char *name, QJsonObject &gi) {
        QFile f(dir + "/" + QLatin1String(name));
        if (!f.open(QIODevice::ReadOnly)) return false;
        gi = QJsonDocument::fromJson(f.readAll()).object()
                 .value(QStringLiteral("gi")).toObject();
        return !gi.isEmpty();
    };
    QJsonObject a, b;
    if (!giOf("snapshot_start.json", a) || !giOf("snapshot_end.json", b)) return;
    const auto c0 = [](const QJsonObject &gi) -> long long {
        const QJsonArray cs = gi.value(QStringLiteral("cascades")).toArray();
        if (cs.isEmpty()) return -1;
        return (long long)cs.at(0).toObject().value(QStringLiteral("rebuilds")).toDouble();
    };
    out.fullRebuildsDelta =
        (long long)b.value(QStringLiteral("cascadeFullRebuilds")).toDouble() -
        (long long)a.value(QStringLiteral("cascadeFullRebuilds")).toDouble();
    out.armRebuildsDelta =
        (long long)b.value(QStringLiteral("rebuilds")).toDouble() -
        (long long)a.value(QStringLiteral("rebuilds")).toDouble();
    const long long sa = c0(a), sb = c0(b);
    if (sa >= 0 && sb >= 0) out.cascade0Steps = sb - sa;
}

void judge(Arm &a, bool walking)
{
    ok(a.frames >= 100,
       QStringLiteral("%1: %2 frames recorded").arg(a.name).arg(a.frames));
    if (a.frames < 10) return;

    const double median = percentile(a.gpu, 50);
    const double p95    = percentile(a.gpu, 95);
    const double p99    = percentile(a.gpu, 99);
    const double worst  = *std::max_element(a.gpu.begin(), a.gpu.end());
    const int over90    = int(std::count_if(a.gpu.begin(), a.gpu.end(),
                                            [](double v) { return v > 11.1; }));
    const int over60    = int(std::count_if(a.gpu.begin(), a.gpu.end(),
                                            [](double v) { return v > 16.7; }));
    report(QStringLiteral("%1: GPU per frame (passes + GI dispatches), "
                          "median %2 p95 %3 p99 %4 max %5 ms")
               .arg(a.name).arg(median, 0, 'f', 3).arg(p95, 0, 'f', 3)
               .arg(p99, 0, 'f', 3).arg(worst, 0, 'f', 3));
    report(QStringLiteral("%1: host work (present excluded) median %2 max %3 ms; "
                          "the rig's present copy was %4 ms a frame")
               .arg(a.name).arg(percentile(a.work, 50), 0, 'f', 3)
               .arg(*std::max_element(a.work.begin(), a.work.end()), 0, 'f', 3)
               .arg(a.swapMean, 0, 'f', 2));
    report(QStringLiteral("%1: against the BUDGET — %2 of %3 frames over 11.1 ms (90 Hz), "
                          "%4 over 16.7 (60 Hz). NOT a verdict on this rig (see the header).")
               .arg(a.name).arg(over90).arg(a.frames).arg(over60));
    report(QStringLiteral("%1: %2 frames did cascade work (%3 of them re-placed the "
                          "irradiance field); the worst cost %4 ms")
               .arg(a.name).arg(a.rebuildFrames).arg(a.followFrames)
               .arg(a.worstRebuildGpu, 0, 'f', 3));

    // THE WORST FRAME, NAMED. A reader who sees a max far above p99 wants to
    // know which pass carried it before deciding whether it was work at all.
    report(QStringLiteral("%1: the worst frame cost %2 ms and its most expensive "
                          "pass was '%3' at %4 ms (nominal %5)")
               .arg(a.name).arg(worst, 0, 'f', 3).arg(a.worstPass)
               .arg(a.worstPassMs, 0, 'f', 3).arg(a.worstPassMedian, 0, 'f', 3));

    // THE RATIO IS A REPORT LINE (the header's arithmetic says why): the defect
    // this suite exists for was a ratio of 2.0.
    report(QStringLiteral("%1: worst/median %2x, p99/median %3x — a READING, not a "
                          "verdict (the step-frame defect was 2.0x)")
               .arg(a.name).arg(median > 0.0 ? worst / median : 0.0, 0, 'f', 2)
               .arg(median > 0.0 ? p99 / median : 0.0, 0, 'f', 2));

    // ---- THE STRUCTURAL ASSERTIONS (the header's (1)-(3)) ------------------
    //
    // (1) The two halves of a step never share a frame. Exactly false on the
    //     pre-fix engine, and a count of frames rather than a cost — so it reads
    //     the same on a loaded box, a cold cache and a free-clocked GPU.
    ok(a.sharedFrames == 0,
       QStringLiteral("%1: no frame paid for BOTH a cascade rebuild and the field's "
                      "re-placement (%2 did; %3 rebuild frames, %4 follows)")
           .arg(a.name).arg(a.sharedFrames).arg(a.rebuildFrames).arg(a.followFrames));

    // (2) No whole-chain rebuild, and no GI rebuild at all, inside the window:
    //     a wearer walking is a SCROLL and a scroll is never a from-scratch
    //     build. (This is what the driver-election defect broke.)
    if (a.fullRebuildsDelta < 0) {
        ok(false, QStringLiteral("%1: the bundle carries both snapshots").arg(a.name));
    } else {
        ok(a.fullRebuildsDelta == 0,
           QStringLiteral("%1: the teleport guard forced no whole-chain rebuild in the "
                          "window (delta %2)").arg(a.name).arg(a.fullRebuildsDelta));
        ok(a.armRebuildsDelta == 0,
           QStringLiteral("%1: the GI arm was not rebuilt from scratch in the window "
                          "(delta %2)").arg(a.name).arg(a.armRebuildsDelta));
    }

    // (3) The walk arm really walked; the still arm really stood still.
    if (a.cascade0Steps < 0) {
        ok(false, QStringLiteral("%1: the snapshots name cascade 0").arg(a.name));
    } else if (walking) {
        ok(a.cascade0Steps >= 2,
           QStringLiteral("%1: THE WINDOW CONTAINED AT LEAST TWO CASCADE-0 STEPS (%2) — "
                          "without one there is no step frame to judge")
               .arg(a.name).arg(a.cascade0Steps));
    } else {
        ok(a.cascade0Steps == 0,
           QStringLiteral("%1: a still wearer took no cascade-0 step (%2)")
               .arg(a.name).arg(a.cascade0Steps));
    }
}

}   // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() < 2) {
        std::fprintf(stderr, "usage: test_vr_frame_budget <bundle-root>\n");
        return 2;
    }
    const QString root = args.at(1);

    // The two arms the app half records, by name. A missing one is a failure and
    // not a skip: the wrapper already skipped the whole suite if there was no
    // runtime to render in.
    for (const QString &name : { QStringLiteral("default"),
                                 QStringLiteral("default-walk"),
                                 QStringLiteral("showroom"),
                                 QStringLiteral("showroom-walk") }) {
        Arm a;
        a.name = name;
        const QString dir = root + "/" + name;
        if (!readArm(dir, a)) continue;
        readSnapshots(dir, a);
        judge(a, name.endsWith(QStringLiteral("-walk")));
    }
    QTextStream(stdout) << (gFailures ? "FAILURES: " : "all cases passed (")
                        << gFailures << (gFailures ? "\n" : ")\n");
    return gFailures ? 1 : 0;
}
