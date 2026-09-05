/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "scripting/modules/cameraapi.h"

#include "scripting/modules/moduleshared.h"
#include "commands/setnodepropertycommand.h"
#include "commands/transformscenenodecommand.h"
#include "services/sceneeditservice.h"
#include "services/services.h"
#include "services/undoservice.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "viewport/ieditorviewport.h"

#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QImage>

using namespace scriptmod;

namespace {

QString authorModeName(iris::CameraAuthorMode m)
{
    return m == iris::CameraAuthorMode::Millimeters ? QStringLiteral("mm")
                                                    : QStringLiteral("degrees");
}

QString focusModeName(iris::CameraFocusMode m)
{
    switch (m) {
    case iris::CameraFocusMode::Track: return QStringLiteral("track");
    case iris::CameraFocusMode::Off:   return QStringLiteral("off");
    case iris::CameraFocusMode::Manual: break;
    }
    return QStringLiteral("manual");
}

}   // namespace

// ---- the shared settings block (see cameraapi.h) --------------------------

namespace camerashared {

const QStringList &settingsKeys()
{
    static const QStringList keys{
        QStringLiteral("sensorWidth"), QStringLiteral("sensorHeight"),
        QStringLiteral("angle"), QStringLiteral("focalLength"),
        QStringLiteral("authorMode"),
        QStringLiteral("projMode"), QStringLiteral("orthoSize"),
        QStringLiteral("nearClip"), QStringLiteral("farClip"),
        QStringLiteral("aspectRatio"), QStringLiteral("constrainAspect"),
        QStringLiteral("dofEnabled"), QStringLiteral("focusMode"),
        QStringLiteral("focusDistance"), QStringLiteral("focusTarget"),
        QStringLiteral("fStop"),
        QStringLiteral("outputHeight"), QStringLiteral("bodyVisible"),
    };
    return keys;
}

QVariantMap settingsToJs(const iris::CameraNodePtr &cam)
{
    QVariantMap out;
    if (!cam) return out;
    out["id"] = cam->getGUID();
    out["name"] = cam->getName();
    out["angle"] = cam->angle;                 // vertical degrees
    out["focalLength"] = cam->focalLength();   // the same angle, in mm
    out["sensorWidth"] = cam->sensorWidth;
    out["sensorHeight"] = cam->sensorHeight;
    out["authorMode"] = authorModeName(cam->authorMode);
    out["projMode"] = cam->projMode == iris::CameraProjection::Perspective
                          ? QStringLiteral("perspective") : QStringLiteral("orthogonal");
    out["orthoSize"] = cam->orthoSize;
    out["nearClip"] = cam->nearClip;
    out["farClip"] = cam->farClip;
    out["aspectRatio"] = cam->aspectRatio;
    out["constrainAspect"] = cam->constrainAspect;
    out["dofEnabled"] = cam->dofEnabled;
    out["focusMode"] = focusModeName(cam->focusMode);
    out["focusDistance"] = cam->focusDistance;
    out["focusTarget"] = cam->focusTarget;
    out["fStop"] = cam->fStop;
    out["outputHeight"] = cam->outputHeight;
    // Derived, never stored (CAMERAS_SPEC §2: one scalar plus the aspect is the
    // whole resolution model). Reported so a caller does not have to redo the
    // multiplication — and rounded up to an even width, which every video
    // encoder in existence wants.
    {
        const int w = int(qRound(cam->outputHeight * qMax(0.0001f, cam->aspectRatio)));
        out["outputWidth"] = qMax(2, w % 2 == 0 ? w : w + 1);
    }
    out["bodyVisible"] = cam->bodyVisible;
    return out;
}

QString applySettings(const iris::CameraNodePtr &cam, const QVariantMap &params,
                      const iris::ScenePtr &scene, UndoService *undo, const QString &verb)
{
    if (!cam) return QStringLiteral("%1: no camera").arg(verb);
    if (params.isEmpty()) return QString();

    // Unknown keys are refused, not ignored (the surface's standing rule): a
    // typo that silently does nothing is the defect an agent cannot see. The
    // three keys the READ side emits that are not settings get their own
    // message, because "read the block, change one row, write it back" is the
    // obvious thing to try and a bare "unknown setting 'id'" does not explain
    // itself.
    static const QStringList readOnlyEcho{ QStringLiteral("id"), QStringLiteral("name"),
                                           QStringLiteral("outputWidth") };
    for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
        if (settingsKeys().contains(it.key())) continue;
        if (readOnlyEcho.contains(it.key()))
            return QStringLiteral("%1: '%2' is reported by camera.settings but is not a setting "
                                  "— id and name identify the node (node.rename renames it) and "
                                  "outputWidth is derived from outputHeight and aspectRatio. "
                                  "Strip id, name and outputWidth before writing a read block "
                                  "back.").arg(verb, it.key());
        return QStringLiteral("%1: unknown setting '%2' (known: %3)")
                   .arg(verb, it.key(), settingsKeys().join(QStringLiteral(", ")));
    }
    // One angle of view, authored one way per call.
    if (params.contains(QStringLiteral("angle")) && params.contains(QStringLiteral("focalLength")))
        return QStringLiteral("%1: angle and focalLength are the SAME setting seen two ways "
                              "(bound through sensorHeight) — pass one of them, not both").arg(verb);

    for (const QString &key : settingsKeys()) {
        if (!params.contains(key)) continue;
        QVariant value = normalizeJs(params.value(key));

        // The three enumerated rows travel as strings on this surface and as
        // ints through the reflection layer. Both spellings are accepted so
        // settings(id, settings(id)) round-trips.
        if (key == QLatin1String("authorMode")) {
            const QString s = value.toString().trimmed().toLower();
            if (s == QLatin1String("mm")) value = int(iris::CameraAuthorMode::Millimeters);
            else if (s == QLatin1String("degrees")) value = int(iris::CameraAuthorMode::Degrees);
            else if (value.typeId() == QMetaType::QString)
                return QStringLiteral("%1: authorMode is \"degrees\" or \"mm\", got '%2'").arg(verb, s);
        } else if (key == QLatin1String("focusMode")) {
            const QString s = value.toString().trimmed().toLower();
            if (s == QLatin1String("manual"))     value = int(iris::CameraFocusMode::Manual);
            else if (s == QLatin1String("track")) value = int(iris::CameraFocusMode::Track);
            else if (s == QLatin1String("off"))   value = int(iris::CameraFocusMode::Off);
            else if (value.typeId() == QMetaType::QString)
                return QStringLiteral("%1: focusMode is \"manual\", \"track\" or \"off\", got '%2'")
                           .arg(verb, s);
        } else if (key == QLatin1String("projMode")) {
            const QString s = value.toString().trimmed().toLower();
            if (s == QLatin1String("perspective"))       value = int(iris::CameraProjection::Perspective);
            else if (s == QLatin1String("orthogonal") ||
                     s == QLatin1String("orthographic")) value = int(iris::CameraProjection::Orthogonal);
            else if (value.typeId() == QMetaType::QString)
                return QStringLiteral("%1: projMode is \"perspective\" or \"orthogonal\", got '%2'")
                           .arg(verb, s);
        } else if (key == QLatin1String("focusTarget")) {
            // A tracked target must exist: focusing on a guid that names
            // nothing is a shot that silently never pulls focus.
            const QString targetId = value.toString();
            if (!targetId.isEmpty() && scene &&
                !findNodeByGuid(scene->getRootNode(), targetId))
                return QStringLiteral("%1: no node with id '%2' to focus on").arg(verb, targetId);
        }

        const QVariant before = cam->getPropertyValue(key);
        if (!cam->setPropertyValue(key, value))
            return QStringLiteral("%1: '%2' was refused").arg(verb, key);
        if (undo)
            undo->push(new SetNodePropertyCommand(cam, key, before, cam->getPropertyValue(key)));
    }
    return QString();
}

}   // namespace camerashared

// ---- the verbs -----------------------------------------------------------

QVector<VerbInfo> CameraApi::verbs() const
{
    return {
        { "settings", "camera.settings(id, {…}?) -> {angle, focalLength, sensorWidth, sensorHeight, "
                      "authorMode, projMode, orthoSize, nearClip, farClip, aspectRatio, "
                      "constrainAspect, dofEnabled, focusMode, focusDistance, focusTarget, fStop, "
                      "outputHeight, outputWidth, bodyVisible}",
          "Reads a scene camera's whole settings block, or writes part of it and returns the "
          "result. `angle` is the VERTICAL field of view in degrees and `focalLength` is the same "
          "value in millimetres, bound through the sensor HEIGHT "
          "(angle = 2*atan(sensorHeight / (2*focalLength))) — setting either moves the other, so "
          "passing BOTH in one call is refused rather than silently letting one win. "
          "`authorMode` (\"degrees\" or \"mm\") only decides which of the two survives a later "
          "sensor change. `projMode` is \"perspective\" or \"orthogonal\"; `focusMode` is "
          "\"manual\", \"track\" or \"off\" and `focusTarget` is the node id tracked in track "
          "mode. The DOF rows (dofEnabled, focusMode, focusDistance, focusTarget, fStop) are "
          "stored, animated and exported today — the live DOF render pass is a later phase, so "
          "they change no pixels yet. `outputHeight` (with aspectRatio) sizes RENDERS and "
          "EXPORTS only; the viewport ignores it, and the reported `outputWidth` is derived, not "
          "stored. Every row is also a reflected node property, so node.setProperty and keyframe "
          "animation reach the same fields. NOTE the read block also carries `id`, `name` and "
          "the derived `outputWidth`, which are NOT settings — strip those three before writing "
          "a read block back, or the write is refused (unknown keys always are, rather than "
          "being silently dropped). Undoable: each row is one step of the run's undo macro.",
          Needs::Document },
        { "lookAt", "camera.lookAt(id, target) -> bool",
          "Points a camera at a target, which is either a node id or a world position {x,y,z}. "
          "Rotation only — the camera does not move — and +Y is up, so a target directly above "
          "or below the camera is refused rather than yielding a degenerate roll. Undoable.",
          Needs::Document },
        { "screenshot", "camera.screenshot(id, path, {width?, height?, probes?, postFx?}) -> {path, width, height, center:{r,g,b}, probes:[...]}",
          "Renders what THIS SCENE CAMERA sees to a PNG — the AI hook of CAMERAS_SPEC \u00a75. It "
          "goes through the same throwaway OFFSCREEN view editor.screenshot uses, so the user's "
          "viewport does not move and is not disturbed: an agent can look through an avatar's "
          "head-socketed camera without taking the editor away from whoever is driving it. "
          "SIZE comes from the CAMERA unless you override it: `height` defaults to the camera's "
          "outputHeight and `width` to height x aspectRatio (both clamped to 16..4096). "
          "`probes` are {x,y} points in normalized 0..1 image coordinates, returned as 5x5 "
          "averaged colours exactly as editor.screenshot returns them; `postFx` (default false) "
          "opts the shot into the scene's post chain so it looks like the viewport instead of "
          "like a neutral readback. A camera riding a SOCKET is resolved on the next synced "
          "frame, so a script that moves the rig should step editor.frame(1) before shooting.",
          Needs::Engine },
    };
}

iris::CameraNodePtr CameraApi::cameraOrFail(const QString &id, const QString &verb)
{
    auto scene = (host.services && host.services->sceneEdit) ? host.services->sceneEdit->scene()
                                                             : iris::ScenePtr();
    if (!scene) {
        fail(QStringLiteral("%1: no scene is open").arg(verb));
        return iris::CameraNodePtr();
    }
    auto node = findNodeByGuid(scene->getRootNode(), id);
    if (!node) {
        fail(QStringLiteral("%1: no node with id '%2'").arg(verb, id));
        return iris::CameraNodePtr();
    }
    if (node->getSceneNodeType() != iris::SceneNodeType::Camera) {
        fail(QStringLiteral("%1: '%2' is a %3, not a camera (scene.cameras() lists them)")
                 .arg(verb, node->getName(), nodeTypeName(node->getSceneNodeType())));
        return iris::CameraNodePtr();
    }
    return node.staticCast<iris::CameraNode>();
}

QVariantMap CameraApi::settings(const QString &id, const QVariant &options)
{
    QVariantMap out;
    auto cam = cameraOrFail(id, QStringLiteral("camera.settings"));
    if (!cam) return out;

    const QVariant normalized = normalizeJs(options);
    QVariantMap params;
    if (normalized.isValid() && !normalized.isNull()) {
        if (normalized.typeId() != QMetaType::QVariantMap) {
            fail("camera.settings: the second argument is an object of settings to write");
            return out;
        }
        params = normalized.toMap();
    }

    const QString error = camerashared::applySettings(
        cam, params,
        host.services && host.services->sceneEdit ? host.services->sceneEdit->scene()
                                                  : iris::ScenePtr(),
        host.services ? host.services->undo : nullptr, QStringLiteral("camera.settings"));
    if (!error.isEmpty()) { fail(error); return out; }

    return camerashared::settingsToJs(cam);
}

bool CameraApi::lookAt(const QString &id, const QVariant &target)
{
    auto cam = cameraOrFail(id, QStringLiteral("camera.lookAt"));
    if (!cam) return false;

    const QVariant value = normalizeJs(target);
    iris::Vec3 point;
    if (value.typeId() == QMetaType::QString) {
        auto scene = host.services->sceneEdit->scene();
        const QString targetId = value.toString();
        auto node = findNodeByGuid(scene->getRootNode(), targetId);
        if (!node)
            return fail(QStringLiteral("camera.lookAt: no node with id '%1' to look at").arg(targetId));
        if (node == cam)
            return fail("camera.lookAt: a camera cannot look at itself");
        // The node's WORLD position: a target parented under something moved is
        // not where its local position says it is. update(0) settles the
        // transform chain only when it is dirty, so this costs nothing normally.
        node->update(0.0f);
        point = node->getGlobalPosition();
    } else {
        point = vecFromJs(value, iris::Vec3(0, 0, 0));
    }

    cam->update(0.0f);
    const iris::Vec3 dir = point - cam->getGlobalPosition();
    if (dir.lengthSquared() <= 0.0f)
        return fail("camera.lookAt: the target is exactly where the camera is — there is no "
                    "direction to look in");
    // CameraNode::lookAt builds its basis against world +Y, so a target on the
    // camera's own vertical axis has no defined roll and decomposes to garbage.
    // Refused loudly rather than pointing somewhere arbitrary.
    if (qAbs(dir.normalized().y()) > 0.99999f)
        return fail("camera.lookAt: the target is straight above or below the camera, which "
                    "leaves the roll undefined — offset it, or set the rotation with "
                    "node.transform");

    const iris::Vec3 pos = cam->getLocalPos();
    const iris::Vec3 scale = cam->getLocalScale();
    const iris::Quat before = cam->getLocalRot();
    // CameraNode::lookAt works in the node's OWN space (it decomposes a matrix
    // built from `pos`, the LOCAL position), so an unparented camera — the only
    // shape this verb can be honest about today — lands exactly where the gizmo
    // and the hierarchy show it.
    cam->lookAt(point);
    const iris::Quat after = cam->getLocalRot();
    if (host.services && host.services->undo)
        host.services->undo->push(new TransformSceneNodeCommand(
            cam, pos, before, scale, pos, after, scale));
    return true;
}

// --- camera.screenshot: what THIS camera sees (CAMERAS_SPEC §5) ------------
//
// MECHANISM, and why it is not a new viewport method. EngineSceneViewport's
// screenshot already renders the live engine scene into a THROWAWAY offscreen
// view and points that view at whatever CameraNode the viewport currently holds
// (`applyCamera(mEditorCam, shot)`). So the whole of "render through a
// different camera" is: hand the viewport this camera for the duration of one
// synchronous call, take the shot, hand back the one it had. Nothing renders in
// between — there is no event loop turn inside takeScreenshot — so the user's
// on-screen view never sees the substitution, and the ONE thing that would have
// leaked (the camera controller's bound camera) is restored by the same
// setEditorCamera call that restores the pointer.
//
// The alternative — a takeCameraScreenshot() on IEditorViewport — is a cleaner
// signature and is worth doing when the viewport is next opened up; it was not
// worth taking src/viewport/ hostage for one call in a parallel-lane sprint.
QVariantMap CameraApi::screenshot(const QString &id, const QString &path,
                                  const QVariantMap &options)
{
    QVariantMap out;
    auto cam = cameraOrFail(id, QStringLiteral("camera.screenshot"));
    if (!cam) return out;
    if (!requireEngine()) return out;
    if (path.isEmpty()) { fail("camera.screenshot: a file path is required"); return out; }

    static const QStringList known = { "width", "height", "probes", "postFx" };
    for (auto it = options.constBegin(); it != options.constEnd(); ++it) {
        if (!known.contains(it.key())) {
            fail(QStringLiteral("camera.screenshot: unknown option '%1' — known options are %2")
                     .arg(it.key(), known.join(QStringLiteral(", "))));
            return out;
        }
    }

    // The camera's own output size is the default (CAMERAS_SPEC §2: outputHeight
    // plus aspectRatio IS the render size), overridable per call.
    const int camHeight = cam->outputHeight > 0 ? cam->outputHeight : 1080;
    const float aspect = cam->aspectRatio > 0.0f ? cam->aspectRatio : 1.0f;
    const int height = qBound(16, options.value(QStringLiteral("height"), camHeight).toInt(), 4096);
    const int width = qBound(16,
        options.value(QStringLiteral("width"), qRound(float(height) * aspect)).toInt(), 4096);
    const bool postFx = options.value(QStringLiteral("postFx"), false).toBool();

    auto saved = host.viewport->editorCamera();
    if (!saved) {
        fail("camera.screenshot: this viewport has no camera to borrow — the engine viewport "
             "is not live");
        return out;
    }

    // applyCamera SUBSTITUTES the scene's active camera while the document is
    // playing (the D6 seam), which would silently photograph a different camera
    // than the one asked for. Point the active camera at this one for the
    // duration; nothing else can observe it inside a synchronous call.
    auto scene = (host.services && host.services->sceneEdit) ? host.services->sceneEdit->scene()
                                                             : iris::ScenePtr();
    const bool substituting = scene && scene->isPlaying();
    const QString savedActive = scene ? scene->getActiveCameraGuid() : QString();
    if (substituting) scene->setActiveCamera(id);

    // BORROWING THE VIEWPORT IS NOW SAFE, and this is where that was proved.
    // Building this verb (2026-09-05) found that setEditorCamera resyncs the
    // camera CONTROLLER, and both controllers' setCamera() used to end in
    // updateCameraRot(), which wrote `Quat::fromEulerAngles(pitch, yaw, 0)`
    // back onto the node — so merely handing a camera to the viewport dropped
    // its roll, permanently, on a document node. This verb worked around it by
    // snapshotting and restoring both cameras' poses around the shot.
    //
    // The controllers were fixed instead (2026-09-06): adoption decomposes and
    // does not write; only navigation input moves a camera
    // (cameracontrollerbase.h states the contract). The workaround is gone, and
    // the roll assertions in sockets.e2e and cameras.e2e.pilot are what keep it
    // from needing to come back.
    host.viewport->setEditorCamera(cam);
    const QImage img = host.viewport->takeScreenshot(width, height, postFx);
    host.viewport->setEditorCamera(saved);
    if (substituting) scene->setActiveCamera(savedActive);

    if (img.isNull()) { fail("camera.screenshot: the viewport returned no image"); return out; }

    QFileInfo info(path);
    if (!info.dir().exists()) info.dir().mkpath(".");
    if (!img.save(path, "PNG")) {
        fail(QStringLiteral("camera.screenshot: could not save '%1'").arg(path));
        return out;
    }

    const QColor center = img.pixelColor(img.width() / 2, img.height() / 2);
    out["path"] = info.absoluteFilePath();
    out["width"] = img.width();
    out["height"] = img.height();
    out["center"] = QVariantMap{ { "r", center.red() }, { "g", center.green() },
                                 { "b", center.blue() } };

    // Probes: the same 5x5 average editor.screenshot returns, so an assertion
    // written against one verb reads identically against the other.
    QVariantList probeResults;
    for (const QVariant &p : options.value(QStringLiteral("probes")).toList()) {
        const QVariantMap pm = normalizeJs(p).toMap();
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
        if (n == 0) n = 1;
        probeResults.append(QVariantMap{ { "x", ix }, { "y", iy },
                                         { "r", r / n }, { "g", g / n }, { "b", b / n } });
    }
    out["probes"] = probeResults;
    return out;
}
