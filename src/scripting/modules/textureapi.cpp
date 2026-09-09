/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "scripting/modules/textureapi.h"

#include <QByteArray>
#include <QColor>
#include <QImage>

#include "scripting/modules/moduleshared.h"
#include "services/livetextures.h"
#include "services/livevideo.h"

using namespace scriptmod;

namespace {

/// The pixels a write was handed, as an RGBA8888 image of exactly w x h, or a
/// null image with `error` set. THREE SPELLINGS, because the three callers a
/// live texture has want different ones: an ARRAY of bytes (a script computing
/// a pattern), a BASE64 string (a payload that came from somewhere else, e.g.
/// an MCP tool call), and a COLOUR (the "fill it with this" case that would
/// otherwise be a loop nobody enjoys writing).
QImage imageFromJs(const QVariant &value, int w, int h, QString *error)
{
    const QVariant v = normalizeJs(value);
    const qsizetype need = qsizetype(w) * h * 4;

    if (v.typeId() == QMetaType::QString) {
        const QString s = v.toString();
        // A COLOUR, not a payload: anything QColor recognises ("#ff0000",
        // "#ff000080", "red"). A base64 RGBA payload is never a colour name —
        // the shortest legal one is 4 characters of a 1x1 texture, and no
        // colour name is valid base64 of the right length for a real image.
        const QColor colour(s);
        if (colour.isValid()) {
            QImage filled(w, h, QImage::Format_RGBA8888);
            filled.fill(colour);
            return filled;
        }
        const QByteArray bytes = QByteArray::fromBase64(s.toLatin1());
        if (bytes.size() != need) {
            if (error)
                *error = QStringLiteral("the base64 payload decodes to %1 bytes; %2x%3 RGBA "
                                        "needs %4").arg(bytes.size()).arg(w).arg(h).arg(need);
            return QImage();
        }
        QImage image(reinterpret_cast<const uchar *>(bytes.constData()), w, h,
                     w * 4, QImage::Format_RGBA8888);
        // DEEP COPY: the QByteArray dies with this scope and QImage's raw-data
        // constructor does not own its buffer.
        return image.copy();
    }

    if (v.canConvert<QVariantList>()) {
        const QVariantList list = v.toList();
        if (list.size() != need) {
            if (error)
                *error = QStringLiteral("the pixel array has %1 entries; %2x%3 RGBA needs %4 "
                                        "(r, g, b, a per texel, row 0 at the top)")
                             .arg(list.size()).arg(w).arg(h).arg(need);
            return QImage();
        }
        QImage image(w, h, QImage::Format_RGBA8888);
        uchar *out = image.bits();
        for (qsizetype i = 0; i < need; ++i)
            out[i] = uchar(qBound(0, list.at(i).toInt(), 255));
        return image;
    }

    if (error)
        *error = QStringLiteral("pixels must be an array of bytes, a base64 string or a "
                                "colour to fill with");
    return QImage();
}

} // namespace

QVector<VerbInfo> TextureApi::verbs() const
{
    return {
        { "createLive", "texture.createLive(name, width, height, {mipmaps}?) -> guid",
          "Creates a LIVE TEXTURE — pixels this session owns, with no file and no library row "
          "behind them — and returns its guid. Bind it like any other texture "
          "(material.set(node, {baseColorMap: guid})) and write into it with texture.write; "
          "every write bumps a generation counter and the renderer re-uploads once per changed "
          "generation, never per frame. Born opaque black at width x height (max 8192 each). "
          "`mipmaps` (default false) builds the whole chain on every write — ask for it when the "
          "texture will be seen minified, and leave it off for a full-screen or close-up surface. "
          "It is fixed HERE, not per write, because the renderer fixes a texture's mip count at "
          "creation and a write replaces the chain it already has. "
          "SESSION ONLY, and that is the design: a live texture is never saved, never pinned to a "
          "project, never exported and never a store object. Saving a scene that binds one writes "
          "the map as ABSENT rather than a guid that could never resolve again, so reopening it "
          "shows the material's base colour and logs the miss.",
          Needs::Document },
        { "write", "texture.write(guid, width, height, pixels) -> bool",
          "Replaces a live texture's pixels. `width`/`height` are the size you believe the "
          "texture is: they must match what createLive made, and a mismatch is refused with both "
          "numbers rather than silently stretched — a live texture cannot resize (the renderer's "
          "texture cannot), so a new size is texture.remove + createLive. `pixels` is one of three "
          "things: an ARRAY of width*height*4 bytes (r, g, b, a per texel, row 0 at the top), a "
          "BASE64 string of the same bytes, or a COLOUR ('#ff0000', '#ff000080', 'red') to fill "
          "the whole texture with. Each accepted write moves the generation the renderer watches, "
          "so a write is all it takes to change what is on screen — no re-binding, no material "
          "edit, no undo entry (pixels are not a document edit).",
          Needs::Document },
        { "info", "texture.info(guid) -> {guid, name, width, height, mipmaps, generation, ref} | null",
          "What a live texture is, or null when the guid names none. `generation` is the write "
          "counter the renderer's upload is keyed on — it starts at 1 (the black birth frame) and "
          "moves by one per accepted write, which is how a producer proves its frames are "
          "landing. `ref` is the string a material row stores for this texture ('live://<guid>'); "
          "it is what tells the scene writer to skip the row and the loader that the pixels "
          "belonged to a session that is over.",
          Needs::Document },
        { "list", "texture.list() -> [{guid, name, width, height, mipmaps, generation}]",
          "Every live texture in this session, in creation order. The catalog is the only place "
          "they exist — assets.list will never show one under scope 'store' or 'project' "
          "(assets.list({scope: 'session', type: 'livetexture'}) does).",
          Needs::Document },
        { "remove", "texture.remove(guid) -> bool",
          "Drops a live texture (named `remove` and not `destroy` because the scripting object "
          "wrapper owns that name and would shadow the verb): its identity, its pixels and any video "
          "bound to it "
          "(video.bind). False when the guid names none. A material still bound to it keeps the "
          "reference string and simply samples nothing — the renderer frees its copy on the next "
          "sweep. Nothing on disk is touched, because a live texture was never on disk.",
          Needs::Document },
    };
}

QVariant TextureApi::createLive(const QString &name, int width, int height,
                                const QVariantMap &options)
{
    const QVariantMap opts = normalizeJs(options).toMap();
    for (auto it = opts.constBegin(); it != opts.constEnd(); ++it) {
        if (it.key() == QLatin1String("mipmaps") || it.key() == QLatin1String("mips")) continue;
        fail(QStringLiteral("texture.createLive: unknown option '%1' — the only option is "
                            "'mipmaps'").arg(it.key()));
        return jsNull();
    }
    const bool mipmaps = opts.value(QStringLiteral("mipmaps"),
                                    opts.value(QStringLiteral("mips"), false)).toBool();

    QString error;
    const QString guid = LiveTextureCatalog::create(name, width, height, mipmaps, &error);
    if (guid.isEmpty()) {
        fail(QStringLiteral("texture.createLive: %1").arg(error));
        return jsNull();
    }
    return guid;
}

bool TextureApi::write(const QString &guid, int width, int height, const QVariant &rgba,
                       const QVariantMap &options)
{
    const QVariantMap opts = normalizeJs(options).toMap();
    if (!opts.isEmpty()) {
        // MIPS ARE A CREATE-TIME DECISION and saying so is the whole point of
        // refusing here: the renderer fixes a texture's mip count when the
        // texture is made, and accepting {mips: true} on a write would look
        // like it did something.
        const QString key = opts.constBegin().key();
        return fail(QStringLiteral("texture.write: unknown option '%1'%2")
                        .arg(key,
                             (key == QLatin1String("mips") || key == QLatin1String("mipmaps"))
                                 ? QStringLiteral(" — mip levels are fixed when the texture is "
                                                  "created: texture.createLive(name, w, h, "
                                                  "{mipmaps: true})")
                                 : QString()));
    }

    const QVariantMap current = LiveTextureCatalog::info(guid);
    if (current.isEmpty())
        return refuse(QStringLiteral("texture.write: no live texture '%1'").arg(guid));
    const int w = current.value(QStringLiteral("width")).toInt();
    const int h = current.value(QStringLiteral("height")).toInt();
    if (width != w || height != h)
        return fail(QStringLiteral("texture.write: live texture '%1' is %2x%3, not %4x%5 — a "
                                   "live texture cannot resize; texture.remove it and create the size "
                                   "you want").arg(guid).arg(w).arg(h).arg(width).arg(height));

    QString error;
    const QImage image = imageFromJs(rgba, w, h, &error);
    if (image.isNull()) return fail(QStringLiteral("texture.write: %1").arg(error));
    if (!LiveTextureCatalog::write(guid, image, &error))
        return fail(QStringLiteral("texture.write: %1").arg(error));
    return true;
}

QVariant TextureApi::info(const QString &guid)
{
    const QVariantMap out = LiveTextureCatalog::info(guid);
    if (out.isEmpty()) {
        refuse(QStringLiteral("texture.info: no live texture '%1'").arg(guid));
        return jsNull();
    }
    return out;
}

QVariantList TextureApi::list()
{
    QVariantList out;
    for (const LiveTextureCatalog::Record &r : LiveTextureCatalog::list()) {
        QVariantMap row = LiveTextureCatalog::info(r.guid);
        row.remove(QStringLiteral("ref"));
        out.append(row);
    }
    return out;
}

bool TextureApi::remove(const QString &guid)
{
    // The video binding first: its decoder writes into these pixels, and a
    // binding left pointing at a destroyed texture would spend the session
    // decoding frames into a refusal.
    for (const QString &videoGuid : LiveVideo::bound()) {
        LiveVideoBinding *binding = LiveVideo::find(videoGuid);
        if (binding && binding->textureGuid() == guid) LiveVideo::unbind(videoGuid);
    }
    if (!LiveTextureCatalog::destroy(guid))
        return refuse(QStringLiteral("texture.remove: no live texture '%1'").arg(guid));
    return true;
}
