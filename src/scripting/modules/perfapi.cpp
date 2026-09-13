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
        { "capture", "perf.capture({seconds, label, out, maxBytes}?) -> {started, path, seconds}",
          "Records the NEXT `seconds` of the render loop into a capture bundle — the monitor's "
          "whole point, and what Ctrl+F4 does. FORWARD ONLY: nothing is recorded before this call, "
          "there is no history to look back at, and the monitor is completely off until it. "
          "`seconds` defaults to the Preferences > Viewport capture length (20 s); `label` names "
          "the bundle directory (the open scene's name when omitted); `out` writes the bundle "
          "somewhere other than the default root (~/Developer/spikes/perf/<date>-<time>-<scene>); "
          "`maxBytes` caps its size (192 MB by default, and whatever the cap cut is reported in "
          "machine.json rather than silently dropped). A toast says it started and another names "
          "the bundle when it is written; calling perf.stop() — or pressing Ctrl+F4 again — stops "
          "early and still writes a complete, shorter bundle. The bundle is machine.json, "
          "snapshot_start.json, snapshot_end.json, frames.jsonl (one record per frame: every "
          "stage, every pass with its workspace and draw counts, each cache's work AND its "
          "reason), events.jsonl (GI rebuilds, shader compiles, texture loads, UI gaps, marks — "
          "each with its cause), trace.json (Chrome/Perfetto) and ogre.log (the window). Refused "
          "while a capture is already running.",
          Needs::Engine },
        { "stop", "perf.stop() -> {stopped, path, frames, events}",
          "Stops the running capture early and writes the bundle, exactly as the 20 s timer "
          "would. Answers {stopped:false} when nothing is running — that is an answer, not an "
          "error. `path` names the bundle directory that was just written.",
          Needs::Engine },
        { "status", "perf.status() -> {phase, recording, seconds, remainingMs, bundle, lastBundle, "
                    "frames, events, root, toast, engine}",
          "What the monitor is doing. `phase` is idle | recording | writing; `bundle` is the "
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
    request.seconds = options.value(QStringLiteral("seconds"), 0.0).toDouble();
    request.label = options.value(QStringLiteral("label")).toString();
    // THE BUNDLE'S NAME, when the caller did not pick one: the open project,
    // because that is what the owner calls the thing they captured. The
    // engine's own scene name (an internal handle like "editor-1-1001...") is
    // the fallback inside the monitor, and it is nobody's idea of a folder name.
    if (request.label.isEmpty() && host.project) request.label = host.project->getProjectName();
    request.outDir = options.value(QStringLiteral("out")).toString();
    request.maxBytes = options.value(QStringLiteral("maxBytes"), 0).toLongLong();

    QString error;
    if (!FrameMonitor::instance().start(request, &error)) {
        // A REFUSAL, not a throw: "a capture is already running" is an answer
        // to a question the caller asked, and a throw would abort its script.
        out["started"] = false;
        out["error"] = error;
        refuse(QStringLiteral("perf.capture: %1").arg(error));
        return out;
    }
    out["started"] = true;
    out["path"] = FrameMonitor::instance().status().value(QStringLiteral("bundle"));
    out["seconds"] = FrameMonitor::instance().status().value(QStringLiteral("seconds"));
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
