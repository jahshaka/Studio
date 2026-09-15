/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef EDITGATE_H
#define EDITGATE_H

// editgate — MAY THE UI WRITE TO THE DOCUMENT RIGHT NOW? (owner, ledger §423)
//
// While a script runs the editor is NON-EDITABLE BUT FULLY NAVIGABLE: the
// viewport camera, panels, tabs, page switches and selection all work, and any
// document write arriving from the UI is refused until the run ends. That rule
// exists because the script engine moved off the UI thread (SCRIPTING_LIVE_
// SPEC): between two verbs the event loop runs, so a gizmo drag or a slider is
// now physically possible in the middle of somebody else's edit — and the run
// IS one open undo macro, so a hand edit would silently become part of it and
// come back on the script's Ctrl+Z. Refusing it with nothing said would read
// as a frozen app; hence the notice below.
//
// THE GATE KEYS ON THE CALLING CONTEXT, NOT ON THE THREAD. The script's own
// writes arrive on the UI thread too — the worker hops here for every verb —
// so "which thread is this" cannot tell the two apart. What can: the verb
// dispatcher wraps every verb call in enterVerb()/leaveVerb(), so inside a
// verb the document is the RUN's to write and outside it, while a run is in
// flight, it is nobody's.
//
// WHY A PROCESS-WIDE GATE AND NOT A PREDICATE THREADED THROUGH THE PANELS.
// The refusal has to be asked at the spine (UndoService::push, where every
// command in the app lands) AND at the handful of gesture funnels that write
// the document LIVE and only push when the gesture ends (a slider drag, the
// gizmo, the transform rows) — a value already written cannot be refused by a
// command that never runs. Those funnels are row factories and free functions
// with no service pointer between them; carrying a predicate into each is the
// per-widget sweep this design exists to avoid. One process has one document,
// one script engine and one UI thread (Ogre::Root is a singleton — the editor
// is one per process, by law), so one gate is the honest shape.
//
// UI THREAD ONLY. Every caller — the dispatcher, the panels, the viewport, the
// undo spine — runs there; nothing here is synchronised and nothing needs to
// be.

#include <functional>

#include <QtGlobal>

namespace editgate {

namespace detail {

struct State
{
    int  runDepth  = 0;     ///< script runs in flight (evaluate() refuses nesting; a counter is free)
    int  verbDepth = 0;     ///< verb calls on the stack — the run's own writes
    quint64 refusals = 0;   ///< hand edits refused during THIS run
    bool noticed = false;   ///< the run's one notice has been raised
    std::function<void()> notice;
};

/// The one instance. An inline function's local static is unique across every
/// translation unit that includes this header, which is what makes a
/// header-only gate correct rather than merely convenient.
inline State &state()
{
    static State s;
    return s;
}

}   // namespace detail

/// A script run owns the document from here. Called for BOTH run policies:
/// "off" only means the viewport holds its picture, not that the app is
/// blocked — a person can click during a --script run exactly as during a
/// console one, and the undo macro they would land in is the same.
inline void runStarted()
{
    auto &s = detail::state();
    ++s.runDepth;
    s.refusals = 0;
    s.noticed = false;      // the notice is per RUN, re-armed here
}

/// ...and gives it back. Balanced by ScriptEngine's own start/end bracket.
inline void runFinished()
{
    auto &s = detail::state();
    if (s.runDepth > 0) --s.runDepth;
}

inline bool runActive() { return detail::state().runDepth > 0; }

/// The run's own write scope: the verb dispatcher's bracket (see the header
/// note — this is what distinguishes the script from the hand).
inline void enterVerb() { ++detail::state().verbDepth; }
inline void leaveVerb() { auto &s = detail::state(); if (s.verbDepth > 0) --s.verbDepth; }
inline bool inVerb() { return detail::state().verbDepth > 0; }

/// THE QUESTION, with no side effect: would a hand edit be refused right now?
/// False inside a verb (the run writing its own document) and false whenever
/// no run is in flight, which is every ordinary moment of the app's life.
inline bool blocked()
{
    const auto &s = detail::state();
    return s.runDepth > 0 && s.verbDepth == 0;
}

/// THE GATE. Ask it immediately before the UI touches the document:
///
///     if (editgate::refuse()) return;      // the script owns the document
///
/// True means refuse. Saying true also RAISES THE RUN'S ONE NOTICE — once per
/// run, not once per refused event: a drag is dozens of events and a toast per
/// event would be its own kind of broken.
inline bool refuse()
{
    auto &s = detail::state();
    if (s.runDepth == 0 || s.verbDepth > 0) return false;
    ++s.refusals;
    if (!s.noticed) {
        s.noticed = true;
        if (s.notice) s.notice();
    }
    return true;
}

/// What to show the person whose edit was refused (the shell wires a toast).
/// Unset in every host that has no window.
inline void setNoticeHook(std::function<void()> hook)
{
    detail::state().notice = std::move(hook);
}

/// Hand edits refused during the run in flight (or the last one). The state
/// verb reports it, which is how a test proves a refusal happened at all.
inline quint64 refusals() { return detail::state().refusals; }
/// Whether this run's notice has been raised.
inline bool noticeShown() { return detail::state().noticed; }

/// TESTS ONLY: forget everything, including the notice hook.
inline void reset() { detail::state() = detail::State(); }

/// RAII for the dispatcher's bracket — exception-safe, because a verb that
/// throws must not leave the gate believing the script is still writing.
struct VerbScope
{
    VerbScope() { enterVerb(); }
    ~VerbScope() { leaveVerb(); }
    VerbScope(const VerbScope &) = delete;
    VerbScope &operator=(const VerbScope &) = delete;
};

}   // namespace editgate

#endif   // EDITGATE_H
