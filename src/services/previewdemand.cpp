/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/previewdemand.h"

#include <QPointer>

PreviewDemand::PreviewDemand(ReadFn read, RequestFn request, int cap, QObject *parent)
    : QObject(parent), mRead(std::move(read)), mRequest(std::move(request)), mCap(qMax(1, cap))
{
    mSettle.setSingleShot(true);
    mSettle.setInterval(kSettleMs);
    connect(&mSettle, &QTimer::timeout, this, &PreviewDemand::settle);
}

void PreviewDemand::want(const QStringList &guids)
{
    if (mSuspended) return;
    mWanted = guids;
    mSettle.start();   // restarted by every publish: the read waits for the scroll to stop
}

void PreviewDemand::runNow()
{
    mSettle.stop();
    settle();
}

void PreviewDemand::setSuspended(bool suspended)
{
    mSuspended = suspended;
    if (!suspended) return;
    mSettle.stop();
    mWanted.clear();
    mPending.clear();
}

void PreviewDemand::settle()
{
    mPending.clear();
    mStamps.clear();
    if (mSuspended) return;
    // Nothing shown, nothing read (an empty list would be no filter at all).
    if (mWanted.isEmpty() || !mRead) return;
    mStamps = mRead(mWanted);
    for (const QString &guid : mWanted) {
        const auto stamp = mStamps.constFind(guid);
        // Once a guid until its state or key moves (a failed render is not asked again for the same).
        if (stamp == mStamps.constEnd() || mAsked.value(guid) == *stamp) continue;
        mPending.append(guid);
    }
    pump();
}

void PreviewDemand::pump()
{
    mPumpScheduled = false;
    if (mSuspended || mPending.isEmpty() || mInFlight >= mCap || !mRequest) return;
    const QString guid = mPending.takeFirst();
    mAsked.insert(guid, mStamps.value(guid));
    ++mInFlight;
    ++mRequested;
    QPointer<PreviewDemand> self(this);
    mRequest(guid, [self]() {
        if (!self) return;
        --self->mInFlight;
        if (!self->mPending.isEmpty() && !self->mPumpScheduled) {
            self->mPumpScheduled = true;
            QMetaObject::invokeMethod(self, [self]() { if (self) self->pump(); }, Qt::QueuedConnection);
        }
    });
    // ONE REQUEST A TURN (a render is inline today): the next on the next turn.
    if (!mPending.isEmpty() && mInFlight < mCap && !mPumpScheduled) {
        mPumpScheduled = true;
        QMetaObject::invokeMethod(this, [this]() { pump(); }, Qt::QueuedConnection);
    }
}
