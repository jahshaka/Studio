/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef PRIMITIVEASSETS_H
#define PRIMITIVEASSETS_H

// EVERY ASSET IS AN ATOM ASSET (SPECS/atom/A2_HONEST_GEOMETRY_AND_EVERY_ASSET_
// DESIGN.md §2.1, ATOM P2). The twelve primitives, the default Ground and the
// Teapot the samples stand on are BAKED LIBRARY ASSETS, created once per library by the ONE
// import pipeline (services/import: sniff -> validate -> convert/bake -> store
// -> register) from the files the app ships, with the reserved guid they have
// always had (src/data/primitives.h is the SEED LIST).
//
// WHAT THIS REPLACES. `iris::Mesh::loadMesh` parsed a primitive's .obj with
// assimp on first use and ran the surface-card generator at creation — ~6 ms
// per node ON THE THREAD THAT DRAWS (SC-1a-F8) — and deliberately built NO LOD
// chain, so every sample scene and every added cube rendered one level at any
// distance while every imported model had a chain. Both halves are gone: a
// primitive now has a real chain, real cards that name real levels, an SDF and
// measured `lodBounds`, and adding one is a bake READ (or a cache hit).
//
// THE DOCUMENT DID NOT CHANGE, on purpose. A mesh node for a built-in still
// stores `":/content/primitives/cube.obj"` in its `mesh` field — that string is
// a SEED KEY now, resolved here to the baked asset, never a file to parse. So
// every scene ever saved (the eight shipped samples name five primitives, the
// Ground and the Teapot between them) opens unchanged and gains the chain, the
// writer is untouched, and the places that recognise the floor by its mesh path
// keep working. The reserved GUID is the LIBRARY row's identity (a favourite,
// the tiles' drop payload, `assets.builtins`); the seed PATH is the document's.
//
// SEEDED ONCE PER LIBRARY, SYNCHRONOUSLY, WHERE THE LIBRARY IS OPENED, and never
// on a background thread: a seed that lands between two `assets.list` calls is a
// row count that moves under a script's feet (MaterialPresetSeeder's header
// states that defect; it cost scripting.e2e.full_surface a red). So the shell
// runs `seedAll` when it opens a library — every session, driven or not, starts
// with the same rows — and a library that already holds them pays one catalog
// query per row and starts nothing.
//
// TWO TRANSLATION UNITS. `mesh` RESOLVES (catalog + the bake reader);
// `ensureSeeded`/`seedAll` CREATE (the whole import pipeline). They are split
// because the resolve half is linked by the scene reader, the floor and the
// preview docks — including four preview-only test binaries with six sources and
// no catalog — and none of them should drag the importer in for geometry it will
// never create.
//
// THE PROCESS-WIDE MESH CACHE is what used to be `Mesh::pinLoadPaths`: these
// are a fixed, small set of meshes every world stands on, so the deserialized
// model is held for the life of the process instead of being re-read per open.
// UI-THREAD (the bake read resolves through the catalog on the caller's
// per-thread connection); a worker with no connection gets a cache hit or
// nothing, never a parse.

#include <QString>
#include <QStringList>

#include "irisgl/irisglfwd.h"

class Database;

namespace primitives { struct Def; }

namespace PrimitiveAssets
{

/// THE OPEN LIBRARY, registered once by the shell when it opens one. The
/// PREVIEW bridges (the asset and material docks, the thumbnail renderer) are
/// handed no database — they render app furniture, not library content — and a
/// seed has to reach the catalog to exist at all. Every caller that HAS a
/// database still passes it; this is the fallback for the ones that do not, and
/// it is the same object (one library, one default connection per thread).
void setLibrary(Database *db);

/// THE BAKED MESH for a seed, by its NAME ("Cube", "Ground") or by its seed
/// PATH (":/content/primitives/cube.obj", an absolute app-folder path), held for
/// the life of the process after the first read. RESOLVE ONLY: null when the
/// library has not seeded that row or its bake cannot be read by this build — the
/// caller renders nothing, exactly as it did when a parse failed.
iris::MeshPtr mesh(const QString &nameOrSeedPath, Database *db = nullptr);

/// The library row's guid for a seed, creating it if needed (the reserved guid
/// from src/data/primitives.h). Empty on failure, with `errorOut` set.
/// SEEDING HALF (primitiveseed.cpp — it links the import pipeline; `mesh` above
/// does not, so a preview-only binary can resolve without it). `db` is required.
QString ensureSeeded(const primitives::Def &def, Database *db, QString *errorOut = nullptr);

/// Seed and bake EVERY row of the list. Returns how many rows this call
/// created; `errors` collects one line per failure. Idempotent and cheap on a
/// library that already holds them (one catalog query each).
int seedAll(Database *db, QStringList *errors = nullptr);

/// Is every seed row present with a bake this build can read? The honest
/// "nothing left to seed" answer for a suite and for the first-run predicate.
bool allSeeded(Database *db);

/// Drop the held meshes (a library wipe, a test moving between data roots).
void clearCache();

}   // namespace PrimitiveAssets

#endif   // PRIMITIVEASSETS_H
