/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef STUDIO_LIVETEXTURES_H
#define STUDIO_LIVETEXTURES_H

// THE LIVE TEXTURE CATALOG (MATERIAL_GAPS_SPEC ADDENDUM A-1, owner decision
// 2026-09-09) — the SESSION-ONLY asset kind.
//
// A live texture is pixels a producer writes at runtime: a script, a video
// decoder, a future render target. It gets a guid and a ModelTypes::LiveTexture
// catalog row for exactly one reason — so `material.set(node, {baseColorMap:
// guid})` resolves it the way it resolves every other texture — and it is
// NEVER persisted, NEVER pinned to a project, NEVER a content-addressed store
// object and NEVER exported. There is no database table and no file behind one.
//
// WHAT THAT COSTS, STATED: reopening a scene that once bound a live texture
// finds the row gone. The scene writer therefore skips live references
// entirely rather than writing a guid that can never resolve again (a dangling
// guid in a saved file is the failure this design exists to prevent), and the
// document-side resolver logs the miss and leaves the slot empty.
//
// TWO HALVES, ONE OWNER. This table owns the IDENTITY (guid, name, size); the
// PIXELS and the generation counter live on the document's iris::Texture2D
// (irisgl/document/assets/livetextures.h), because that is what a material row
// points at and what SceneMirror uploads from. Everything here is static: the
// catalog is a property of the process, like AssetManager's list, not of any
// service that can be absent in a headless run.
//
// The rows are ALSO mirrored into AssetManager as ModelTypes::LiveTexture
// entries, so `assets.list({scope: 'session'})` and every AssetManager lookup
// sees them. That mirror is re-asserted lazily rather than maintained: opening
// a project calls AssetManager::clearAssetList(), which would otherwise delete
// a session identity that has nothing to do with the project.

#include <QImage>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

class LiveTextureCatalog
{
public:
    struct Record
    {
        QString guid;
        QString name;
        int     width = 0;
        int     height = 0;
        bool    mipmaps = false;
    };

    /// Registers a new live texture and returns its guid; an empty string on
    /// failure, with `error` filled in. Born opaque black.
    static QString create(const QString &name, int width, int height, bool mipmaps,
                          QString *error = nullptr);

    /// Replaces the pixels and bumps the document generation the mirror
    /// watches. Refuses a size other than the one it was created with — the
    /// renderer's texture cannot resize, so that is destroy + create.
    static bool write(const QString &guid, const QImage &rgba, QString *error = nullptr);

    static bool exists(const QString &guid);
    /// {guid, name, width, height, mipmaps, generation, ref} — an empty map
    /// when the guid names no live texture.
    static QVariantMap info(const QString &guid);
    /// Drops the identity and the pixels. The renderer's copy is freed by the
    /// mirror's ordinary sweep the moment nothing binds it any more.
    static bool destroy(const QString &guid);
    static QVector<Record> list();

    /// The string a material row stores for this guid ("live://<guid>").
    static QString refFor(const QString &guid);
    /// The guid a live reference names, or an empty string.
    static QString guidOfRef(const QString &ref);

private:
    static QVector<Record> &rows();
    /// Re-adds any row missing from AssetManager (see the header note).
    static void ensureRegistered();
};

#endif // STUDIO_LIVETEXTURES_H
