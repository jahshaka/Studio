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
#include <QRect>
#include <QFileInfo>
#include <QImage>
#include <QMediaPlayer>
#include <QTimer>
#include <QUrl>
#include <QVideoFrame>
#include <QVideoSink>

#include <cmath>
#include <cstdlib>
#include <vector>

#include "modules/capture/capturemodule.h"
#include "modules/capture/mp4faststart.h"
#include "modules/capture/videorecorder.h"
#include "scripting/modules/moduleshared.h"

namespace {
/// A decoded frame's LUMA plane, row-packed: plane 0 of a YUV frame as it is;
/// any other format through Qt's own conversion (Rec. 709 weights on RGB).
bool lumaOf(const QVideoFrame &source, std::vector<unsigned char> &out)
{
    QVideoFrame f(source);
    const int w = f.width(), h = f.height();
    if (w <= 0 || h <= 0) return false;
    out.resize(size_t(w) * h);
    const QVideoFrameFormat::PixelFormat pf = f.pixelFormat();
    const bool yuv = pf == QVideoFrameFormat::Format_NV12 || pf == QVideoFrameFormat::Format_NV21 ||
                     pf == QVideoFrameFormat::Format_YUV420P || pf == QVideoFrameFormat::Format_YV12 ||
                     pf == QVideoFrameFormat::Format_P010 || pf == QVideoFrameFormat::Format_P016 ||
                     pf == QVideoFrameFormat::Format_YUV420P10;
    if (yuv && f.map(QVideoFrame::ReadOnly)) {
        const bool wide = pf == QVideoFrameFormat::Format_P010 || pf == QVideoFrameFormat::Format_P016 ||
                          pf == QVideoFrameFormat::Format_YUV420P10;
        const uchar *base = f.bits(0);
        const int stride = f.bytesPerLine(0);
        for (int y = 0; y < h; ++y) {
            const uchar *row = base + size_t(y) * stride;
            for (int x = 0; x < w; ++x)
                out[size_t(y) * w + x] = wide ? uchar(reinterpret_cast<const quint16 *>(row)[x] >> 8) : row[x];
        }
        f.unmap();
        return true;
    }
    const QImage img = f.toImage().convertToFormat(QImage::Format_RGB888);
    if (img.width() != w || img.height() != h) return false;
    for (int y = 0; y < h; ++y) {
        const uchar *row = img.constScanLine(y);
        for (int x = 0; x < w; ++x)
            out[size_t(y) * w + x] = uchar(qBound(0.0, 0.2126 * row[3 * x] + 0.7152 * row[3 * x + 1] +
                                                          0.0722 * row[3 * x + 2] + 0.5, 255.0));
    }
    return true;
}
}   // namespace

CaptureApi::CaptureApi(ScriptHost &host, CaptureModule *module) : ApiModule(host), mModule(module) {}

QVector<VerbInfo> CaptureApi::verbs() const
{
    return {
        { "start", "capture.start({path?, helpers?, mode?}) -> status | null",
          "STARTS A VIDEO RECORDING of the editor's camera (VIDEO_CAPTURE_SPEC §10): a SEPARATE "
          "1920x1080 render of this camera every frame, converted to NV12 on the GPU (BT.709, "
          "limited range) and encoded H.264 at ~14 Mbps by the machine's hardware encoder where "
          "Qt finds one; 60 fps on the SCENE's clock — one video frame per 1/60 s step the scene "
          "advanced, a slow editor frame HELD over the steps it spanned, never a sped-up file. "
          "`mode` (default \"realtime\", what that sentence describes) or \"offline\": PERFECT 60 — "
          "every render-loop frame (and every editor.frame) is one video frame and the scene clock is "
          "handed EXACTLY one 1/60 s step for it (animation, physics, particles, the shader clock, the "
          "recording view's temporal histories and the PLAYING VIDEO TEXTURES — decoded forward to "
          "the frame's time — step once per recorded frame, as a 60 fps run would show them), never "
          "the wall time and never a script's editor.frame dt; any other frame (a panel's refresh, a "
          "screenshot's settle) is outside the recording: dt 0, the recording view not drawn. Nothing "
          "is held or dropped — a full readback ring is waited out (one GPU frame at most), a full "
          "encoder queue or a video texture still decoding holds the render loop's ticks (an "
          "editor.frame waits) until it is ready. The editor runs as slowly as it must and stays "
          "interactive (on a display faster than 60 Hz its world runs FASTER than real time while "
          "recording); `clipSeconds` against `wallSeconds` in the status says how much slower. "
          "REFUSED while a VR session is live (the wearer would see the recording's pace), and a VR "
          "session that starts during one ENDS it (the file is kept). The VR rig and interaction "
          "clocks stay on the wall clock. "
          "`path` (.mp4) defaults to ~/Videos/Jahshaka/<scene>_<date-time>.mp4; `helpers` "
          "(default false: scene only, the photo rule) draws the editor's grid, gizmo, outline "
          "and icons into the picture. The first recorded frame comes after the view's warm-up "
          "(`warming` in the status). Null when it cannot start (no engine viewport, no world, "
          "no H.264 encoder, a folder that cannot be written), with the reason in app.lastError. "
          "`fault: \"noEncoder\" | \"encoderError\" | \"slowSave\" | \"slowEncoder\" | \"skipPoll\"` is a test instrument on the "
          "path the real event takes: no encoder; the recorder emitting errorOccurred mid-recording; "
          "a save that runs until it is cancelled (capture.abandon); `\"slowEncoder\"` an encoder input "
          "that takes one frame per 50 ms (the queue fills: the offline mode's hold runs); `\"skipPoll\"` "
          "a readback ring nobody polls (it fills: the offline mode's ring wait runs). A stop during the warm-up is a "
          "quiet cancel (state idle, warning \"cancelled before the first frame\").",
          Needs::Engine },
        { "stop", "capture.stop({wait?, timeoutMs?}) -> status | null",
          "Ends the recording: the frames already on the GPU are drained, the recording view is "
          "closed, and the file is finished ASYNCHRONOUSLY — the encoder's own stop, then the "
          "fast-start step (the index moved before the media) on a worker and one atomic rename "
          "(state `finishing` -> `done`, or `failed`). `wait: true` waits for that here (a nested "
          "event loop, at most `timeoutMs`, default 30000). Null when nothing is recording.",
          Needs::Engine },
        { "status", "capture.status() -> {state, recording, path, mode, frames, armed, held, sent, queued, "
          "dropped, encoderDropped, inFlight, elapsed, clipSeconds, wallSeconds, heldTicks, ringWaits, "
          "worstRingWaitMs, warming, startMs, probeMs, encoder, hardwareEncoder, width, height, fps, bitRate, "
          "helpers, error?, warning?}",
          "The recorder now. `state` is idle | recording | finishing | done | failed; `frames` "
          "the video frames written (one per scene step, holds included), `held` how many were a "
          "previous picture held over a step, `dropped` frames the GPU readback ring had to skip "
          "(the recorder fell three frames behind), `encoderDropped` frames the encoder's queue "
          "refused; `elapsed` = frames / 60 s; `encoder` the codec implementation Qt opened "
          "(\"h264_nvenc\" here) and `hardwareEncoder` whether Qt called it a hardware one; "
          "`path` the file (the finished one once `done`). `mode` is realtime | offline; "
          "`clipSeconds` (= `elapsed`) is the video recorded and `wallSeconds` the wall time since "
          "the start (frozen at the stop) — an offline recording's clip runs slower than the wall; "
          "`startMs` is the start's own UI-thread time and `probeMs` the part of it Qt's encoder probe took "
          "(the startup gate makes the process's first, ~0.7 s, probe behind the splash). "
          "`heldTicks` counts the frames an offline recording held back (a render-loop tick held, or an "
          "editor.frame that waited) for its encoder or a video texture's stepped frame, `ringWaits` "
          "the frames it waited for a readback ticket and `worstRingWaitMs` the longest such wait.",
          Needs::Document },
        { "wait", "capture.wait(timeoutMs=30000) -> status",
          "Waits (a nested event loop) until the recorder is neither recording nor finishing, at "
          "most `timeoutMs`. A script's way to the finished file after capture.stop().",
          Needs::Engine },
        { "abandon", "capture.abandon() -> status",
          "WHAT QUITTING DOES TO A RECORDING STILL BEING SAVED (the module's shutdown and the "
          "recorder's destructor run it): the fast-start step is stopped and the encoder's own "
          "complete file is published at the final path — playable, its index at the end — with "
          "the status's `warning` saying so. A no-op unless the state is `finishing` with the "
          "fast-start step running.",
          Needs::Engine },
        { "inspect", "capture.inspect(path, {decode?, perFrame?, timeoutMs?}) -> {ok, fastStart, moovOffset, "
          "mdatOffset, codec, width, height, timescale, duration, frames, keyframes, constantRate, "
          "sampleDelta, lastDelta, fps, presentationMs, decoded?:{frames, width, height, mean:[r,g,b], "
          "perFrame?:[{diff, angle, area}]}}",
          "Reads an MP4's own boxes: where the index sits (`fastStart`: moov before mdat), and the "
          "video track's sample entry (codec, coded size), timescale, sample count (`frames`) and "
          "whether every sample but the last has one duration (`constantRate`, `fps` = timescale / "
          "delta; the last sample's duration, `lastDelta`, is reported beside it) and how long the "
          "movie says it plays (`presentationMs`, mvhd). A recorder file has its timing CLOSED by "
          "the fast-start step: N frames are N steps long in every box that states a duration. "
          "`decode: true` also plays it through Qt's decoder (at half speed, so no late frame is "
          "skipped) and counts the frames delivered, "
          "with the first frame's size and mean colour. `perFrame: {rect:[x,y,w,h], threshold}` (decoded "
          "pixels; it implies decode, played at a tenth of the speed) also measures EVERY decoded frame "
          "on its luma plane: `diff` the mean absolute luma difference from the previous decoded frame "
          "(0 for the first; ~0 = the same picture sent twice), and inside `rect` the pixels whose luma "
          "is at least `threshold` — `area` how many, `angle` the orientation of their principal axis in "
          "degrees (-90..90, image x right, y down; from the second moments), the instrument a spinning "
          "bar's motion per frame is read with. A measuring instrument.",
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
        { "button", "capture.button() -> {text, toolTip, enabled, red, recording, helpers, mode, placed, "
          "visible, width, height, barHeight, menu:[rows], menuOpen, failure, dialogOpen}",
          "What the record button shows: its text (the elapsed \"m:ss\" while recording — an "
          "OFFLINE recording shows its clip time against the wall time, \"m:ss / m:ss\"; icon "
          "only while idle), its popup's mode switch (`mode`), whether its "
          "icon is the red dot, its size and its bar's height (it never makes the bar taller, and adds "
          "only its layout gap to the window's minimum width), its popup's rows (press-and-hold or right-click), and the last "
          "failure message with whether its "
          "dialog is open.",
          Needs::Window },
        { "mode", "capture.mode(mode?) -> \"realtime\" | \"offline\"",
          "The record button's MODE switch (its popup's two rows, persisted `capture/mode`, default "
          "\"realtime\"): with no argument answers it, with \"realtime\" or \"offline\" sets it (the "
          "next press records in that mode; capture.start's `mode` is independent). Anything else is "
          "refused.",
          Needs::Document },
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
                                       QStringLiteral("fault"), QStringLiteral("mode") };
    const QString unknown = scriptmod::refuseUnknownKeys(QStringLiteral("capture.start"), options, known);
    if (!unknown.isEmpty()) { fail(unknown); return jsNull(); }
    VideoRecorder *r = mModule ? mModule->recorder() : nullptr;
    if (!r) { refuse(QStringLiteral("capture.start: recording needs the engine viewport")); return jsNull(); }
    VideoRecorder::Options o;
    o.path = options.value(QStringLiteral("path")).toString();
    o.helpers = options.value(QStringLiteral("helpers"), false).toBool();
    o.fault = options.value(QStringLiteral("fault")).toString();
    const QString mode = options.value(QStringLiteral("mode"), QStringLiteral("realtime")).toString();
    if (!VideoRecorder::parseMode(mode, &o.mode)) {
        fail(QStringLiteral("capture.start: mode must be \"realtime\" or \"offline\""));
        return jsNull();
    }
    static const QStringList faults = { QStringLiteral("noEncoder"), QStringLiteral("encoderError"),
                                        QStringLiteral("slowSave"), QStringLiteral("slowEncoder"),
                                        QStringLiteral("skipPoll") };
    if (!o.fault.isEmpty() && !faults.contains(o.fault)) {
        fail(QStringLiteral("capture.start: fault must be one of %1").arg(faults.join(QStringLiteral(", "))));
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

QVariantMap CaptureApi::abandon()
{
    if (VideoRecorder *r = mModule ? mModule->recorder() : nullptr) r->abandonSave();
    return status();
}

QVariantMap CaptureApi::wait(int timeoutMs)
{
    VideoRecorder *r = mModule ? mModule->recorder() : nullptr;
    if (r) r->waitFinished(timeoutMs);
    return status();
}

QVariantMap CaptureApi::inspect(const QString &path, const QVariantMap &options)
{
    static const QStringList known = { QStringLiteral("decode"), QStringLiteral("timeoutMs"),
                                       QStringLiteral("perFrame") };
    const QString unknown = scriptmod::refuseUnknownKeys(QStringLiteral("capture.inspect"), options, known);
    if (!unknown.isEmpty()) { fail(unknown); return QVariantMap(); }
    QVariantMap out = mp4::inspect(path).toMap();
    // THE PER-FRAME INSTRUMENT (VIDEO-REC-2): every decoded frame's luma, against
    // the previous one and as a principal axis inside a rect.
    const bool perFrame = options.contains(QStringLiteral("perFrame"));
    QRect rect;
    int threshold = 128;
    if (perFrame) {
        const QVariantMap pf = options.value(QStringLiteral("perFrame")).toMap();
        const QVariantList r = pf.value(QStringLiteral("rect")).toList();
        if (r.size() != 4) { fail(QStringLiteral("capture.inspect: perFrame.rect must be [x, y, w, h]")); return QVariantMap(); }
        rect = QRect(r[0].toInt(), r[1].toInt(), r[2].toInt(), r[3].toInt());
        threshold = pf.value(QStringLiteral("threshold"), 128).toInt();
    }
    if (!perFrame && !options.value(QStringLiteral("decode"), false).toBool()) return out;
    QVariantList perFrameRows;
    std::vector<unsigned char> previousLuma, luma;
    // THE DECODER'S OWN ANSWER: Qt plays the file into a sink, every frame counted.
    QMediaPlayer player;
    QVideoSink sink;
    player.setVideoOutput(&sink);
    int frames = 0;
    QSize size;
    double mean[3] = { 0, 0, 0 };
    QObject::connect(&sink, &QVideoSink::videoFrameChanged, &sink, [&](const QVideoFrame &f) {
        if (!f.isValid()) return;
        if (perFrame) {
            QVariantMap row;
            if (lumaOf(f, luma)) {
                const int w = f.width(), h = f.height();
                double diff = 0.0;
                if (previousLuma.size() == luma.size()) {
                    quint64 sum = 0;
                    for (size_t i = 0; i < luma.size(); ++i) sum += quint64(std::abs(int(luma[i]) - int(previousLuma[i])));
                    diff = double(sum) / double(luma.size());
                }
                // The principal axis of the bright pixels inside the rect.
                const QRect r = rect.intersected(QRect(0, 0, w, h));
                double n = 0, sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0;
                for (int y = r.top(); y <= r.bottom(); ++y)
                    for (int x = r.left(); x <= r.right(); ++x)
                        if (luma[size_t(y) * w + x] >= threshold) {
                            n += 1; sx += x; sy += y; sxx += double(x) * x; syy += double(y) * y; sxy += double(x) * y;
                        }
                double angle = 0.0;
                if (n > 0) {
                    const double mx = sx / n, my = sy / n;
                    const double mu20 = sxx / n - mx * mx, mu02 = syy / n - my * my, mu11 = sxy / n - mx * my;
                    angle = 0.5 * std::atan2(2.0 * mu11, mu20 - mu02) * 180.0 / M_PI;
                }
                row["diff"] = diff;
                row["angle"] = angle;
                row["area"] = int(n);
                previousLuma.swap(luma);
            } else {
                row["error"] = QStringLiteral("no luma plane");
            }
            perFrameRows << row;
        }
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
    // ...and at a tenth when every frame is measured on this thread.
    player.setPlaybackRate(perFrame ? 0.1 : 0.5);
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
    if (perFrame) d["perFrame"] = perFrameRows;
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

QVariant CaptureApi::mode(const QVariant &mode)
{
    if (!mModule) return jsNull();
    if (mode.isValid() && !mode.isNull()) {
        VideoRecorder::Mode m;
        if (!VideoRecorder::parseMode(mode.toString(), &m)) {
            fail(QStringLiteral("capture.mode: mode must be \"realtime\" or \"offline\""));
            return jsNull();
        }
        mModule->setModeSwitch(m);
    }
    return VideoRecorder::modeName(mModule->modeSwitch());
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
