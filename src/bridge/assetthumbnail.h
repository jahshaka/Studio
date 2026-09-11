/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef ASSETTHUMBNAIL_H
#define ASSETTHUMBNAIL_H

// assetthumb — THE thumbnail of a MODEL asset (smoke S6).
//
// One routine, three callers: `assets.refreshThumbnail` (the verb, and
// therefore `assets.import`/`assets.importFile`), the Assets page's import
// tail, and the page's tile "Rebuild Thumbnail". They used to be two: the verb
// rebuilt the node from the stored blob (textured), while the page rendered
// the LIVE import node through its preview viewer and persisted that white
// image as the thumbnail — the owner's "GLB imports show no textures in the
// Assets tiles" (2026-09-11). A page import and a scripted import now produce
// the same bytes because they run the same code.
//
// The node comes from libraryasset::fromLibrary (blob + the asset's fit) and
// the render from EngineThumbnailRenderer, framed from the subject's world
// bounds like the editor's F (viewport/previewframing.h).
//
// Studio-side: the engine abstraction, never Ogre.
#include <memory>
#include <QImage>
#include <QSize>
#include <QString>

namespace jahshaka { namespace engine { class Engine; } }

class Database;
class Project;

namespace assetthumb
{

/// The default tile render size (the Assets grid tile and the library row).
inline QSize defaultSize() { return QSize(512, 512); }

/// Renders the library node of a MODEL asset. Null image with no engine, no
/// blob, or a blob with no geometry.
QImage renderObject(Database *db, Project *project, const QString &guid,
                    const std::shared_ptr<jahshaka::engine::Engine> &engine,
                    QSize size = defaultSize());

/// renderObject + the database write. False when nothing was rendered (the
/// stored thumbnail is then left alone rather than blanked).
bool storeObject(Database *db, Project *project, const QString &guid,
                 const std::shared_ptr<jahshaka::engine::Engine> &engine,
                 QSize size = defaultSize());

}   // namespace assetthumb

#endif   // ASSETTHUMBNAIL_H
