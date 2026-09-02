/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "modules/avatar/api/avatarapi.h"

#include <QColor>
#include <QDir>
#include <QFileInfo>

#include "modules/avatar/avatarpreviewmodel.h"

AvatarApi::AvatarApi(ScriptHost &host, avatar::AvatarPreviewModel *model, QObject *parent)
    : ApiModule(host, parent), mModel(model)
{
}

QVector<VerbInfo> AvatarApi::verbs() const
{
    return {
        { "loadPreview", "avatar.loadPreview(path) -> {name, file, bones, meshes, vertices, influences, clips:[{name, rawName, length}]}",
          "Loads a rigged model file (fbx/glb/obj/...) into the Avatar page's own preview — no library row, no project pin, no database write, no undo command. Embedded textures are extracted to a per-session scratch dir. Replaces whatever was loaded (one subject at a time).",
          Needs::Document },
        { "clearPreview", "avatar.clearPreview() -> bool",
          "Removes the previewed model and deletes its scratch extract dir.",
          Needs::Document },
        { "preview", "avatar.preview() -> {name, file, bones, meshes, vertices, influences, clips, activeClip, time, duration, playing, looping, meshVisible, skeletonVisible} | undefined",
          "Everything the page shows about the loaded model, including transport and toggle state. Undefined (falsy) when nothing is loaded.",
          Needs::Document },
        { "setMeshVisible", "avatar.setMeshVisible(on) -> bool",
          "Shows or hides the skinned mesh. Independent of the skeleton toggle: all four combinations are valid.",
          Needs::Document },
        { "setSkeletonVisible", "avatar.setSkeletonVisible(on) -> bool",
          "Shows or hides the bone-line overlay. Independent of the mesh toggle.",
          Needs::Document },
        { "clips", "avatar.clips() -> [{name, rawName, length, looping, active}]",
          "The clips embedded in the loaded file. `name` is the display name: every Mixamo clip is literally called 'mixamo.com', so junk names fall back to the source file's base name (`rawName` keeps what the file said).",
          Needs::Document },
        { "setClip", "avatar.setClip(name) -> bool",
          "Makes `name` (display or raw) the active clip and rewinds to 0.",
          Needs::Document },
        { "playClip", "avatar.playClip(name) -> bool",
          "Starts the preview transport. With a name, selects that clip first; without one, resumes the active clip. Drives the module's OWN preview document — never the editor scene's clock.",
          Needs::Document },
        { "pause", "avatar.pause() -> bool", "Stops advancing time, keeping the current pose.", Needs::Document },
        { "stop", "avatar.stop() -> bool", "Pauses and rewinds to time 0.", Needs::Document },
        { "setLooping", "avatar.setLooping(on) -> bool", "Loops the active clip (default on).", Needs::Document },
        { "setTime", "avatar.setTime(seconds) -> bool",
          "Scrubs the preview to `seconds` and re-evaluates the pose immediately.",
          Needs::Document },
        { "time", "avatar.time() -> number", "The preview's current time in seconds.", Needs::Document },
        { "bones", "avatar.bones() -> [{name, parent, position:{x,y,z}}]",
          "The rig as the preview resolves it: one entry per bone that has a scene node, `parent` being the NEAREST ancestor that is also a bone (assimp pivot nodes sit between real bones, and Bone::parentBone is empty for such rigs). World-space positions at the current time — the headless assertion surface for the pose and for the overlay.",
          Needs::Document },
        { "snapshot", "avatar.snapshot(path, w=256, h=256, probes=[]) -> {path, width, height, center:{r,g,b}, probes:[{x,y,r,g,b}]}",
          "Offscreen render of the Avatar page's preview scene to a PNG, with the centre pixel and each probe point ({x,y} normalized 0..1) returned so scripts can assert on colours — the way a script (or an MCP session) proves the skeleton-only view from outside the app.",
          Needs::Engine },
    };
}

void AvatarApi::notifyChanged()
{
    if (mChanged) mChanged();
}

void AvatarApi::notifySubjectChanged()
{
    if (mSubjectChanged) mSubjectChanged();
    notifyChanged();
}

QVariantMap AvatarApi::previewState() const
{
    QVariantMap out;
    out["name"] = mModel->name();
    out["file"] = mModel->filePath();
    out["bones"] = mModel->boneCount();
    out["meshes"] = mModel->meshCount();
    out["vertices"] = mModel->vertexCount();
    out["influences"] = mModel->influencesPerVertex();
    out["hasSkeleton"] = mModel->hasSkeleton();
    QVariantList clipList;
    for (const auto &c : mModel->clips())
        clipList.append(QVariantMap{ { "name", c.name }, { "rawName", c.rawName },
                                     { "length", c.length }, { "looping", c.looping },
                                     { "active", c.active } });
    out["clips"] = clipList;
    out["activeClip"] = mModel->activeClip();
    out["duration"] = mModel->duration();
    out["time"] = mModel->time();
    out["playing"] = mModel->isPlaying();
    out["looping"] = mModel->looping();
    out["meshVisible"] = mModel->meshVisible();
    out["skeletonVisible"] = mModel->skeletonVisible();
    return out;
}

QVariant AvatarApi::loadPreview(const QString &path)
{
    if (!mModel) { fail("avatar: not available in this session"); return QVariant(); }
    if (path.trimmed().isEmpty()) { fail("avatar.loadPreview: a file path is required"); return QVariant(); }
    QString error;
    if (!mModel->load(path, &error)) { fail(QStringLiteral("avatar.loadPreview: %1").arg(error)); return QVariant(); }
    notifySubjectChanged();
    return previewState();
}

bool AvatarApi::clearPreview()
{
    if (!mModel) return fail("avatar: not available in this session");
    mModel->clear();
    notifySubjectChanged();
    return true;
}

QVariant AvatarApi::preview()
{
    if (!mModel || !mModel->isLoaded()) return QVariant();
    return previewState();
}

bool AvatarApi::setMeshVisible(bool on)
{
    if (!mModel) return fail("avatar: not available in this session");
    mModel->setMeshVisible(on);
    notifyChanged();
    return true;
}

bool AvatarApi::setSkeletonVisible(bool on)
{
    if (!mModel) return fail("avatar: not available in this session");
    mModel->setSkeletonVisible(on);
    notifyChanged();
    return true;
}

QVariantList AvatarApi::clips()
{
    QVariantList out;
    if (!mModel) { fail("avatar: not available in this session"); return out; }
    for (const auto &c : mModel->clips())
        out.append(QVariantMap{ { "name", c.name }, { "rawName", c.rawName },
                                { "length", c.length }, { "looping", c.looping },
                                { "active", c.active } });
    return out;
}

bool AvatarApi::setClip(const QString &name)
{
    if (!mModel) return fail("avatar: not available in this session");
    if (!mModel->isLoaded()) return fail("avatar.setClip: nothing is loaded");
    if (!mModel->setClip(name))
        return fail(QStringLiteral("avatar.setClip: no clip named '%1'").arg(name));
    notifyChanged();
    return true;
}

bool AvatarApi::playClip(const QString &name)
{
    if (!mModel) return fail("avatar: not available in this session");
    if (!mModel->isLoaded()) return fail("avatar.playClip: nothing is loaded");
    if (!name.isEmpty() && !mModel->setClip(name))
        return fail(QStringLiteral("avatar.playClip: no clip named '%1'").arg(name));
    if (mModel->clips().isEmpty()) return fail("avatar.playClip: this file has no clips");
    mModel->play();
    notifyChanged();
    return true;
}

bool AvatarApi::pause()
{
    if (!mModel) return fail("avatar: not available in this session");
    mModel->pause();
    notifyChanged();
    return true;
}

bool AvatarApi::stop()
{
    if (!mModel) return fail("avatar: not available in this session");
    mModel->stop();
    notifyChanged();
    return true;
}

bool AvatarApi::setLooping(bool on)
{
    if (!mModel) return fail("avatar: not available in this session");
    if (!mModel->isLoaded()) return fail("avatar.setLooping: nothing is loaded");
    mModel->setLooping(on);
    notifyChanged();
    return true;
}

bool AvatarApi::setTime(double seconds)
{
    if (!mModel) return fail("avatar: not available in this session");
    if (!mModel->isLoaded()) return fail("avatar.setTime: nothing is loaded");
    mModel->setTime(float(seconds));
    notifyChanged();
    return true;
}

double AvatarApi::time()
{
    if (!mModel) { fail("avatar: not available in this session"); return 0.0; }
    return double(mModel->time());
}

QVariantList AvatarApi::bones()
{
    QVariantList out;
    if (!mModel) { fail("avatar: not available in this session"); return out; }
    for (const auto &b : mModel->bones()) {
        out.append(QVariantMap{
            { "name", b.name }, { "parent", b.parent },
            { "position", QVariantMap{ { "x", b.position.x() }, { "y", b.position.y() },
                                       { "z", b.position.z() } } } });
    }
    return out;
}

QVariantMap AvatarApi::snapshot(const QString &path, int width, int height,
                                const QVariantList &probes)
{
    QVariantMap out;
    if (!mSnapshot) { fail("avatar.snapshot: the avatar preview is not running in this session"); return out; }
    if (path.isEmpty()) { fail("avatar.snapshot: a file path is required"); return out; }

    const QImage img = mSnapshot(qBound(16, width, 4096), qBound(16, height, 4096));
    if (img.isNull()) { fail("avatar.snapshot: the preview returned no image"); return out; }

    QFileInfo info(path);
    if (!info.dir().exists()) info.dir().mkpath(".");
    if (!img.save(path, "PNG")) {
        fail(QStringLiteral("avatar.snapshot: could not save '%1'").arg(path));
        return out;
    }

    const QColor center = img.pixelColor(img.width() / 2, img.height() / 2);
    out["path"] = info.absoluteFilePath();
    out["width"] = img.width();
    out["height"] = img.height();
    out["center"] = QVariantMap{ { "r", center.red() }, { "g", center.green() }, { "b", center.blue() } };

    // Same probe convention as editor.screenshot: normalized 0..1 coordinates,
    // each returning the average of the 5x5 block so assertions survive minor
    // framing drift across drivers.
    QVariantList probeResults;
    for (const QVariant &p : probes) {
        const QVariantMap pm = p.toMap();
        const double px = qBound(0.0, pm.value("x").toDouble(), 1.0);
        const double py = qBound(0.0, pm.value("y").toDouble(), 1.0);
        const int ix = qMin(int(px * img.width()), img.width() - 1);
        const int iy = qMin(int(py * img.height()), img.height() - 1);
        int r = 0, g = 0, b = 0, n = 0;
        for (int dy = -2; dy <= 2; ++dy) {
            for (int dx = -2; dx <= 2; ++dx) {
                const int x = ix + dx, y = iy + dy;
                if (x < 0 || y < 0 || x >= img.width() || y >= img.height()) continue;
                const QColor c = img.pixelColor(x, y);
                r += c.red(); g += c.green(); b += c.blue(); ++n;
            }
        }
        if (n > 0) { r /= n; g /= n; b /= n; }
        probeResults.append(QVariantMap{ { "x", px }, { "y", py }, { "r", r }, { "g", g }, { "b", b } });
    }
    if (!probeResults.isEmpty()) out["probes"] = probeResults;
    return out;
}
