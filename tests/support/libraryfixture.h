/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef TESTS_SUPPORT_LIBRARYFIXTURE_H
#define TESTS_SUPPORT_LIBRARYFIXTURE_H

// A LARGE LIBRARY, GENERATED THROUGH THE DATABASE's OWN WRITERS (LARGE-LIBRARY-1). The rows have the shape
// one real import / material / project of this build writes (the column comparison of stage 1:
// ~/Developer/spikes/large-library-1/stage1/columns.txt), in seconds rather than the import pipeline's minutes:
//
//   createAssetEntry (real-shaped `properties`: `import` + `metadata`), createDependency (the intrinsic
//   edges a material and a model write), memberstamp::stamp (a picture picked into a material),
//   AssetCas::stage + commitStaged (a DISTINCT store object and its files / asset_files rows per asset),
//   MeshBakeStore::bakeSource (a model's REAL bake of a real OBJ: a generated library boots with nothing for the
//   bake sweep to rebuild), AssetCas::writePin, createProject (a scene blob SceneWriter writes at this build's format version, a
//   tile-sized thumbnail).
// THE ROWS ARE DETERMINISTIC: every row guid is name-based on the library's running count, so two libraries
// grown by the same calls differ only where `Options` says (the bytes law compares one with thumbnails and
// one without).
//
// THE MIX, per 100 assets (the spec's call 4, INFERRED as typical):
//   Assets home     46 textures (the user's imports; 1 in 4 in a drawer), 7 materials each holding ONE
//                   picked (stamped) texture of the Assets home, those 7 stamped textures, 10 models
//                   (an Object and its Mesh member);
//   Materials home  10 materials (the Materials module's own storage: its custom drawer), each holding one
//                   picked (stamped) texture minted in that home, and those 10 textures.
// The first `grow` also writes a drawer, 5 favourites and THE OPEN PROJECT (`Library::openProject`): 20
// rows of its own and 15 pins (10 textures, 5 Materials-home materials) — its size never grows, so a
// listing of it is the same at every library size.
//
// Thumbnails come from pools encoded once (64 asset pictures 256 px high, 8 project tiles at the size a
// windowed save writes, `Constants::TILE_SIZE * 2`: a headless save writes none), with low-amplitude noise
// so their bytes are a real picture's order (the reference torus thumbnail: 39,434 B). Textures, materials
// and models all get one, as a WINDOWED session writes them (a material's rendered tile: effectspage.cpp
// onShaderThumbnail; a model's: the Assets page's render) — the headless reference has none for the last
// two, a stated gap of the column comparison. `Options::thumbnails = false` writes none at all.

#include <QString>
#include <QStringList>

class Database;

namespace libraryfixture {

struct Options {
    bool thumbnails = true;   ///< asset and project thumbnails from the pools
};

/// What has been written so far; `grow` appends to it.
struct Library {
    QString openProject;          ///< the project the listings open (fixed size)
    int drawer = -1;              ///< the drawer a quarter of the textures are filed in
    QStringList textures;         ///< Assets home, the user's imports
    QStringList assetsMaterials;  ///< Assets home materials (each holds one stamped texture)
    QStringList stampedTextures;  ///< Assets home pictures picked into those materials (the fold's rows)
    QStringList homeMaterials;    ///< Materials home: the Materials module's custom drawer
    QStringList homePicked;       ///< the pictures picked into them (Materials home, stamped)
    QStringList objects;          ///< Assets home models
    QStringList meshes;           ///< their Mesh members (parent = the object)
    QStringList projects;         ///< every project, the open one first
    int assets = 0;               ///< library assets written (the mix's rows; the open project's own not)
    qint64 thumbnailBytes = 0;    ///< the asset thumbnail bytes written
    quint64 guids = 0;            ///< row guids issued: they are name-based on this count (deterministic)
};

/// Appends `assets` (a multiple of 100) library assets and `projects` projects to the OPEN library `db`,
/// their store objects under `storeRoot` (its sources staged beside it, on the same filesystem). One
/// DbBatch. False (with `error`) on the first refused write.
bool grow(Database *db, const QString &storeRoot, int assets, int projects, Library &lib,
          const Options &options, QString *error);

}   // namespace libraryfixture

#endif   // TESTS_SUPPORT_LIBRARYFIXTURE_H
