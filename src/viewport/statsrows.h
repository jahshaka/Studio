/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef STATSROWS_H
#define STATSROWS_H

// THE F3 READOUT'S TEXT (owner review 2026-09-18, answer Q3).
//
// The rows the engine draws on the HUD, composed HERE as a pure function of the
// numbers, for one reason: the owner's complaint about this panel was never
// about the layout, it was that the words lied. "4,611 triangles" was every
// triangle of every pass and read as the scene's content; "104,329 skipped"
// was a lifetime tick counter and read as dropped frames. A row's WORDING is
// therefore a contract, and a contract wants a test — which a function taking
// numbers and returning strings can have, and a method reaching into a live
// viewport, an engine and a render driver cannot.
//
// What the four rows say, and why each is here (the owner's answer, in order):
//
//   1  idle / drawing, and FRAMES ACTUALLY DRAWN per second. The state first,
//      because "0 fps" on a page with no viewport is not a stall, it is the
//      loop correctly doing nothing, and that was the honest half of the
//      "skipped" number this replaces.
//   2  how long a frame's work takes, and the rate that cost alone would allow
//      (the loop is a TIMER — a healthy editor reads whatever the pacing
//      allows whatever the scene costs, so the cost is the number that moves).
//   3  THE SCENE'S OWN TRIANGLES beside what the renderer SUBMITTED. Two
//      numbers, both labelled, because the gap between them IS the answer to
//      "why is an empty world 4,611 triangles" (services/scenestats.h).
//   4  draws, and SLOW FRAMES IN THE LAST MINUTE — a rolling window, never a
//      lifetime total (viewport/framewindows.h).

#include <QLocale>
#include <QObject>
#include <QString>
#include <QStringList>

namespace statsrows {

/// Everything the rows say, as numbers. Filled by the viewport from the render
/// driver, the engine's counters and the document walk.
struct Input
{
    /// Is the loop drawing at all? False on a page with no visible viewport —
    /// the state the deleted `skipped` counter was counting up.
    bool    drawing = false;
    /// Frames ACTUALLY DRAWN in the last second (not timer ticks, and
    /// including the frames a script or the VR pump drew outside the tick).
    double  fpsDrawn = 0.0;
    /// The rolling average cost of a frame's work, ms.
    double  workMs = 0.0;
    /// The scene's own authored triangles (scenestats::sceneGeometry).
    quint64 sceneTriangles = 0;
    /// What the renderer handed the GPU last frame, every pass included.
    quint64 submittedTriangles = 0;
    /// Draw calls in that same frame.
    quint64 draws = 0;
    /// Frames over the 100 ms hitch threshold in the last minute.
    int     slowFramesLastMinute = 0;
    /// ATOM's readout (P1): how many DRAWN objects have a baked LOD chain at all,
    /// how many of those are currently on a level below the authored one, and the
    /// deepest level anything is on. Row 5 exists only when `lodObjects > 0` — a
    /// scene with no chained geometry has nothing to say and the overlay has one
    /// corner to say it in.
    int     lodObjects = 0;
    int     lodCoarser = 0;
    int     lodDeepest = 0;
};

/// A count with thousands separators, in the C locale so the rows read the
/// same on every machine and a suite can assert them. The C locale OMITS the
/// group separator by default (QLocale::OmitGroupSeparator is part of it), and
/// a bare "2178" is the reading this row exists to make easy — so the option is
/// cleared rather than the locale changed.
inline QString grouped(quint64 n)
{
    QLocale loc = QLocale::c();
    loc.setNumberOptions(loc.numberOptions() & ~QLocale::OmitGroupSeparator);
    return loc.toString(qulonglong(n));
}

/// The rows, top to bottom. Pure: same numbers in, same strings out.
inline QStringList compose(const Input &in)
{
    QStringList rows;
    // Row 1 — the state and the honest frame rate.
    rows << QStringLiteral("%1   %2 fps drawn")
                .arg(in.drawing ? QObject::tr("drawing") : QObject::tr("idle"))
                .arg(in.fpsDrawn, 0, 'f', 0);
    // Row 2 — what a frame costs, and what that cost alone would allow. The
    // potential saturates at 999: below a millisecond of work the division is
    // measuring the timer's own resolution, not the renderer.
    {
        const double potential = in.workMs > 1.0 ? 1000.0 / in.workMs : 999.0;
        rows << QStringLiteral("%1 ms/frame   ~%2 uncapped")
                    .arg(in.workMs, 0, 'f', 1)
                    .arg(qMin(potential, 999.0), 0, 'f', 0);
    }
    // Row 3 — THE TWO TRIANGLE NUMBERS, both labelled. Neither word is
    // optional: "tris" alone is the row the owner read as his scene's content.
    rows << QStringLiteral("%1 scene tris   %2 submitted")
                .arg(grouped(in.sceneTriangles), grouped(in.submittedTriangles));
    // Row 4 — draws, and the rolling hitch count. "last min" is part of the
    // row: a bare "3 slow" is a lifetime total to anybody who reads it.
    rows << QStringLiteral("%1 draws   %2 slow frames (last min)")
                .arg(grouped(in.draws)).arg(in.slowFramesLastMinute);
    // Row 5 — THE CHAIN, WHERE IT CAN BE SEEN WORKING (ATOM P1's readout, the gap
    // OWN-TRI left). `submittedTriangles` above says the scene shed triangles;
    // this says which levels did the shedding, which is the difference between a
    // number moving and a mechanism being observable. Only present when something
    // in the frame HAS a chain.
    if (in.lodObjects > 0)
        rows << QStringLiteral("LOD %1/%2 objects coarser   deepest L%3")
                    .arg(in.lodCoarser).arg(in.lodObjects).arg(in.lodDeepest);
    return rows;
}

}   // namespace statsrows

#endif // STATSROWS_H
