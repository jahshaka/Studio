/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "modules/capture/mp4faststart.h"

#include <QByteArray>
#include <QFile>
#include <QVector>

#include <limits>

namespace mp4 {

namespace {

// ---- the box grammar (ISO/IEC 14496-12 §4.2) --------------------------------
// size:u32 type:4cc [largesize:u64 when size == 1] [payload]; size 0 = to the end.

quint32 be32(const char *p)
{
    const auto *u = reinterpret_cast<const unsigned char *>(p);
    return (quint32(u[0]) << 24) | (quint32(u[1]) << 16) | (quint32(u[2]) << 8) | quint32(u[3]);
}
quint64 be64(const char *p) { return (quint64(be32(p)) << 32) | be32(p + 4); }
quint16 be16(const char *p)
{
    const auto *u = reinterpret_cast<const unsigned char *>(p);
    return quint16((u[0] << 8) | u[1]);
}
void put32(QByteArray &out, quint32 v)
{
    const char b[4] = { char(v >> 24), char(v >> 16), char(v >> 8), char(v) };
    out.append(b, 4);
}
void put64(QByteArray &out, quint64 v)
{
    put32(out, quint32(v >> 32));
    put32(out, quint32(v));
}

struct Box {
    QByteArray type;
    qint64 offset = 0;   ///< of the header
    qint64 header = 8;   ///< 8 or 16
    qint64 size = 0;     ///< header + payload
};

/// The top-level boxes of a file, by streaming their headers only.
bool topLevel(QFile &f, QVector<Box> &out, QString *error)
{
    const qint64 total = f.size();
    qint64 at = 0;
    while (at < total) {
        if (total - at < 8) { if (error) *error = QStringLiteral("a truncated box header"); return false; }
        if (!f.seek(at)) { if (error) *error = f.errorString(); return false; }
        char h[16];
        if (f.read(h, 8) != 8) { if (error) *error = f.errorString(); return false; }
        Box b;
        b.type = QByteArray(h + 4, 4);
        b.offset = at;
        quint64 size = be32(h);
        if (size == 1) {
            if (f.read(h + 8, 8) != 8) { if (error) *error = QStringLiteral("a truncated large box size"); return false; }
            size = be64(h + 8);
            b.header = 16;
        } else if (size == 0) {
            size = quint64(total - at);
        }
        if (size < quint64(b.header) || size > quint64(total - at)) {
            if (error) *error = QStringLiteral("box '%1' at %2 has an impossible size")
                                    .arg(QString::fromLatin1(b.type)).arg(at);
            return false;
        }
        b.size = qint64(size);
        out.append(b);
        at += b.size;
    }
    return true;
}

/// Children of a container payload held in memory.
bool children(const char *data, qint64 len, QVector<Box> &out)
{
    qint64 at = 0;
    while (at + 8 <= len) {
        Box b;
        b.type = QByteArray(data + at + 4, 4);
        b.offset = at;
        quint64 size = be32(data + at);
        if (size == 1) {
            if (at + 16 > len) return false;
            size = be64(data + at + 8);
            b.header = 16;
        } else if (size == 0) {
            size = quint64(len - at);
        }
        if (size < quint64(b.header) || size > quint64(len - at)) return false;
        b.size = qint64(size);
        out.append(b);
        at += b.size;
    }
    return at == len;
}

bool isContainer(const QByteArray &t)
{
    // Only the path from moov to the sample tables: nothing else holds offsets.
    return t == "moov" || t == "trak" || t == "mdia" || t == "minf" || t == "stbl";
}

void header(QByteArray &out, quint64 size, const char *type)
{
    if (size <= std::numeric_limits<quint32>::max()) {
        put32(out, quint32(size));
        out.append(type, 4);
    } else {
        put32(out, 1u);
        out.append(type, 4);
        put64(out, size + 8u);   // the larger header
    }
}

/// THE REWRITE: `box` (header included) re-emitted with every chunk offset moved
/// by `delta`; `wide` writes every stco as a co64. `fits` goes false when a
/// 32-bit table would overflow (the caller widens and runs again).
bool rewrite(const char *box, qint64 len, qint64 headerLen, const QByteArray &type, qint64 delta,
             bool wide, bool &fits, QByteArray &out)
{
    const char *payload = box + headerLen;
    const qint64 plen = len - headerLen;
    if (isContainer(type)) {
        QVector<Box> kids;
        if (!children(payload, plen, kids)) return false;
        QByteArray body;
        for (const Box &k : kids)
            if (!rewrite(payload + k.offset, k.size, k.header, k.type, delta, wide, fits, body)) return false;
        header(out, quint64(body.size()) + 8u, type.constData());
        out.append(body);
        return true;
    }
    if (type == "stco" || type == "co64") {
        if (plen < 8) return false;
        const quint32 count = be32(payload + 4);
        const bool is64 = type == "co64";
        const qint64 entry = is64 ? 8 : 4;
        if (plen < 8 + qint64(count) * entry) return false;
        const bool out64 = is64 || wide;
        QByteArray body;
        body.append(payload, 4);   // version + flags
        put32(body, count);
        for (quint32 i = 0; i < count; ++i) {
            const char *e = payload + 8 + qint64(i) * entry;
            const quint64 v = (is64 ? be64(e) : be32(e)) + quint64(delta);
            if (out64) put64(body, v);
            else {
                if (v > std::numeric_limits<quint32>::max()) fits = false;
                put32(body, quint32(v));
            }
        }
        header(out, quint64(body.size()) + 8u, out64 ? "co64" : "stco");
        out.append(body);
        return true;
    }
    out.append(box, len);
    return true;
}

bool copyRange(QFile &in, QFile &out, qint64 from, qint64 size, QString *error,
               const std::atomic<bool> *cancel)
{
    if (!in.seek(from)) { if (error) *error = in.errorString(); return false; }
    QByteArray buf;
    buf.resize(4 << 20);
    while (size > 0) {
        if (cancel && cancel->load()) { if (error) *error = QStringLiteral("cancelled"); return false; }
        const qint64 want = qMin<qint64>(size, buf.size());
        const qint64 got = in.read(buf.data(), want);
        if (got != want) { if (error) *error = QStringLiteral("read: %1").arg(in.errorString()); return false; }
        if (out.write(buf.constData(), got) != got) {
            if (error) *error = QStringLiteral("write: %1").arg(out.errorString());
            return false;
        }
        size -= got;
    }
    return true;
}

/// A box's payload, found by a path of four-ccs under `data`.
const char *find(const char *data, qint64 len, const QList<QByteArray> &path, qint64 *plen)
{
    QVector<Box> kids;
    if (!children(data, len, kids)) return nullptr;
    for (const Box &k : kids) {
        if (k.type != path.first()) continue;
        const char *p = data + k.offset + k.header;
        const qint64 pl = k.size - k.header;
        if (path.size() == 1) { if (plen) *plen = pl; return p; }
        return find(p, pl, path.mid(1), plen);
    }
    return nullptr;
}

void set32(char *p, quint32 v)
{
    p[0] = char(v >> 24); p[1] = char(v >> 16); p[2] = char(v >> 8); p[3] = char(v);
}
void set64(char *p, quint64 v) { set32(p, quint32(v >> 32)); set32(p + 4, quint32(v)); }

/// THE TRACK'S TIMING, CLOSED (the second half of the step). FFmpeg's mov writer
/// is handed packets with no duration by Qt's encoder, so it guesses the LAST
/// sample's duration (measured: 0 or 2000 where every other sample is 1000 at
/// 1/60000) and sizes the track from the B-frame reorder's composition end — a
/// 240-frame clip came out 4.050 s long (elst/tkhd/mvhd) with an mdhd of 245000,
/// which a tool deriving the rate reads as 58.75 fps (GStreamer did). For ONE
/// video track whose samples all share a duration but the last, the last takes
/// that duration and the clip is exactly N frames long everywhere it is stated:
/// stts, mdhd (the sum of the sample durations), the single edit's segment, tkhd
/// and mvhd (in the movie's timescale). In place: no box changes size. Anything
/// else — two tracks, a variable rate, several edits — is left as the muxer wrote it.
void normaliseTiming(QByteArray &moovBox, qint64 header)
{
    char *mv = moovBox.data() + header;
    const qint64 ml = moovBox.size() - header;
    QVector<Box> kids;
    if (!children(mv, ml, kids)) return;
    const Box *mvhd = nullptr, *trak = nullptr;
    int traks = 0;
    for (const Box &k : kids) {
        if (k.type == "mvhd") mvhd = &k;
        if (k.type == "trak") { trak = &k; ++traks; }
    }
    if (!mvhd || traks != 1) return;
    char *mvhdP = mv + mvhd->offset + mvhd->header;
    const bool mv64 = mvhdP[0] == 1;
    if (mvhd->size - mvhd->header < (mv64 ? 32 : 20)) return;
    const quint32 movieScale = be32(mvhdP + (mv64 ? 20 : 12));
    char *tp = mv + trak->offset + trak->header;
    const qint64 tl = trak->size - trak->header;
    qint64 l = 0;
    const char *hdlr = find(tp, tl, { "mdia", "hdlr" }, &l);
    if (!hdlr || l < 12 || QByteArray(hdlr + 8, 4) != "vide") return;
    char *mdhd = const_cast<char *>(find(tp, tl, { "mdia", "mdhd" }, &l));
    if (!mdhd) return;
    const bool md64 = mdhd[0] == 1;
    if (l < (md64 ? 32 : 20)) return;
    const quint32 mediaScale = be32(mdhd + (md64 ? 20 : 12));
    char *stts = const_cast<char *>(find(tp, tl, { "mdia", "minf", "stbl", "stts" }, &l));
    if (!stts || l < 8) return;
    const quint32 runs = be32(stts + 4);
    if (runs < 2 || l < 8 + qint64(runs) * 8) return;
    const quint32 delta = be32(stts + 8 + 4);
    quint64 samples = 0;
    for (quint32 i = 0; i < runs; ++i) {
        const quint32 count = be32(stts + 8 + qint64(i) * 8), d = be32(stts + 8 + qint64(i) * 8 + 4);
        samples += count;
        const bool last = i + 1 == runs;
        if (!last && d != delta) return;                  // a variable rate: not ours to close
        if (last && count != 1) return;                   // the last run is not a lone sample
    }
    if (delta == 0 || movieScale == 0 || mediaScale == 0) return;
    // The edit list, when there is one, must be the single offset edit B-frames make.
    qint64 el = 0;
    char *elst = const_cast<char *>(find(tp, tl, { "edts", "elst" }, &el));
    if (elst) {
        if (el < 8 || be32(elst + 4) != 1) return;
        if ((elst[0] == 1 && el < 8 + 20) || (elst[0] == 0 && el < 8 + 12)) return;
    }
    char *tkhd = const_cast<char *>(find(tp, tl, { "tkhd" }, &l));
    if (!tkhd || l < (tkhd[0] == 1 ? 36 : 24)) return;
    const quint64 media = samples * delta;
    const quint64 movie = (media * movieScale + mediaScale / 2) / mediaScale;
    set32(stts + 8 + qint64(runs - 1) * 8 + 4, delta);
    if (md64) set64(mdhd + 24, media); else set32(mdhd + 16, quint32(media));
    if (elst) { if (elst[0] == 1) set64(elst + 8, movie); else set32(elst + 8, quint32(movie)); }
    if (tkhd[0] == 1) set64(tkhd + 28, movie); else set32(tkhd + 20, quint32(movie));
    if (mv64) set64(mvhdP + 24, movie); else set32(mvhdP + 16, quint32(movie));
}

}   // namespace

QVariantMap Facts::toMap() const
{
    QVariantMap m;
    m["ok"] = ok;
    if (!error.isEmpty()) m["error"] = error;
    m["moovOffset"] = moovOffset;
    m["mdatOffset"] = mdatOffset;
    m["fastStart"] = fastStart;
    m["codec"] = codec;
    m["width"] = width;
    m["height"] = height;
    m["timescale"] = timescale;
    m["duration"] = qulonglong(duration);
    m["frames"] = samples;
    m["keyframes"] = syncSamples;
    m["constantRate"] = constantRate;
    m["sampleDelta"] = sampleDelta;
    m["lastDelta"] = lastDelta;
    m["presentationMs"] = presentationMs;
    // The rate as a ratio, the way a player states it (60/1).
    m["fps"] = (constantRate && sampleDelta) ? double(timescale) / double(sampleDelta) : 0.0;
    return m;
}

Facts inspect(const QString &path)
{
    Facts f;
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly)) { f.error = in.errorString(); return f; }
    QVector<Box> top;
    if (!topLevel(in, top, &f.error)) return f;
    const Box *moov = nullptr;
    for (const Box &b : top) {
        if (b.type == "moov" && f.moovOffset < 0) { f.moovOffset = b.offset; moov = &b; }
        if (b.type == "mdat" && f.mdatOffset < 0) f.mdatOffset = b.offset;
    }
    if (!moov) { f.error = QStringLiteral("no moov box (the recording was not finished)"); return f; }
    f.fastStart = f.mdatOffset < 0 || f.moovOffset < f.mdatOffset;
    in.seek(moov->offset + moov->header);
    const QByteArray mv = in.read(moov->size - moov->header);
    if (mv.size() != moov->size - moov->header) { f.error = QStringLiteral("a truncated moov"); return f; }
    {
        qint64 l = 0;
        if (const char *mvhd = find(mv.constData(), mv.size(), { "mvhd" }, &l)) {
            const bool v1 = mvhd[0] == 1;
            if (l >= (v1 ? 32 : 20)) {
                const quint32 scale = be32(mvhd + (v1 ? 20 : 12));
                const quint64 dur = v1 ? be64(mvhd + 24) : be32(mvhd + 16);
                if (scale) f.presentationMs = double(dur) * 1000.0 / scale;
            }
        }
    }
    QVector<Box> traks;
    if (!children(mv.constData(), mv.size(), traks)) { f.error = QStringLiteral("a malformed moov"); return f; }
    for (const Box &t : traks) {
        if (t.type != "trak") continue;
        const char *tp = mv.constData() + t.offset + t.header;
        const qint64 tl = t.size - t.header;
        qint64 hl = 0;
        const char *hdlr = find(tp, tl, { "mdia", "hdlr" }, &hl);
        if (!hdlr || hl < 12 || QByteArray(hdlr + 8, 4) != "vide") continue;
        qint64 ml = 0;
        if (const char *mdhd = find(tp, tl, { "mdia", "mdhd" }, &ml)) {
            if (ml >= 24 && mdhd[0] == 0) { f.timescale = be32(mdhd + 12); f.duration = be32(mdhd + 16); }
            else if (ml >= 36 && mdhd[0] == 1) { f.timescale = be32(mdhd + 20); f.duration = be64(mdhd + 24); }
        }
        const QList<QByteArray> stbl = { "mdia", "minf", "stbl" };
        qint64 sl = 0;
        if (const char *stsd = find(tp, tl, stbl + QList<QByteArray>{ "stsd" }, &sl)) {
            // full box (4) + entry count (4), then the first sample entry: size, 4cc,
            // 6 reserved + 2 index, 16 of pre-defined/reserved, width u16, height u16.
            if (sl >= 8 + 8 + 8 + 16 + 4) {
                const char *e = stsd + 8;
                f.codec = QString::fromLatin1(e + 4, 4);
                f.width = be16(e + 8 + 8 + 16);
                f.height = be16(e + 8 + 8 + 16 + 2);
            }
        }
        if (const char *stsz = find(tp, tl, stbl + QList<QByteArray>{ "stsz" }, &sl))
            if (sl >= 12) f.samples = be32(stsz + 8);
        if (const char *stss = find(tp, tl, stbl + QList<QByteArray>{ "stss" }, &sl))
            if (sl >= 8) f.syncSamples = be32(stss + 4);
        if (const char *stts = find(tp, tl, stbl + QList<QByteArray>{ "stts" }, &sl)) {
            if (sl >= 8) {
                const quint32 runs = be32(stts + 4);
                // THE LAST SAMPLE'S DURATION IS THE MUXER'S, not the stream's: FFmpeg's
                // mov writer closes a reordered (B-frame) track with an end-of-stream
                // value of its own (measured: 89 x 1000 then one 2000 at 1/60000). The
                // rate is every OTHER sample's; the last one is reported beside it.
                QVector<QPair<quint32, quint32>> table;
                if (runs > 0 && sl >= 8 + qint64(runs) * 8)
                    for (quint32 i = 0; i < runs; ++i)
                        table.append({ be32(stts + 8 + qint64(i) * 8), be32(stts + 8 + qint64(i) * 8 + 4) });
                if (!table.isEmpty()) {
                    f.lastDelta = table.last().second;
                    if (--table.last().first == 0) table.removeLast();
                }
                quint32 delta = 0;
                bool constant = !table.isEmpty();
                for (int i = 0; constant && i < table.size(); ++i) {
                    if (i == 0) delta = table[i].second;
                    else if (table[i].second != delta) constant = false;
                }
                f.constantRate = constant;
                f.sampleDelta = constant ? delta : 0;
            }
        }
        f.ok = true;
        return f;
    }
    f.error = QStringLiteral("no video track");
    return f;
}

bool fastStart(const QString &inPath, const QString &outPath, QString *error,
               const std::atomic<bool> *cancel)
{
    QFile in(inPath);
    if (!in.open(QIODevice::ReadOnly)) { if (error) *error = in.errorString(); return false; }
    QVector<Box> top;
    if (!topLevel(in, top, error)) return false;
    int moovIdx = -1, mdatIdx = -1;
    for (int i = 0; i < top.size(); ++i) {
        if (top[i].type == "moov" && moovIdx < 0) moovIdx = i;
        if (top[i].type == "mdat" && mdatIdx < 0) mdatIdx = i;
    }
    if (moovIdx < 0) { if (error) *error = QStringLiteral("no moov box (the recording was not finished)"); return false; }
    QFile out(outPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) { if (error) *error = out.errorString(); return false; }
    // ALREADY FAST-START: the same bytes.
    if (mdatIdx < 0 || moovIdx < mdatIdx) {
        if (!copyRange(in, out, 0, in.size(), error, cancel)) return false;
        return out.flush();
    }
    const Box &moov = top[moovIdx];
    if (!in.seek(moov.offset)) { if (error) *error = in.errorString(); return false; }
    QByteArray mv = in.read(moov.size);
    if (mv.size() != moov.size) { if (error) *error = QStringLiteral("a truncated moov"); return false; }
    normaliseTiming(mv, moov.header);
    // THE DISTANCE THE MEDIA MOVES is the size of the moov that now precedes it —
    // which does not depend on the offsets' values, only on their width. So: size
    // the rewritten moov once with no shift, then write it with that shift; widen
    // every table if a 32-bit one overflowed and do both again.
    QByteArray rewritten;
    for (bool wide : { false, true }) {
        bool fits = true;
        QByteArray sized;
        if (!rewrite(mv.constData(), mv.size(), moov.header, moov.type, 0, wide, fits, sized)) {
            if (error) *error = QStringLiteral("a malformed moov");
            return false;
        }
        fits = true;
        rewritten.clear();
        if (!rewrite(mv.constData(), mv.size(), moov.header, moov.type, sized.size(), wide, fits, rewritten)) {
            if (error) *error = QStringLiteral("a malformed moov");
            return false;
        }
        if (fits) break;
    }
    // The order: everything before the first mdat, the moov, then the rest without it.
    for (int i = 0; i < top.size(); ++i) {
        if (i == mdatIdx && out.write(rewritten) != rewritten.size()) {
            if (error) *error = QStringLiteral("write: %1").arg(out.errorString());
            return false;
        }
        if (i == moovIdx) continue;
        if (!copyRange(in, out, top[i].offset, top[i].size, error, cancel)) return false;
    }
    if (!out.flush()) { if (error) *error = out.errorString(); return false; }
    return true;
}

}   // namespace mp4
