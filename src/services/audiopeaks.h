/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef AUDIOPEAKS_H
#define AUDIOPEAKS_H

#include <QJsonArray>
#include <QString>
#include <QVector>

// Waveform peak buckets for the audio preview (ASSET_MEDIA_SPEC §2):
// the file's samples reduced to `buckets` [min,max] pairs in -127..127.
// Channels are folded together (the envelope, not per-channel lanes).
//
// compute() is worker-thread friendly by design — the wav path is a pure
// RIFF/PCM parse, and the QAudioDecoder path (mp3/ogg/flac…) spins a local
// QEventLoop on whatever thread it runs on (QtConcurrent threads are
// QThreads; event loops work there). Cached per guid in the asset row's
// properties JSON under "waveform" beside "metadata".
class AudioPeaks
{
public:
    struct Peaks
    {
        QVector<qint8> mins;   // one per bucket
        QVector<qint8> maxs;
        bool isEmpty() const { return mins.isEmpty(); }
        int buckets() const { return mins.size(); }
    };

    static constexpr int DefaultBuckets = 1024;

    // Dispatch: .wav via the pure PCM parse, everything else via QAudioDecoder.
    static Peaks compute(const QString &filePath, int buckets = DefaultBuckets);

    // Pure parser for canonical PCM wav (8/16/24/32-bit int + 32-bit float).
    static Peaks computeFromWav(const QString &filePath, int buckets = DefaultBuckets);

    // QAudioDecoder (ffmpeg backend) fallback for compressed formats.
    static Peaks computeWithDecoder(const QString &filePath, int buckets = DefaultBuckets);

    // Cache round-trip: flat [min0, max0, min1, max1, ...] JSON array.
    static QJsonArray toJson(const Peaks &peaks);
    static Peaks fromJson(const QJsonArray &array);
};

#endif // AUDIOPEAKS_H
