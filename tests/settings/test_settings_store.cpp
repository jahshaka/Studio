// settings.store — SettingsStore (src/data/settingsstore.h, SHADER-WARM-2).
//
//   reads the file once        §1  a seeded INI comes back, groups and all
//   memory semantics           §2  set/read/contains/remove/groups, QSettings' contract
//   durability                 §3  flush() puts every change in the file; the destructor too
//   other writers survive      §4  a key another process wrote is kept (Qt's merge)
//   THE CALLING THREAD NEVER   §5  the test HOLDS the file's lock: every set returns at
//   WAITS FOR THE FILE             once, the write waits on the store's thread, and lands
//                                  the moment the lock goes
#include "data/settingsstore.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QLockFile>
#include <QSettings>
#include <QTemporaryDir>

#include <chrono>
#include <cstdio>
#include <thread>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (cond) std::printf("  ok    %s\n", msg); \
    else { std::printf("  FAIL  %s  (%s:%d)\n", msg, __FILE__, __LINE__); ++failures; } \
} while (0)

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    CHECK(dir.isValid(), "a scratch directory");

    // ---- §1 the file is read once, at construction ------------------------
    const QString path = dir.filePath("jahsettings.ini");
    {
        QSettings seed(path, QSettings::IniFormat);
        seed.setValue("show_fps", true);
        seed.setValue("log/db", "warning");
        seed.setValue("log/mirror", "verbose");
        seed.setValue("snap/translate", 0.5);
        seed.sync();
    }
    {
        SettingsStore s(path);
        CHECK(s.value("show_fps").toBool(), "a top-level key comes back");
        CHECK(s.value("log/db").toString() == "warning", "a grouped key comes back");
        CHECK(s.value("missing", 7).toInt() == 7, "an absent key reads as the caller's fallback");
        CHECK(s.fileName() == path, "fileName() is the path it was given");

        // ---- §2 memory semantics (QSettings' contract) --------------------
        s.setValue("a/b/c", 3);
        CHECK(s.value("a/b/c").toInt() == 3 && s.contains("a/b/c"), "a set is readable at once");
        CHECK(s.value("//a//b/c/").toInt() == 3, "keys are normalised like QSettings'");
        s.beginGroup("log");
        QStringList keys = s.childKeys();
        keys.sort();
        CHECK(keys == QStringList({ "db", "mirror" }), "childKeys() inside a group");
        CHECK(s.value("db").toString() == "warning", "a read inside a group is relative to it");
        s.endGroup();
        CHECK(s.childGroups().contains("log") && s.childGroups().contains("a"), "childGroups()");
        s.setValue("a/b/d", 4);
        s.remove("a/b");
        CHECK(!s.contains("a/b/c") && !s.contains("a/b/d"), "remove(key) takes every key under key/");
        s.beginGroup("snap");
        s.remove(QString());
        s.endGroup();
        CHECK(!s.contains("snap/translate"), "remove(\"\") inside a group empties the group");
        CHECK(s.contains("log/db"), "...and nothing outside it");

        // ---- §3 durability ------------------------------------------------
        s.setValue("camera/speed", 23);
        CHECK(s.flush(), "flush() drains the writer");
        QSettings back(path, QSettings::IniFormat);
        CHECK(back.value("camera/speed").toInt() == 23, "the file has the set after flush()");
        CHECK(!back.contains("a/b/c") && !back.contains("snap/translate"), "...and not the removed keys");
        CHECK(back.value("log/db").toString() == "warning", "...and still the untouched ones");
    }
    {
        {
            SettingsStore s(path);
            s.setValue("closing/key", "kept");
        }   // no flush: the destructor owes the file the write
        QSettings back(path, QSettings::IniFormat);
        CHECK(back.value("closing/key").toString() == "kept", "the destructor writes what was owed");
    }

    // ---- §4 another writer's keys survive -----------------------------------
    {
        SettingsStore s(path);
        {
            QSettings other(path, QSettings::IniFormat);   // "another process"
            other.setValue("other/key", "theirs");
            other.sync();
        }
        s.setValue("mine/key", "ours");
        CHECK(s.flush(), "flush()");
        QSettings back(path, QSettings::IniFormat);
        CHECK(back.value("other/key").toString() == "theirs", "a key written by another writer survives our write");
        CHECK(back.value("mine/key").toString() == "ours", "...beside ours");
    }

    // ---- §5 THE CALLING THREAD NEVER WAITS FOR THE FILE ---------------------
    // QSettings writes under "<file>.lock". Holding it here is exactly a sync
    // that cannot finish — the owner's 16 s freeze was a UI thread waiting in
    // QLockFile::tryLock. With a QSettings this loop would never return.
    {
        SettingsStore s(path);
        QLockFile held(path + ".lock");
        CHECK(held.tryLock(0), "the test holds the settings file's lock");
        QElapsedTimer t;
        t.start();
        for (int i = 0; i < 100; ++i) s.setValue("burst/n", i);
        s.remove("mine");
        const qint64 ms = t.elapsed();
        std::printf("    100 sets + a remove with the file locked: %lld ms\n", static_cast<long long>(ms));
        CHECK(s.value("burst/n").toInt() == 99, "every set is readable at once while the file is locked");
        CHECK(!s.flush(300), "...and the write is still waiting (on the store's thread) for the lock");
        CHECK(s.pendingWrites() > 0, "pendingWrites() says so");
        {
            // RAW BYTES, not a QSettings: a QSettings on the same path in this
            // process shares Qt's per-file object and its mutex, which the
            // store's writer holds for the whole (here: blocked) sync — the very
            // wait the store keeps off the UI thread.
            QFile raw(path);
            CHECK(raw.open(QIODevice::ReadOnly) && !raw.readAll().contains("burst"),
                  "the file does not have it yet");
        }
        held.unlock();
        CHECK(s.flush(), "the lock goes, and the writer drains");
        QSettings back(path, QSettings::IniFormat);
        CHECK(back.value("burst/n").toInt() == 99, "the last value of the burst is the file's");
        CHECK(!back.contains("mine/key"), "...and the remove landed with it");
    }

    // ---- §6 A BURST IS ONE WRITE -------------------------------------------
    // A wheel's notches and a slider's drag are dozens of sets a second; each
    // write is a QSaveFile commit (fdatasync + rename). The writer waits its
    // coalescing window after the first op, so the burst lands as ONE write —
    // without anybody flushing (bounded wait, it must land by itself), and the
    // same through flush().
    {
        SettingsStore s(path);
        CHECK(s.flush(), "idle before the burst");
        int before = s.completedWrites();
        for (int i = 0; i < 50; ++i) s.setValue("burst/drag", i);
        QElapsedTimer t;
        t.start();
        while (s.completedWrites() == before && t.elapsed() < 5000)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        CHECK(s.completedWrites() == before + 1, "50 sets with nobody flushing land as ONE write");
        CHECK(s.flush() && s.completedWrites() == before + 1, "...and nothing is left to write");
        before = s.completedWrites();
        for (int i = 0; i < 50; ++i) s.setValue("burst/drag", 100 + i);
        CHECK(s.flush(), "flush() cuts the coalescing window short");
        CHECK(s.completedWrites() == before + 1, "50 sets then a flush: ONE write");
        QSettings back(path, QSettings::IniFormat);
        CHECK(back.value("burst/drag").toInt() == 149, "...carrying the last value");
    }

    // ---- §7 shutdown() drains and joins; a set after it stays in memory ------
    {
        SettingsStore s(path);
        s.setValue("shut/down", 1);
        s.shutdown();
        {
            QSettings back(path, QSettings::IniFormat);
            CHECK(back.value("shut/down").toInt() == 1, "shutdown() wrote what was queued");
        }
        s.setValue("after/shutdown", 1);
        CHECK(s.value("after/shutdown").toInt() == 1, "a set after shutdown() is still readable");
        s.shutdown();   // idempotent; the destructor calls it again
    }

    std::printf("%s (%d failure(s))\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
