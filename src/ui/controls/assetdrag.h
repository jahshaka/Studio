/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef ASSETDRAG_H
#define ASSETDRAG_H

#include <QMap>
#include <QString>
#include <QVariant>

class QMimeData;

/// AssetDrag — THE asset drag payload (MATERIAL-PREVIEW-1 item c).
///
/// Everything a user can drag out of an asset view — the browser grid, the
/// drawer tiles, the two preset trays — travels as one QMimeData under
/// "application/x-qabstractitemmodeldatalist" holding a QMap<int, QVariant>
/// with FOUR slots, and every drop handler in the app reads those four slots
/// back. The four slots were hand-written at four sites and read back at five,
/// each with its own literal indices and its own idea of what slot 2 is (one
/// wrote the string "not used" into it); a fifth writer would have had nothing
/// to copy but a comment.
///
///   0  the ModelTypes value          (what this is)
///   1  the display NAME              (Qt::UserRole at the tile)
///   2  the MESH/file name, if any    (MODEL_MESH_ROLE; empty for most types)
///   3  the asset GUID                (MODEL_GUID_ROLE) — what every handler
///                                      actually resolves against
namespace AssetDrag
{

enum Slot { TypeSlot = 0, NameSlot = 1, MeshSlot = 2, GuidSlot = 3 };

/// The MIME type every asset drag uses. Qt's own item-view name, kept because
/// item views already produce and consume it.
const char *format();

/// A QMimeData carrying those four slots. Caller owns it (hand it straight to
/// QDrag::setMimeData, which takes ownership).
QMimeData *mimeFor(int type, const QString &name, const QString &mesh, const QString &guid);

/// Is this an asset drag at all?
bool isAssetDrag(const QMimeData *mime);

/// The four slots back. An empty map when the mime is not an asset drag —
/// never a half-decoded one.
QMap<int, QVariant> roles(const QMimeData *mime);

/// Slot readers, so a handler never spells an index. -1 / empty when absent.
int typeOf(const QMimeData *mime);
QString guidOf(const QMimeData *mime);

} // namespace AssetDrag

#endif // ASSETDRAG_H
