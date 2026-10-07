/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef VIDEORECORDER_H
#define VIDEORECORDER_H

// THE VIEWPORT VIDEO RECORDER (VIDEO-REC-1; SPECS/VIDEO_CAPTURE_SPEC.md §2 + §10,
// encoder route A of §9). One recording at a time, end to end:
//
//   the picture   IEditorViewport::beginRecordingView — a SEPARATE 1920x1080
//                 offscreen render of the editor's camera (owner §10.2), scene
//                 only unless the helpers switch is on (§10.5);
//   the colour    the engine's NV12 pass on the GPU (View::setVideoReadback:
//                 BT.709, limited range, on the display codes);
//   the readback  a ring of three AsyncTextureTickets, polled once a frame —
//                 the UI thread never waits on the GPU;
//   the time      the SCENE's clock (the 1/60 s grid, §10.3): a video frame per
//                 grid step, a slow editor frame HELD across the steps it
//                 spanned (the same picture sent again), never a sped-up file;
//   the encoder   Qt's QVideoFrameInput -> QMediaCaptureSession -> QMediaRecorder,
//                 NV12 frames (which is what makes Qt pick the hardware H.264
//                 encoder, §9), the format-less input constructor (Qt's bug, §9);
//   the file      MPEG-4 H.264 ~14 Mbps, no audio, no length cap; on stop the
//                 index is moved to the front on a worker (mp4faststart.h) and
//                 the result published by ONE atomic rename.
//
// NOTHING BLOCKS THE UI THREAD except the stop's drain (at most the three frames
// already submitted, waited on their own fences). A failure — no encoder, a
// folder that cannot be written, an encoder error mid-recording — ends the
// recording cleanly with one truthful message (`failed`), never a crash.

#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantMap>
#include <QVideoFrame>
#include <QVideoFrameFormat>

#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <vector>

#include "jahshaka/engine/Types.h"

namespace jahshaka { namespace engine { class View; } }

class IEditorViewport;
class QMediaCaptureSession;
class QMediaRecorder;
class QVideoFrameInput;

class VideoRecorder : public QObject
{
    Q_OBJECT
public:
    enum class State { Idle, Recording, Finishing, Done, Failed };
    struct Options {
        QString path;         ///< empty = ~/Videos/Jahshaka/<scene>_<date-time>.mp4
        bool helpers = false; ///< the editor's furniture in the picture (§10.5)
        /// TEST INSTRUMENTS, each on the path a real one takes: "noEncoder" answers
        /// the encoder check with none, "encoderError" has the recorder emit
        /// errorOccurred, "slowSave" holds the fast-start step until cancelled.
        QString fault;
    };

    static constexpr int kWidth = 1920, kHeight = 1080, kFps = 60;
    static constexpr int kBitRate = 14000000;   // §10.4: ~12-16 Mbps
    /// Frames the recording view renders before the first recorded one: its own
    /// young histories (the gather's pixel history, the ray tier's young-view
    /// fade, the exposure's first meter) settle in them — a quarter second of
    /// latency after the click, never a settle loop inside a frame.
    static constexpr int kWarmFrames = 16;

    VideoRecorder(IEditorViewport *viewport, std::function<QString()> sceneName,
                  QObject *parent = nullptr);
    ~VideoRecorder() override;

    bool start(const Options &options, QString *why);
    /// Ends the recording: drains the readback, closes the view, finishes the
    /// file asynchronously (state Finishing -> Done | Failed). False when
    /// nothing is recording.
    bool stop(QString *why = nullptr);
    /// Waits (a nested event loop, the house's video.step pattern) until the
    /// recorder is not Recording/Finishing. A script and shutdown tool.
    bool waitFinished(int timeoutMs);
    bool recording() const { return mState == State::Recording; }
    bool busy() const { return mState == State::Recording || mState == State::Finishing; }
    State state() const { return mState; }
    QVariantMap status() const;
    /// The last finished file (Done), or empty.
    QString lastPath() const { return mLastPath; }
    QString lastError() const { return mError; }
    /// Seconds of VIDEO recorded so far (frames / 60) — what the button shows.
    double elapsedSeconds() const { return double(mNextSlot) / kFps; }
    /// The last frame handed to the encoder, as RGB (BT.709 limited range back
    /// to the display codes) — a test's and a thumbnail's view of the picture.
    bool lastFrameImage(class QImage &out) const;
    /// The Esc route: stops a running recording, true when it did.
    bool escape();
    /// QUIT WHILE SAVING: stops the fast-start worker and publishes the encoder's
    /// own complete file at the final path (status warning "saved without
    /// fast-start"). The destructor runs it; capture.abandon is the test door.
    bool abandonSave();

    static QString stateName(State s);

signals:
    void stateChanged();
    /// The whole seconds of recorded video moved (what the button shows).
    void elapsedChanged();
    /// Once per finished recording: ok with the file, or the failure's message.
    void finished(bool ok, const QString &path, const QString &error);

private:
    void onFrame(quint64 step);
    void deliver(jahshaka::engine::VideoFrameNv12 &frame);
    void enqueue(const std::vector<unsigned char> &nv12, quint64 slot);
    void flush();
    void finishEncoder();
    void onRecorderState();
    void startFastStart();
    void fail(const QString &message);
    void cancelQuietly();
    bool publish(const QString &fastStartError, bool notify);
    void teardownView();
    void teardownEncoder(bool now = false);
    void setState(State s);
    QString defaultPath() const;

    IEditorViewport *mViewport;
    std::function<QString()> mSceneName;
    State mState = State::Idle;
    Options mOptions;
    QString mFinalPath, mRawPath, mPartialPath, mLastPath, mError, mWarning;

    jahshaka::engine::View *mView = nullptr;
    int mWarm = 0;
    bool mArmedAny = false;
    quint64 mLastArmedStep = 0;
    quint64 mArmed = 0;
    bool mHaveStep0 = false;
    quint64 mStep0 = 0, mNextSlot = 0, mHeld = 0, mEncoderDropped = 0, mSent = 0;
    jahshaka::engine::VideoFrameNv12 mFrame;        ///< the poll's buffer
    std::vector<unsigned char> mLast;               ///< the last frame enqueued (holds)

    std::unique_ptr<QMediaCaptureSession> mSession;
    std::unique_ptr<QMediaRecorder> mRecorder;
    std::unique_ptr<QVideoFrameInput> mInput;
    QVideoFrameFormat mFormat;
    std::deque<QVideoFrame> mQueue;
    bool mEndPending = false, mEndSent = false;
    QString mEncoderName;
    bool mEncoderHardware = false;

    QFutureWatcher<QString> mFastStart;
    std::shared_ptr<std::atomic<bool>> mCancel;
    QElapsedTimer mWall;
    double mStartMs = 0.0;
    bool mFastStartRunning = false;
    bool mFaultSent = false;
    qint64 mStopWallMs = 0;
};

#endif // VIDEORECORDER_H
