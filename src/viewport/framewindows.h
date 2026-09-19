/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef FRAMEWINDOWS_H
#define FRAMEWINDOWS_H

// A ROLLING COUNT OVER A TIME WINDOW (owner review 2026-09-18, answer Q3).
//
// The F3 readout used to show two LIFETIME totals — "104,329 skipped, 8 slow"
// — and a lifetime total is the one shape a live readout must never have: it
// only ever goes up, so it says nothing about now. The owner's answer asked for
// "slow frames IN THE LAST MINUTE", and "frames actually drawn per second" is
// the same shape with a one-second window, so both are this one class.
//
// TIME IS AN ARGUMENT, NEVER A CLOCK READ IN HERE. That is deliberate and it is
// what makes the behaviour testable: "a wall-clock settle measures nothing" in
// this tree, and a suite proving that the window FORGETS after sixty seconds
// must be able to hand it sixty seconds without waiting for them. The driver
// passes its own monotonic milliseconds; the suite passes whatever it likes.
//
// Header-only, no Qt beyond QtGlobal: it is read from the render driver, the
// stats rows and a unit suite that links none of the shell.

#include <QtGlobal>

#include <deque>

namespace framewindows {

/// Events stamped with a time, counted over the last `windowMs`.
class EventWindow
{
public:
    /// A HARD CAP on how many stamps are held, so a pathological caller (a
    /// zero-interval loop with a sixty-second window) cannot grow this without
    /// bound. At the two sizes actually used — one second of frames, one minute
    /// of frames slower than 100 ms — it is never reached.
    static constexpr int kMaxEvents = 4096;

    explicit EventWindow(double windowMs) : mWindowMs(windowMs) {}

    double windowMs() const { return mWindowMs; }

    /// Records an event at `nowMs` and drops everything that has fallen out of
    /// the window. Stamps are expected non-decreasing (one monotonic clock);
    /// an out-of-order stamp is stored as given and simply ages out on its own.
    void add(double nowMs)
    {
        mAt.push_back(nowMs);
        prune(nowMs);
        while (int(mAt.size()) > kMaxEvents) mAt.pop_front();
    }

    /// How many events fall in (nowMs - windowMs, nowMs]. Const: it counts
    /// from the newest end and stops at the first stamp outside the window, so
    /// a reader never has to mutate to get an honest answer.
    int count(double nowMs) const
    {
        const double cutoff = nowMs - mWindowMs;
        int n = 0;
        for (auto it = mAt.rbegin(); it != mAt.rend(); ++it) {
            if (*it <= cutoff) break;
            ++n;
        }
        return n;
    }

    /// The count expressed as a RATE per second — what "frames drawn per
    /// second" is over a one-second window, and still the right number over a
    /// window of any other length.
    double perSecond(double nowMs) const
    {
        if (mWindowMs <= 0.0) return 0.0;
        return double(count(nowMs)) * 1000.0 / mWindowMs;
    }

    /// How many stamps are held at all (the cap's witness; not a public
    /// statistic).
    int held() const { return int(mAt.size()); }

    void clear() { mAt.clear(); }

private:
    void prune(double nowMs)
    {
        const double cutoff = nowMs - mWindowMs;
        while (!mAt.empty() && mAt.front() <= cutoff) mAt.pop_front();
    }

    double mWindowMs;
    std::deque<double> mAt;
};

/// The two windows this app reads, named once so the readout, the verb and the
/// suite cannot disagree about them.
constexpr double kDrawnRateWindowMs = 1000.0;    ///< frames drawn per second
constexpr double kSlowFrameWindowMs = 60000.0;   ///< slow frames in the last minute

}   // namespace framewindows

#endif // FRAMEWINDOWS_H
