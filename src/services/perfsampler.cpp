/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/perfsampler.h"

#include "bridge/enginehost.h"
#include "data/settingsmanager.h"
#include "services/engineerrorpump.h"
#include "services/jahlog.h"
#include "services/mainthreadheartbeat.h"
#include "viewport/enginerenderdriver.h"

#include "irisgl/document/scenegraph/scene.h"

#include <QDateTime>
#include <QFile>
#include <QTimer>

namespace {

/// Resident set size in MB, or -1 where we cannot know.
///
/// LINUX ONLY IN V1, stated rather than faked. /proc/self/statm's second field
/// is the resident page count; multiplied by the page size that is the number
/// a leak hunt actually wants. macOS (task_info/TASK_BASIC_INFO) and Windows
/// (GetProcessMemoryInfo) are deliberately NOT here — the field prints "n/a"
/// there, which is honest, and inventing a per-platform memory story inside a
/// logging feature is how a logging feature becomes a platform feature.
double residentMb()
{
#ifdef Q_OS_LINUX
    QFile statm(QStringLiteral("/proc/self/statm"));
    if (!statm.open(QIODevice::ReadOnly)) return -1.0;
    const QByteArray line = statm.readLine();
    const QList<QByteArray> fields = line.simplified().split(' ');
    if (fields.size() < 2) return -1.0;
    bool ok = false;
    const qulonglong pages = fields[1].toULongLong(&ok);
    if (!ok) return -1.0;
    return double(pages) * 4096.0 / (1024.0 * 1024.0);
#else
    return -1.0;
#endif
}

/// The moment the sampler was constructed — "since-open" in the line is really
/// "since this session started measuring", which is what makes two samples an
/// hour apart comparable.
qint64 &startMs()
{
    static qint64 ms = QDateTime::currentMSecsSinceEpoch();
    return ms;
}

}   // namespace

PerfSampler::PerfSampler(QObject *parent) : QObject(parent)
{
    startMs();
    mTimer = new QTimer(this);
    connect(mTimer, &QTimer::timeout, this, [this] { sampleNow(); });
}

int PerfSampler::defaultSeconds()
{
#ifdef QT_DEBUG
    return 60;      // the daily driver: a sample a minute
#else
    return 300;     // a shipped tool: a sample every five
#endif
}

void PerfSampler::start(int seconds)
{
    mSeconds = seconds;
    if (seconds <= 0) { mTimer->stop(); return; }
    mTimer->start(seconds * 1000);
}

void PerfSampler::stop() { mSeconds = 0; mTimer->stop(); }
bool PerfSampler::isRunning() const { return mTimer->isActive(); }

void PerfSampler::startFromSettings()
{
    SettingsManager *sm = SettingsManager::getDefaultManager();
    const int seconds =
        sm ? sm->getValue(QStringLiteral("log/perfSampleSeconds"), defaultSeconds()).toInt()
           : defaultSeconds();
    start(seconds);
}

QString PerfSampler::sampleNow()
{
    // Everything read here is a counter someone else already maintains. The
    // sampler adds no measurement of its own, which is why it can be this
    // cheap and why its numbers agree with the verbs by construction.
    EngineRenderDriver::Stats fs;
    if (auto *driver = EngineHost::instance().driver()) fs = driver->stats();

    QString line = QStringLiteral("fps %1 work %2ms worst %3ms slow %4")
                       .arg(0.0, 0, 'f', 1)
                       .arg(fs.workMs, 0, 'f', 1)
                       .arg(fs.worstMs, 0, 'f', 1)
                       .arg(fs.slowFrames);

    if (auto engine = EngineHost::instance().engine()) {
        jahshaka::engine::RenderStats rs;
        if (engine->renderStats(rs)) {
            line = QStringLiteral("fps %1 work %2ms worst %3ms slow %4")
                       .arg(rs.fps, 0, 'f', 1)
                       .arg(fs.workMs, 0, 'f', 1)
                       .arg(fs.worstMs, 0, 'f', 1)
                       .arg(fs.slowFrames);
            line += QStringLiteral(" | draws %1 tris %2")
                        .arg(qulonglong(rs.draws))
                        .arg(qulonglong(rs.triangles));
        }
    }

    line += QStringLiteral(" | rendered %1 skipped %2").arg(fs.rendered).arg(fs.skipped);

    const QVariantMap errors = EngineErrorPump::instance().report();
    line += QStringLiteral(" | engineErrors %1")
                .arg(errors.value(QStringLiteral("recorded")).toULongLong());

    const QVariantMap hb = MainThreadHeartbeat::stats();
    if (hb.value(QStringLiteral("running")).toBool())
        line += QStringLiteral(" | uiMaxGap %1ms")
                    .arg(hb.value(QStringLiteral("maxGapMs")).toDouble(), 0, 'f', 0);

    const double rss = residentMb();
    line += QStringLiteral(" | rss %1")
                .arg(rss < 0 ? QStringLiteral("n/a")
                             : QStringLiteral("%1MB").arg(rss, 0, 'f', 0));

    const qint64 minutes = (QDateTime::currentMSecsSinceEpoch() - startMs()) / 60000;
    line += QStringLiteral(" | since-start %1m").arg(minutes);

    JAH_LOG(JahLog::perf, Log, line);
    return line;
}
