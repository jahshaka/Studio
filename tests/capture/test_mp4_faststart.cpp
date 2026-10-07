// capture.faststart — THE FAST-START STEP ON SYNTHETIC FILES (VIDEO-REC-1; mp4faststart.h).
//
// The recorder's real clips prove the common case end to end (capture.record_basic); these
// prove the algorithm on the shapes a 90-frame clip never has: a chunk table whose shifted
// offsets no longer fit 32 bits (widened to co64), a co64 table already, a file that is
// already fast-start (copied unchanged), and a truncated moov (refused, never a crash). Each
// rewritten file's offsets are read back and checked to point at the SAME media bytes.
#include <QByteArray>
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

#include <cstdio>
#include <limits>

#include "modules/capture/mp4faststart.h"

static int gFailures = 0, gChecks = 0;
#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        ++gChecks;                                                             \
        if (!(cond)) {                                                         \
            ++gFailures;                                                       \
            std::printf("    FAIL %s:%d: %s — ", __FILE__, __LINE__, #cond);   \
            std::printf(__VA_ARGS__);                                          \
            std::printf("\n");                                                 \
        }                                                                      \
    } while (0)

namespace {
void put32(QByteArray &b, quint32 v) { const char c[4] = { char(v >> 24), char(v >> 16), char(v >> 8), char(v) }; b.append(c, 4); }
void put64(QByteArray &b, quint64 v) { put32(b, quint32(v >> 32)); put32(b, quint32(v)); }
void put16(QByteArray &b, quint16 v) { const char c[2] = { char(v >> 8), char(v) }; b.append(c, 2); }
QByteArray box(const char *type, const QByteArray &payload)
{
    QByteArray b;
    put32(b, quint32(payload.size() + 8));
    b.append(type, 4);
    b.append(payload);
    return b;
}
QByteArray full(quint32 vf) { QByteArray b; put32(b, vf); return b; }
quint32 be32(const char *p) { const auto *u = reinterpret_cast<const unsigned char *>(p); return (quint32(u[0]) << 24) | (quint32(u[1]) << 16) | (quint32(u[2]) << 8) | u[3]; }
quint64 be64(const char *p) { return (quint64(be32(p)) << 32) | be32(p + 4); }

/// A one-track 'vide' moov with `n` chunks at `offsets`, 64-bit table when `co64`.
/// `muxerTail` >= 0 builds the muxer's shape (mvhd, tkhd, one B-frame edit) with the
/// LAST sample's duration `muxerTail` and the track sized 3 frames too long.
QByteArray moov(const QVector<quint64> &offsets, bool co64, int muxerTail = -1)
{
    QByteArray stsd = full(0); put32(stsd, 1);
    QByteArray entry; entry.append(QByteArray(6, 0)); put16(entry, 1); entry.append(QByteArray(16, 0));
    put16(entry, 1920); put16(entry, 1080); entry.append(QByteArray(50, 0));
    stsd.append(box("avc1", entry));
    QByteArray stts = full(0);
    if (muxerTail < 0) { put32(stts, 1); put32(stts, quint32(offsets.size())); put32(stts, 1000); }
    else { put32(stts, 2); put32(stts, quint32(offsets.size() - 1)); put32(stts, 1000); put32(stts, 1); put32(stts, quint32(muxerTail)); }
    QByteArray stsz = full(0); put32(stsz, 4); put32(stsz, quint32(offsets.size()));
    QByteArray stco = full(0); put32(stco, quint32(offsets.size()));
    for (quint64 o : offsets) { if (co64) put64(stco, o); else put32(stco, quint32(o)); }
    const QByteArray stbl = box("stsd", stsd) + box("stts", stts) + box("stsz", stsz) +
                            box(co64 ? "co64" : "stco", stco);
    QByteArray hdlr = full(0); put32(hdlr, 0); hdlr.append("vide", 4); hdlr.append(QByteArray(13, 0));
    QByteArray mdhd = full(0); put32(mdhd, 0); put32(mdhd, 0); put32(mdhd, 60000); put32(mdhd, quint32(offsets.size()) * 1000); put32(mdhd, 0);
    const QByteArray mdia = box("mdhd", mdhd) + box("hdlr", hdlr) + box("minf", box("stbl", stbl));
    if (muxerTail < 0) return box("moov", box("trak", box("mdia", mdia)));
    const quint32 n = quint32(offsets.size());
    const quint32 longMs = (n + 3) * 1000 / 60;   // the muxer's track: three frames too long
    QByteArray mvhd = full(0); put32(mvhd, 0); put32(mvhd, 0); put32(mvhd, 1000); put32(mvhd, longMs);
    mvhd.append(QByteArray(80, 0));
    QByteArray tkhd = full(3); put32(tkhd, 0); put32(tkhd, 0); put32(tkhd, 1); put32(tkhd, 0); put32(tkhd, longMs);
    tkhd.append(QByteArray(60, 0));
    QByteArray elst = full(0); put32(elst, 1); put32(elst, longMs); put32(elst, 3000); put32(elst, 0x10000);
    mdhd = full(0); put32(mdhd, 0); put32(mdhd, 0); put32(mdhd, 60000); put32(mdhd, (n + 5) * 1000); put32(mdhd, 0);
    const QByteArray mdia2 = box("mdhd", mdhd) + box("hdlr", hdlr) + box("minf", box("stbl", stbl));
    return box("moov", box("mvhd", mvhd) +
                       box("trak", box("tkhd", tkhd) + box("edts", box("elst", elst)) + box("mdia", mdia2)));
}

/// ftyp + mdat (`media`, chunk i = 4 bytes "c<i>..") + moov at the end, offsets + `bias`.
QByteArray file(int chunks, quint64 bias, bool co64, QVector<quint64> *offsetsOut, int muxerTail = -1)
{
    QByteArray f = box("ftyp", QByteArray("isom\0\0\x02\0isomavc1", 16));
    QByteArray media;
    QVector<quint64> offsets;
    const qint64 mdatStart = f.size() + 8;
    for (int i = 0; i < chunks; ++i) {
        offsets << quint64(mdatStart + media.size()) + bias;
        media.append(char('A' + i)); media.append(char(i)); media.append("xy", 2);
    }
    f.append(box("mdat", media));
    f.append(moov(offsets, co64, muxerTail));
    if (offsetsOut) *offsetsOut = offsets;
    return f;
}

bool write(const QString &path, const QByteArray &b)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(b) == b.size();
}
QByteArray read(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

/// Every offset of the rewritten file's chunk table, by a plain byte scan for the table box.
QVector<quint64> offsetsIn(const QByteArray &f, bool *wide)
{
    QVector<quint64> out;
    int at = f.indexOf("co64");
    *wide = at >= 0;
    if (at < 0) at = f.indexOf("stco");
    if (at < 0) return out;
    const char *p = f.constData() + at + 4;
    const quint32 n = be32(p + 4);
    for (quint32 i = 0; i < n; ++i) out << (*wide ? be64(p + 8 + 8 * i) : be32(p + 8 + 4 * i));
    return out;
}
}   // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir tmp;
    CHECK(tmp.isValid(), "a scratch dir");
    const QString in = tmp.filePath("in.mp4"), out = tmp.filePath("out.mp4");

    // ---- 1. the common case: a 32-bit table, shifted by the moov's size ----
    {
        QVector<quint64> offs;
        const QByteArray src = file(5, 0, false, &offs);
        CHECK(write(in, src), "write");
        const mp4::Facts before = mp4::inspect(in);
        CHECK(before.ok && !before.fastStart, "the encoder's shape: moov after mdat (%s)", qPrintable(before.error));
        QString err;
        CHECK(mp4::fastStart(in, out, &err), "fastStart: %s", qPrintable(err));
        const QByteArray dst = read(out);
        const mp4::Facts after = mp4::inspect(out);
        CHECK(after.ok && after.fastStart && after.moovOffset < after.mdatOffset, "moov first");
        CHECK(after.codec == "avc1" && after.width == 1920 && after.height == 1080, "the sample entry survives");
        CHECK(after.samples == 5 && after.constantRate && after.sampleDelta == 1000 && after.timescale == 60000,
              "the sample table survives");
        CHECK(dst.size() == src.size(), "same size (%lld vs %lld)", qlonglong(dst.size()), qlonglong(src.size()));
        bool wide = true;
        const QVector<quint64> moved = offsetsIn(dst, &wide);
        CHECK(!wide && moved.size() == 5, "still a 32-bit table of 5");
        for (int i = 0; i < moved.size(); ++i)
            CHECK(dst.at(int(moved[i])) == char('A' + i) && dst.at(int(moved[i]) + 1) == char(i),
                  "chunk %d points at its own bytes", i);
    }
    // ---- 2. a shift that overflows 32 bits: the table is widened to co64 ----
    {
        // Offsets biased just under 4 GiB (a synthetic table: the bytes they name are not in
        // this small file, so this checks the ARITHMETIC — every entry + the new moov size).
        const quint64 bias = 0xFFFFFF00ull;
        QVector<quint64> offs;
        const QByteArray src = file(3, bias, false, &offs);
        CHECK(write(in, src), "write");
        QString err;
        CHECK(mp4::fastStart(in, out, &err), "fastStart: %s", qPrintable(err));
        const QByteArray dst = read(out);
        bool wide = false;
        const QVector<quint64> moved = offsetsIn(dst, &wide);
        const mp4::Facts after = mp4::inspect(out);
        CHECK(wide, "the table was widened to co64");
        CHECK(after.ok && after.fastStart, "and the file is fast-start");
        const qint64 moovSize = after.mdatOffset - after.moovOffset;
        for (int i = 0; i < moved.size() && i < offs.size(); ++i)
            CHECK(moved[i] == offs[i] + quint64(moovSize), "entry %d moved by the WIDENED moov's size", i);
        CHECK(moved[0] > std::numeric_limits<quint32>::max(), "past 4 GiB, as it had to be");
    }
    // ---- 3. a co64 table already ----
    {
        QVector<quint64> offs;
        const QByteArray src = file(4, 0, true, &offs);
        CHECK(write(in, src), "write");
        QString err;
        CHECK(mp4::fastStart(in, out, &err), "fastStart: %s", qPrintable(err));
        const QByteArray dst = read(out);
        bool wide = false;
        const QVector<quint64> moved = offsetsIn(dst, &wide);
        CHECK(wide && moved.size() == 4, "co64 stays co64");
        for (int i = 0; i < moved.size(); ++i)
            CHECK(dst.at(int(moved[i])) == char('A' + i), "chunk %d points at its own bytes", i);
    }
    // ---- 4. already fast-start: the same bytes ----
    {
        QString err;
        CHECK(mp4::fastStart(out, tmp.filePath("again.mp4"), &err), "fastStart of a fast-start file: %s", qPrintable(err));
        CHECK(read(out) == read(tmp.filePath("again.mp4")), "copied unchanged");
    }
    // ---- 5. the muxer's tail: the last sample's guessed duration and a track sized past
    //         its frames, closed to exactly N frames everywhere (stts, mdhd, elst, tkhd, mvhd) ----
    for (int tail : { 0, 2000 }) {
        QVector<quint64> offs;
        const QByteArray src = file(6, 0, false, &offs, tail);
        CHECK(write(in, src), "write");
        const mp4::Facts raw = mp4::inspect(in);
        CHECK(raw.ok && raw.lastDelta == quint32(tail), "the muxer's last sample: %u", raw.lastDelta);
        QString err;
        CHECK(mp4::fastStart(in, out, &err), "fastStart: %s", qPrintable(err));
        const mp4::Facts f = mp4::inspect(out);
        CHECK(f.ok && f.fastStart, "fast-start");
        CHECK(f.constantRate && f.sampleDelta == 1000 && f.lastDelta == 1000, "every sample one step (last %u)", f.lastDelta);
        CHECK(f.duration == 6000, "mdhd: 6 frames at 1/60000 = 6000 (%llu)", (unsigned long long)f.duration);
        CHECK(qAbs(f.presentationMs - 100.0) < 1e-9, "mvhd: 100 ms (%f)", f.presentationMs);
        const QByteArray dst = read(out);
        const int e = dst.indexOf("elst");
        CHECK(e > 0 && be32(dst.constData() + e + 12) == 100 && be32(dst.constData() + e + 16) == 3000,
              "the one edit plays 100 ms from the B-frame offset");
        const int t = dst.indexOf("tkhd");
        CHECK(t > 0 && be32(dst.constData() + t + 24) == 100, "tkhd: 100 ms");
        bool wide = true;
        const QVector<quint64> moved = offsetsIn(dst, &wide);
        for (int i = 0; i < moved.size(); ++i)
            CHECK(dst.at(int(moved[i])) == char('A' + i), "chunk %d still points at its own bytes", i);
    }
    // ---- 6. a truncated moov: refused, never a crash ----
    {
        QByteArray src = file(3, 0, false, nullptr);
        src.chop(10);
        CHECK(write(in, src), "write");
        QString err;
        CHECK(!mp4::fastStart(in, out, &err) && !err.isEmpty(), "refused with a reason: %s", qPrintable(err));
        CHECK(!mp4::inspect(in).ok, "and inspect says so");
        CHECK(!mp4::inspect(tmp.filePath("absent.mp4")).ok, "an absent file is not ok");
    }
    std::printf("%d check(s), %d failure(s)\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
