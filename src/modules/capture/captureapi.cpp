/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "modules/capture/captureapi.h"

#include <QEventLoop>
#include <QFileInfo>
#include <QImage>
#include <QMediaPlayer>
#include <QTimer>
#include <QUrl>
#include <QVideoFrame>
#include <QVideoSink>

#include "modules/capture/capturemodule.h"
#include "modules/capture/mp4faststart.h"
#include "modules/capture/videorecorder.h"
#include "scripting/modules/moduleshared.h"

CaptureApi::CaptureApi(ScriptHost &host, CaptureModule *module) : ApiModule(host), mModule(module) {}

QVector<VerbInfo> CaptureApi::verbs() const
{
    return {
        { "start", "capture.start({path?, helpers?}) -> status | null",
          "STARTS A VIDEO RECORDING of the editor's camera (VIDEO_CAPTURE_SPEC §10): a SEPARATE "
          "1920x1080 render of this camera every frame, converted to NV12 on the GPU (BT.709, "
          "limited range) and encoded H.264 at ~14 Mbps by the machine's hardware encoder where "
          "Qt finds one; 60 fps on the SCENE's clock — one video frame per 1/60 s step the scene "
          "advanced, a slow editor frame HELD over the steps it spanned, never a sped-up file. "
          "`path` (.mp4) defaults to ~/Videos/Jahshaka/<scene>_<date-time>.mp4; `helpers` "
          "(default false: scene only, the photo rule) draws the editor's grid, gizmo, outline "
          "and icons into the picture. The first recorded frame comes after the view's warm-up "
          "(`warming` in the status). Null when it cannot start (no engine viewport, no world, "
          "no H.264 encoder, a folder that cannot be written), with the reason in app.lastError. "
          "`fault: \"noEncoder\" | \"encoderError\"` is a test instrument: the same failure path "
          "a real missing encoder or a mid-recording encoder error takes.",
          Needs::Engine },
        { "stop", "capture.stop({wait?, timeoutMs?}) -> status | null",
          "Ends the recording: the frames already on the GPU are drained, the recording view is "
          "closed, and the file is finished ASYNCHRONOUSLY — the encoder's own stop, then the "
          "fast-start step (the index moved before the media) on a worker and one atomic rename "
          "(state `finishing` -> `done`, or `failed`). `wait: true` waits for that here (a nested "
          "event loop, at most `timeoutMs`, default 30000). Null when nothing is recording.",
          Needs::Engine },
        { "status", "capture.status() -> {state, recording, path, frames, armed, held, sent, queued, "
          "dropped, encoderDropped, inFlight, elapsed, warming, encoder, hardwareEncoder, width, "
          "height, fps, bitRate, helpers, error?, warning?}",
          "The recorder now. `state` is idle | recording | finishing | done | failed; `frames` "
          "the video frames written (one per scene step, holds included), `held` how many were a "
          "previous picture held over a step, `dropped` frames the GPU readback ring had to skip "
          "(the recorder fell three frames behind), `encoderDropped` frames the encoder's queue "
          "refused; `elapsed` = frames / 60 s; `encoder` the codec implementation Qt opened "
          "(\"h264_nvenc\" here) and `hardwareEncoder` whether Qt called it a hardware one; "
          "`path` the file (the finished one once `done`).",
          Needs::Document },
        { "wait", "capture.wait(timeoutMs=30000) -> status",
          "Waits (a nested event loop) until the recorder is neither recording nor finishing, at "
          "most `timeoutMs`. A script's way to the finished file after capture.stop().",
          Needs::Engine },
        { "inspect", "capture.inspect(path, {decode?, timeoutMs?}) -> {ok, fastStart, moovOffset, "
          "mdatOffset, codec, width, height, timescale, duration, frames, keyframes, constantRate, "
          "sampleDelta, lastDelta, fps, presentationMs, decoded?:{frames, width, height, mean:[r,g,b]}}",
          "Reads an MP4's own boxes: where the index sits (`fastStart`: moov before mdat), and the "
          "video track's sample entry (codec, coded size), timescale, sample count (`frames`) and "
          "whether every sample but the last has one duration (`constantRate`, `fps` = timescale / "
          "delta; the last sample's duration, `lastDelta`, is reported beside it) and how long the "
          "movie says it plays (`presentationMs`, mvhd). A recorder file has its timing CLOSED by "
          "the fast-start step: N frames are N steps long in every box that states a duration. "
          "`decode: true` also plays it through Qt's decoder (at half speed, so no late frame is "
          "skipped) and counts the frames delivered, "
          "with the first frame's size and mean colour. A measuring instrument.",
          Needs::Document },
        { "lastFrame", "capture.lastFrame(path) -> bool",
          "Writes the last frame the recorder handed the encoder as a PNG — the NV12 picture "
          "turned back into display codes by the exact inverse matrix. False when no frame was "
          "recorded in this process.",
          Needs::Document },
        { "press", "capture.press() -> bool",
          "THE RECORD BUTTON, exactly as a click runs it: records (with the popup's helpers "
          "switch) when idle, stops when recording. A refusal shows the button's failure "
          "dialog. True when the click started or stopped a recording.",
          Needs::Window },
        { "button", "capture.button() -> {text, toolTip, enabled, red, recording, helpers, placed, "
          "visible, menu:[rows], menuOpen, failure, dialogOpen}",
          "What the record button shows: its text (\"REC m:ss\" while recording), whether its "
          "icon is the red dot, its popup's rows, and the last failure message with whether its "
          "dialog is open.",
          Needs::Window },
        { "helpers", "capture.helpers(on?) -> bool",
          "The record button's helpers switch (persisted `capture/helpers`): with no argument "
          "answers it, with one sets it.",
          Needs::Document },
        { "dismiss", "capture.dismiss() -> bool",
          "Closes the recording-failure dialog. False when none is open.",
          Needs::Window },
    };
}

QVariant CaptureApi::start(const QVariantMap &options)
{
    static const QStringList known = { QStringLiteral("path"), QStringLiteral("helpers"),
                                       QStringLiteral("fault") };
    const QString unknown = scriptmod::refuseUnknownKeys(QStringLiteral("capture.start"), options, known);
    if (!unknown.isEmpty()) { fail(unknown); return jsNull(); }
    VideoRecorder *r = mModule ? mModule->recorder() : nullptr;
    if (!r) { refuse(QStringLiteral("capture.start: recording needs the engine viewport")); return jsNull(); }
    VideoRecorder::Options o;
    o.path = options.value(QStringLiteral("path")).toString();
    o.helpers = options.value(QStringLiteral("helpers"), false).toBool();
    o.fault = options.value(QStringLiteral("fault")).toString();
    if (!o.fault.isEmpty() && o.fault != QLatin1String("noEncoder") && o.fault != QLatin1String("encoderError")) {
        fail(QStringLiteral("capture.start: fault must be \"noEncoder\" or \"encoderError\""));
        return jsNull();
    }
    QString why;
    if (!r->start(o, &why)) {
        refuse(QStringLiteral("capture.start: %1").arg(why));
        return jsNull();
    }
    return r->status();
}

QVariant CaptureApi::stop(const QVariantMap &options)
{
    static const QStringList known = { QStringLiteral("wait"), QStringLiteral("timeoutMs") };
    const QString unknown = scriptmod::refuseUnknownKeys(QStringLiteral("capture.stop"), options, known);
    if (!unknown.isEmpty()) { fail(unknown); return jsNull(); }
    VideoRecorder *r = mModule ? mModule->recorder() : nullptr;
    if (!r) { refuse(QStringLiteral("capture.stop: recording needs the engine viewport")); return jsNull(); }
    QString why;
    if (!r->stop(&why)) {
        refuse(QStringLiteral("capture.stop: %1").arg(why));
        return jsNull();
    }
    if (options.value(QStringLiteral("wait"), false).toBool())
        r->waitFinished(options.value(QStringLiteral("timeoutMs"), 30000).toInt());
    return r->status();
}

QVariantMap CaptureApi::status()
{
    VideoRecorder *r = mModule ? mModule->recorder() : nullptr;
    if (!r) {
        QVariantMap m;
        m["state"] = QStringLiteral("unavailable");
        m["recording"] = false;
        m["error"] = QStringLiteral("recording needs the engine viewport");
        return m;
    }
    return r->status();
}

QVariantMap CaptureApi::wait(int timeoutMs)
{
    VideoRecorder *r = mModule ? mModule->recorder() : nullptr;
    if (r) r->waitFinished(timeoutMs);
    return status();
}

QVariantMap CaptureApi::inspect(const QString &path, const QVariantMap &options)
{
    static const QStringList known = { QStringLiteral("decode"), QStringLiteral("timeoutMs") };
    const QString unknown = scriptmod::refuseUnknownKeys(QStringLiteral("capture.inspect"), options, known);
    if (!unknown.isEmpty()) { fail(unknown); return QVariantMap(); }
    QVariantMap out = mp4::inspect(path).toMap();
    if (!options.value(QStringLiteral("decode"), false).toBool()) return out;
    // THE DECODER'S OWN ANSWER: Qt plays the file into a sink, every frame counted.
    QMediaPlayer player;
    QVideoSink sink;
    player.setVideoOutput(&sink);
    int frames = 0;
    QSize size;
    double mean[3] = { 0, 0, 0 };
    QObject::connect(&sink, &QVideoSink::videoFrameChanged, &sink, [&](const QVideoFrame &f) {
        if (!f.isValid()) return;
        if (frames == 0) {
            size = f.size();
            const QImage img = f.toImage().convertToFormat(QImage::Format_RGB888);
            double sum[3] = { 0, 0, 0 };
            for (int y = 0; y < img.height(); ++y) {
                const uchar *row = img.constScanLine(y);
                for (int x = 0; x < img.width(); ++x)
                    for (int c = 0; c < 3; ++c) sum[c] += row[3 * x + c];
            }
            const double n = double(img.width()) * img.height();
            for (int c = 0; c < 3; ++c) mean[c] = n > 0 ? sum[c] / n : 0.0;
        }
        ++frames;
    });
    QEventLoop loop;
    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&player, &QMediaPlayer::mediaStatusChanged, &loop, [&loop](QMediaPlayer::MediaStatus s) {
        if (s == QMediaPlayer::EndOfMedia || s == QMediaPlayer::InvalidMedia) loop.quit();
    });
    QString error;
    QObject::connect(&player, &QMediaPlayer::errorOccurred, &loop,
                     [&loop, &error](QMediaPlayer::Error, const QString &s) { error = s; loop.quit(); });
    player.setSource(QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath()));
    // At half speed: a player keeps its clock by skipping late frames, and an
    // instrument that counts frames must not be the one under time pressure.
    player.setPlaybackRate(0.5);
    player.play();
    deadline.start(options.value(QStringLiteral("timeoutMs"), 20000).toInt());
    loop.exec();
    player.stop();
    QVariantMap d;
    d["frames"] = frames;
    d["width"] = size.width();
    d["height"] = size.height();
    d["mean"] = QVariantList{ mean[0], mean[1], mean[2] };
    if (!error.isEmpty()) d["error"] = error;
    d["timedOut"] = !deadline.isActive();
    out["decoded"] = d;
    return out;
}

bool CaptureApi::lastFrame(const QString &path)
{
    VideoRecorder *r = mModule ? mModule->recorder() : nullptr;
    QImage img;
    if (!r || !r->lastFrameImage(img)) return refuse(QStringLiteral("capture.lastFrame: no frame was recorded"));
    if (!img.save(path)) return refuse(QStringLiteral("capture.lastFrame: could not write %1").arg(path));
    return true;
}

bool CaptureApi::press()
{
    if (!mModule) return refuse(QStringLiteral("capture.press: the capture module is not wired"));
    return mModule->press();
}

QVariantMap CaptureApi::button()
{
    return mModule ? mModule->button() : QVariantMap();
}

bool CaptureApi::helpers(const QVariant &on)
{
    if (!mModule) return false;
    if (on.isValid() && !on.isNull()) mModule->setHelpersSwitch(on.toBool());
    return mModule->helpersSwitch();
}

bool CaptureApi::dismiss()
{
    return mModule && mModule->dismissFailure();
}
