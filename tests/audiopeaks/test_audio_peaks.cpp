/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// Waveform peak computation (ASSET_MEDIA_SPEC §2): the pure wav parse on the
// committed fixture, synthetic wavs with known amplitudes, the JSON cache
// round-trip, and the QAudioDecoder (ffmpeg) fallback agreeing on the same
// file. Headless (offscreen platform).

#include <QGuiApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtMath>

#include "services/audiopeaks.h"

static const char *FIXTURES = JAHSHAKA_TEST_SOURCE_DIR "/tests/scripting/fixtures";

static int failures = 0;
#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        if (cond) { printf("ok: %s\n", msg); }                               \
        else { printf("FAIL: %s\n", msg); ++failures; }                      \
    } while (0)

// Minimal canonical 16-bit PCM mono wav writer.
static QString writeWav(const QString &path, const QVector<qint16> &samples, int sampleRate)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return QString();
    const quint32 dataBytes = quint32(samples.size()) * 2;
    QByteArray header;
    auto append32 = [&](quint32 v) { quint32 le = qToLittleEndian(v); header.append(reinterpret_cast<const char*>(&le), 4); };
    auto append16 = [&](quint16 v) { quint16 le = qToLittleEndian(v); header.append(reinterpret_cast<const char*>(&le), 2); };
    header.append("RIFF"); append32(36 + dataBytes); header.append("WAVE");
    header.append("fmt "); append32(16);
    append16(1);                       // PCM
    append16(1);                       // mono
    append32(quint32(sampleRate));
    append32(quint32(sampleRate) * 2); // byte rate
    append16(2);                       // block align
    append16(16);                      // bits
    header.append("data"); append32(dataBytes);
    file.write(header);
    for (qint16 s : samples) {
        const qint16 le = qToLittleEndian(s);
        file.write(reinterpret_cast<const char*>(&le), 2);
    }
    file.close();
    return path;
}

static bool wellFormed(const AudioPeaks::Peaks &p)
{
    if (p.isEmpty() || p.mins.size() != p.maxs.size()) return false;
    for (int i = 0; i < p.buckets(); ++i)
        if (p.mins[i] > p.maxs[i]) return false;
    return true;
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QTemporaryDir tmp;

    // ---- the committed fixture (8000 Hz mono, 50 ms) ----
    const QString tinyWav = QString(FIXTURES) + "/tiny.wav";
    const auto tiny = AudioPeaks::computeFromWav(tinyWav);
    CHECK(wellFormed(tiny), "tiny.wav parses to well-formed peaks");
    CHECK(AudioPeaks::compute(tinyWav).buckets() == tiny.buckets(),
          "compute() dispatches wav to the pure parser");

    // ---- synthetic full-scale sine: peaks reach near +/-127 ----
    {
        QVector<qint16> sine;
        const int rate = 8000, seconds = 2;
        for (int i = 0; i < rate * seconds; ++i)
            sine.append(qint16(32000.0 * qSin(2.0 * M_PI * 440.0 * i / rate)));
        const QString path = writeWav(tmp.filePath("sine.wav"), sine, rate);
        const auto peaks = AudioPeaks::computeFromWav(path, 64);
        CHECK(wellFormed(peaks), "sine peaks well-formed");
        CHECK(peaks.buckets() > 4 && peaks.buckets() <= 64,
              "sine bucket count within the requested bound");
        int hi = 0, lo = 0;
        for (int i = 0; i < peaks.buckets(); ++i) {
            hi = qMax<int>(hi, peaks.maxs[i]);
            lo = qMin<int>(lo, peaks.mins[i]);
        }
        CHECK(hi >= 115 && lo <= -115, "full-scale sine reaches near +/-127");

        // decoder fallback (ffmpeg) sees the same file with comparable energy
        const auto decoded = AudioPeaks::computeWithDecoder(path, 64);
        if (decoded.isEmpty()) {
            printf("note: QAudioDecoder produced no buffers here — fallback "
                   "coverage comes from the app run\n");
        } else {
            int dhi = 0;
            for (int i = 0; i < decoded.buckets(); ++i) dhi = qMax<int>(dhi, decoded.maxs[i]);
            CHECK(wellFormed(decoded), "decoder peaks well-formed");
            CHECK(dhi >= 100, "decoder path hears the full-scale sine");
        }
    }

    // ---- silence stays flat ----
    {
        const QString path = writeWav(tmp.filePath("silence.wav"),
                                      QVector<qint16>(8000, 0), 8000);
        const auto peaks = AudioPeaks::computeFromWav(path, 32);
        CHECK(wellFormed(peaks), "silence peaks well-formed");
        bool flat = true;
        for (int i = 0; i < peaks.buckets(); ++i)
            if (qAbs(int(peaks.mins[i])) > 1 || qAbs(int(peaks.maxs[i])) > 1) flat = false;
        CHECK(flat, "silence stays at zero");
    }

    // ---- cache round-trip ----
    {
        const auto json = AudioPeaks::toJson(tiny);
        CHECK(json.size() == tiny.buckets() * 2, "toJson interleaves min/max");
        const auto back = AudioPeaks::fromJson(json);
        CHECK(back.mins == tiny.mins && back.maxs == tiny.maxs,
              "fromJson(toJson(p)) == p");
    }

    // ---- garbage refuses cleanly ----
    {
        QFile junk(tmp.filePath("junk.wav"));
        junk.open(QIODevice::WriteOnly);
        junk.write("not a riff file at all");
        junk.close();
        CHECK(AudioPeaks::computeFromWav(junk.fileName()).isEmpty(),
              "non-wav bytes yield empty peaks");
        CHECK(AudioPeaks::computeFromWav(tmp.filePath("missing.wav")).isEmpty(),
              "missing file yields empty peaks");
    }

    printf(failures == 0 ? "audio peaks: all assertions passed\n"
                         : "audio peaks: %d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
