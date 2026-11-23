/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#pragma once

// A BAKE's MEMBER ROWS, ALL OR NONE (NOQT-DATABASE-1 stage 2, the second confirm's finding 2). The graph save
// (graphdefinition.cpp) bakes its maps to files; these become the material's member Texture rows - `parent` = the
// material, ONE ROW PER SLOT FOREVER, its source pointer moved to the new bytes on every re-bake. In a file of its
// own so it links headless (materials.bundle runs it): the bake itself needs the graph and its baker.
//
// THE LAW: every slot's row (and the material's home) is READ in one pass BEFORE the first write - a read that
// failed is no "no row yet" (a second row would be minted, the definition's guid moved) - and the writes (a new
// row, the object, the source pointer) run in ONE unit: a failure after the reads rolls every slot back. The
// sidecars are written after the commit (a projection of committed rows).

#include <QHash>
#include <QMap>
#include <QString>
#include <QStringList>

class Database;

namespace materials {

struct BakedMembers
{
    QHash<QString, QString> guidForSlot;   ///< slot -> its member row, for every slot whose file was written
    QStringList dropped;                   ///< "<slot> <- <why>": a slot the bake wrote no file for
    QString error;                         ///< a read or a write failed: nothing was written (the build is refused)
    bool ok() const { return error.isEmpty(); }
};

/// The member rows of `materialGuid` for `files` (slot -> the PNG the bake wrote), found or minted, all or none.
BakedMembers bakedMemberRows(Database *db, const QString &materialGuid, const QMap<QString, QString> &files);

} // namespace materials
