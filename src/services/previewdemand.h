/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SERVICES_PREVIEWDEMAND_H
#define SERVICES_PREVIEWDEMAND_H

// THE RENDER DEMAND (PREVIEWS-1 stage 2, the lead's ruling): what the shown views want (their
// published visible guids, ui/controls/tilecache.h) and whose preview is MISSING or STALE is requested
// from the preview service - visible first, at most `cap` in flight, ONE request an event-loop turn (a
// request renders inline today), a guid at most once until its state or key changes. Raised from a
// view's publish (TileCache::setDemand), never from a showEvent and never per paint; it reads the
// wanted guids' state BY GUID (never the library) once the publishes have SETTLED. A row that scrolls
// out before its turn is not rendered: every settle replaces what is pending.
//
// IT SERVES A PERSON LOOKING AT A VIEW, NEVER A SCRIPT (the lead's ruling): while a script runs it is
// SUSPENDED - no read, no render (a render inside a script's loop turns lands at a time nobody chose); a
// script asks for previews by previews.render / renderAll. At the run's end the shell has every live view
// publish once (TileCache::republishAll), so the rows on screen are demanded then, with no scroll.
//
// The service is reached through two functions (the shell binds them to PreviewService, a suite to
// fakes): `read` answers, for the guids it is handed, a stamp ("state|key") of each row that needs a
// render (the rest absent); `request` renders one and calls `done` once when it completed.

#include <QHash>
#include <QObject>
#include <QStringList>
#include <QTimer>

#include <functional>

class PreviewDemand : public QObject
{
    Q_OBJECT

public:
    /// THE SETTLE: the demand reads only after the publishes have stopped this long - a scroll in
    /// flight must not render what it passes. A UI debounce ONLY (wall-clock): no row waits on it
    /// (runNow is the rows' door).
    static constexpr int kSettleMs = 150;

    using ReadFn = std::function<QHash<QString, QString>(const QStringList &guids)>;
    using RequestFn = std::function<void(const QString &guid, std::function<void()> done)>;

    PreviewDemand(ReadFn read, RequestFn request, int cap, QObject *parent = nullptr);

    /// A PUBLISH: the guids the shown views want, the most wanted first. Read after the settle.
    void want(const QStringList &guids);
    /// THE ROWS' DOOR: settle NOW (the read, then the first request; the rest follow a turn each).
    void runNow();
    /// A script run started (true) or ended (false). Suspended: publishes are ignored, what was
    /// pending is dropped and nothing is read or requested (a request already made completes).
    void setSuspended(bool suspended);
    bool suspended() const { return mSuspended; }

    /// Requests made this session.
    int requested() const { return mRequested; }
    /// Requests made and not yet completed.
    int inFlight() const { return mInFlight; }

private:
    void settle();
    void pump();

    ReadFn mRead;
    RequestFn mRequest;
    int mCap = 1;
    QTimer mSettle;
    QStringList mWanted;                 // the last publish's guids
    QStringList mPending;                // the last settle's rows to render, in wanted order
    QHash<QString, QString> mStamps;     // guid -> the last settle's stamp
    QHash<QString, QString> mAsked;      // guid -> the stamp it was requested at
    int mInFlight = 0;
    int mRequested = 0;
    bool mPumpScheduled = false;
    bool mSuspended = false;
};

#endif   // SERVICES_PREVIEWDEMAND_H
