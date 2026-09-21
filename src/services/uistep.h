/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef UISTEP_H
#define UISTEP_H

// UiStep — the name of what the UI thread is doing right now, for the two
// probes that report it having stopped answering.
//
// WHY THIS IS NOT LoadTimeline (FSYNC-1). LoadTimeline names the stage of an
// OPEN, and the heartbeat and the watchdog already print it. Every UI-thread
// block outside an open therefore reported "stage: -", which is what the
// 2026-09-15 archive reds looked like: `[heartbeat] UI thread blocked 1018 ms
// (stage: -)` for a block the archiver owned and `618 ms (stage: -)` for one
// the shader cache's own save owned, in the same run, with nothing to tell
// them apart. It cannot be fixed by calling LoadTimeline::begin from those two
// places either: the shader-cache watchdog DEFERS itself while a timeline run
// is open (deliberately — a save must never land inside an open), so marking
// the archive with one would defer the save for the whole archive and mark the
// save's own step with a timeline that turns the save off.
//
// So this is the smaller thing that was missing: one process-wide pointer to a
// STRING LITERAL, set by a scope guard at the few places that can hold the UI
// thread outside an open, read by the heartbeat and by the watchdog thread.
//
//   * literals only — the pointer is read from another thread with no lock, so
//     what it points at must outlive every reader, and a std::atomic<const
//     char *> is exactly one word;
//   * nested scopes restore their parent on the way out, so a step inside a
//     step reads as the inner one and then as the outer one again;
//   * costs a relaxed store per scope: it is free enough to sit in a loop.
//
// It answers "where did the UI thread go", nothing else. Nothing branches on
// it, and nothing may: a step that some code TESTS is a state machine, and
// this is a label.

#include <QString>

#include <atomic>

namespace UiStep
{

/// The live step, or nullptr. Safe from any thread.
const char *currentRaw();

/// The live step as a QString ("" when none) — what a probe prints.
inline QString current() { const char *s = currentRaw(); return s ? QString::fromLatin1(s) : QString(); }

/// Sets the step; `step` must be a string LITERAL (or otherwise outlive the
/// process). nullptr clears it. Returns what was set before.
const char *set(const char *step);

/// The scope guard. `UiStep::Scope step("archive: install import slice");`
class Scope
{
public:
    explicit Scope(const char *step) : mPrevious(set(step)) {}
    ~Scope() { set(mPrevious); }
    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;

private:
    const char *mPrevious = nullptr;
};

}   // namespace UiStep

#endif   // UISTEP_H
