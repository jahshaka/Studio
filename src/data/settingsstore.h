#ifndef SETTINGSSTORE_H
#define SETTINGSSTORE_H

// THE USER'S PREFERENCES, WITH NO FILE SYNC ON THE UI THREAD (SHADER-WARM-2).
//
// The owner's phase-2 smoke froze for 16 s on the Desktop page: the UI thread
// was inside QSettings::event -> QLockFile::tryLock, a QSettings auto-sync,
// queued behind the shader cache writer's 19 s fsync on a loaded disk
// (spikes/owner-smoke-phase2.log:1045-1072). QSettings cannot be fixed from the
// outside: its sync runs on the thread its object lives on, through a lock
// file and a QSaveFile commit (fdatasync + rename), and it holds the per-file
// QConfFile mutex for the whole I/O — so moving the object to a worker would
// only move the stall to the next UI-thread value() (Qt 6.10.2 qsettings.cpp,
// QConfFileSettingsPrivate::sync / syncConfFile).
//
// So this is a store of our own, shaped like the slice of QSettings the app
// uses. READS AND WRITES ARE MEMORY: the file is read once, at construction,
// into a map, and value()/setValue()/remove() touch only that map. DURABILITY
// IS A WORKER'S: every change is queued as an operation (set or remove) and a
// thread of the store's own applies the queued operations to a QSettings of
// ITS OWN on the same file and syncs it — Qt's own INI writer, lock file and
// atomic commit, so the file's format and its merge with other processes'
// writes are exactly what they were. Operations arriving while a sync runs
// are batched into the next one; there is no timer.
//
// flush() waits for the worker (the clean quit, and a test that reads the
// file back). The UI thread calls it exactly once in a session, at the
// ordered exit, where the window is already gone.
//
// Not shared across threads: every method but flush()/pendingWrites() is for
// the thread that created the store (the UI thread, in the app).
//
// TWO CONSEQUENCES, both deliberate. (1) The file is read once: a hand edit of
// the ini while the app runs is not seen until the next launch (and a key the
// session also writes is merged by Qt at the next write, per key). (2) No
// other QSettings on this path belongs in the process: Qt shares one object
// and one mutex per file, and the writer holds that mutex for the whole sync —
// a QSettings read here would be exactly the wait this store removes (the
// suite reads the file back as bytes for that reason).

#include "irisgl/core/keyvaluestore.h"

#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

class SettingsStore : public iris::KeyValueStore
{
public:
    /// Reads `path` (an INI file; absent = empty) and starts the writer.
    explicit SettingsStore(const QString &path);
    /// Flushes (waits for every queued write) and stops the writer.
    ~SettingsStore() override;

    SettingsStore(const SettingsStore &) = delete;
    SettingsStore &operator=(const SettingsStore &) = delete;

    // ---- the QSettings-shaped surface (keys are relative to group()) ------
    QVariant value(const QString &key, const QVariant &fallback = QVariant()) const override;
    bool contains(const QString &key) const override;
    void setValue(const QString &key, const QVariant &value) override;
    /// The key and every key under "key/"; an EMPTY key removes everything in
    /// the current group (QSettings::remove's contract).
    void remove(const QString &key) override;

    void beginGroup(const QString &prefix);
    void endGroup();
    QString group() const;
    QStringList allKeys() const;
    QStringList childKeys() const;
    QStringList childGroups() const;
    QString fileName() const { return mPath; }

    // ---- durability -------------------------------------------------------
    /// Blocks until every change made so far is in the file (or the budget
    /// runs out). True when nothing is left to write.
    bool flush(int budgetMs = 20000);
    /// Writes queued or in flight (diagnostics; any thread).
    int pendingWrites() const;
    /// Syncs the writer has completed this session (diagnostics; any thread).
    int completedWrites() const;

private:
    struct Op {
        bool remove = false;
        QString key;      // absolute (the group already applied)
        QVariant value;
    };

    QString absolute(const QString &key) const;
    void enqueue(Op op);
    void writerLoop();

    QString mPath;
    QMap<QString, QVariant> mValues;   // absolute key -> value; the UI thread's
    QStringList mGroups;               // beginGroup stack (each already normalised)

    mutable std::mutex mMutex;
    std::condition_variable mWake;     // the writer: work arrived, or stop
    std::condition_variable mIdle;     // flush(): the writer drained the queue
    std::deque<Op> mQueue;             // GUARDED_BY(mMutex)
    int mInFlight = 0;                 // ops taken by the writer, not yet synced
    int mCompleted = 0;
    bool mStop = false;
    std::thread mWriter;
};

#endif // SETTINGSSTORE_H
