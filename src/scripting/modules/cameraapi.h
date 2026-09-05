/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCRIPTING_CAMERAAPI_H
#define SCRIPTING_CAMERAAPI_H

// camera.* — the settings of a SCENE camera (CAMERAS_SPEC §6, phase 1).
//
// Cameras are ordinary scene nodes: they are created by scene.addCamera, moved
// by node.transform, deleted by node.delete, parented by node.parent and keyed
// by anim.* — none of that is duplicated here. Nor are the individual settings:
// every one of them is a reflected property on CameraNode, so
// node.setProperty(id, "fStop", 4) works exactly as it does for a light's
// intensity.
//
// What lives here is what the node surface cannot express well:
//   * settings — the WHOLE camera in one read, and one atomic write; a lens is
//     authored as a group ("35 mm on a 36x24 sensor at f/2"), and the
//     angle/focal-length pair in particular must be set together or the two
//     writes fight each other;
//   * lookAt — geometry, not a field: it computes the rotation that points a
//     camera at a target (a point or another node), which no property write
//     can do.
//
// The pilot/dropdown verbs (editor.pilot, editor.setViewCamera), the socket
// verbs and screenshot({camera}) are LATER PHASES of the same spec and are
// deliberately absent — a verb that exists but does nothing is worse than one
// that does not exist.

#include <QStringList>
#include <QVariantMap>

#include "scripting/apimodule.h"
#include "irisgl/irisglfwd.h"

class UndoService;

/// The camera settings block, as free functions, because TWO modules author it:
/// camera.settings and scene.addCamera's `settings` option. They must refuse the
/// same things in the same words, and a verb's error has to be thrown by the
/// module the script actually called (ApiModule::fail finds the JS engine
/// through the module's own QObject — a module built on the stack to borrow a
/// method from would swallow every error it raised).
namespace camerashared {

/// Every settings key, in the order applySettings writes them.
const QStringList &settingsKeys();

/// The whole §2 table as JSON-native values, plus the derived outputWidth.
QVariantMap settingsToJs(const iris::CameraNodePtr &camera);

/// Applies a settings block to `camera`, pushing one undo command per row.
/// Returns an EMPTY string on success, or the message the calling verb must
/// report through its own fail(). `scene` resolves focusTarget; `undo` may be
/// null (a host with no undo service still writes the document).
QString applySettings(const iris::CameraNodePtr &camera, const QVariantMap &params,
                      const iris::ScenePtr &scene, UndoService *undo,
                      const QString &verb);

}   // namespace camerashared

class CameraApi : public ApiModule
{
    Q_OBJECT
public:
    using ApiModule::ApiModule;

    QString jsName() const override { return QStringLiteral("camera"); }
    QVector<VerbInfo> verbs() const override;

    Q_INVOKABLE QVariantMap settings(const QString &id, const QVariant &options = QVariant());
    Q_INVOKABLE bool lookAt(const QString &id, const QVariant &target);

private:
    /// The scene camera with this guid, or null with a JS error already thrown.
    iris::CameraNodePtr cameraOrFail(const QString &id, const QString &verb);
};

#endif // SCRIPTING_CAMERAAPI_H
