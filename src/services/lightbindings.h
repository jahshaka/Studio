/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef LIGHTBINDINGS_H
#define LIGHTBINDINGS_H

// Binding a library asset to a light: the IES photometric profile
// (LIGHTS_COMPLETION_SPEC phase 3; the area light's mask was deleted with the
// area approximation, PHOTON-7 7-0 — LTC has no mask term).
//
// THE one implementation behind the `node.setLightProfile` verb and the light
// property panel's profile picker, so the two cannot diverge. It does three
// things the document cannot do for
// itself, because irisgl has no database:
//   * resolves the guid to the bytes the renderer will open (pin first, then
//     the library source — the same route the scene reader takes);
//   * reads the profile's photometric scale out of the asset's metadata block
//     so the mirror can divide it out of the light's intensity;
//   * pins the asset into the project as a DEPENDENCY, never a direct add
//     (ProjectAssets::AddKind::Binding) — binding an image to a light must not
//     spawn a companion PBR material.

#include <QString>

#include <QJsonObject>
#include <functional>

#include "irisgl/irisglfwd.h"

class Database;
class Project;

class LightBindings
{
public:
    /// Bind (or clear, when `guid` is empty) the light's IES profile. Fills
    /// iesProfileGuid/iesProfilePath/iesNormalisation. False ⇒ `errorOut`
    /// carries a user-facing reason and the node is left untouched.
    static bool bindProfile(const iris::LightNodePtr &light, const QString &guid,
                            Database *db, Project *project, QString *errorOut = nullptr);

    /// Re-resolve the runtime halves (path + normalisation) of the guid the
    /// node already carries — the scene-load rehydration. Never pins and never
    /// fails: an unresolvable guid leaves the path empty, so the light renders
    /// unprofiled rather than not at all.
    static void resolve(const iris::LightNodePtr &light, Database *db, Project *project);

    /// The profile's peak photometric scale, from the asset's metadata block
    /// (1.0 when unknown — that is "no correction", never a divide by zero).
    static float normalisationFor(const QString &guid, Database *db);

    /// THE SKY LIGHT's SOURCE (PHOTON-7 10-3 R5b): `cubemap` binds an invisible dome - six TEXTURE asset guids under
    /// their roles (front, back, top, bottom, left, right; world.sky's cubemap keys), each pinned to the project as a
    /// binding and loaded into skyCubeTexture - or, with `faces` empty, the scene's sky (the default). False =>
    /// `errorOut` carries a user-facing reason and the node is left untouched. Sky lights only.
    static bool bindSkyCube(const iris::LightNodePtr &light, const QJsonObject &faces, Database *db, Project *project,
                            QString *errorOut = nullptr);
    /// The six faces loaded as one cube texture (Texture2D::createCubeMap), each guid resolved by `resolve`; null when
    /// any face is missing (all six or nothing - a cube with a hole is no dome).
    static QSharedPointer<iris::Texture2D> loadSkyCube(const QJsonObject &faces,
                                                        const std::function<QString(const QString &)> &resolve);
};

#endif // LIGHTBINDINGS_H
