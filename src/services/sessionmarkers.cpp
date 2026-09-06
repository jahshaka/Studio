/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/sessionmarkers.h"

#include "bridge/enginehost.h"
#include "services/engineerrorpump.h"
#include "services/jahlog.h"
#include "services/playbackservice.h"
#include "viewport/enginerenderdriver.h"

#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/meshnode.h"

#include <QDateTime>

namespace {

EngineRenderDriver::Stats currentFrameStats()
{
    if (auto *driver = EngineHost::instance().driver()) return driver->stats();
    return EngineRenderDriver::Stats {};
}

/// The wall clock this session started on. A static rather than a member so
/// the quit summary can report the duration without being handed the object.
qint64 sessionStartMs()
{
    static const qint64 start = QDateTime::currentMSecsSinceEpoch();
    return start;
}

}   // namespace

void SessionMarkers::attach(PlaybackService *playback)
{
    if (!playback) return;
    sessionStartMs();   // pin the start now, not at the first play
    connect(playback, &PlaybackService::playModeEntered, this, &SessionMarkers::onPlayStart);
    connect(playback, &PlaybackService::editModeEntered, this, &SessionMarkers::onPlayStop);
}

void SessionMarkers::onPlayStart()
{
    // editModeEntered fires on paths that were never in play mode; only a
    // matched pair produces a bracket.
    const EngineRenderDriver::Stats s = currentFrameStats();
    mPlayStartMs = QDateTime::currentMSecsSinceEpoch();
    mRendered = s.rendered;
    mSkipped = s.skipped;
    mSlow = s.slowFrames;
    mWorst = s.worstMs;
    mInPlay = true;
    JAH_LOG(JahLog::scene, Display, QStringLiteral("=== PLAY START ==="));
}

void SessionMarkers::onPlayStop()
{
    if (!mInPlay) return;
    mInPlay = false;
    const EngineRenderDriver::Stats s = currentFrameStats();
    const qint64 ms = QDateTime::currentMSecsSinceEpoch() - mPlayStartMs;
    const quint64 rendered = s.rendered - mRendered;
    const double fps = ms > 0 ? double(rendered) * 1000.0 / double(ms) : 0.0;

    // THE DECAY EVIDENCE. A delta across the bracket, not a snapshot: two play
    // sessions in one run become directly comparable, which is exactly the
    // comparison "it was fine when I opened it and slow twenty minutes later"
    // needs and has never had.
    JAH_LOG(JahLog::scene, Display,
            QStringLiteral("=== PLAY STOP === %1 ms | rendered %2 (%3 fps avg) skipped %4 | "
                           "workMs %5 worst %6 (session worst %7) | slowFrames +%8")
                .arg(ms).arg(rendered).arg(fps, 0, 'f', 1).arg(s.skipped - mSkipped)
                .arg(s.workMs, 0, 'f', 2).arg(mWorst, 0, 'f', 1).arg(s.worstMs, 0, 'f', 1)
                .arg(s.slowFrames - mSlow));
}

void SessionMarkers::logSpaceSwitch(const QString &from, const QString &to)
{
    JAH_LOG(JahLog::ui, Log, QStringLiteral("space: %1 -> %2").arg(from, to));
}

QStringList SessionMarkers::sceneStats(const iris::ScenePtr &scene)
{
    QStringList out;
    if (!scene) return out;

    int meshes = 0, lights = 0, cameras = 0, particles = 0, decals = 0, empties = 0;
    QSet<QString> materials;
    for (auto it = scene->nodes.constBegin(); it != scene->nodes.constEnd(); ++it) {
        const iris::SceneNodePtr &node = it.value();
        if (!node) continue;
        switch (node->getSceneNodeType()) {
        case iris::SceneNodeType::Mesh: {
            ++meshes;
            // The material identity is what a "did the scene get heavier"
            // reading wants, and a mesh node is where one lives.
            if (auto mesh = node.staticCast<iris::MeshNode>())
                if (mesh->getMaterial()) materials.insert(mesh->getGUID());
            break;
        }
        case iris::SceneNodeType::Light:          ++lights; break;
        case iris::SceneNodeType::Camera:         ++cameras; break;
        case iris::SceneNodeType::ParticleSystem: ++particles; break;
        case iris::SceneNodeType::Decal:          ++decals; break;
        default:                                  ++empties; break;
        }
    }
    out << QStringLiteral("scene: %1 nodes (%2 mesh, %3 light, %4 camera, %5 particle, "
                          "%6 decal, %7 other), %8 materials")
               .arg(scene->nodes.size()).arg(meshes).arg(lights).arg(cameras)
               .arg(particles).arg(decals).arg(empties).arg(materials.size());
    return out;
}

void SessionMarkers::logQuitSummary()
{
    const EngineRenderDriver::Stats s = currentFrameStats();
    const qint64 ms = QDateTime::currentMSecsSinceEpoch() - sessionStartMs();
    const QVariantMap errors = EngineErrorPump::instance().report();
    JAH_LOG(JahLog::app, Display,
            QStringLiteral("session: %1 min | frames rendered %2 skipped %3 slow %4 | "
                           "worst frame %5 ms | engine errors recorded %6 suppressed %7")
                .arg(double(ms) / 60000.0, 0, 'f', 1)
                .arg(s.rendered).arg(s.skipped).arg(s.slowFrames)
                .arg(s.worstMs, 0, 'f', 1)
                .arg(errors.value(QStringLiteral("recorded")).toULongLong())
                .arg(errors.value(QStringLiteral("suppressed")).toULongLong()));
}
