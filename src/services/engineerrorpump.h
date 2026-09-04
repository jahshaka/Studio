/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef ENGINEERRORPUMP_H
#define ENGINEERRORPUMP_H

// EngineErrorPump — the reader the engine's error sink never had.
// (STABILITY_PROGRAM_SPEC.md Lane 1; STABILITY_AUDIT §4.1)
//
// THE PROBLEM. The backend refuses rather than throws: every one of its
// virtuals is wrapped in `JAH_TRY { ... } JAH_CATCH(mError, <refusal>)`
// (irisgl/engine/src/EnginePrivate.h), which records a reason in ONE
// process-wide string and returns 0/false. That contract is right — an
// exception crossing the boundary would take the editor down mid-frame — but
// it only works if someone READS the string, and until this class nobody did:
// SceneMirror ignores the return of setPbrMaterial, attachSkinnedMesh, setSky,
// loadTexture and every setNodeTransform. So a texture that will not decode, a
// mesh with no tangents or a full decal atlas produced a wrong picture and
// ZERO log lines.
//
// THE SHAPE. Once per frame, whoever drove that frame calls drain(engine).
// drain() takes Engine::takeLastError() — which returns AND clears, so a
// failure is reported once instead of forever — and hands it to the rate
// limiter. It is deliberately NOT called from SceneMirror::sync(): the mirror
// has no Engine* and giving it one for logging would be the wrong dependency.
// It does not need one either. There is exactly one sink for the whole
// process, so a thumbnail or preview renderer that drives its own
// renderOneFrame() leaves its failure in the same string, and the next driver
// tick or scripted frame drains it.
//
// RATE LIMITING, because this is polled at 60Hz. Identical messages inside a
// 5s window are counted, not logged; the next log outside the window carries
// "(repeated Nx)". Distinct messages never suppress each other. The key table
// is capped (a message built from a per-frame varying value must not grow it
// without bound) and evicts the least recently seen key.
//
// There is a SECOND, global budget on top of that, and it is not paranoia — the
// first real failure this pump caught needed it. A particle datablock whose
// pixel shader could not be generated made Ogre retry the compile every frame,
// and each retry named a NEW shader ("300000065PixelShader_ps", 66, 67 …), so
// per-message limiting suppressed nothing: 122 distinct messages in two
// seconds. Keying cannot fix that (they really are distinct), so at most
// kMaxLogsPerWindow lines are written per window and the rest are counted and
// reported as one summary line. app.engineErrors() still lists them all.
//
// Logged through iris::Logger::warn — NOT irisLog(), which is hardwired to
// [info] (irisgl/core/logger.cpp).
//
// UI THREAD ONLY, and unsynchronised on purpose. Both call sites are the frame
// loop, which is the Engine's own thread-affinity rule anyway (Engine.h), and a
// mutex on a 60Hz path that exists to make failures visible would be the wrong
// trade. Nothing here may be called from a worker.

#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QElapsedTimer>

#include <vector>

namespace jahshaka { namespace engine { class Engine; } }

class EngineErrorPump
{
public:
    /// Process-wide, like the sink it drains.
    static EngineErrorPump &instance();

    /// Takes whatever the engine has recorded since the last call and logs it
    /// (rate limited). Null engine and an empty sink are both no-ops. Cheap
    /// enough to call every frame: one virtual call and, in the common case, a
    /// std::string move of an empty string.
    void drain(jahshaka::engine::Engine *engine);

    /// The same path drain() uses once it has a message — the seam the gates
    /// and any future non-engine producer go through.
    void record(const QString &message);

    /// {drains, recorded, suppressed, entries:[{message, count, firstMs, lastMs}]}
    /// — what app.engineErrors() returns. Newest entry first, capped at
    /// kMaxKeys. `drains` counts CALLS to drain(), which is how a gate proves
    /// the frame loop is actually pumping.
    QVariantMap report() const;

    /// The distinct messages seen, newest first. Convenience for tests.
    QStringList messages() const;

    /// Forgets everything, including the drain counter.
    void reset();

    /// Test seam: the suppression window. 5000 ms in the app.
    void setWindowMs(qint64 ms) { mWindowMs = ms; }
    qint64 windowMs() const { return mWindowMs; }

    /// How many distinct messages are remembered before the oldest is evicted.
    static constexpr int kMaxKeys = 64;
    /// How many lines the pump may write per window, across ALL messages.
    static constexpr int kMaxLogsPerWindow = 12;

private:
    EngineErrorPump();

    /// The global budget. True when the line may be written.
    bool takeLogBudget(qint64 now);

    struct Entry {
        QString message;
        qint64  firstMs   = 0;   ///< when this message was first seen
        qint64  lastMs    = 0;   ///< when it was last seen (logged or not)
        qint64  loggedMs  = 0;   ///< when it was last actually written out
        quint64 count     = 0;   ///< total occurrences
        quint64 suppressed = 0;  ///< occurrences swallowed since the last write
    };

    std::vector<Entry>::iterator find(const QString &message);

    std::vector<Entry> mEntries;   ///< most recently seen LAST
    QElapsedTimer      mClock;
    qint64             mWindowMs  = 5000;
    quint64            mDrains    = 0;
    quint64            mRecorded  = 0;
    quint64            mSuppressed = 0;
    qint64             mBudgetWindowMs = 0;   ///< start of the current global window
    int                mLoggedInWindow = 0;
    quint64            mFloodedInWindow = 0;
};

#endif // ENGINEERRORPUMP_H
