// SettingsStore — read data/settingsstore.h first.
#include "data/settingsstore.h"

#include <QSettings>

#include <chrono>

namespace {

/// QSettings' key normalisation: empty segments go ("a//b/" -> "a/b").
QString normalised(const QString &key)
{
    return key.split(QLatin1Char('/'), Qt::SkipEmptyParts).join(QLatin1Char('/'));
}

}  // namespace

SettingsStore::SettingsStore(const QString &path) : mPath(path)
{
    // ONE READ, here, before anything draws (the store is built before the
    // splash). A read takes no lock file and syncs nothing.
    {
        QSettings file(mPath, QSettings::IniFormat);
        const QStringList keys = file.allKeys();
        for (const QString &k : keys) mValues.insert(normalised(k), file.value(k));
    }
    mWriter = std::thread([this] { writerLoop(); });
}

SettingsStore::~SettingsStore()
{
    flush();
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mStop = true;
    }
    mWake.notify_all();
    if (mWriter.joinable()) mWriter.join();
}

QString SettingsStore::absolute(const QString &key) const
{
    const QString k = normalised(key);
    const QString g = group();
    if (g.isEmpty()) return k;
    return k.isEmpty() ? g : g + QLatin1Char('/') + k;
}

QVariant SettingsStore::value(const QString &key, const QVariant &fallback) const
{
    const auto it = mValues.constFind(absolute(key));
    return it == mValues.constEnd() ? fallback : it.value();
}

bool SettingsStore::contains(const QString &key) const
{
    return mValues.contains(absolute(key));
}

void SettingsStore::setValue(const QString &key, const QVariant &value)
{
    const QString k = absolute(key);
    if (k.isEmpty()) return;
    mValues.insert(k, value);
    enqueue({ false, k, value });
}

void SettingsStore::remove(const QString &key)
{
    const QString k = absolute(key);
    if (k.isEmpty()) {
        mValues.clear();
    } else {
        const QString prefix = k + QLatin1Char('/');
        for (auto it = mValues.begin(); it != mValues.end();) {
            if (it.key() == k || it.key().startsWith(prefix)) it = mValues.erase(it);
            else ++it;
        }
    }
    enqueue({ true, k, QVariant() });
}

void SettingsStore::beginGroup(const QString &prefix)
{
    mGroups.append(normalised(prefix));
}

void SettingsStore::endGroup()
{
    if (!mGroups.isEmpty()) mGroups.removeLast();
}

QString SettingsStore::group() const
{
    QStringList parts;
    for (const QString &g : mGroups)
        if (!g.isEmpty()) parts.append(g);
    return parts.join(QLatin1Char('/'));
}

QStringList SettingsStore::allKeys() const
{
    const QString g = group();
    const QString prefix = g.isEmpty() ? QString() : g + QLatin1Char('/');
    QStringList out;
    for (auto it = mValues.constBegin(); it != mValues.constEnd(); ++it)
        if (it.key().startsWith(prefix)) out.append(it.key().mid(prefix.size()));
    return out;
}

QStringList SettingsStore::childKeys() const
{
    QStringList out;
    for (const QString &k : allKeys())
        if (!k.contains(QLatin1Char('/'))) out.append(k);
    return out;
}

QStringList SettingsStore::childGroups() const
{
    QStringList out;
    for (const QString &k : allKeys()) {
        const int slash = k.indexOf(QLatin1Char('/'));
        if (slash > 0 && !out.contains(k.left(slash))) out.append(k.left(slash));
    }
    return out;
}

void SettingsStore::enqueue(Op op)
{
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mQueue.push_back(std::move(op));
    }
    mWake.notify_one();
}

bool SettingsStore::flush(int budgetMs)
{
    std::unique_lock<std::mutex> lock(mMutex);
    return mIdle.wait_for(lock, std::chrono::milliseconds(budgetMs),
                          [this] { return mQueue.empty() && mInFlight == 0; });
}

int SettingsStore::pendingWrites() const
{
    std::lock_guard<std::mutex> lock(mMutex);
    return int(mQueue.size()) + mInFlight;
}

int SettingsStore::completedWrites() const
{
    std::lock_guard<std::mutex> lock(mMutex);
    return mCompleted;
}

void SettingsStore::writerLoop()
{
    for (;;) {
        std::deque<Op> batch;
        {
            std::unique_lock<std::mutex> lock(mMutex);
            mWake.wait(lock, [this] { return mStop || !mQueue.empty(); });
            if (mQueue.empty()) return;   // stopping, and nothing left to write
            batch.swap(mQueue);
            mInFlight = int(batch.size());
        }
        // A QSettings OF THIS THREAD'S OWN, per batch: Qt's INI writer, its
        // lock file and its atomic commit, re-reading the file first when it
        // changed on disk — so another process's keys survive exactly as they
        // did when every QSettings synced itself. The object dies with the
        // batch; its sync below leaves it nothing pending, so its destructor
        // writes nothing.
        bool ok = true;
        {
            QSettings file(mPath, QSettings::IniFormat);
            for (const Op &op : batch) {
                if (op.remove) {
                    if (op.key.isEmpty()) file.clear();
                    else file.remove(op.key);
                } else {
                    file.setValue(op.key, op.value);
                }
            }
            file.sync();
            ok = file.status() == QSettings::NoError;
        }
        if (!ok)
            qWarning("settings: could not write %s — the change stays in memory for this session",
                     qPrintable(mPath));
        {
            std::lock_guard<std::mutex> lock(mMutex);
            mInFlight = 0;
            ++mCompleted;
        }
        mIdle.notify_all();
    }
}
