/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "scripting/modules/videoapi.h"

#include <QDir>
#include <QFileInfo>
#include <QSqlDatabase>

#include "data/database/database.h"
#include "data/project.h"
#include "services/assetcas.h"
#include "services/assetstorepaths.h"
#include "services/livetextures.h"
#include "services/livevideo.h"
#include "services/videoutils.h"

QVector<VerbInfo> VideoApi::verbs() const
{
    return {
        { "bind", "video.bind(videoGuid, textureGuid) -> bool",
          "Decodes a video library asset into a LIVE TEXTURE (texture.createLive): every frame "
          "the player produces is written into the texture and bumps its generation, so any "
          "material that binds the texture — material.set(node, {baseColorMap: textureGuid}) — "
          "plays the clip. `videoGuid` is a library row (assets.importFile of an mp4/webm/mov) "
          "or, for a fixture or a scratch file, a path that exists. Frames are SCALED to the "
          "texture's size, ignoring the clip's aspect: the texture's shape is the material "
          "author's decision, not the file's. Binding starts nothing — call video.play (or "
          "video.step for one frame). Re-binding the same video replaces its binding; binding is "
          "not a document edit and is not undoable. NO AUDIO is attached: a video on a surface is "
          "a material, and enumerating audio devices for it would both make noise and drag the "
          "audio stack into every scene that has a screen in it.",
          Needs::Document },
        { "unbind", "video.unbind(videoGuid) -> bool",
          "Stops the decoder and drops the binding. The live texture keeps whatever frame it last "
          "received — an unbind is a pause that gives the decoder back, not a clear. False when "
          "nothing was bound.",
          Needs::Document },
        { "play", "video.play(videoGuid) -> bool",
          "Starts (or resumes) decoding into the bound texture. Frames arrive asynchronously, at "
          "the clip's own rate; nothing else in the scene has to do anything for the surface to "
          "animate.",
          Needs::Document },
        { "pause", "video.pause(videoGuid) -> bool",
          "Holds the current frame. The texture keeps it — a paused video is a still image on the "
          "material, not a black one.",
          Needs::Document },
        { "stop", "video.stop(videoGuid) -> bool",
          "Stops decoding and rewinds to the start. The texture keeps the last frame it received; "
          "the next play begins from position 0.",
          Needs::Document },
        { "seek", "video.seek(videoGuid, positionMs) -> bool",
          "Jumps to a position in milliseconds. On a paused video the frame at that position "
          "arrives shortly after — video.step is the synchronous way to wait for it.",
          Needs::Document },
        { "loop", "video.loop(videoGuid, on=true) -> bool",
          "Loops the clip forever (or stops looping). Off by default: a video bound to a material "
          "ends on its last frame rather than snapping to black.",
          Needs::Document },
        { "step", "video.step(videoGuid, timeoutMs=4000) -> bool",
          "Decodes and delivers the NEXT frame, synchronously: it spins the event loop until one "
          "frame lands in the texture (or the timeout expires, or the clip ends), then leaves the "
          "player exactly as it found it — playing if it was playing, paused if it was paused. "
          "The primitive for anything that must know a frame arrived rather than hope so: a test, "
          "a thumbnail sweep, a scripted render. Returns false on a timeout or at the end of a "
          "clip that is not looping. Each successful step moves texture.info(...).generation by "
          "one.",
          Needs::Document },
        { "state", "video.state(videoGuid) -> {video, texture, source, playing, paused, position, duration, loop, frames} | null",
          "The binding's state, or null when the video is not bound. `frames` counts the frames "
          "actually written into the texture since the bind — the honest answer to 'is this "
          "surface alive', and the number a test asserts on.",
          Needs::Document },
        { "list", "video.list() -> [{video, texture, playing, frames, ...}]",
          "Every video currently bound to a live texture in this session.",
          Needs::Document },
    };
}

QString VideoApi::sourceFor(const QString &videoGuid)
{
    if (videoGuid.isEmpty()) {
        fail(QStringLiteral("video: no video guid"));
        return QString();
    }
    // A PATH THAT EXISTS is taken as-is — the same tolerance material.set has
    // for a texture path, and what lets a fixture clip be bound with no import.
    if (QFileInfo::exists(videoGuid)) return videoGuid;

    if (!host.db || !host.isProjectOpen()) {
        fail(QStringLiteral("video: '%1' is not a file and no project is open to resolve it as "
                            "an asset").arg(videoGuid));
        return QString();
    }
    const auto record = host.db->fetchAsset(videoGuid);
    if (record.guid.isEmpty()) {
        fail(QStringLiteral("video: no video file or asset '%1'").arg(videoGuid));
        return QString();
    }
    QSqlDatabase conn = QSqlDatabase::database();
    QString resolved = AssetCas::resolvePinned(conn, AssetStorePaths::root(),
                                               host.project->getProjectGuid(), videoGuid);
    if (resolved.isEmpty())
        resolved = AssetCas::resolveSource(conn, AssetStorePaths::root(), videoGuid);
    if (resolved.isEmpty())
        resolved = QDir(host.project->getProjectFolder()).filePath(record.name);
    if (!QFileInfo::exists(resolved)) {
        fail(QStringLiteral("video: asset '%1' has no readable file in the store").arg(videoGuid));
        return QString();
    }
    return resolved;
}

bool VideoApi::bind(const QString &videoGuid, const QString &textureGuid)
{
    if (!VideoUtils::canUseMultimedia())
        return fail(QStringLiteral("video.bind: Qt Multimedia is only usable on the GUI thread "
                                   "of a running application"));
    if (!LiveTextureCatalog::exists(textureGuid))
        return fail(QStringLiteral("video.bind: no live texture '%1' — texture.createLive makes "
                                   "one").arg(textureGuid));
    const QString file = sourceFor(videoGuid);
    if (file.isEmpty()) return false;   // sourceFor reported why

    QString error;
    if (!LiveVideo::bind(videoGuid, textureGuid, file, &error))
        return fail(QStringLiteral("video.bind: %1").arg(error));
    return true;
}

bool VideoApi::unbind(const QString &videoGuid)
{
    if (!LiveVideo::unbind(videoGuid))
        return refuse(QStringLiteral("video.unbind: '%1' is not bound").arg(videoGuid));
    return true;
}

bool VideoApi::play(const QString &videoGuid)
{
    LiveVideoBinding *binding = LiveVideo::find(videoGuid);
    if (!binding)
        return refuse(QStringLiteral("video.play: '%1' is not bound (video.bind first)").arg(videoGuid));
    return binding->play();
}

bool VideoApi::pause(const QString &videoGuid)
{
    LiveVideoBinding *binding = LiveVideo::find(videoGuid);
    if (!binding)
        return refuse(QStringLiteral("video.pause: '%1' is not bound").arg(videoGuid));
    return binding->pause();
}

bool VideoApi::stop(const QString &videoGuid)
{
    LiveVideoBinding *binding = LiveVideo::find(videoGuid);
    if (!binding)
        return refuse(QStringLiteral("video.stop: '%1' is not bound").arg(videoGuid));
    return binding->stop();
}

bool VideoApi::seek(const QString &videoGuid, double positionMs)
{
    LiveVideoBinding *binding = LiveVideo::find(videoGuid);
    if (!binding)
        return refuse(QStringLiteral("video.seek: '%1' is not bound").arg(videoGuid));
    if (positionMs < 0)
        return fail(QStringLiteral("video.seek: a position is milliseconds from the start and "
                                   "cannot be negative"));
    return binding->seek(qint64(positionMs));
}

bool VideoApi::loop(const QString &videoGuid, bool on)
{
    LiveVideoBinding *binding = LiveVideo::find(videoGuid);
    if (!binding)
        return refuse(QStringLiteral("video.loop: '%1' is not bound").arg(videoGuid));
    binding->setLoop(on);
    return true;
}

bool VideoApi::step(const QString &videoGuid, int timeoutMs)
{
    LiveVideoBinding *binding = LiveVideo::find(videoGuid);
    if (!binding)
        return refuse(QStringLiteral("video.step: '%1' is not bound").arg(videoGuid));
    if (!binding->step(timeoutMs))
        return refuse(QStringLiteral("video.step: no frame arrived within %1 ms (the clip may "
                                     "have ended — video.loop(guid) or video.seek(guid, 0))")
                          .arg(timeoutMs));
    return true;
}

QVariant VideoApi::state(const QString &videoGuid)
{
    LiveVideoBinding *binding = LiveVideo::find(videoGuid);
    if (!binding) {
        refuse(QStringLiteral("video.state: '%1' is not bound").arg(videoGuid));
        return jsNull();
    }
    return binding->state();
}

QVariantList VideoApi::list()
{
    QVariantList out;
    for (const QString &guid : LiveVideo::bound())
        if (LiveVideoBinding *binding = LiveVideo::find(guid)) out.append(binding->state());
    return out;
}
