/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCENENODEHELPER_H
#define SCENENODEHELPER_H

#include "irisgl/irisglfwd.h"

#include "irisgl/document/scenegraph/meshnode.h"

#include "data/constants.h"
#include "data/project.h"
#include "data/database/database.h"

class SceneNodeHelper
{
public:
    /// A node on a SEEDED BUILT-IN MESH (ATOM P2, services/primitiveassets.h).
    /// `meshSeed` is the shipped mesh's seed key — the ":/..."/"app/..." string
    /// the document has always stored for a built-in — and the node gets the
    /// BAKED asset behind it (a real LOD chain, cards, an SDF), not a run-time
    /// parse. Null mesh when there is no library or the seed is unknown: the
    /// node still carries its path, which is what the reader has always left
    /// behind for a mesh that did not load.
    static iris::MeshNodePtr createBasicMeshNode(
        const QString &meshSeed,
        const QString &meshName,
        const QString &guid,
        Database *db
    );
};

#endif