/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "scripting/modules/perfapi.h"

#include "data/project.h"
#include "services/framemonitor.h"

QVector<VerbInfo> PerfApi::verbs() const
{
    return {
        { "capture", "perf.capture({frames, seconds, label, out, maxBytes, trace}?) -> "
                     "{started, path, frames, seconds}",
          "Records the NEXT `frames` frames — or the next `seconds` — of the render loop into a "
          "capture bundle: the monitor's whole point, and what Ctrl+F4 does. FORWARD ONLY: nothing "
          "is recorded before this call, there is no history to look back at, and the monitor is "
          "completely off until it. "
          "`frames` IS THE WINDOW A SCRIPT WANTS, and it counts frames WHOEVER draws them — the "
          "driver's tick or a scripted editor.frame(). Use it for every measurement: under "
          "--script the render driver skips every tick by design (ScriptRunPolicy::Off), so a "
          "wall-clock window measures a clock against a loop with no driver frames in it — a "
          "script that steps nothing records ZERO frames and closes itself, and a script that "
          "does step frames is cut off mid-loop when the seconds run out (measured: a 1 s window "
          "over 300 stepped frames wrote 88). A frame-counted bundle holds EXACTLY that many "
          "records and arms no timer at all. "
          "`seconds` is the INTERACTIVE window (Ctrl+F4, the owner watching a live loop) and "
          "defaults to the Preferences > Viewport capture length (20 s); `label` names "
          "the bundle directory (the open scene's name when omitted); `out` writes the bundle "
          "somewhere other than the default root (~/Developer/spikes/perf/<date>-<time>-<scene>); "
          "`maxBytes` caps its size (192 MB by default, and whatever the cap cut is reported in "
          "machine.json rather than silently dropped). A toast says it started and another names "
          "the bundle when it is written; calling perf.stop() — or pressing Ctrl+F4 again — stops "
          "early and still writes a complete, shorter bundle. The bundle is machine.json, "
          "snapshot_start.json, snapshot_end.json, frames.jsonl (one record per frame: every "
          "stage, every pass with its workspace and draw counts, each cache's work AND its "
          "reason), events.jsonl (GI rebuilds, shader compiles, texture loads, UI gaps, marks — "
          "each with its cause) and ogre.log (the window) — plus trace.json (Chrome/Perfetto) when "
          "`trace:true` is asked for. THE TIMELINE IS OPT-IN because writing it is the most "
          "expensive thing a capture does on the UI thread — 0.54-0.60 ms of the monitor's "
          "1.11-1.14 ms per frame on an 8,404-node scene — and it is a RECONSTRUCTION of what "
          "frames.jsonl already holds (the records carry exclusive milliseconds per stage and "
          "per pass, not timestamps, so the timeline lays them end to end). machine.json's "
          "capture block says whether the bundle has one. Refused "
          "while a capture is already running.",
          Needs::Engine },
        { "stop", "perf.stop() -> {stopped, path, frames, events}",
          "Stops the running capture early and writes the bundle, exactly as the 20 s timer "
          "would. Answers {stopped:false} when nothing is running — that is an answer, not an "
          "error. `path` names the bundle directory that was just written.",
          Needs::Engine },
        { "status", "perf.status() -> {phase, recording, seconds, plannedFrames, remainingFrames, "
                    "remainingMs, bundle, lastBundle, frames, events, root, toast, engine}",
          "What the monitor is doing. `phase` is idle | recording | writing. A frame-counted "
          "capture reports `plannedFrames` and `remainingFrames` and carries NO `remainingMs` (it "
          "arms no timer); a timed one is the other way round. `bundle` is the "
          "directory being written now and `lastBundle` the one written last, which is what an "
          "agent needs to go and read it. `engine` carries the engine's own MonitorStatus, "
          "including the four assertions behind 'zero cost when off' (attachedListeners, "
          "ringCapacity, ring contents, GPU query pools — all 0 when idle) and whether GPU "
          "timing is compiled in, supported by this device and active, with the reason when it "
          "is not. `toast` is the last message the shell was asked to show, so a test can assert "
          "the two toasts without a screenshot.",
          Needs::Document },
        { "mark", "perf.mark(label) -> bool",
          "Drops a named marker into the running capture's events.jsonl — 'sweep off', 'drag "
          "starts here', 'the hitch happened now'. The anchor that turns a 20 s recording into a "
          "before/after. False when no capture is running: a mark nobody records would be a lie, "
          "not a no-op.",
          Needs::Document },
    };
}

QVariantMap PerfApi::capture(const QVariantMap &options)
{
    QVariantMap out;
    FrameMonitor::Request request;
    // A NEGATIVE OR FRACTIONAL FRAME COUNT IS ZERO, not a wrapped unsigned:
    // `frames: -1` must fall back to the timed window rather than ask for
    // eighteen quintillion records.
    const double wantFrames = options.value(QStringLiteral("frames"), 0.0).toDouble();
    request.frames = wantFrames >= 1.0 ? static_cast<unsigned long long>(wantFrames) : 0ull;
    request.seconds = options.value(QStringLiteral("seconds"), 0.0).toDouble();
    request.label = options.value(QStringLiteral("label")).toString();
    // THE BUNDLE'S NAME, when the caller did not pick one: the open project,
    // because that is what the owner calls the thing they captured. The
    // engine's own scene name (an internal handle like "editor-1-1001...") is
    // the fallback inside the monitor, and it is nobody's idea of a folder name.
    if (request.label.isEmpty() && host.project) request.label = host.project->getProjectName();
    request.outDir = options.value(QStringLiteral("out")).toString();
    request.maxBytes = options.value(QStringLiteral("maxBytes"), 0).toLongLong();
    request.trace = options.value(QStringLiteral("trace"), false).toBool();

    QString error;
    if (!FrameMonitor::instance().start(request, &error)) {
        // A REFUSAL, not a throw: "a capture is already running" is an answer
        // to a question the caller asked, and a throw would abort its script.
        out["started"] = false;
        out["error"] = error;
        refuse(QStringLiteral("perf.capture: %1").arg(error));
        return out;
    }
    const QVariantMap after = FrameMonitor::instance().status();
    out["started"] = true;
    out["path"] = after.value(QStringLiteral("bundle"));
    // BOTH WINDOWS ARE REPORTED, and the one that is not running reads 0: a
    // caller that asked for frames must be able to see that frames is what it
    // got, and `seconds: 20` beside it would say the opposite.
    out["frames"] = after.value(QStringLiteral("plannedFrames"));
    out["seconds"] = after.value(QStringLiteral("seconds"));
    return out;
}

QVariantMap PerfApi::stop()
{
    QVariantMap out;
    QString error;
    if (!FrameMonitor::instance().stop(&error)) {
        out["stopped"] = false;
        out["error"] = error;
        return out;
    }
    const QVariantMap after = FrameMonitor::instance().status();
    out["stopped"] = true;
    out["path"] = after.value(QStringLiteral("lastBundle"));
    // Everything is read from the state AFTER the stop: the frames and events
    // are counted by the bundle itself as it writes, and the closed capture's
    // totals survive it (FrameMonitor::mLastFrames). A `before` snapshot used
    // to be taken here and Q_UNUSED'd — a whole status() (engine query
    // included) computed and thrown away on every perf.stop.
    out["frames"] = after.value(QStringLiteral("frames"));
    out["events"] = after.value(QStringLiteral("events"));
    return out;
}

QVariantMap PerfApi::status()
{
    return FrameMonitor::instance().status();
}

bool PerfApi::mark(const QString &label)
{
    if (!FrameMonitor::instance().mark(label))
        return refuse(QStringLiteral("perf.mark: no capture is running"));
    return true;
}
