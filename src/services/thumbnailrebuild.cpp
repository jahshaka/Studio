/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/thumbnailrebuild.h"

#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPixmap>
#include <QSqlDatabase>

#include "bridge/assetthumbnail.h"
#include "bridge/enginehost.h"
#include "bridge/enginethumbnailrenderer.h"
#include "data/constants.h"
#include "data/database/database.h"
#include "data/project.h"
#include "io/materialreader.h"
#include "irisgl/core/irisutils.h"
#include "services/animationfile.h"
#include "services/assetcas.h"
#include "services/assethelper.h"
#include "services/assetstorepaths.h"
#include "services/iesprofile.h"
#include "services/avatarassets.h"
#include "services/thumbnailmanager.h"
#include "services/videoutils.h"

namespace thumbrebuild
{
namespace
{

QString storeFileFor(const QString &guid)
{
    return AssetCas::resolveSource(QSqlDatabase::database(), AssetStorePaths::root(), guid);
}

Outcome store(Database *db, const QString &guid, const QPixmap &pixmap)
{
    if (pixmap.isNull()) return Outcome::bad(QStringLiteral("the render produced no image"));
    if (!db->updateAssetThumbnail(guid, AssetHelper::makeBlobFromPixmap(pixmap)))
        return Outcome::bad(QStringLiteral("the thumbnail could not be written to the library"));
    return Outcome::good();
}

/// The AVATAR branch (THUMBS-1 item 3). An avatar's tile used to be a ONE-TIME
/// COPY of its model's thumbnail, taken at create — so an avatar minted from a
/// model that had no thumbnail yet (every avatar the owner has) is grey for
/// ever, and no verb and no menu could redraw it. An avatar LOOKS like its
/// character, so its thumbnail is that character's model, rendered now.
/// (Everything else about the avatar module's design is parked — this is the
/// thumbnail and nothing more.)
Outcome rebuildAvatar(Database *db, Project *project, const QString &guid)
{
    AvatarAssets::Loaded loaded = AvatarAssets::load(guid, AvatarAssets::Scope::Library, db, project);
    if (!loaded.ok() && project)
        loaded = AvatarAssets::load(guid, AvatarAssets::Scope::Project, db, project);
    if (!loaded.ok())
        return Outcome::bad(QStringLiteral("the avatar definition could not be read: %1").arg(loaded.error));
    if (loaded.definition.modelAsset.isEmpty())
        return Outcome::bad(QStringLiteral("this avatar names no model to draw"));

    QString reason;
    const QImage image = assetthumb::renderObject(db, project, loaded.definition.modelAsset,
                                                  EngineHost::instance().engine(),
                                                  assetthumb::defaultSize(), &reason);
    if (image.isNull())
        return Outcome::bad(QStringLiteral("the avatar's model could not be rendered: %1").arg(reason));
    return store(db, guid, QPixmap::fromImage(image));
}

Outcome rebuildMaterialOrShader(Database *db, Project *project, const QString &guid, bool shader)
{
    auto engine = EngineHost::instance().engine();
    auto loan = EngineThumbnailRenderer::borrow(engine, shader ? "the shader thumbnail"
                                                               : "the material thumbnail");
    if (!loan) return Outcome::bad(loan.reason());

    MaterialReader reader;
    reader.setProject(project);
    iris::MaterialPtr material;
    if (shader) {
        material = reader.parseShaderAsPbr(guid, db);
        if (!material)
            return Outcome::bad(QStringLiteral(
                "this shader carries no evaluated material (re-save the graph, or run "
                "materials.regenerate; baked maps need an open project)"));
    } else {
        const auto object = QJsonDocument::fromJson(db->fetchAssetData(guid)).object();
        material = reader.parseMaterialTyped(object, db);
    }
    const QImage image = loan->renderMaterial(material, QSize(512, 512));
    if (image.isNull()) return Outcome::bad(loan->lastFailure());
    return store(db, guid, QPixmap::fromImage(image));
}

}   // namespace

bool thumbnailIsMissing(Database *db, const QString &guid)
{
    if (!db) return false;
    const auto record = db->fetchAsset(guid);
    if (record.thumbnail.isEmpty()) return true;
    QImage image;
    return !image.loadFromData(record.thumbnail, "PNG") || image.isNull();
}

Outcome rebuildOne(Database *db, Project *project, const QString &guid)
{
    if (!db) return Outcome::bad(QStringLiteral("there is no library in this session"));
    const auto record = db->fetchAsset(guid);
    if (record.guid.isEmpty())
        return Outcome::bad(QStringLiteral("no asset with guid '%1'").arg(guid));

    // Document-only types first — no engine needed (headless-safe).
    switch (static_cast<ModelTypes>(record.type)) {
    case ModelTypes::Texture: {
        auto thumb = ThumbnailManager::createThumbnail(storeFileFor(guid), 256, 256);
        if (!thumb || thumb->thumb.isNull())
            return Outcome::bad(QStringLiteral("the image could not be read"));
        return store(db, guid, QPixmap::fromImage(thumb->thumb));
    }
    case ModelTypes::Music:
        return store(db, guid,
                     QPixmap(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-file-music.png")));
    case ModelTypes::Video:
        // First-second frame re-grab; VideoUtils falls back to the film icon
        // when decode fails, so this always writes something sensible.
        return store(db, guid, VideoUtils::thumbnailFor(storeFileFor(guid)));
    case ModelTypes::Animation: {
        // The POSE STRIP the import drew, redrawn — a clip file has no engine
        // render to make (there is nothing to put the clip ON), so this is the
        // same three projected poses, from the stored bytes.
        QImage strip;
        animfile::read(storeFileFor(guid), &strip, 256, 256);
        if (strip.isNull()) return Outcome::bad(QStringLiteral("the animation could not be read"));
        return store(db, guid, QPixmap::fromImage(strip));
    }
    case ModelTypes::File:
        return store(db, guid,
                     QPixmap(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-file-72.png")));
    case ModelTypes::LightProfile: {
        // The candela lobe the import drew, redrawn — two .ies files look
        // identical as names and completely different as light. Pure QPainter,
        // no engine (services/iesprofile.h). The verb and the sweep used to
        // have no branch for these at all, so a profile row whose thumbnail
        // went missing could never get one back.
        const IesProfile profile = IesProfile::parse(storeFileFor(guid));
        if (!profile.ok)
            return Outcome::bad(QStringLiteral("the photometric profile could not be read: %1")
                                    .arg(profile.error));
        return store(db, guid, QPixmap::fromImage(profile.polarThumbnail(256)));
    }
    case ModelTypes::Object:
    case ModelTypes::ParticleSystem: {
        // THE one model-thumbnail routine (bridge/assetthumbnail.h): ALWAYS
        // from the blob — the session-registered import node carries texture
        // properties into the staging directory the commit deleted, so
        // rendering it gives a white, untextured thumbnail.
        QString reason;
        bool noModel = false;
        if (!assetthumb::storeObject(db, project, guid, EngineHost::instance().engine(),
                                     assetthumb::defaultSize(), &reason, &noModel)) {
            Outcome outcome = Outcome::bad(
                reason.isEmpty() ? QStringLiteral("the object could not be rebuilt") : reason);
            outcome.nothingToDraw = noModel;
            return outcome;
        }
        return Outcome::good();
    }
    case ModelTypes::Material:
        return rebuildMaterialOrShader(db, project, guid, /*shader=*/false);
    case ModelTypes::Shader:
        // A shader asset is a graph definition: it thumbnails as the material
        // the evaluator baked into it, on the same preview sphere a .material
        // uses (VISUAL_PARITY_SPEC item 5).
        return rebuildMaterialOrShader(db, project, guid, /*shader=*/true);
    case ModelTypes::Avatar:
        return rebuildAvatar(db, project, guid);
    default:
        return Outcome::bad(QStringLiteral("this asset type (%1) has no thumbnail to render")
                                .arg(record.type));
    }
}

SweepResult rebuildMissing(Database *db, Project *project, const SweepOptions &options,
                           const std::function<void()> &yield)
{
    SweepResult result;
    if (!db) return result;

    QVector<AssetRecord> rows;
    if (options.projectOnly && project && !project->getProjectGuid().isEmpty()) {
        rows = db->fetchProjectPinnedAssets(project->getProjectGuid());
    } else {
        // EVERY ROW, whatever its view filter. A thumbnail is shown wherever
        // an asset is shown, and the filters name WHERE: 2/3 are the Assets
        // page's library tiles, 1 is the editor tray's (a shader graph created
        // from the materials module is an Editor row). A sweep written against
        // the page's own query — which is where this started — silently
        // skipped every graph and every editor-scope row, i.e. most of what
        // has no thumbnail. `rebuildOne` decides per TYPE what can be drawn.
        const QStringList guids = db->fetchAllAssetGuids();
        rows.reserve(guids.size());
        for (const QString &guid : guids) {
            const AssetRecord record = db->fetchAsset(guid);
            if (!record.guid.isEmpty()) rows.append(record);
        }
    }

    for (const AssetRecord &row : rows) {
        if (options.limit > 0 && result.rebuilt + result.failed.size() >= options.limit) break;
        switch (static_cast<ModelTypes>(row.type)) {
        case ModelTypes::Texture: case ModelTypes::Music: case ModelTypes::Video:
        case ModelTypes::Animation: case ModelTypes::File: case ModelTypes::Object:
        case ModelTypes::ParticleSystem: case ModelTypes::Material: case ModelTypes::Shader:
        case ModelTypes::Avatar: case ModelTypes::LightProfile:
            break;
        default:
            continue;   // nothing to draw for this type
        }
        ++result.considered;
        if (options.missingOnly && !thumbnailIsMissing(db, row.guid)) continue;

        const Outcome outcome = rebuildOne(db, project, row.guid);
        if (outcome.ok) { ++result.rebuilt; result.rebuiltGuids.append(row.guid); }
        else if (outcome.nothingToDraw) ++result.skipped;
        else result.failed.append({ row.guid, outcome.reason });

        // BETWEEN ASSETS, NOT INSIDE ONE: each rebuild is a synchronous engine
        // render, and a library of hundreds must not hold the UI thread for the
        // whole sweep. The renderer is not borrowed here — a queued thumbnail
        // tick that lands in the yield gets it, finishes, and gives it back.
        if (yield) yield();
    }
    return result;
}

}   // namespace thumbrebuild
