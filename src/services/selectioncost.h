/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SELECTIONCOST_H
#define SELECTIONCOST_H

// selcost — WHAT A SELECTION CHANGE COSTS, PER CONSUMER (lane SELECT-COST-1).
//
// THE MEASUREMENT THIS EXISTS FOR (VR-INPUT-1S, ledger §673): `vr.select()`
// cost 16-17 ms per call, INDEPENDENT OF SCENE SIZE — at 90 Hz a controller
// trigger press costs more than a frame, and a desktop click pays the same.
// "Inside the selection machinery" was as far as that measurement went, and
// four consumers share that machinery (MainWindow::applySelectionToUi):
// the viewport's outline and gizmo, the Properties column, the outliner's
// current row and the timeline's subject — plus the Properties column's
// DEFERRED mount (ADD-1), which lands in the turn after the selection and is
// therefore invisible to a timer wrapped around the call.
//
// So the cost is accounted here, exclusively, per consumer, always on: five
// buckets of wall time plus the counts that tell a reading apart from a
// coincidence (how many selections were made, how many of them actually
// changed the primary, how many mounts they cost). A QElapsedTimer start/stop
// pair is ~40 ns against consumers measured in milliseconds, which is why this
// is not behind a flag: a number nobody can read in the product is a number
// nobody checks.
//
// THE READOUT IS A VERB (`editor.selectionCost()`), API-first, and the
// absolute bound in `ui.selection_cost` is derived from it. Nothing here
// judges: it counts, exactly like FrameMonitor.
//
// UI THREAD ONLY. Every writer (the shell's fan-out, the properties panel's
// mount) runs there and nothing is synchronised, exactly like editgate.
//
// WHY HEADER-ONLY, with the state in an inline function's local static: the
// writers are the shell, a panel and a verb module, and the readers are a verb
// and two suites that link different slices of the app. One instance across
// every translation unit, no new .cpp in eleven test targets' source lists.

#include <QElapsedTimer>
#include <QtGlobal>

namespace selcost {

/// The consumers of a selection change, in the order applySelectionToUi runs
/// them, with the Properties column's deferred mount last (it is charged in a
/// later turn, which is the whole reason it needs its own bucket).
enum Stage {
    Viewport = 0,       ///< outline, gizmo, gizmo group
    Properties,         ///< SceneNodePropertiesWidget::setSceneNode — the debt, not the mount
    Hierarchy,          ///< the outliner's current row
    Timeline,           ///< the animation widget's subject
    Mount,              ///< SceneNodePropertiesWidget::mountNow — the deferred column build
    StageCount
};

struct Bucket
{
    double  ms = 0.0;       ///< total wall time charged to this consumer
    double  lastMs = 0.0;   ///< the most recent charge
    double  maxMs = 0.0;    ///< the worst single charge
    quint64 calls = 0;      ///< how many charges
};

namespace detail {

struct State
{
    Bucket  buckets[StageCount];
    quint64 selections = 0;     ///< applySelectionToUi calls
    quint64 primaryChanges = 0; ///< ...of which actually changed the primary node
    quint64 mountsSkipped = 0;  ///< owed mounts a coalesce or a no-op selection saved
};

/// The one instance (editgate's rationale: an inline function's local static is
/// unique across every translation unit that includes this header).
inline State &state()
{
    static State s;
    return s;
}

} // namespace detail

inline void charge(Stage stage, double ms)
{
    if (stage < 0 || stage >= StageCount) return;
    Bucket &b = detail::state().buckets[stage];
    b.ms += ms;
    b.lastMs = ms;
    if (ms > b.maxMs) b.maxMs = ms;
    ++b.calls;
}

/// Times a consumer for as long as it is in scope.
class Scope
{
public:
    explicit Scope(Stage stage) : mStage(stage) { mTimer.start(); }
    ~Scope() { charge(mStage, double(mTimer.nsecsElapsed()) / 1e6); }

    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;

private:
    Stage mStage;
    QElapsedTimer mTimer;
};

/// One selection arrived at the shell's fan-out. `primaryChanged` is false when
/// the same node was re-selected — the case the fan-out must make free.
inline void noteSelection(bool primaryChanged)
{
    ++detail::state().selections;
    if (primaryChanged) ++detail::state().primaryChanges;
}

/// An owed column mount that never had to happen: the same node re-selected,
/// or a burst of selections coalesced into one mount.
inline void noteMountSkipped() { ++detail::state().mountsSkipped; }

inline const Bucket &bucket(Stage stage)
{
    static const Bucket empty;
    if (stage < 0 || stage >= StageCount) return empty;
    return detail::state().buckets[stage];
}

inline quint64 selections()     { return detail::state().selections; }
inline quint64 primaryChanges() { return detail::state().primaryChanges; }
inline quint64 mountsSkipped()  { return detail::state().mountsSkipped; }

/// The whole cost of one selection change, as the user pays it: every consumer
/// plus the deferred mount.
inline double totalMs()
{
    double sum = 0.0;
    for (int i = 0; i <= Mount; ++i) sum += detail::state().buckets[i].ms;
    return sum;
}

inline void reset() { detail::state() = detail::State(); }

} // namespace selcost

#endif // SELECTIONCOST_H
