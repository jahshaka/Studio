/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef LOADTIMELINE_H
#define LOADTIMELINE_H

// LoadTimeline — the millisecond ledger of "opening a world".
//
// "Why does opening a scene take seconds?" was unanswerable because nothing
// in the open path was timed. This is the ledger: one run per open, a named
// stage per step, printed to the log and readable from scripts
// (app.openTimings()). It is ALWAYS on — the cost is one QElapsedTimer read
// per stage, and an open that is not measured is an open nobody can defend.
//
// Aggregating counters (assimp parses, session registrations) accumulate
// through add() from wherever they happen and are folded into the run when it
// ends. Thread affinity: the timeline belongs to the thread that called
// begin(); add() may be called from a worker between begin() and end() ONLY
// while the owning thread is blocked waiting on it (the open runner's shape).
//
// Stage names are stable strings — the profiling table in the lane report and
// the heartbeat e2e both read them.

#include <QString>
#include <QVariantList>

namespace LoadTimeline {

/// Starts a run (discarding any unfinished one). `label` names what is being
/// opened.
void begin(const QString &label);
/// Closes the stage that was running and opens `stage`.
void mark(const QString &stage);
/// Adds `ms` to a named accumulator (assimp, db, ...) with an item count.
/// Accumulators are reported beside the stage table and are NOT part of the
/// wall-clock total (they are inside the stages).
void add(const QString &counter, double ms, int items = 1);
/// Closes the run, logs the table, and keeps it for lastRun().
void end();

/// True between begin() and end().
bool isRunning();
/// The stage that is open right now ("" when none) — what a heartbeat gap
/// names when it reports where the UI thread went.
QString currentStage();
/// Milliseconds since begin(), or 0.
double elapsedMs();

/// The last completed run as a script-friendly list of
/// {stage, ms} maps, with the run's label/total as the first entry
/// ({stage:"total", ms:<wall clock>, label:"..."}).
QVariantList lastRun();

/// Scoped stage-accumulator helper: adds its lifetime to `counter`.
class Accumulate
{
public:
    explicit Accumulate(const QString &counter, int items = 1);
    ~Accumulate();
    /// Bank the elapsed time now instead of at scope exit (the destructor
    /// then does nothing). For spans that end mid-function.
    void stop();

private:
    QString mCounter;
    int mItems;
    qint64 mStartNs;
    bool mActive;
};

}   // namespace LoadTimeline

#endif   // LOADTIMELINE_H
