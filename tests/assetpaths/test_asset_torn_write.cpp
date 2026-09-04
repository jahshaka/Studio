// assets.torn_write — STABILITY_PROGRAM_SPEC.md Lane 2.
//
// The CAS names an object after the sha256 of its bytes, and NOTHING re-hashes
// on read (resolveSource/resolveFile check existence and stop). A file that is
// only partly written is therefore a file that LIES: every later run believes
// it, the texture decodes as nothing, the material renders unmapped, and — this
// is the part that made it worth a lane — not one log line is produced
// (the other end of that story is engine.error_pump).
//
// AssetCas::storeObject had two ways to leave one behind:
//   * QFile::copy straight to the final content-addressed path, and
//   * for the replace case, a QFile::remove of the good object BEFORE its
//     replacement existed.
// Both are closed by staging into a sibling temp and moving it in with one
// rename. This suite asserts the property that fix buys, not its
// implementation: AT NO INSTANT does the final path hold anything but a
// complete object.
//
// THE KILL IS NOT TIMED. A wall-clock sleep would make this suite a coin flip
// (spec §3.9). The child stores from a FIFO on /dev/shm: /dev/shm is a
// different filesystem, so the ::link() fast path fails EXDEV and the copy path
// is the one under test, and a FIFO with a writer that stops writing blocks the
// copy for as long as we like. The child is therefore provably INSIDE the copy
// when it is killed.
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "services/assetcas.h"
#include "services/assetstorepaths.h"
#include "services/filewriteatomic.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

static QString writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return QString();
    f.write(bytes);
    f.close();
    return path;
}

/// Every file under <root>/objects — the whole store, temps included.
static QStringList objectTree(const QString &root)
{
    QStringList out;
    QDirIterator it(QDir(root).filePath(QStringLiteral("objects")),
                    QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) out << it.next();
    out.sort();
    return out;
}

static QStringList stagingLeftovers(const QString &root)
{
    QStringList out;
    for (const QString &p : objectTree(root))
        if (QFileInfo(p).fileName().contains(QStringLiteral(".tmp-"))) out << p;
    return out;
}

/// Staging temps anywhere under a directory (not just objects/) — the sidecar
/// and store.json writers stage beside their own targets.
static QStringList tempsUnder(const QString &dir)
{
    QStringList out;
    QDirIterator it(dir, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString p = it.next();
        if (QFileInfo(p).fileName().contains(QStringLiteral(".tmp-"))) out << p;
    }
    out.sort();
    return out;
}

static QByteArray readAll(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QByteArray();
    return f.readAll();
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication app(argc, argv);

    const QString root = QDir(QDir::currentPath()).filePath(QStringLiteral("torn-store"));
    QDir(root).removeRecursively();
    QDir().mkpath(root);

    // ---- 1. the ordinary store, and what it must NOT leave behind ------------
    const QByteArray payload("jahshaka torn-write gate payload\n", 33);
    const QString src = writeFile(QDir(root).filePath(QStringLiteral("src/thing.bin")), payload);
    CHECK(!src.isEmpty(), "wrote a source file");

    const QString oid = AssetCas::hashFile(src);
    CHECK(oid.length() == 64, "hashFile gives a sha256");

    QString err;
    CHECK(AssetCas::storeObject(src, root, oid, QStringLiteral("bin"), &err),
          "storeObject succeeds");
    const QString dst = AssetStorePaths::objectPathIn(root, oid, QStringLiteral("bin"));
    CHECK(QFileInfo::exists(dst), "the object is at its content-addressed path");
    CHECK(AssetCas::hashFile(dst) == oid, "and its bytes hash to the name it was given");
    CHECK(stagingLeftovers(root).isEmpty(), "a successful store leaves no staging temp");

    CHECK(AssetCas::storeObject(src, root, oid, QStringLiteral("bin"), &err),
          "storing the same content again is a no-op that succeeds (the dedup)");
    CHECK(objectTree(root).size() == 1, "and creates no second file");

    // ---- 2. a FAILED store must not touch the final path ---------------------
    {
        const QString missingOid(64, QLatin1Char('a'));
        const QString missingDst = AssetStorePaths::objectPathIn(root, missingOid,
                                                                 QStringLiteral("bin"));
        err.clear();
        CHECK(!AssetCas::storeObject(QDir(root).filePath(QStringLiteral("src/nope.bin")),
                                     root, missingOid, QStringLiteral("bin"), &err),
              "storing a source that does not exist fails");
        CHECK(!err.isEmpty(), "with a reason");
        CHECK(!QFileInfo::exists(missingDst), "and creates NOTHING at the final path");
        CHECK(stagingLeftovers(root).isEmpty(), "and cleans up its own staging temp");
    }

    // ---- 3. replacing a torn object never leaves the store empty -------------
    // The old code removed the existing object first; a crash in that window
    // destroyed a good object outright. The rename-based store overwrites.
    {
        // A previously torn object: right name, wrong (short) bytes. The remove
        // first is not optional — the stored object is a HARDLINK to the source
        // on this filesystem, so writing through the name would truncate the
        // source too and the test would be measuring itself.
        QFile::remove(dst);
        writeFile(dst, payload.left(7));
        CHECK(AssetCas::hashFile(dst) != oid, "planted a torn object under a good name");
        CHECK(AssetCas::storeObject(src, root, oid, QStringLiteral("bin"), &err),
              "storeObject replaces it (the size mismatch is the trigger)");
        CHECK(AssetCas::hashFile(dst) == oid, "and the object now hashes to its name again");
        CHECK(stagingLeftovers(root).isEmpty(), "with no staging temp left over");
    }

    // ---- 4. kill -9 INSIDE the copy ------------------------------------------
    const QString fifo = QStringLiteral("/dev/shm/jah-torn-%1.fifo").arg(::getpid());
    ::unlink(QFile::encodeName(fifo).constData());
    const bool haveFifo = ::mkfifo(QFile::encodeName(fifo).constData(), 0600) == 0;
    CHECK(haveFifo, "made a FIFO on /dev/shm (a different filesystem, so ::link cannot win)");

    if (haveFifo) {
        // The oid is a lie on purpose: the point is what lands at the path, not
        // what the content is. Reuse the good object's neighbours so the shard
        // directory already exists.
        const QString fifoOid(64, QLatin1Char('b'));
        const QString fifoDst = AssetStorePaths::objectPathIn(root, fifoOid,
                                                             QStringLiteral("bin"));

        int ready[2];
        CHECK(::pipe(ready) == 0, "made the child's ready pipe");
        const pid_t child = ::fork();
        CHECK(child >= 0, "forked a child to be killed mid-copy");

        if (child == 0) {
            ::close(ready[0]);
            const char go = 'g';
            (void)!::write(ready[1], &go, 1);
            QString cerr;
            // Blocks: opening the FIFO for reading waits for a writer, and the
            // read waits for bytes that stop coming.
            AssetCas::storeObject(fifo, root, fifoOid, QStringLiteral("bin"), &cerr);
            ::_exit(0);
        }

        ::close(ready[1]);
        char go = 0;
        (void)!::read(ready[0], &go, 1);
        ::close(ready[0]);

        // Open for writing (this unblocks the child's open), push more than a
        // pipe buffer so the child is definitely reading, then stop.
        int w = ::open(QFile::encodeName(fifo).constData(), O_WRONLY);
        CHECK(w >= 0, "opened the FIFO for writing, which unblocks the child's copy");
        QByteArray chunk(64 * 1024, 'x');
        long total = 0;
        for (int i = 0; i < 16 && w >= 0; ++i) {
            const ssize_t n = ::write(w, chunk.constData(), size_t(chunk.size()));
            if (n <= 0) break;
            total += n;
        }
        CHECK(total > 256 * 1024, "pushed a megabyte through it, so the child is mid-copy");

        // The child is blocked reading a FIFO nobody is writing to. Kill it there.
        CHECK(::kill(child, SIGKILL) == 0, "SIGKILL, with the copy in flight");
        int status = 0;
        ::waitpid(child, &status, 0);
        CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL,
              "the child really died on the signal, inside storeObject");
        if (w >= 0) ::close(w);

        CHECK(!QFileInfo::exists(fifoDst),
              "NOTHING is at the content-addressed path — the object either exists whole "
              "or does not exist");
        // A killed process cannot clean up after itself; what it may leave is a
        // temp, and a temp is inert: guidForStorePath rejects it (its
        // completeBaseName is not 64 hex chars) and no resolver will ever open it.
        for (const QString &leftover : stagingLeftovers(root))
            std::printf("info: staging leftover from the killed run: %s\n",
                        qPrintable(QFileInfo(leftover).fileName()));

        // The store's other object survived the whole thing untouched.
        CHECK(QFileInfo::exists(dst) && AssetCas::hashFile(dst) == oid,
              "and the object stored earlier is still intact");

        // ---- 5. the DESTRUCTIVE window, which is the half a crash used to lose
        // An object already exists and is being REPLACED (the size-mismatch
        // repair path). The old code removed it and only then started writing,
        // so a crash in between left the store with no object at all — worse
        // than the torn write it was repairing. Same deterministic kill, and
        // now the assertion is about what is STILL THERE.
        {
            const QString repOid(64, QLatin1Char('c'));
            const QString repDst = AssetStorePaths::objectPathIn(root, repOid,
                                                                 QStringLiteral("bin"));
            const QByteArray good("the object that must survive the crash\n", 39);
            writeFile(repDst, good);
            CHECK(QFileInfo(repDst).size() == good.size(), "an object is in the store already");

            int ready2[2];
            (void)!::pipe(ready2);
            const pid_t child2 = ::fork();
            if (child2 == 0) {
                ::close(ready2[0]);
                const char go2 = 'g';
                (void)!::write(ready2[1], &go2, 1);
                QString cerr;
                // A FIFO reads as size 0, so this is the REPLACE path.
                AssetCas::storeObject(fifo, root, repOid, QStringLiteral("bin"), &cerr);
                ::_exit(0);
            }
            ::close(ready2[1]);
            char go2 = 0;
            (void)!::read(ready2[0], &go2, 1);
            ::close(ready2[0]);

            int w2 = ::open(QFile::encodeName(fifo).constData(), O_WRONLY);
            long total2 = 0;
            for (int i = 0; i < 16 && w2 >= 0; ++i) {
                const ssize_t n = ::write(w2, chunk.constData(), size_t(chunk.size()));
                if (n <= 0) break;
                total2 += n;
            }
            CHECK(total2 > 256 * 1024, "the replacement copy is in flight");
            CHECK(::kill(child2, SIGKILL) == 0, "SIGKILL, mid-replace");
            int status2 = 0;
            ::waitpid(child2, &status2, 0);
            if (w2 >= 0) ::close(w2);

            CHECK(QFileInfo::exists(repDst),
                  "the object being replaced is STILL THERE — a crash mid-replace "
                  "never empties the store");
            QFile check(repDst);
            check.open(QIODevice::ReadOnly);
            CHECK(check.readAll() == good, "and it is byte-for-byte the old content");
        }

        // ---- 6. the SAME property for every other store artifact -------------
        // Deep audit 2026-09 (area 6): sidecars, store.json and the baked maps
        // all wrote truncate-in-place — the exact pattern storeObject exists to
        // prevent. The tail of storeObject is now FileWrite::writeFileAtomic
        // (src/services/filewriteatomic.h) and they go through it. Same
        // deterministic kill, same assertion: the previous file is either
        // completely replaced or completely untouched, never truncated.
        {
            const QString metaDir = QDir(root).filePath(QStringLiteral("meta"));
            const QString target = QDir(metaDir).filePath(QStringLiteral("sidecar.json"));
            const QByteArray v1("{\"formatVersion\":1,\"files\":[\"a\",\"b\"]}\n");
            const QByteArray v2("{\"formatVersion\":1,\"files\":[\"a\",\"b\",\"c\",\"d\"]}\n");

            QString werr;
            CHECK(FileWrite::writeFileAtomic(target, v1, &werr),
                  "writeFileAtomic creates the file (and its directory)");
            CHECK(readAll(target) == v1, "with exactly the bytes asked for");
            CHECK(tempsUnder(metaDir).isEmpty(), "and leaves no staging temp");

            CHECK(FileWrite::writeFileAtomic(target, v2, &werr),
                  "writeFileAtomic REPLACES an existing file");
            CHECK(readAll(target) == v2, "with the new bytes");
            CHECK(tempsUnder(metaDir).isEmpty(), "still no staging temp");

            werr.clear();
            CHECK(!FileWrite::writeFileAtomic(target, [](QFile &) { return false; }, &werr),
                  "a writer that fails reports failure");
            CHECK(readAll(target) == v2,
                  "and the EXISTING file is untouched (never truncated first)");
            CHECK(tempsUnder(metaDir).isEmpty(), "and its temp is cleaned up");

            // Kill the process inside the write, exactly as sections 4/5 do.
            int ready3[2];
            (void)!::pipe(ready3);
            const pid_t child3 = ::fork();
            if (child3 == 0) {
                ::close(ready3[0]);
                const char go3 = 'g';
                (void)!::write(ready3[1], &go3, 1);
                QString cerr;
                // Streams from the FIFO into the staged temp — blocks forever
                // once the parent stops writing, so the kill is provably inside
                // the write and not a race.
                FileWrite::writeFileAtomic(target, [&](QFile &out) {
                    const int fd = ::open(QFile::encodeName(fifo).constData(), O_RDONLY);
                    if (fd < 0) return false;
                    QByteArray buf(64 * 1024, Qt::Uninitialized);
                    for (;;) {
                        const ssize_t n = ::read(fd, buf.data(), size_t(buf.size()));
                        if (n <= 0) break;
                        out.write(buf.constData(), n);
                    }
                    ::close(fd);
                    return true;
                }, &cerr);
                ::_exit(0);
            }
            ::close(ready3[1]);
            char go3 = 0;
            (void)!::read(ready3[0], &go3, 1);
            ::close(ready3[0]);

            int w3 = ::open(QFile::encodeName(fifo).constData(), O_WRONLY);
            CHECK(w3 >= 0, "opened the FIFO, which unblocks the child's atomic write");
            long total3 = 0;
            for (int i = 0; i < 16 && w3 >= 0; ++i) {
                const ssize_t n = ::write(w3, chunk.constData(), size_t(chunk.size()));
                if (n <= 0) break;
                total3 += n;
            }
            CHECK(total3 > 256 * 1024, "the atomic write is in flight");
            CHECK(::kill(child3, SIGKILL) == 0, "SIGKILL, mid-write");
            int status3 = 0;
            ::waitpid(child3, &status3, 0);
            if (w3 >= 0) ::close(w3);

            CHECK(readAll(target) == v2,
                  "the sidecar the crash interrupted still holds its COMPLETE previous "
                  "content — the truncate-in-place writers could only have left a stub");
        }

        ::unlink(QFile::encodeName(fifo).constData());
    }

    QDir(root).removeRecursively();
    std::printf(failures ? "FAILED: %d check(s)\n" : "assets.torn_write: PASS\n", failures);
    return failures ? 1 : 0;
}
