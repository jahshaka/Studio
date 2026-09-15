/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef LIBRARYASSETNODE_H
#define LIBRARYASSETNODE_H

// libraryasset — THE document node of a stored MODEL asset (smoke S5 + S6).
//
// An Object row has exactly one texture-correct source: its stored BLOB, read
// through SceneReader with the database handle, which resolves every mesh and
// texture guid to a file in the store. Anything else is a copy of the truth
// that goes stale:
//
//   * the IMPORT-TIME fragment (ImportResult::node, deleted 2026-09-11 once
//     nothing read it) carried material paths into the import's STAGING
//     directory, which the commit deletes — so the
//     Assets page rendered a white model and PERSISTED it as the thumbnail
//     (the owner's "GLB imports show no textures in Assets", 2026-09-11);
//     `assets.refreshThumbnail` already said so in a comment and rebuilt from
//     the blob, which is why the verb path was textured and the page was not.
//
// The SECOND reason this function existed — applying the asset's fit-to-size
// factor so a preview and a drop were the same size — is retired with the fit
// itself (SPECS/IMPORT_DIALOG_SPEC.md §6): the size is BAKED into the asset at
// import, so the blob alone IS the size every surface shows and every
// placement uses. Every preview surface (the Assets page's import tail, its
// viewer, the thumbnail routine and `assets.refreshThumbnail`) still asks this
// one function, for the first reason.

#include <QString>
#include "irisgl/irisglfwd.h"

class Database;
class Project;

namespace libraryasset
{

/// The library node for an Object/ParticleSystem asset: its stored blob, with
/// the asset's fit-to-size factor applied at the root. Null when the row has no
/// blob or the blob's root is a retired node type. `project` may be null (the
/// resolution is the LIBRARY's, not a project's pins).
iris::SceneNodePtr fromLibrary(Database *db, Project *project, const QString &guid);

}   // namespace libraryasset

#endif   // LIBRARYASSETNODE_H
