/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef PERFSAMPLER_H
#define PERFSAMPLER_H

// PerfSampler — the periodic `perf` line (SESSION_LOG_SPEC §8-R3).
//
// THIS IS THE FEATURE THE WHOLE PROGRAM EXISTS FOR. The motivating case is a
// progressive fps decay with no record of what happened between "opened fast"
// and "slow twenty minutes later" — and the reason that was undiagnosable is
// not that the numbers were missing. Every one of them is already a registry
// verb (app.frameStats, app.renderStats, app.engineErrors, app.heartbeatStats).
// NOTHING WROTE ANY OF THEM DOWN. This does, once a minute.
//
// Reading the resulting series is the diagnosis:
//   * `work` rising while `draws` stays flat        -> our code got slower
//   * `draws`/`tris` rising                          -> the scene grew
//   * `rss` rising with everything else flat         -> a leak
//   * `slow` climbing in steps                       -> hitches, not a slope
//
// A TIMER, NOT A HOOK, and that is the whole design. It reads counters other
// people already maintain and writes ONE line; it never touches the frame path,
// never adds a per-frame branch, and costs a handful of struct reads once per
// interval. The discipline (§8-R1) is zero log calls on the frame path at
// default verbosity, and a sampler implemented as a frame hook would have been
// the first violation of it.

#include <QObject>
#include <QString>

class QTimer;

class PerfSampler : public QObject
{
    Q_OBJECT
public:
    explicit PerfSampler(QObject *parent = nullptr);

    /// Starts (or re-times) the sampler. `seconds` <= 0 stops it — which is
    /// what `log/perfSampleSeconds = 0` means. Defaults: 60 s in a development
    /// build, 300 s in a release one (spec §3.4/§8-R3).
    void start(int seconds);
    void stop();
    bool isRunning() const;
    int intervalSeconds() const { return mSeconds; }

    /// Reads the settings file's `log/perfSampleSeconds` (absent = the build's
    /// default) and starts accordingly.
    void startFromSettings();

    /// Emits one sample NOW, whatever the timer is doing. The verb behind
    /// log.sample() and the way a test gets a line without waiting a minute.
    QString sampleNow();

    /// The default interval for this build.
    static int defaultSeconds();

private:
    QTimer *mTimer = nullptr;
    int mSeconds = 0;
};

#endif   // PERFSAMPLER_H
