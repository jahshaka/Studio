#ifndef LIVECOMPILES_H
#define LIVECOMPILES_H

// A SHADER COMPILE THE USER WAITS FOR IS A DEFECT, AND IT IS SAID (SHADER-WARM-2).
//
// The owner's shape (2026-10-05, "the Unreal way"): the startup gate compiles the
// global set behind the splash; a project open compiles the project's set behind its
// dialog; after that, nothing compiles on the thread that draws. A compile that lands
// on the UI thread outside those windows froze the window for as long as glslang and
// spirv-opt took, and nothing used to name it — the owner's heartbeat lines said
// "(stage: -)".
//
// So the engine's running compile total is read at every FRAME (the one render loop,
// and the scripted frames that bypass it) and at every WINDOW BOUNDARY (an open's
// ledger beginning or ending, a Sanctioned scope — a compile dialog — opening or
// closing). Once the startup gate has armed it, whatever the total moved by outside a
// window is LIVE: logged as "[shader] ... a UI-thread compile is a defect", counted in
// app.shaderCache().liveCompiles (shader.live_compiles fails on it), and the heartbeat
// names a long gap that compiled ("stage: shader compilation (N)") instead of "-".
// Whatever it moved by inside a window is that window's, and is not counted.
//
// Header-only, UI-thread state, and it includes nothing of ours: the driver, the
// viewport's scripted frames, the heartbeat, the open ledger and the gate all reach it
// without a link dependency (a suite that compiles loadtimeline.cpp links nothing new).

#include <QtGlobal>

#include <atomic>
#include <functional>

namespace livecompiles
{

struct State
{
    std::function<unsigned()> total;          ///< the engine's running compile total
    bool                      armed = false;  ///< the startup gate has returned
    int                       windows = 0;    ///< open sanctioned windows
    std::atomic<unsigned>     observed{ 0 };  ///< the total at the last check
    std::atomic<unsigned>     live{ 0 };      ///< compiles counted as live (defects)
};

inline State &state()
{
    static State s;
    return s;
}

/// Reads the total and attributes whatever it moved by since the last read: to the
/// open window if there is one, else (once armed) to the live count, said in the log.
/// `where` names the site for the log line.
inline void check(const char *where)
{
    State &s = state();
    if (!s.total) return;
    const unsigned now = s.total();
    const unsigned before = s.observed.exchange(now, std::memory_order_relaxed);
    if (now <= before || !s.armed || s.windows > 0) return;
    const unsigned n = now - before;
    s.live.fetch_add(n, std::memory_order_relaxed);
    qWarning("[shader] %u shader(s) compiled on the UI thread after the splash, outside a "
             "compile dialog (%s; %u this session) — a UI-thread compile is a defect "
             "(SHADER-WARM-2)",
             n, where, s.live.load(std::memory_order_relaxed));
}

/// The startup gate is over. `total` reads the engine's running compile count; what the
/// gate built is the gate's.
inline void arm(std::function<unsigned()> total)
{
    State &s = state();
    s.total = std::move(total);
    s.observed.store(s.total ? s.total() : 0u, std::memory_order_relaxed);
    s.armed = true;
}
inline bool armed() { return state().armed; }

/// A window in which compiling on the UI thread is the contract (a compile dialog is up,
/// an open is in flight). Entering it settles what came before; leaving it keeps what
/// it built as its own.
inline void enterWindow() { check("before a compile window"); ++state().windows; }
inline void leaveWindow()
{
    State &s = state();
    if (s.windows > 0) --s.windows;
    if (s.windows == 0 && s.total) s.observed.store(s.total(), std::memory_order_relaxed);
}
inline bool inWindow() { return state().windows > 0; }

class Sanctioned
{
public:
    Sanctioned() { enterWindow(); }
    ~Sanctioned() { leaveWindow(); }
    Sanctioned(const Sanctioned &) = delete;
    Sanctioned &operator=(const Sanctioned &) = delete;
};

/// Live compiles counted this session.
inline unsigned live() { return state().live.load(std::memory_order_relaxed); }
/// The total as last read (the heartbeat's namer compares it tick to tick).
inline unsigned observed() { return state().observed.load(std::memory_order_relaxed); }

}  // namespace livecompiles

#endif // LIVECOMPILES_H
