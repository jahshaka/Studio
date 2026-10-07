/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef MP4FASTSTART_H
#define MP4FASTSTART_H

// THE MP4 FAST-START STEP, AND THE CONTAINER'S OWN FACTS (VIDEO-REC-1;
// SPECS/VIDEO_CAPTURE_SPEC.md §9 route A).
//
// Qt's recorder writes the index (`moov`) AFTER the media (`mdat`), and
// QMediaRecorder has no movflags setting (the probe, spikes/venc-probe/). A file
// shaped that way cannot start playing until it has been downloaded whole, which
// is the wrong shape for the social sites the recorder is for. `fastStart` is the
// well-known qt-faststart algorithm: move `moov` in front of `mdat` and add the
// distance it moved to every chunk offset it holds (`stco`, and `co64`), widening
// 32-bit offset tables to 64 bits when a shifted offset would not fit. Plain Qt
// file I/O, no library; the media bytes are streamed, never held. On the way it
// CLOSES THE TRACK'S TIMING (normaliseTiming in the .cpp): the muxer's guess at
// the last sample's duration is replaced by the stream's own, so a constant-rate
// clip of N frames is N frames long everywhere the file says how long it is.
//
// `inspect` reads the same boxes back for a test and for `capture.inspect`: where
// the index sits, and the video track's codec, size, timescale and sample table.

#include <QString>
#include <QVariantMap>

#include <atomic>

namespace mp4 {

struct Facts {
    bool ok = false;
    QString error;
    /// Top-level offsets; -1 when the box is absent.
    qint64 moovOffset = -1, mdatOffset = -1;
    bool fastStart = false;           ///< moov before the first mdat
    /// The first video track ('vide' handler).
    QString codec;                    ///< the sample entry's four-cc ("avc1")
    int width = 0, height = 0;        ///< the sample entry's coded size
    quint32 timescale = 0;            ///< the media timescale (mdhd)
    quint64 duration = 0;             ///< in timescale units (mdhd)
    quint32 samples = 0;              ///< frames (stsz)
    quint32 syncSamples = 0;          ///< keyframes (stss; 0 = every sample is one)
    /// The stts table: true when every sample BUT THE LAST has the same
    /// duration, which is then `sampleDelta` (a constant frame rate of
    /// timescale / sampleDelta). The last sample's duration is the muxer's
    /// end-of-stream value (`lastDelta`), not a frame interval of the stream.
    bool constantRate = false;
    quint32 sampleDelta = 0;
    quint32 lastDelta = 0;
    /// How long the movie says it plays (mvhd), in milliseconds.
    double presentationMs = 0.0;
    QVariantMap toMap() const;
};

Facts inspect(const QString &path);

/// Writes `in` re-ordered fast-start into `out` (which it creates or truncates).
/// A file that already is fast-start is copied unchanged. False with `error` on
/// any malformed box or I/O failure; `cancel` (optional) aborts between chunks.
bool fastStart(const QString &in, const QString &out, QString *error,
               const std::atomic<bool> *cancel = nullptr);

}   // namespace mp4

#endif // MP4FASTSTART_H
