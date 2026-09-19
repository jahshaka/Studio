/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#pragma once

// A GRAPH BECOMES A BUNDLE DEFINITION — MATERIAL_BUNDLE_SPEC §2.3, phase 1.
//
// The Materials module's side of `services/materialbundle.h`. A node graph is
// a PAYLOAD of the one Material row, not a second asset, so the module's save
// has exactly one job: turn the live graph into the definition that row
// stores. Three things happen here and nowhere else —
//
//  1. THE GRAPH IS EVALUATED AND BAKED, once, through the existing emitter +
//     GraphBaker. Nothing about that changed.
//  2. EVERY MAP VALUE COMES BACK AS A GUID. The baker needs real image PATHS
//     to read and write pixels, so its output names files; a definition may
//     never name a file (F3). A bare texture chain's path is mapped back to
//     the graph texture node's own guid, and a BAKED map becomes a member
//     Texture row in the content-addressed store. That is lock 2 of the
//     spec's three: the evaluator resolves to paths only when a material is
//     BUILT, never when one is stored.
//  3. BAKED MAPS ARE MEMBERS, not loose files in a project folder. One member
//     row per slot per material, `parent` = the material (so both browsers
//     hide it as an import member already does) and the bytes in the store —
//     which is what makes a baked map travel in an archive, resolve on
//     another machine, and get collected when nothing names it. The old
//     `<projectFolder>/BakedMaps/<guid>/` tree and the project-relative paths
//     that named it are gone, and with them the rule that a graph material
//     could not be read with no project open.

#include <QJsonObject>
#include <QString>
#include <QStringList>

class Database;
class NodeGraph;
class Project;

namespace materials {

struct DefinitionBuild
{
    /// The bundle definition: values (GUIDs only), the `shadergraph` payload,
    /// and the bake record.
    QJsonObject definition;
    /// The member Texture rows this build wrote (one per baked slot).
    QStringList bakedMembers;
    /// Diagnostics the page surfaces: sockets the evaluator could not fold.
    QStringList unsupportedNodes;
    QString error;
    bool ok() const { return error.isEmpty(); }
};

/// Build `materialGuid`'s definition from `graph`. `db` must be open; `project`
/// may be null (a library material with no project open bakes and reads
/// perfectly well now — its maps are store objects, not project-relative
/// paths). `bake` false skips the per-texel bake: the uniform folds and bare
/// texture chains still land, which is what a fast preview wants.
DefinitionBuild buildDefinition(NodeGraph *graph, const QString &materialGuid,
                                Database *db, Project *project, bool bake = true);

/// The member Texture row for `slot` of `materialGuid`, found or minted:
/// `parent` = the material, one row per slot forever, its source pointer moved
/// to the new bytes on every re-bake. Empty on failure.
QString bakedMemberRow(Database *db, const QString &materialGuid, const QString &slot,
                       const QString &filePath, QString *errorOut = nullptr);

} // namespace materials
