/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "audiopeaks.h"

#include <QAudioBuffer>
#include <QAudioDecoder>
#include <QAudioFormat>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <QtEndian>

#include <algorithm>
#include <cmath>

namespace {

constexpr int FineChunk = 1024;   // samples per fine bucket while streaming

// Accumulates normalized samples (-1..1) into fine min/max chunks, then
// folds them into the requested bucket count.
struct PeakAccumulator
{
    QVector<float> mins, maxs;
    float curMin = 1.0f, curMax = -1.0f;
    int filled = 0;

    void add(float v)
    {
        curMin = std::min(curMin, v);
        curMax = std::max(curMax, v);
        if (++filled == FineChunk) flush();
    }
    void flush()
    {
        if (filled == 0) return;
        mins.append(curMin);
        maxs.append(curMax);
        curMin = 1.0f; curMax = -1.0f; filled = 0;
    }
    AudioPeaks::Peaks bucketed(int buckets) const
    {
        AudioPeaks::Peaks peaks;
        const int fine = mins.size();
        if (fine == 0 || buckets <= 0) return peaks;
        const int outCount = std::min(buckets, fine);
        peaks.mins.reserve(outCount);
        peaks.maxs.reserve(outCount);
        for (int b = 0; b < outCount; ++b) {
            const int begin = static_cast<int>(static_cast<qint64>(b) * fine / outCount);
            const int end = std::max(begin + 1,
                                     static_cast<int>(static_cast<qint64>(b + 1) * fine / outCount));
            float lo = 1.0f, hi = -1.0f;
            for (int i = begin; i < end && i < fine; ++i) {
                lo = std::min(lo, mins[i]);
                hi = std::max(hi, maxs[i]);
            }
            const auto clamp8 = [](float v) {
                return static_cast<qint8>(std::lround(std::clamp(v, -1.0f, 1.0f) * 127.0f));
            };
            peaks.mins.append(clamp8(lo));
            peaks.maxs.append(clamp8(hi));
        }
        return peaks;
    }
};

} // namespace

AudioPeaks::Peaks AudioPeaks::compute(const QString &filePath, int buckets)
{
    if (QFileInfo(filePath).suffix().toLower() == QLatin1String("wav")) {
        const Peaks peaks = computeFromWav(filePath, buckets);
        if (!peaks.isEmpty()) return peaks;
    }
    return computeWithDecoder(filePath, buckets);
}

AudioPeaks::Peaks AudioPeaks::computeFromWav(const QString &filePath, int buckets)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return Peaks();

    // RIFF header (same walk assetmetadata's parseWavHeader does).
    if (file.read(4) != "RIFF") return Peaks();
    file.read(4);
    if (file.read(4) != "WAVE") return Peaks();

    quint16 audioFormat = 0, channels = 0, bitsPerSample = 0;
    qint64 dataOffset = -1, dataBytes = 0;
    while (!file.atEnd()) {
        const QByteArray id = file.read(4);
        const QByteArray sizeBytes = file.read(4);
        if (id.size() < 4 || sizeBytes.size() < 4) break;
        const quint32 chunkSize =
            qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(sizeBytes.constData()));
        if (id == "fmt ") {
            const QByteArray fmt = file.read(chunkSize + (chunkSize & 1));
            if (fmt.size() < 16) return Peaks();
            const auto *d = reinterpret_cast<const uchar *>(fmt.constData());
            audioFormat = qFromLittleEndian<quint16>(d);
            channels = qFromLittleEndian<quint16>(d + 2);
            bitsPerSample = qFromLittleEndian<quint16>(d + 14);
        }
        else if (id == "data") {
            dataOffset = file.pos();
            dataBytes = chunkSize;
            break;
        }
        else {
            file.seek(file.pos() + chunkSize + (chunkSize & 1));
        }
    }

    // PCM int (1) or IEEE float (3) only; anything else -> decoder path.
    if (dataOffset < 0 || channels == 0) return Peaks();
    if (audioFormat != 1 && audioFormat != 3) return Peaks();
    if (audioFormat == 1 && bitsPerSample != 8 && bitsPerSample != 16
        && bitsPerSample != 24 && bitsPerSample != 32) return Peaks();
    if (audioFormat == 3 && bitsPerSample != 32) return Peaks();

    file.seek(dataOffset);
    dataBytes = std::min(dataBytes, file.size() - dataOffset);
    const int bytesPerSample = bitsPerSample / 8;
    const int frameBytes = bytesPerSample * channels;
    if (frameBytes <= 0) return Peaks();

    PeakAccumulator acc;
    QByteArray block;
    qint64 remaining = dataBytes - (dataBytes % frameBytes);
    while (remaining > 0) {
        block = file.read(std::min<qint64>(remaining, 1 << 20));
        if (block.isEmpty()) break;
        remaining -= block.size();
        const auto *bytes = reinterpret_cast<const uchar *>(block.constData());
        const int samples = block.size() / bytesPerSample;
        for (int s = 0; s < samples; ++s) {
            const uchar *p = bytes + static_cast<qint64>(s) * bytesPerSample;
            float v = 0.0f;
            if (audioFormat == 3) {
                float f;
                memcpy(&f, p, 4);
                v = f;
            }
            else if (bitsPerSample == 8) {
                v = (int(*p) - 128) / 128.0f;
            }
            else if (bitsPerSample == 16) {
                v = qFromLittleEndian<qint16>(p) / 32768.0f;
            }
            else if (bitsPerSample == 24) {
                qint32 i = (p[0] | (p[1] << 8) | (p[2] << 16));
                if (i & 0x800000) i |= ~0xFFFFFF;   // sign-extend
                v = i / 8388608.0f;
            }
            else {   // 32-bit int
                v = qFromLittleEndian<qint32>(p) / 2147483648.0f;
            }
            acc.add(v);   // channels fold into the same envelope
        }
    }
    acc.flush();
    return acc.bucketed(buckets);
}

AudioPeaks::Peaks AudioPeaks::computeWithDecoder(const QString &filePath, int buckets)
{
    if (!QFileInfo::exists(filePath)) return Peaks();

    QAudioDecoder decoder;
    QAudioFormat format;
    format.setSampleFormat(QAudioFormat::Float);   // normalized output
    decoder.setAudioFormat(format);
    decoder.setSource(QUrl::fromLocalFile(filePath));

    PeakAccumulator acc;
    QEventLoop loop;

    QObject::connect(&decoder, &QAudioDecoder::bufferReady, &loop, [&]() {
        const QAudioBuffer buffer = decoder.read();
        if (!buffer.isValid()) return;
        const QAudioFormat fmt = buffer.format();
        const int count = buffer.sampleCount();   // across channels
        if (fmt.sampleFormat() == QAudioFormat::Float) {
            const float *data = buffer.constData<float>();
            for (int i = 0; i < count; ++i) acc.add(data[i]);
        }
        else if (fmt.sampleFormat() == QAudioFormat::Int16) {
            const qint16 *data = buffer.constData<qint16>();
            for (int i = 0; i < count; ++i) acc.add(data[i] / 32768.0f);
        }
        else if (fmt.sampleFormat() == QAudioFormat::Int32) {
            const qint32 *data = buffer.constData<qint32>();
            for (int i = 0; i < count; ++i) acc.add(data[i] / 2147483648.0f);
        }
        else if (fmt.sampleFormat() == QAudioFormat::UInt8) {
            const quint8 *data = buffer.constData<quint8>();
            for (int i = 0; i < count; ++i) acc.add((int(data[i]) - 128) / 128.0f);
        }
    });
    QObject::connect(&decoder, &QAudioDecoder::finished, &loop, &QEventLoop::quit);
    QObject::connect(&decoder, qOverload<QAudioDecoder::Error>(&QAudioDecoder::error),
                     &loop, [&]() { loop.quit(); });

    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(60000);

    decoder.start();
    loop.exec();
    decoder.stop();

    acc.flush();
    return acc.bucketed(buckets);
}

QJsonArray AudioPeaks::toJson(const Peaks &peaks)
{
    QJsonArray array;
    for (int i = 0; i < peaks.buckets(); ++i) {
        array.append(peaks.mins[i]);
        array.append(peaks.maxs[i]);
    }
    return array;
}

AudioPeaks::Peaks AudioPeaks::fromJson(const QJsonArray &array)
{
    Peaks peaks;
    const int count = array.size() / 2;
    peaks.mins.reserve(count);
    peaks.maxs.reserve(count);
    for (int i = 0; i < count; ++i) {
        peaks.mins.append(static_cast<qint8>(array[i * 2].toInt()));
        peaks.maxs.append(static_cast<qint8>(array[i * 2 + 1].toInt()));
    }
    return peaks;
}
