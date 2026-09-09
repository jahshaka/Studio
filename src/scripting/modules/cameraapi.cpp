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
// CAMERA_LENS_SPEC §5: the override slots the DOCUMENT declares are joined to
// the World rows' labels, ranges and availability here — one table for both
// panels and both verbs, so a camera can never offer a row the world does not.
#include "services/worldmodes.h"
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

QString exposureModeName(iris::CameraExposureMode m)
{
    switch (m) {
    case iris::CameraExposureMode::Auto:   return QStringLiteral("auto");
    case iris::CameraExposureMode::Manual: return QStringLiteral("manual");
    case iris::CameraExposureMode::Inherit: break;
    }
    return QStringLiteral("inherit");
}

QString sensorFitName(iris::CameraSensorFit f)
{
    switch (f) {
    case iris::CameraSensorFit::Horizontal: return QStringLiteral("horizontal");
    case iris::CameraSensorFit::Auto:       return QStringLiteral("auto");
    case iris::CameraSensorFit::Vertical:   break;
    }
    return QStringLiteral("vertical");
}

}   // namespace

// ---- the shared settings block (see cameraapi.h) --------------------------

namespace camerashared {

const QStringList &settingsKeys()
{
    static const QStringList keys{
        QStringLiteral("sensorWidth"), QStringLiteral("sensorHeight"),
        // CAMERA_LENS_SPEC §3, the filmback block. It comes BEFORE the lens
        // rows on purpose: applySettings writes in THIS order, and the fit and
        // the squeeze decide what a focal length in the same call means.
        QStringLiteral("sensorFit"), QStringLiteral("anamorphicSqueeze"),
        QStringLiteral("lensShiftX"), QStringLiteral("lensShiftY"),
        QStringLiteral("angle"), QStringLiteral("focalLength"),
        QStringLiteral("authorMode"),
        QStringLiteral("projMode"), QStringLiteral("orthoSize"),
        QStringLiteral("nearClip"), QStringLiteral("farClip"),
        QStringLiteral("aspectRatio"), QStringLiteral("constrainAspect"),
        QStringLiteral("dofEnabled"), QStringLiteral("focusMode"),
        QStringLiteral("focusDistance"), QStringLiteral("focusTarget"),
        QStringLiteral("fStop"),
        // CAMERA_LENS_SPEC §3 P2, the focus block.
        QStringLiteral("focusOffset"), QStringLiteral("smoothFocus"),
        QStringLiteral("focusSmoothingSpeed"), QStringLiteral("minFocusDistance"),
        QStringLiteral("bladeCount"), QStringLiteral("focusPlaneVisible"),
        QStringLiteral("outputHeight"), QStringLiteral("bodyVisible"),
        // CAMERA_LENS_SPEC §4, the exposure block. In STOPS, unlike
        // world.postFx's `exposure` — see the verb doc.
        QStringLiteral("exposureMode"), QStringLiteral("exposure"),
        QStringLiteral("exposureMin"), QStringLiteral("exposureMax"),
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
    // CAMERA_LENS_SPEC §3.
    out["sensorFit"] = sensorFitName(cam->sensorFit);
    out["anamorphicSqueeze"] = cam->anamorphicSqueeze;
    out["lensShiftX"] = cam->lensShiftX;
    out["lensShiftY"] = cam->lensShiftY;
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
    // CAMERA_LENS_SPEC §3 P2.
    out["focusOffset"] = cam->focusOffset;
    out["smoothFocus"] = cam->smoothFocus;
    out["focusSmoothingSpeed"] = cam->focusSmoothingSpeed;
    out["minFocusDistance"] = cam->minFocusDistance;
    out["bladeCount"] = cam->bladeCount;
    out["focusPlaneVisible"] = cam->focusPlaneVisible;
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
    // CAMERA_LENS_SPEC §4.
    out["exposureMode"] = exposureModeName(cam->exposureMode);
    out["exposure"] = cam->exposure;
    out["exposureMin"] = cam->exposureMin;
    out["exposureMax"] = cam->exposureMax;
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
        } else if (key == QLatin1String("sensorFit")) {
            const QString s = value.toString().trimmed().toLower();
            if (s == QLatin1String("auto"))            value = int(iris::CameraSensorFit::Auto);
            else if (s == QLatin1String("horizontal")) value = int(iris::CameraSensorFit::Horizontal);
            else if (s == QLatin1String("vertical"))   value = int(iris::CameraSensorFit::Vertical);
            else if (value.typeId() == QMetaType::QString)
                return QStringLiteral("%1: sensorFit is \"auto\", \"horizontal\" or \"vertical\", "
                                      "got '%2'").arg(verb, s);
        } else if (key == QLatin1String("anamorphicSqueeze")) {
            // A squeeze of zero would divide the sensor away; a negative one is
            // not a lens. Refused rather than silently clamped, because the
            // caller meant something and we cannot guess what.
            if (!(value.toFloat() > 0.0f))
                return QStringLiteral("%1: anamorphicSqueeze must be greater than 0 "
                                      "(1.0 is spherical, 2.0 is a 2x anamorphic)").arg(verb);
        } else if (key == QLatin1String("lensShiftX") || key == QLatin1String("lensShiftY")) {
            const float v = value.toFloat();
            if (v < -1.0f || v > 1.0f)
                return QStringLiteral("%1: %2 is a FRACTION of the frame and lives in [-1, 1] "
                                      "(0.5 slides the image half a frame), got %3")
                           .arg(verb, key, QString::number(v));
        } else if (key == QLatin1String("bladeCount")) {
            const int n = value.toInt();
            if (n < 3 || n > 16)
                return QStringLiteral("%1: bladeCount is a diaphragm, 3 to 16 blades, got %2")
                           .arg(verb).arg(n);
        } else if (key == QLatin1String("projMode")) {
            const QString s = value.toString().trimmed().toLower();
            if (s == QLatin1String("perspective"))       value = int(iris::CameraProjection::Perspective);
            else if (s == QLatin1String("orthogonal") ||
                     s == QLatin1String("orthographic")) value = int(iris::CameraProjection::Orthogonal);
            else if (value.typeId() == QMetaType::QString)
                return QStringLiteral("%1: projMode is \"perspective\" or \"orthogonal\", got '%2'")
                           .arg(verb, s);
        } else if (key == QLatin1String("exposureMode")) {
            const QString s = value.toString().trimmed().toLower();
            if (s == QLatin1String("inherit"))     value = int(iris::CameraExposureMode::Inherit);
            else if (s == QLatin1String("auto"))   value = int(iris::CameraExposureMode::Auto);
            else if (s == QLatin1String("manual")) value = int(iris::CameraExposureMode::Manual);
            else if (value.typeId() == QMetaType::QString)
                return QStringLiteral("%1: exposureMode is \"inherit\", \"auto\" or \"manual\", "
                                      "got '%2'").arg(verb, s);
        } else if (key == QLatin1String("exposure") || key == QLatin1String("exposureMin") ||
                   key == QLatin1String("exposureMax")) {
            // STOPS, and a camera that is 20 stops off is a typo rather than a
            // shot. The range is generous (a real scene spans maybe 14) and it
            // exists so a bad number is refused instead of producing a black or
            // white frame nobody can explain.
            const float v = value.toFloat();
            if (v < -20.0f || v > 20.0f)
                return QStringLiteral("%1: %2 is in STOPS and lives in [-20, 20], got %3")
                           .arg(verb, key, QString::number(v));
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
                      "authorMode, sensorFit, anamorphicSqueeze, lensShiftX, lensShiftY, "
                      "projMode, orthoSize, nearClip, farClip, aspectRatio, "
                      "constrainAspect, dofEnabled, focusMode, focusDistance, focusTarget, fStop, "
                      "focusOffset, smoothFocus, focusSmoothingSpeed, minFocusDistance, "
                      "bladeCount, focusPlaneVisible, outputHeight, outputWidth, bodyVisible, "
                      "exposureMode, exposure, exposureMin, exposureMax}",
          "Reads a scene camera's whole settings block, or writes part of it and returns the "
          "result. `angle` is the VERTICAL field of view in degrees and `focalLength` is the same "
          "value in millimetres, bound through the sensor HEIGHT "
          "(angle = 2*atan(sensorHeight / (2*focalLength))) — setting either moves the other, so "
          "passing BOTH in one call is refused rather than silently letting one win. "
          "`authorMode` (\"degrees\" or \"mm\") only decides which of the two survives a later "
          "sensor change. `projMode` is \"perspective\" or \"orthogonal\"; `focusMode` is "
          "\"manual\", \"track\" or \"off\" and `focusTarget` is the node id tracked in track "
          "mode — where the mirror rewrites `focusDistance` every synced frame from that node's "
          "world position along the optical axis, plus `focusOffset`, clamped to "
          "`minFocusDistance` and eased when `smoothFocus` is on (at `focusSmoothingSpeed` "
          "e-folds per second). The DOF rows (dofEnabled, focusMode, focusDistance, focusTarget, "
          "fStop, focusOffset, smoothFocus, focusSmoothingSpeed, minFocusDistance, bladeCount) "
          "are stored, animated and exported today — the live DOF render pass is a later phase, "
          "so they change no pixels yet; camera.focusInfo reports what they WOULD blur. "
          "`focusPlaneVisible` draws the focus plane inside the camera's frustum helper (an "
          "editor helper: never in a render, hidden in play). The filmback rows (sensorFit, "
          "anamorphicSqueeze, lensShiftX, lensShiftY) are camera.filmback's and are documented "
          "there; lens shift is the one row in this group that moves pixels, because it offsets "
          "the projection itself. `outputHeight` (with aspectRatio) sizes RENDERS and "
          "EXPORTS only; the viewport ignores it, and the reported `outputWidth` is derived, not "
          "stored. "
          "`exposureMode` is \"inherit\" (the default — the world's exposure reaches the view "
          "untouched), \"auto\" (this camera's own auto-exposure midpoint and window) or "
          "\"manual\" (a pinned grade that measures nothing). `exposure`, `exposureMin` and "
          "`exposureMax` are in STOPS — NOT the same unit as world.postFx's `exposure`, which "
          "is the post chain's own natural-log value; 0 stops IS the default world grade and "
          "+1 is one doubling, converted once at the mirror. In manual mode the window is "
          "ignored (the clamp is pinned to a fixed reference, which is what makes one authored "
          "stop move the picture by exactly one stop). A camera's exposure applies while it is "
          "the one DRIVING a view — piloted, played through, or an opted-in "
          "camera.screenshot({postFx:true}) — and never to a thumbnail, a preview or a pixel "
          "suite. Cutting to a camera re-seeds the auto-exposure history so the new grade "
          "arrives on the cut instead of fading in over a second. Per-camera BLOOM, AO and the "
          "rest of the chain are camera.postFx's tri-state overrides, not settings keys. "
          "Every row is also a reflected node property, so node.setProperty and keyframe "
          "animation reach the same fields. NOTE the read block also carries `id`, `name` and "
          "the derived `outputWidth`, which are NOT settings — strip those three before writing "
          "a read block back, or the write is refused (unknown keys always are, rather than "
          "being silently dropped). Undoable: each row is one step of the run's undo macro.",
          Needs::Document },
        { "filmback", "camera.filmback(id, preset|{preset?, sensorWidth?, sensorHeight?, "
                      "sensorFit?, anamorphicSqueeze?, lensShiftX?, lensShiftY?}?) -> "
                      "{sensorWidth, sensorHeight, sensorFit, fitAxis, anamorphicSqueeze, "
                      "lensShiftX, lensShiftY, aspectRatio, preset, focalLength, angle, "
                      "horizontalFov, diagonalFov}",
          "The FILMBACK — the piece of film the lens projects onto — read, or written and read "
          "back. Pass a preset name (camera.filmbackPresets() lists them) to load a sensor size in "
          "one word, or an object to set rows individually; a preset plus explicit rows in the "
          "same call means \"that preset, but…\". "
          "`sensorFit` is \"vertical\" (the default and the historical behaviour: the angle of "
          "view binds through the sensor HEIGHT), \"horizontal\" (through the WIDTH, crossed to "
          "the stored vertical angle through aspectRatio — the cine convention, and what finally "
          "makes sensorWidth matter), or \"auto\" (Blender's rule: the width covers the larger "
          "image axis, so landscape frames behave horizontally). `anamorphicSqueeze` multiplies "
          "the effective sensor WIDTH — a 2x anamorphic sees as wide as a half-length spherical "
          "lens — and is therefore inert on a vertical fit. `lensShiftX/Y` shift the frustum "
          "WITHOUT rotating the camera (the architectural rise/fall that keeps verticals "
          "parallel), as a FRACTION of the frame in [-1, 1]: 0.5 slides the image half a frame. "
          "The reported `fitAxis` is which axis \"auto\" resolved to for this aspect, and "
          "`horizontalFov`/`diagonalFov` are derived from the stored vertical `angle` and "
          "`aspectRatio` — a view that does not constrain its aspect renders at its own target's, "
          "so those two are the authored frame's angles, not necessarily the window's. "
          "Every row is also a camera.settings key and a keyable node property. Undoable.",
          Needs::Document },
        { "lens", "camera.lens(id, preset|focalLengthMm|{preset?, focalLength?, fStop?}?) -> "
                  "{focalLength, fStop, angle, horizontalFov, diagonalFov, preset, sensorFit}",
          "The LENS on the camera: focal length in millimetres and the aperture. Pass a preset "
          "name (\"50mm\", or camera.lensPresets() for the kit), a bare number of millimetres, or "
          "an object. Setting a focal length sets the SAME value `angle` names — they are one "
          "value seen two ways, bound through the filmback (camera.filmback explains the binding) "
          "— and flips authorMode to \"mm\", so a later sensor or fit change keeps the LENS and "
          "moves the framing. A preset changes the lens ONLY: the f-stop is a shot decision and "
          "is never reopened behind your back, so pass fStop explicitly (the preset's widest "
          "aperture is reported as minFStop by camera.lensPresets). The 12mm preset is a "
          "rectilinear ultra-wide, not a fisheye: no engine bends the projection for that, and "
          "neither do we. Undoable.",
          Needs::Document },
        { "filmbackPresets", "camera.filmbackPresets() -> [{name, sensorWidth, sensorHeight, "
                             "anamorphicSqueeze, sensorAspect}]",
          "The named filmbacks camera.filmback accepts, smallest sensor first. These are "
          "JAHSHAKA's defaults — a conventional sensor chart, not a reproduction of any other "
          "tool's published table. `sensorAspect` is the SENSOR's own shape, which is not the "
          "camera's aspectRatio: shooting a 16:9 frame on a Super 35 sensor is ordinary. Needs no "
          "camera and changes nothing.",
          Needs::Document },
        { "lensPresets", "camera.lensPresets() -> [{name, focalLength, minFStop, note}]",
          "The prime lens kit camera.lens accepts, wide to long. `minFStop` is the widest "
          "aperture that lens is conventionally offered at — a suggestion for a UI, never applied "
          "by camera.lens on its own. Needs no camera and changes nothing.",
          Needs::Document },
        { "focusInfo", "camera.focusInfo(id) -> {focusDistance, hyperfocal, nearLimit, farLimit, "
                       "cocLimit}",
          "The DEPTH OF FIELD this camera's lens, aperture and sensor actually produce, in "
          "metres — read-only, derived, and true today even though the DoF render pass is a later "
          "phase (that is the point: a focus pull can be authored and checked before anything "
          "blurs). From the standard optics, with the circle of confusion taken as the sensor "
          "diagonal over 1500 (`cocLimit`, in MILLIMETRES — 0.0288 mm on full frame, the value "
          "published 35 mm depth-of-field tables use): hyperfocal H = f^2/(N*c) + f, near = "
          "s*(H - f)/(H + s - 2f), far = s*(H - f)/(H - s). `farLimit` is Infinity when focus is "
          "at or past the hyperfocal distance — where the sharp range is exactly [H/2, infinity), "
          "the textbook identity — so test it "
          "with isFinite() (JSON.stringify renders it as null). In \"track\" focus mode "
          "`focusDistance` is whatever the last synced frame resolved from focusTarget, so step "
          "editor.frame(1) after moving the rig before reading it.",
          Needs::Document },
        { "postFx", "camera.postFx(id, {hdr?, bloom?, bloomThreshold?, bloomKnee?, ssao?, ssaoPower?, "
                    "ssaoRadius?, smaa?, ssr?, refractions?}?) -> "
                    "{id, overrides, resolved, available}",
          "The camera's PER-CAMERA POST OVERRIDES over the world's post chain "
          "(CAMERA_LENS_SPEC §5), read or written. Each row is TRI-STATE: a key present "
          "here overrides the world while this camera is the one driving a view (piloted, "
          "played through, or an opted-in camera.screenshot({postFx:true})); a key ABSENT "
          "inherits. Pass null as a value to go back to inheriting — that is the only way to "
          "say it, which is why a plain value write cannot express this block and why it is "
          "not a camera.settings key. There is NO blend weight between cameras and there will "
          "not be one: half of this chain is compositor SHAPE (a workspace rebuild), not a "
          "float anything could cross-fade, so a weight would lie about what it did. "
          "The read returns `overrides` (only what THIS camera pins), `resolved` (what each "
          "row actually evaluates to right now — the override if there is one, the world's "
          "value otherwise) and `available` (false for a row the renderer declares but does "
          "not serve yet; it may still be authored, so files stay forward-compatible). "
          "BLOOM is the headline: a bloomy world with one clean camera, or a clean world with "
          "one blooming camera, is `camera.postFx(id, {bloom:true})`. "
          "EXPOSURE IS NOT HERE — a camera's exposure is its own block, in STOPS, with a mode "
          "(camera.settings' exposureMode/exposure/exposureMin/exposureMax), because it is a "
          "camera setting and not a value layered over the world's. "
          "SMAA takes only -1 (off): the PRESET is a shader recompile and a per-camera one "
          "would hitch on every cut, so it stays world-level (world.setAntiAliasing), and so "
          "does MSAA. "
          "REFRACTIONS is the one row whose override is not purely local: a refractive "
          "material must be offered refractions by EVERY view drawing the scene or the engine "
          "downgrades it to plain glass for all of them (the interlock in "
          "OgreScene::setRefractionsActive), so a camera that switches refractions off while "
          "another view is showing the same scene changes that view too. Overriding it to "
          "\"auto\" (1) means \"whatever the world resolved\", which is the same as inheriting. "
          "Rows, ranges and labels come from the same table the World > Post "
          "Process panel is generated from. Undoable: each row is one step of the run's undo "
          "macro. NEVER applies to thumbnails, previews or the pixel suites — those render "
          "through offscreen views, which discard the whole post description by construction.",
          Needs::Document },
        { "clearPostOverride", "camera.clearPostOverride(id, row?) -> {id, cleared}",
          "Drops one per-camera post override, or ALL of them when `row` is omitted — "
          "\"put this camera back on the world\". Identical to passing null for the row "
          "through camera.postFx, and offered separately because \"stop overriding\" is a "
          "thing a user does to a whole camera and not row by row. Returns the rows that "
          "were actually cleared (a row that was already inheriting is not one). Undoable.",
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

// ---- CAMERA_LENS_SPEC §3: filmback, lens, presets, focus ------------------
//
// WHY THESE GO THROUGH camerashared::applySettings AND NOT THROUGH THE FIELDS:
// every write in this file must be undoable, validated and refused the same way
// whichever verb a script called. So both verbs below TRANSLATE their arguments
// into a settings block and hand it to the one writer. A preset is therefore
// not a special path — it is a few settings rows with a name.

namespace {

/// Case- and space-insensitive preset matching ("super 35" == "Super 35").
QString normalizedName(const QString &s) { return s.trimmed().toLower().simplified(); }

int findFilmbackPreset(const QString &name)
{
    int count = 0;
    const iris::lens::FilmbackPreset *table = iris::lens::filmbackPresets(count);
    const QString want = normalizedName(name);
    for (int i = 0; i < count; ++i)
        if (normalizedName(QString::fromLatin1(table[i].name)) == want) return i;
    return -1;
}

int findLensPreset(const QString &name)
{
    int count = 0;
    const iris::lens::LensPreset *table = iris::lens::lensPresets(count);
    QString want = normalizedName(name);
    for (int i = 0; i < count; ++i)
        if (normalizedName(QString::fromLatin1(table[i].name)) == want) return i;
    // "50" is as good a name for the 50 mm as "50mm" is — a script that
    // computed a focal length should not have to format it our way.
    if (!want.endsWith(QLatin1String("mm"))) want += QLatin1String("mm");
    for (int i = 0; i < count; ++i)
        if (normalizedName(QString::fromLatin1(table[i].name)) == want) return i;
    return -1;
}

QString filmbackPresetNames()
{
    int count = 0;
    const iris::lens::FilmbackPreset *table = iris::lens::filmbackPresets(count);
    QStringList names;
    for (int i = 0; i < count; ++i) names << QString::fromLatin1(table[i].name);
    return names.join(QStringLiteral(", "));
}

QString lensPresetNames()
{
    int count = 0;
    const iris::lens::LensPreset *table = iris::lens::lensPresets(count);
    QStringList names;
    for (int i = 0; i < count; ++i) names << QString::fromLatin1(table[i].name);
    return names.join(QStringLiteral(", "));
}

/// The filmback preset this camera's sensor pair + squeeze currently matches,
/// or an empty string. Exact-value comparison with a hair of tolerance: a
/// camera whose sensor was nudged by hand is honestly NOT on a preset.
QString matchedFilmback(const iris::CameraNodePtr &cam)
{
    int count = 0;
    const iris::lens::FilmbackPreset *table = iris::lens::filmbackPresets(count);
    for (int i = 0; i < count; ++i) {
        if (qAbs(cam->sensorWidth - table[i].sensorWidth) < 0.005f &&
            qAbs(cam->sensorHeight - table[i].sensorHeight) < 0.005f &&
            qAbs(cam->anamorphicSqueeze - table[i].squeeze) < 0.001f)
            return QString::fromLatin1(table[i].name);
    }
    return QString();
}

/// The lens preset this camera's focal length currently matches (within a tenth
/// of a millimetre), or an empty string.
QString matchedLens(const iris::CameraNodePtr &cam)
{
    int count = 0;
    const iris::lens::LensPreset *table = iris::lens::lensPresets(count);
    const float mm = cam->focalLength();
    for (int i = 0; i < count; ++i)
        if (qAbs(mm - table[i].focalMm) < 0.1f) return QString::fromLatin1(table[i].name);
    return QString();
}

QVariantMap filmbackToJs(const iris::CameraNodePtr &cam)
{
    QVariantMap out;
    out["sensorWidth"] = cam->sensorWidth;
    out["sensorHeight"] = cam->sensorHeight;
    out["sensorFit"] = sensorFitName(cam->sensorFit);
    out["anamorphicSqueeze"] = cam->anamorphicSqueeze;
    out["lensShiftX"] = cam->lensShiftX;
    out["lensShiftY"] = cam->lensShiftY;
    out["aspectRatio"] = cam->aspectRatio;
    // Which axis the fit RESOLVES to for this aspect — the answer "auto" alone
    // does not give, and the thing a user is actually asking about.
    out["fitAxis"] = iris::lens::fitAxis(cam->sensorFit, cam->filmback().aspect) ==
                             iris::lens::FitAxis::Horizontal
                         ? QStringLiteral("horizontal") : QStringLiteral("vertical");
    out["preset"] = matchedFilmback(cam);
    out["focalLength"] = cam->focalLength();
    out["angle"] = cam->angle;
    out["horizontalFov"] = cam->horizontalFov();
    out["diagonalFov"] = cam->diagonalFov();
    return out;
}

QVariantMap lensToJs(const iris::CameraNodePtr &cam)
{
    QVariantMap out;
    out["focalLength"] = cam->focalLength();
    out["fStop"] = cam->fStop;
    out["angle"] = cam->angle;
    out["horizontalFov"] = cam->horizontalFov();
    out["diagonalFov"] = cam->diagonalFov();
    out["preset"] = matchedLens(cam);
    out["sensorFit"] = sensorFitName(cam->sensorFit);
    return out;
}

}   // namespace

QVariantMap CameraApi::filmback(const QString &id, const QVariant &options)
{
    QVariantMap out;
    auto cam = cameraOrFail(id, QStringLiteral("camera.filmback"));
    if (!cam) return out;

    const QVariant normalized = normalizeJs(options);
    QVariantMap write;
    if (normalized.isValid() && !normalized.isNull()) {
        QVariantMap params;
        if (normalized.typeId() == QMetaType::QString) {
            params.insert(QStringLiteral("preset"), normalized.toString());
        } else if (normalized.typeId() == QMetaType::QVariantMap) {
            params = normalized.toMap();
        } else {
            fail("camera.filmback: the second argument is a preset NAME or an object of "
                 "filmback settings");
            return out;
        }

        static const QStringList known = { "preset", "sensorWidth", "sensorHeight", "sensorFit",
                                           "anamorphicSqueeze", "lensShiftX", "lensShiftY" };
        for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
            if (known.contains(it.key())) continue;
            fail(QStringLiteral("camera.filmback: unknown filmback setting '%1' (known: %2) — the "
                                "rest of the camera is camera.settings")
                     .arg(it.key(), known.join(QStringLiteral(", "))));
            return out;
        }

        if (params.contains(QStringLiteral("preset"))) {
            const QString name = params.value(QStringLiteral("preset")).toString();
            const int idx = findFilmbackPreset(name);
            if (idx < 0) {
                fail(QStringLiteral("camera.filmback: no filmback preset named '%1' (known: %2)")
                         .arg(name, filmbackPresetNames()));
                return out;
            }
            int count = 0;
            const iris::lens::FilmbackPreset *table = iris::lens::filmbackPresets(count);
            // The preset is the FLOOR: explicit keys in the same call win, so
            // "Super 35, but shifted" is one call and not two.
            write.insert(QStringLiteral("sensorWidth"), table[idx].sensorWidth);
            write.insert(QStringLiteral("sensorHeight"), table[idx].sensorHeight);
            write.insert(QStringLiteral("anamorphicSqueeze"), table[idx].squeeze);
        }
        for (const QString &key : { QStringLiteral("sensorWidth"), QStringLiteral("sensorHeight"),
                                    QStringLiteral("sensorFit"), QStringLiteral("anamorphicSqueeze"),
                                    QStringLiteral("lensShiftX"), QStringLiteral("lensShiftY") })
            if (params.contains(key)) write.insert(key, params.value(key));
    }

    const QString error = camerashared::applySettings(
        cam, write,
        host.services && host.services->sceneEdit ? host.services->sceneEdit->scene()
                                                  : iris::ScenePtr(),
        host.services ? host.services->undo : nullptr, QStringLiteral("camera.filmback"));
    if (!error.isEmpty()) { fail(error); return out; }

    return filmbackToJs(cam);
}

QVariantMap CameraApi::lens(const QString &id, const QVariant &options)
{
    QVariantMap out;
    auto cam = cameraOrFail(id, QStringLiteral("camera.lens"));
    if (!cam) return out;

    const QVariant normalized = normalizeJs(options);
    QVariantMap write;
    if (normalized.isValid() && !normalized.isNull()) {
        QVariantMap params;
        if (normalized.typeId() == QMetaType::QString) {
            params.insert(QStringLiteral("preset"), normalized.toString());
        } else if (normalized.typeId() == QMetaType::Double ||
                   normalized.typeId() == QMetaType::Int) {
            // camera.lens(id, 35) is the obvious thing to type and it means
            // exactly what it looks like: a 35 mm lens, preset or not.
            params.insert(QStringLiteral("focalLength"), normalized.toDouble());
        } else if (normalized.typeId() == QMetaType::QVariantMap) {
            params = normalized.toMap();
        } else {
            fail("camera.lens: the second argument is a preset NAME, a focal length in "
                 "millimetres, or an object {preset?, focalLength?, fStop?}");
            return out;
        }

        static const QStringList known = { "preset", "focalLength", "fStop" };
        for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
            if (known.contains(it.key())) continue;
            fail(QStringLiteral("camera.lens: unknown lens setting '%1' (known: %2) — the sensor "
                                "is camera.filmback and the rest is camera.settings")
                     .arg(it.key(), known.join(QStringLiteral(", "))));
            return out;
        }
        if (params.contains(QStringLiteral("preset")) &&
            params.contains(QStringLiteral("focalLength"))) {
            fail("camera.lens: a preset IS a focal length — pass one of them, not both");
            return out;
        }

        if (params.contains(QStringLiteral("preset"))) {
            const QString name = params.value(QStringLiteral("preset")).toString();
            const int idx = findLensPreset(name);
            if (idx < 0) {
                fail(QStringLiteral("camera.lens: no lens preset named '%1' (known: %2)")
                         .arg(name, lensPresetNames()));
                return out;
            }
            int count = 0;
            const iris::lens::LensPreset *table = iris::lens::lensPresets(count);
            // A preset changes the LENS, never the aperture: the f-stop is a
            // shot decision and silently reopening it to the lens's maximum
            // would change the exposure of somebody's scene. `minFStop` is
            // reported by camera.lensPresets() for a caller who wants it.
            write.insert(QStringLiteral("focalLength"), table[idx].focalMm);
        }
        for (const QString &key : { QStringLiteral("focalLength"), QStringLiteral("fStop") })
            if (params.contains(key)) write.insert(key, params.value(key));
    }

    const QString error = camerashared::applySettings(
        cam, write,
        host.services && host.services->sceneEdit ? host.services->sceneEdit->scene()
                                                  : iris::ScenePtr(),
        host.services ? host.services->undo : nullptr, QStringLiteral("camera.lens"));
    if (!error.isEmpty()) { fail(error); return out; }

    return lensToJs(cam);
}

QVariantList CameraApi::filmbackPresets()
{
    QVariantList out;
    int count = 0;
    const iris::lens::FilmbackPreset *table = iris::lens::filmbackPresets(count);
    for (int i = 0; i < count; ++i) {
        QVariantMap m;
        m["name"] = QString::fromLatin1(table[i].name);
        m["sensorWidth"] = table[i].sensorWidth;
        m["sensorHeight"] = table[i].sensorHeight;
        m["anamorphicSqueeze"] = table[i].squeeze;
        // The sensor's own shape, which is NOT the camera's aspectRatio: a
        // 16:9 frame on a Super 35 sensor is a normal thing to shoot.
        m["sensorAspect"] = table[i].sensorHeight > 0.0f
                                ? table[i].sensorWidth / table[i].sensorHeight : 0.0f;
        out.append(m);
    }
    return out;
}

QVariantList CameraApi::lensPresets()
{
    QVariantList out;
    int count = 0;
    const iris::lens::LensPreset *table = iris::lens::lensPresets(count);
    for (int i = 0; i < count; ++i) {
        QVariantMap m;
        m["name"] = QString::fromLatin1(table[i].name);
        m["focalLength"] = table[i].focalMm;
        m["minFStop"] = table[i].minFStop;
        m["note"] = QString::fromLatin1(table[i].note);
        out.append(m);
    }
    return out;
}

QVariantMap CameraApi::focusInfo(const QString &id)
{
    QVariantMap out;
    auto cam = cameraOrFail(id, QStringLiteral("camera.focusInfo"));
    if (!cam) return out;

    const iris::lens::FocusInfo info = cam->focusInfo();
    out["focusDistance"] = info.focusDistance;
    out["hyperfocal"] = info.hyperfocal;
    out["nearLimit"] = info.nearLimit;
    // Infinity is the ANSWER at and past the hyperfocal distance, not a
    // failure — a script tests it with isFinite(), and JSON.stringify will
    // render it as null, which is why the doc string says so.
    out["farLimit"] = info.farLimit;
    out["cocLimit"] = info.cocLimit;
    return out;
}

// ---- CAMERA_LENS_SPEC §5: the per-camera post overrides -------------------
//
// THE DOCUMENT owns which keys exist (iris::cameraPostKeys) and refuses
// everything else; WORLDMODES owns what each one is called, what it costs, what
// range it lives in and whether the renderer serves it at all. This verb is the
// join, and it is the same join the camera panel makes — neither of them
// invents a row, a label or a range, which is why the panel and the verb cannot
// disagree about what a camera can override.

namespace {

/// Every override key, in the document's order, as script-facing names.
QString postKeyNames()
{
    int count = 0;
    const iris::CameraPostKey *table = iris::cameraPostKeys(count);
    QStringList names;
    for (int i = 0; i < count; ++i) names << QString::fromLatin1(table[i].id);
    return names.join(QStringLiteral(", "));
}

/// What the WORLD currently says for a key — the value a camera that does not
/// override it inherits. Reads through the same worldmodes tables the World
/// panel and world.postFx use.
QVariant worldPostValue(const iris::ScenePtr &scene, const QString &key)
{
    if (!scene) return QVariant();
    if (const worldmodes::ParamRow *p = worldmodes::postFxParam(key))
        return p->get ? QVariant(p->get(scene)) : QVariant();
    if (const worldmodes::Row *r = worldmodes::row(key))
        return QVariant(worldmodes::resolved(scene, *r));
    return QVariant();
}

/// Is the effect this key belongs to actually served by the renderer? A row
/// declared-but-not-implemented (POST_CHAIN_SPEC §9.2 — SSR was one) may still
/// be AUTHORED so files are forward-compatible, but the verb says so.
bool postKeyAvailable(const QString &key)
{
    const worldmodes::ParamRow *p = worldmodes::postFxParam(key);
    const QString rowId = p ? p->ownerRowId : key;
    const worldmodes::Row *r = worldmodes::row(rowId);
    return r ? r->available : true;
}

}   // namespace

QVariantMap CameraApi::postFx(const QString &id, const QVariant &options)
{
    QVariantMap out;
    auto cam = cameraOrFail(id, QStringLiteral("camera.postFx"));
    if (!cam) return out;
    auto scene = (host.services && host.services->sceneEdit) ? host.services->sceneEdit->scene()
                                                             : iris::ScenePtr();

    const QVariant normalized = normalizeJs(options);
    QVariantMap params;
    if (normalized.isValid() && !normalized.isNull()) {
        if (normalized.typeId() != QMetaType::QVariantMap) {
            fail("camera.postFx: the second argument is an object of overrides to write "
                 "(a null value clears one)");
            return out;
        }
        params = normalized.toMap();
    }

    // VALIDATE THE WHOLE BLOCK BEFORE WRITING ANY OF IT: a call that names one
    // good key and one bad one must not leave the camera half-changed.
    for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
        if (!iris::cameraPostKey(it.key())) {
            const bool isExposure = it.key().startsWith(QLatin1String("exposure"));
            fail(isExposure
                     ? QStringLiteral("camera.postFx: '%1' is not an override — a camera's "
                                      "exposure is its own block (camera.settings' exposureMode, "
                                      "exposure, exposureMin, exposureMax, in STOPS), not a "
                                      "value layered over the world's").arg(it.key())
                     : QStringLiteral("camera.postFx: unknown override '%1' (known: %2)")
                           .arg(it.key(), postKeyNames()));
            return out;
        }
    }

    UndoService *undo = host.services ? host.services->undo : nullptr;
    for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
        const QString key = it.key();
        const QVariant value = normalizeJs(it.value());
        const QString property = QStringLiteral("postFx.") + key;
        const QVariant before = cam->getPropertyValue(property);
        // A NULL CLEARS. "Inherit" has to be sayable through the same door that
        // overrides, or a script could only ever turn overrides on.
        if (!value.isValid() || value.isNull()) {
            if (!cam->clearPostOverride(key)) continue;   // was not overridden: nothing to record
        } else if (!cam->setPostOverride(key, value)) {
            if (key == QLatin1String("smaa")) {
                fail("camera.postFx: a camera may switch SMAA OFF (-1) but not to another "
                     "PRESET — the preset is a shader recompile, so a per-camera one would "
                     "hitch on every cut. The preset stays world-level (world.setAntiAliasing).");
            } else {
                fail(QStringLiteral("camera.postFx: '%1' cannot hold %2")
                         .arg(key, value.toString()));
            }
            return out;
        }
        if (undo)
            undo->push(new SetNodePropertyCommand(cam, property, before,
                                                  cam->getPropertyValue(property)));
    }

    // ---- the read side: what is overridden, and what it all RESOLVES to ----
    QVariantMap overrides, resolved, available;
    int count = 0;
    const iris::CameraPostKey *table = iris::cameraPostKeys(count);
    for (int i = 0; i < count; ++i) {
        const QString key = QString::fromLatin1(table[i].id);
        const QVariant own = cam->postOverride(key);
        if (own.isValid()) overrides.insert(key, own);
        const QVariant world = worldPostValue(scene, key);
        resolved.insert(key, own.isValid() ? own : world);
        available.insert(key, postKeyAvailable(key));
    }
    out["id"] = cam->getGUID();
    out["overrides"] = overrides;
    out["resolved"] = resolved;
    out["available"] = available;
    return out;
}

QVariantMap CameraApi::clearPostOverride(const QString &id, const QVariant &row)
{
    QVariantMap out;
    auto cam = cameraOrFail(id, QStringLiteral("camera.clearPostOverride"));
    if (!cam) return out;

    const QVariant normalized = normalizeJs(row);
    QStringList keys;
    if (!normalized.isValid() || normalized.isNull()) {
        // No row named: clear the lot. "Put this camera back on the world" is
        // one action a user takes, and asking for it key by key is not it.
        keys = cam->postOverrides.keys();
    } else {
        const QString key = normalized.toString();
        if (!iris::cameraPostKey(key)) {
            fail(QStringLiteral("camera.clearPostOverride: unknown override '%1' (known: %2)")
                     .arg(key, postKeyNames()));
            return out;
        }
        keys << key;
    }

    UndoService *undo = host.services ? host.services->undo : nullptr;
    QVariantList cleared;
    for (const QString &key : keys) {
        const QString property = QStringLiteral("postFx.") + key;
        const QVariant before = cam->getPropertyValue(property);
        if (!cam->clearPostOverride(key)) continue;
        cleared << key;
        if (undo) undo->push(new SetNodePropertyCommand(cam, property, before, QVariant()));
    }
    out["id"] = cam->getGUID();
    out["cleared"] = cleared;
    return out;
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
