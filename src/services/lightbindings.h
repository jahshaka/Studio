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

// Binding library assets to a light: the IES photometric profile and the area
// light's mask image (LIGHTS_COMPLETION_SPEC phases 3 and 5).
//
// THE one implementation behind the `node.setLightProfile` /
// `node.setLightTexture` verbs and the light property panel's two pickers, so
// the two cannot diverge. It does three things the document cannot do for
// itself, because irisgl has no database:
//   * resolves the guid to the bytes the renderer will open (pin first, then
//     the library source — the same route the scene reader takes);
//   * reads the profile's photometric scale out of the asset's metadata block
//     so the mirror can divide it out of the light's intensity;
//   * pins the asset into the project as a DEPENDENCY, never a direct add
//     (ProjectAssets::AddKind::Binding) — binding an image to a light must not
//     spawn a companion PBR material.

#include <QString>

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

    /// Bind (or clear) the area light's mask image. Fills
    /// lightTextureGuid/lightTexturePath.
    static bool bindTexture(const iris::LightNodePtr &light, const QString &guid,
                            Database *db, Project *project, QString *errorOut = nullptr);

    /// Re-resolve the runtime halves (path + normalisation) of whatever guids
    /// the node already carries — the scene-load rehydration. Never pins and
    /// never fails: an unresolvable guid leaves the path empty, so the light
    /// renders unprofiled/unmasked rather than not at all.
    static void resolve(const iris::LightNodePtr &light, Database *db, Project *project);

    /// The profile's peak photometric scale, from the asset's metadata block
    /// (1.0 when unknown — that is "no correction", never a divide by zero).
    static float normalisationFor(const QString &guid, Database *db);
};

#endif // LIGHTBINDINGS_H
