/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "modules/capture/videorecorder.h"

#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QLoggingCategory>
#include <QMediaCaptureSession>
#include <QMediaFormat>
#include <QMediaRecorder>
#include <QMutex>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVideoFrameInput>
#include <QtConcurrent/QtConcurrentRun>

#include <cstring>

#include "jahshaka/engine/Engine.h"
#include "modules/capture/mp4faststart.h"
#include "services/livevideo.h"
#include "services/filewriteatomic.h"
#include "viewport/ieditorviewport.h"

namespace {

// ---- WHICH ENCODER QT PICKED --------------------------------------------------
//
// Qt Multimedia has no API that names the codec implementation it opened; its
// FFmpeg backend SAYS it, once, in the debug category below ("found hw encoder
// \"h264_nvenc\"" / "found sw encoder \"libx264\""), and the status a recorder
// shows must be that answer, not a guess. So while a recording opens, that one
// category's debug output is switched on (a chained category filter — the
// category's own object, nothing global), the one line is read by a chained
// message handler, and the category goes quiet again: it also prints four lines
// a FRAME, which never reach the session log or stderr.
constexpr const char *kEncoderCategory = "qt.multimedia.ffmpeg.videoencoder";

struct EncoderSpy {
    QMutex mutex;
    QString name;
    bool hardware = false;
    std::atomic<bool> listening { false };
    QLoggingCategory *category = nullptr;
    QtMessageHandler previousHandler = nullptr;
    QLoggingCategory::CategoryFilter previousFilter = nullptr;
    bool installed = false;
};
EncoderSpy &spy()
{
    static EncoderSpy s;
    return s;
}

void spyFilter(QLoggingCategory *cat)
{
    EncoderSpy &s = spy();
    if (s.previousFilter) s.previousFilter(cat);
    if (qstrcmp(cat->categoryName(), kEncoderCategory) == 0) {
        s.category = cat;
        cat->setEnabled(QtDebugMsg, s.listening.load());
    }
}

void spyHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    EncoderSpy &s = spy();
    if (ctx.category && qstrcmp(ctx.category, kEncoderCategory) == 0 && type == QtDebugMsg) {
        // "found hw encoder \"h264_nvenc\" for id 27"
        static const QRegularExpression re(QStringLiteral("found (hw|sw) encoder \"([^\"]+)\""));
        const QRegularExpressionMatch m = re.match(msg);
        if (m.hasMatch()) {
            QMutexLocker lock(&s.mutex);
            s.name = m.captured(2);
            s.hardware = m.captured(1) == QLatin1String("hw");
        }
        return;   // the category's debug lines are ours alone
    }
    if (s.previousHandler) s.previousHandler(type, ctx, msg);
    else qt_message_output(type, ctx, msg);
}

void spyListen(bool on)
{
    EncoderSpy &s = spy();
    if (on && !s.installed) {
        s.installed = true;
        s.previousHandler = qInstallMessageHandler(spyHandler);
        s.previousFilter = QLoggingCategory::installFilter(spyFilter);
    }
    if (on) {
        QMutexLocker lock(&s.mutex);
        s.name.clear();
        s.hardware = false;
    }
    s.listening.store(on);
    if (s.category) s.category->setEnabled(QtDebugMsg, on);
}

QString spyName(bool *hardware)
{
    EncoderSpy &s = spy();
    QMutexLocker lock(&s.mutex);
    if (hardware) *hardware = s.hardware;
    return s.name;
}

/// NV12 (BT.709 limited) back to RGB display codes — the exact inverse matrix.
void nv12ToRgb(const unsigned char *nv12, int w, int h, QImage &out)
{
    out = QImage(w, h, QImage::Format_RGB888);
    const unsigned char *Y = nv12;
    const unsigned char *C = nv12 + size_t(w) * h;
    for (int y = 0; y < h; ++y) {
        unsigned char *o = out.scanLine(y);
        for (int x = 0; x < w; ++x) {
            const double yy = (Y[size_t(y) * w + x] - 16.0) / 219.0;
            const unsigned char *c = C + size_t(y / 2) * w + size_t(x / 2) * 2;
            const double cb = (c[0] - 128.0) / 224.0, cr = (c[1] - 128.0) / 224.0;
            const double r = yy + 1.5748 * cr;
            const double b = yy + 1.8556 * cb;
            const double g = (yy - 0.2126 * r - 0.0722 * b) / 0.7152;
            const auto q = [](double v) { return (unsigned char)(qBound(0.0, v, 1.0) * 255.0 + 0.5); };
            o[3 * x] = q(r);
            o[3 * x + 1] = q(g);
            o[3 * x + 2] = q(b);
        }
    }
}

/// A recording's leftovers can be gigabytes: unlinking one is disk work, so it
/// runs on the pool (the shell's quit waits for the pool), never on the UI thread.
void removeOffThread(const QStringList &paths)
{
    QStringList live;
    for (const QString &p : paths) if (!p.isEmpty()) live << p;
    if (live.isEmpty()) return;
    (void)QtConcurrent::run([live]() { for (const QString &p : live) QFile::remove(p); });
}

}   // namespace

VideoRecorder::VideoRecorder(IEditorViewport *viewport, std::function<QString()> sceneName,
                             QObject *parent)
    : QObject(parent), mViewport(viewport), mSceneName(std::move(sceneName))
{
    connect(&mFastStart, &QFutureWatcher<QString>::finished, this, [this]() {
        if (mState == State::Finishing && mFastStartRunning) publish(mFastStart.result(), true);
    });
}

bool VideoRecorder::publish(const QString &fastStartError, bool notify)
{
    mFastStartRunning = false;
    QString renameError;
    if (fastStartError.isEmpty()) {
        // ONE rename(2) THAT REPLACES (RENAME-ATOMIC-1): the destination is
        // nothing until the instant it is the whole, playable file.
        if (!FileWrite::atomicRename(mPartialPath, mFinalPath, &renameError)) {
            if (notify) fail(QStringLiteral("the recording could not be moved into place: %1").arg(renameError));
            return false;
        }
        removeOffThread({ mRawPath });
    } else {
        // The encoder's own file is complete and plays; only the index is at
        // the end. Published as it is, and said so — never a lost recording.
        removeOffThread({ mPartialPath });
        if (!FileWrite::atomicRename(mRawPath, mFinalPath, &renameError)) {
            if (notify) fail(QStringLiteral("the recording could not be moved into place: %1").arg(renameError));
            return false;
        }
        mWarning = QStringLiteral("saved without fast-start (%1)").arg(fastStartError);
    }
    mLastPath = mFinalPath;
    if (!notify) { mState = State::Done; return true; }
    setState(State::Done);
    emit finished(true, mFinalPath, QString());
    return true;
}

bool VideoRecorder::abandonSave()
{
    // WHAT QUITTING DOES TO A RECORDING STILL BEING SAVED (the fix round's
    // defect 1): the fast-start worker is stopped and the ENCODER'S OWN file —
    // complete, playable, its index at the end — is published at the final
    // path, said in the status's warning. Never a hidden raw file and no file.
    if (mState != State::Finishing || !mFastStartRunning) return false;
    if (mCancel) mCancel->store(true);
    mFastStart.waitForFinished();
    const QString error = mFastStart.result();
    const bool ok = publish(error.isEmpty() ? error : QStringLiteral("the app quit while it ran"), false);
    if (ok) qWarning("capture: %s — %s", qPrintable(mFinalPath), qPrintable(mWarning.isEmpty() ? QStringLiteral("saved") : mWarning));
    emit stateChanged();
    return ok;
}

VideoRecorder::~VideoRecorder()
{
    abandonSave();
    if (mCancel) mCancel->store(true);
    mFastStart.waitForFinished();
    teardownView();
    teardownEncoder(true);
}

double VideoRecorder::warmEncoderProbe()
{
    QElapsedTimer t;
    t.start();
    (void)QMediaFormat(QMediaFormat::MPEG4).supportedVideoCodecs(QMediaFormat::Encode);
    return double(t.nsecsElapsed()) / 1e6;
}

QString VideoRecorder::modeName(Mode m)
{
    return m == Mode::Offline ? QStringLiteral("offline") : QStringLiteral("realtime");
}

bool VideoRecorder::parseMode(const QString &name, Mode *out)
{
    if (name == QLatin1String("realtime")) { if (out) *out = Mode::Realtime; return true; }
    if (name == QLatin1String("offline")) { if (out) *out = Mode::Offline; return true; }
    return false;
}

double VideoRecorder::wallSeconds() const
{
    if (!mWall.isValid()) return 0.0;
    if (mState != State::Recording && mStopWallMs > 0) return double(mStopWallMs) / 1000.0;
    return double(mWall.elapsed()) / 1000.0;
}

QString VideoRecorder::stateName(State s)
{
    switch (s) {
    case State::Idle: return QStringLiteral("idle");
    case State::Recording: return QStringLiteral("recording");
    case State::Finishing: return QStringLiteral("finishing");
    case State::Done: return QStringLiteral("done");
    case State::Failed: return QStringLiteral("failed");
    }
    return QStringLiteral("?");
}

void VideoRecorder::setState(State s)
{
    if (mState == s) return;
    mState = s;
    emit stateChanged();
}

QString VideoRecorder::defaultPath() const
{
    // §10.1: ~/Videos/Jahshaka/<scene>_<date-time>.mp4, no dialog.
    QString base = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (base.isEmpty()) base = QDir::homePath() + QStringLiteral("/Videos");
    QString scene = mSceneName ? mSceneName() : QString();
    scene.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9 _-]")), QStringLiteral("_"));
    scene = scene.trimmed();
    if (scene.isEmpty()) scene = QStringLiteral("Scene");
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss"));
    const QString dir = base + QStringLiteral("/Jahshaka");
    QString path = QStringLiteral("%1/%2_%3.mp4").arg(dir, scene, stamp);
    for (int n = 2; QFileInfo::exists(path); ++n)
        path = QStringLiteral("%1/%2_%3_%4.mp4").arg(dir, scene, stamp).arg(n);
    return path;
}

bool VideoRecorder::start(const Options &options, QString *why)
{
    const auto refuse = [why](const QString &reason) {
        if (why) *why = reason;
        return false;
    };
    QElapsedTimer clickTimer;
    clickTimer.start();
    if (mState == State::Recording) return refuse(QStringLiteral("a recording is already running"));
    if (mState == State::Finishing) return refuse(QStringLiteral("the last recording is still being saved"));
    if (!mViewport) return refuse(QStringLiteral("there is no editor viewport"));
    mOptions = options;
    mError.clear();
    mWarning.clear();

    // ---- the file: a folder that can really be written, checked FIRST --------
    QString path = options.path.isEmpty() ? defaultPath() : QFileInfo(options.path).absoluteFilePath();
    if (!path.endsWith(QStringLiteral(".mp4"), Qt::CaseInsensitive)) path += QStringLiteral(".mp4");
    const QFileInfo fi(path);
    const QString dir = fi.absolutePath();
    if (!QDir().mkpath(dir))
        return refuse(QStringLiteral("the folder %1 cannot be created").arg(dir));
    {
        QTemporaryFile probe(dir + QStringLiteral("/.jahshaka-write-check-XXXXXX"));
        if (!probe.open()) return refuse(QStringLiteral("the folder %1 cannot be written").arg(dir));
    }
    mFinalPath = path;
    mRawPath = dir + QStringLiteral("/.") + fi.completeBaseName() + QStringLiteral(".recording.mp4");
    mPartialPath = path + QStringLiteral(".partial");
    removeOffThread({ mRawPath });

    // ---- the encoder: H.264 into MPEG-4, or no recording at all --------------
    QMediaFormat format(QMediaFormat::MPEG4);
    // QT'S ENCODER PROBE: its FIRST call in a process enumerates the encoders
    // (~0.7 s, measured in VIDEO-REC-2 — the "585 ms first click" of
    // VIDEO-REC-1); the startup shader gate makes that call behind the splash
    // (warmEncoderProbe), so here it is a lookup. Timed: `probeMs`.
    QElapsedTimer probeTimer;
    probeTimer.start();
    const bool haveH264 = options.fault != QLatin1String("noEncoder") &&
                          format.supportedVideoCodecs(QMediaFormat::Encode).contains(QMediaFormat::VideoCodec::H264);
    mProbeMs = double(probeTimer.nsecsElapsed()) / 1e6;
    if (!haveH264)
        return refuse(QStringLiteral("no H.264 video encoder is available on this machine "
                                     "(Qt Multimedia's FFmpeg backend offers none)"));

    // ---- the picture: the separate 1080p view (§10.2) -------------------------
    IEditorViewport::RecordingHooks hooks;
    hooks.frame = [this](quint64 step) { return onFrame(step); };
    hooks.ends = [this]() { stop(); };
    hooks.escape = [this]() { return escape(); };
    hooks.offline = options.mode == Mode::Offline;
    if (hooks.offline) hooks.hold = [this]() { return holdTick(); };
    QString viewWhy;
    mView = mViewport->beginRecordingView(kWidth, kHeight, options.helpers, std::move(hooks), &viewWhy);
    if (!mView) return refuse(viewWhy.isEmpty() ? QStringLiteral("the recording view could not be made") : viewWhy);

    // ---- the session --------------------------------------------------------
    format.setVideoCodec(QMediaFormat::VideoCodec::H264);
    format.setAudioCodec(QMediaFormat::AudioCodec::Unspecified);
    mSession = std::make_unique<QMediaCaptureSession>();
    mRecorder = std::make_unique<QMediaRecorder>();
    // THE FORMAT-LESS CONSTRUCTOR (§9): an input built with a format never
    // accepts a frame at Qt 6.10; the first frame sets it.
    mInput = std::make_unique<QVideoFrameInput>();
    mSession->setRecorder(mRecorder.get());
    mSession->setVideoFrameInput(mInput.get());
    mRecorder->setMediaFormat(format);
    mRecorder->setVideoResolution(kWidth, kHeight);
    mRecorder->setVideoFrameRate(kFps);
    mRecorder->setEncodingMode(QMediaRecorder::AverageBitRateEncoding);
    mRecorder->setVideoBitRate(kBitRate);
    mRecorder->setOutputLocation(QUrl::fromLocalFile(mRawPath));
    connect(mRecorder.get(), &QMediaRecorder::errorOccurred, this,
            [this](QMediaRecorder::Error, const QString &message) {
                fail(QStringLiteral("the encoder failed: %1").arg(message));
            });
    connect(mRecorder.get(), &QMediaRecorder::recorderStateChanged, this, [this]() { onRecorderState(); });
    connect(mInput.get(), &QVideoFrameInput::readyToSendVideoFrame, this, [this]() { flush(); });

    mFormat = QVideoFrameFormat(QSize(kWidth, kHeight), QVideoFrameFormat::Format_NV12);
    mFormat.setStreamFrameRate(kFps);
    // What the pass wrote, stated to the encoder (Types.h VideoFrameNv12).
    mFormat.setColorSpace(QVideoFrameFormat::ColorSpace_BT709);
    mFormat.setColorTransfer(QVideoFrameFormat::ColorTransfer_BT709);
    mFormat.setColorRange(QVideoFrameFormat::ColorRange_Video);

    mWarm = kWarmFrames;
    mArmedAny = false;
    mArmed = 0;
    mHaveStep0 = false;
    mStep0 = mNextSlot = mHeld = mEncoderDropped = mSent = 0;
    mOfflineSerial = mHeldTicks = mRingWaits = 0;
    mWorstRingWaitMs = 0.0;
    mLast.clear();
    mQueue.clear();
    mEndPending = mEndSent = false;
    mEncoderName.clear();
    mEncoderHardware = false;
    mFaultSent = false;
    mFastStartRunning = false;
    mSlowEncoderSince.invalidate();
    mSlowEncoderRetry = false;
    // THE VIDEO TEXTURES LEAVE THE WALL CLOCK (offline): each playing clip is
    // paused and advanced one 1/60 s per recorded frame (LiveVideo's stepped
    // clock), or a 20 fps editor would show them at 3x in the clip.
    mVideoStepped = options.mode == Mode::Offline;
    if (mVideoStepped) LiveVideo::beginStepped();
    mWall.start();
    mStopWallMs = 0;
    spyListen(true);
    setState(State::Recording);
    mRecorder->record();
    // THE CLICK'S OWN COST on the UI thread (the view, the session, the encoder's open).
    mStartMs = double(clickTimer.nsecsElapsed()) / 1e6;
    return true;
}

bool VideoRecorder::onFrame(quint64 step)
{
    if (!mView) return false;
    // What finished since the last frame, oldest first — a poll, never a wait.
    // ("skipPoll", a test instrument, leaves the ring to fill.)
    if (mOptions.fault != QLatin1String("skipPoll"))
        while (mView && mView->takeVideoFrame(mFrame, false)) deliver(mFrame);
    if (mState != State::Recording || !mView) return false;
    // THE WARM-UP DRAWS (the view's young histories settle) but records nothing.
    if (mWarm > 0) { --mWarm; return true; }
    if (mOptions.mode == Mode::Offline) {
        // OFFLINE: THIS FRAME IS A VIDEO FRAME, whatever the clock did (the host
        // handed it exactly one step; a paused scene is a still picture the user
        // asked for). Tagged with the recording's own serial, so the slots run
        // 0, 1, 2 ... with no gap and nothing is ever held.
        //
        // NEVER DROPPED: a frame armed while all three tickets are pending would
        // not be read back, so the oldest is TAKEN first, waiting for its copy.
        // That wait is free in practice: the oldest ticket was recorded three
        // frames ago, and Ogre waits for frame N-3's fence before it records
        // frame N anyway (OgreVideoReadback.cpp) — the UI thread waits here
        // instead of a moment later, for at most one GPU frame. Measured and
        // reported (ringWaits, worstRingWaitMs).
        const jahshaka::engine::VideoReadbackStatus rb = mView->videoReadbackStatus();
        if (rb.ringSize > 0 && rb.pending >= rb.ringSize) {
            QElapsedTimer waited;
            waited.start();
            if (mView->takeVideoFrame(mFrame, true)) deliver(mFrame);
            const double ms = double(waited.nsecsElapsed()) / 1e6;
            ++mRingWaits;
            if (ms > mWorstRingWaitMs) mWorstRingWaitMs = ms;
            if (mState != State::Recording || !mView) return false;
        }
        mView->armVideoFrame(mOfflineSerial++);
        mArmedAny = true;
        ++mArmed;
        // ...and the video textures step to the NEXT recorded frame's time (the
        // next frame is held until their frames have landed: holdTick).
        if (mVideoStepped) LiveVideo::advanceStepped(1000000 / kFps);
        return true;
    }
    // ONE FRAME PER GRID STEP: a frame that bought no step (the scene paused, a
    // fast panel's odd frame) shows an instant the file already holds — and
    // the view does not draw it.
    if (mArmedAny && step == mLastArmedStep) return false;
    mView->armVideoFrame(step);
    mArmedAny = true;
    mLastArmedStep = step;
    ++mArmed;
    return true;
}

void VideoRecorder::deliver(jahshaka::engine::VideoFrameNv12 &frame)
{
    if (mState != State::Recording && mState != State::Finishing) return;
    if (frame.width != unsigned(kWidth) || frame.height != unsigned(kHeight)) return;
    if (!mHaveStep0) { mHaveStep0 = true; mStep0 = frame.tag; }
    if (frame.tag < mStep0) return;
    const quint64 slot = frame.tag - mStep0;
    if (slot < mNextSlot) return;
    // THE HELD FRAMES: the steps this picture's frame spanned (a slow frame, or
    // a frame the readback ring had to drop) show the previous picture, so the
    // file stays a constant 60 and plays at the speed the scene ran.
    while (mNextSlot < slot && !mLast.empty()) {
        enqueue(mLast, mNextSlot++);
        ++mHeld;
    }
    mNextSlot = slot;
    mLast.swap(frame.nv12);
    const quint64 secondsBefore = mNextSlot / kFps;
    enqueue(mLast, mNextSlot++);
    if (mNextSlot / kFps != secondsBefore) emit elapsedChanged();
}

void VideoRecorder::enqueue(const std::vector<unsigned char> &nv12, quint64 slot)
{
    // REAL-TIME: bounded at kQueueBound (~75 MB) of an encoder that has fallen
    // behind. Past it a frame is dropped and counted — the file then holds the
    // previous picture a frame longer, never stalls the editor.
    // OFFLINE: never dropped. The bound is enforced UPSTREAM instead — the
    // driver's ticks are held while the queue is at it (holdTick) — so only a
    // script's own editor.frame(n) can carry it past, by at most its n.
    if (mOptions.mode == Mode::Realtime && mQueue.size() >= kQueueBound) { ++mEncoderDropped; return; }
    QVideoFrame f(mFormat);
    if (!f.map(QVideoFrame::WriteOnly)) { ++mEncoderDropped; return; }
    const size_t w = kWidth, h = kHeight;
    const unsigned char *src = nv12.data();
    for (size_t y = 0; y < h; ++y)
        std::memcpy(f.bits(0) + y * size_t(f.bytesPerLine(0)), src + y * w, w);
    for (size_t y = 0; y < h / 2; ++y)
        std::memcpy(f.bits(1) + y * size_t(f.bytesPerLine(1)), src + w * h + y * w, w);
    f.unmap();
    // THE SCENE'S CLOCK: slot n is grid step n of the recording.
    // Microseconds ROUNDED UP: a 1/60 s step is no whole number of them, and the
    // encoder's 1/60000 time base then lands every frame exactly on its step.
    f.setStartTime((qint64(slot) * 1000000 + kFps - 1) / kFps);
    f.setEndTime((qint64(slot + 1) * 1000000 + kFps - 1) / kFps);
    mQueue.push_back(std::move(f));
    flush();
}

void VideoRecorder::flush()
{
    if (!mRecorder || !mInput) return;
    if (mRecorder->recorderState() != QMediaRecorder::RecordingState) return;
    while (!mQueue.empty()) {
        // THE "slowEncoder" INSTRUMENT: the input takes one frame per 50 ms —
        // an encoder that has fallen behind, on the path a real one takes.
        if (mOptions.fault == QLatin1String("slowEncoder")) {
            if (mSlowEncoderSince.isValid() && mSlowEncoderSince.elapsed() < 50) {
                if (!mSlowEncoderRetry) {
                    mSlowEncoderRetry = true;
                    QTimer::singleShot(50, this, [this]() { mSlowEncoderRetry = false; flush(); });
                }
                return;
            }
        }
        if (!mInput->sendVideoFrame(mQueue.front())) return;   // readyToSendVideoFrame resumes it
        mSlowEncoderSince.start();
        mQueue.pop_front();
        ++mSent;
    }
    // THE INJECTED ENCODER ERROR (capture.start fault "encoderError"): the very
    // path a real errorOccurred takes, once the encoder has a frame.
    if (mOptions.fault == QLatin1String("encoderError") && mSent > 0 && mState == State::Recording &&
        !mFaultSent) {
        mFaultSent = true;
        // EMITTED BY THE RECORDER, queued: the handler runs exactly as for a real
        // error — inside the recorder's own emission.
        QMediaRecorder *r = mRecorder.get();
        QMetaObject::invokeMethod(r, [r]() {
            emit r->errorOccurred(QMediaRecorder::ResourceError,
                                  QStringLiteral("a fault injected by capture.start({fault: \"encoderError\"})"));
        }, Qt::QueuedConnection);
    }
    if (mEncoderName.isEmpty() && mSent > 0) {
        mEncoderName = spyName(&mEncoderHardware);
        if (!mEncoderName.isEmpty()) spyListen(false);
    }
    if (mEndPending && !mEndSent) finishEncoder();
}

void VideoRecorder::finishEncoder()
{
    mEndSent = true;
    if (mEncoderName.isEmpty()) mEncoderName = spyName(&mEncoderHardware);
    spyListen(false);
    mRecorder->stop();
}

void VideoRecorder::onRecorderState()
{
    if (!mRecorder) return;
    const QMediaRecorder::RecorderState st = mRecorder->recorderState();
    if (st == QMediaRecorder::RecordingState) { flush(); return; }
    if (st != QMediaRecorder::StoppedState) return;
    if (mState == State::Recording) {
        fail(QStringLiteral("the encoder stopped by itself%1")
                 .arg(mRecorder->errorString().isEmpty() ? QString()
                                                         : QStringLiteral(": ") + mRecorder->errorString()));
        return;
    }
    if (mState == State::Finishing && mEndSent) startFastStart();
}

void VideoRecorder::startFastStart()
{
    const QString actual = mRecorder ? mRecorder->actualLocation().toLocalFile() : QString();
    if (!actual.isEmpty()) mRawPath = actual;
    teardownEncoder();
    if (!QFileInfo::exists(mRawPath)) {
        fail(QStringLiteral("the encoder wrote no file"));
        return;
    }
    mCancel = std::make_shared<std::atomic<bool>>(false);
    const QString raw = mRawPath, partial = mPartialPath;
    const std::shared_ptr<std::atomic<bool>> cancel = mCancel;
    // A TEST INSTRUMENT (`fault: "slowSave"`): the save holds until it is
    // cancelled — what a multi-GB file looks like when the app quits.
    const bool slow = mOptions.fault == QLatin1String("slowSave");
    mFastStartRunning = true;
    // On a WORKER: the media is streamed through once, which for a long
    // recording is seconds of disk.
    mFastStart.setFuture(QtConcurrent::run([raw, partial, cancel, slow]() -> QString {
        for (int i = 0; slow && !cancel->load() && i < 6000; ++i) QThread::msleep(10);
        if (slow) return QStringLiteral("cancelled");
        QString error;
        if (!mp4::fastStart(raw, partial, &error, cancel.get())) {
            QFile::remove(partial);
            return error.isEmpty() ? QStringLiteral("fast-start failed") : error;
        }
        FileWrite::fsyncPath(partial);
        return QString();
    }));
}

bool VideoRecorder::holdTick()
{
    if (mState != State::Recording || mOptions.mode != Mode::Offline) return false;
    // Whatever Qt's input will take now goes first: a hold is only for a queue
    // the encoder really cannot accept yet.
    flush();
    // ...and a video texture whose stepped frame has not landed yet (bounded
    // per frame inside LiveVideo).
    if (mQueue.size() < kQueueBound && !(mVideoStepped && LiveVideo::steppedPending())) return false;
    ++mHeldTicks;
    return true;
}

bool VideoRecorder::stop(QString *why)
{
    if (mState != State::Recording) {
        if (why) *why = QStringLiteral("nothing is recording");
        return false;
    }
    setState(State::Finishing);
    mStopWallMs = mWall.elapsed();
    // THE DRAIN: what the GPU already holds of this recording (at most three
    // frames, each waited on the fence of the frame that recorded it).
    if (mView) {
        while (mView->takeVideoFrame(mFrame, true)) deliver(mFrame);
    }
    teardownView();
    if (mNextSlot == 0) {
        // STOPPED DURING THE WARM-UP (a second click within a quarter second):
        // nothing was recorded and nothing went wrong — a quiet cancel.
        cancelQuietly();
        return true;
    }
    mEndPending = true;
    flush();
    return true;
}

bool VideoRecorder::escape()
{
    if (mState != State::Recording) return false;
    stop();
    return true;
}

void VideoRecorder::cancelQuietly()
{
    spyListen(false);
    teardownView();
    teardownEncoder();
    removeOffThread({ mRawPath, mPartialPath });
    mError.clear();
    mWarning = QStringLiteral("cancelled before the first frame");
    setState(State::Idle);
}

void VideoRecorder::fail(const QString &message)
{
    // A late error of a recording that already ended changes nothing.
    if (mState == State::Failed || mState == State::Idle || mState == State::Done) return;
    mError = message;
    spyListen(false);
    if (mView) {
        // The frames in flight are the device's: waited out with the view.
        jahshaka::engine::VideoFrameNv12 drop;
        while (mView->takeVideoFrame(drop, true)) {}
    }
    teardownView();
    teardownEncoder();
    if (mCancel) mCancel->store(true);
    mFastStartRunning = false;
    removeOffThread({ mRawPath, mPartialPath });
    setState(State::Failed);
    emit finished(false, QString(), mError);
}

void VideoRecorder::teardownView()
{
    // The video textures go back to the wall clock with the recording's view.
    if (mVideoStepped) { mVideoStepped = false; LiveVideo::endStepped(); }
    if (mView && mViewport) mViewport->endRecordingView();
    mView = nullptr;
}

void VideoRecorder::teardownEncoder(bool now)
{
    if (mRecorder) mRecorder->disconnect(this);
    if (mInput) mInput->disconnect(this);
    mQueue.clear();
    if (now) {
        // The session first (it holds the other two).
        mSession.reset();
        mInput.reset();
        mRecorder.reset();
        return;
    }
    // DEFERRED: this runs inside the recorder's own recorderStateChanged /
    // errorOccurred emission, and an object must not be deleted in its own
    // signal. Posted in order, so the session still goes first.
    if (mSession) mSession.release()->deleteLater();
    if (mInput) mInput.release()->deleteLater();
    if (mRecorder) mRecorder.release()->deleteLater();
}

bool VideoRecorder::waitFinished(int timeoutMs)
{
    if (!busy()) return true;
    QEventLoop loop;
    QTimer deadline;
    deadline.setSingleShot(true);
    connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    connect(this, &VideoRecorder::stateChanged, &loop, [this, &loop]() {
        if (!busy()) loop.quit();
    });
    deadline.start(timeoutMs > 0 ? timeoutMs : 30000);
    loop.exec();
    return !busy();
}

bool VideoRecorder::lastFrameImage(QImage &out) const
{
    if (mLast.size() != size_t(kWidth) * kHeight * 3 / 2) return false;
    nv12ToRgb(mLast.data(), kWidth, kHeight, out);
    return true;
}

QVariantMap VideoRecorder::status() const
{
    QVariantMap m;
    m["state"] = stateName(mState);
    m["recording"] = mState == State::Recording;
    m["path"] = (mState == State::Done) ? mLastPath : mFinalPath;
    m["frames"] = qulonglong(mNextSlot);
    m["armed"] = qulonglong(mArmed);
    m["held"] = qulonglong(mHeld);
    m["sent"] = qulonglong(mSent);
    m["queued"] = int(mQueue.size());
    m["encoderDropped"] = qulonglong(mEncoderDropped);
    m["elapsed"] = elapsedSeconds();
    // THE CLIP AGAINST THE WALL (VIDEO-REC-2): an offline recording renders every
    // frame, so its clip time runs slower than the wall's — both, side by side.
    m["mode"] = modeName(mOptions.mode);
    m["clipSeconds"] = elapsedSeconds();
    m["wallSeconds"] = wallSeconds();
    m["heldTicks"] = qulonglong(mHeldTicks);
    m["ringWaits"] = qulonglong(mRingWaits);
    m["worstRingWaitMs"] = mWorstRingWaitMs;
    m["warming"] = mState == State::Recording && mWarm > 0;
    m["startMs"] = mStartMs;
    m["probeMs"] = mProbeMs;
    const jahshaka::engine::VideoReadbackStatus rb =
        mView ? mView->videoReadbackStatus() : jahshaka::engine::VideoReadbackStatus();
    m["dropped"] = qulonglong(rb.dropped);
    m["inFlight"] = rb.pending;
    QString encoderName = mEncoderName;
    bool hardware = mEncoderHardware;
    if (encoderName.isEmpty() && mState == State::Recording) encoderName = spyName(&hardware);
    m["encoder"] = encoderName;
    m["hardwareEncoder"] = hardware;
    m["width"] = kWidth;
    m["height"] = kHeight;
    m["fps"] = kFps;
    m["bitRate"] = kBitRate;
    m["helpers"] = mOptions.helpers;
    if (!mError.isEmpty()) m["error"] = mError;
    if (!mWarning.isEmpty()) m["warning"] = mWarning;
    return m;
}
