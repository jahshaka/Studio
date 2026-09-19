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
#include "bridge/enginethumbnailrenderer.h"
#include "jahshaka/engine/Engine.h"
#include "data/constants.h"
#include "data/database/database.h"
#include "data/project.h"
#include "io/materialreader.h"
#include "services/materialbundle.h"
#include "irisgl/core/irisutils.h"
#include "services/animationfile.h"
#include "services/assetcas.h"
#include "services/assethelper.h"
#include "services/assetstorepaths.h"
#include "services/iesprofile.h"
#include "services/avatarassets.h"
#include "services/thumbnailmanager.h"
#include "services/videoutils.h"

using jahshaka::engine::Engine;

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
Outcome rebuildAvatar(Database *db, Project *project, const QString &guid,
                      const std::shared_ptr<Engine> &engine)
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
                                                  engine, assetthumb::defaultSize(), &reason);
    if (image.isNull())
        return Outcome::bad(QStringLiteral("the avatar's model could not be rendered: %1").arg(reason));
    return store(db, guid, QPixmap::fromImage(image));
}

Outcome rebuildMaterialOrShader(Database *db, Project *project, const QString &guid,
                                const std::shared_ptr<Engine> &engine, bool shader)
{
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
        // THE BUNDLE'S DEFINITION, pin-first (MATERIAL_BUNDLE_SPEC D-2), not
        // the row's blob cache: a tile must show the version its project
        // holds.
        const auto object = MaterialBundle::read(db, guid, project);
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

Outcome rebuildOne(Database *db, Project *project, const QString &guid,
                   const std::shared_ptr<Engine> &engine)
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
        if (!assetthumb::storeObject(db, project, guid, engine,
                                     assetthumb::defaultSize(), &reason, &noModel)) {
            Outcome outcome = Outcome::bad(
                reason.isEmpty() ? QStringLiteral("the object could not be rebuilt") : reason);
            outcome.nothingToDraw = noModel;
            return outcome;
        }
        return Outcome::good();
    }
    case ModelTypes::Material:
        return rebuildMaterialOrShader(db, project, guid, engine, /*shader=*/false);
    case ModelTypes::Shader:
        // A shader asset is a graph definition: it thumbnails as the material
        // the evaluator baked into it, on the same preview sphere a .material
        // uses (VISUAL_PARITY_SPEC item 5).
        return rebuildMaterialOrShader(db, project, guid, engine, /*shader=*/true);
    case ModelTypes::Avatar:
        return rebuildAvatar(db, project, guid, engine);
    default:
        return Outcome::bad(QStringLiteral("this asset type (%1) has no thumbnail to render")
                                .arg(record.type));
    }
}

namespace
{
bool typeHasAThumbnail(ModelTypes type)
{
    switch (type) {
    case ModelTypes::Texture: case ModelTypes::Music: case ModelTypes::Video:
    case ModelTypes::Animation: case ModelTypes::File: case ModelTypes::Object:
    case ModelTypes::ParticleSystem: case ModelTypes::Material: case ModelTypes::Shader:
    case ModelTypes::Avatar: case ModelTypes::LightProfile:
        return true;
    default:
        return false;
    }
}
}   // namespace

SweepResult rebuildMissing(Database *db, Project *project,
                           const std::shared_ptr<Engine> &engine, const SweepOptions &options,
                           const std::function<void()> &yield)
{
    SweepResult result;
    if (!db) return result;

    // THE ROW LIST IS (guid, type) AND NOTHING ELSE (fix round F2). It used to
    // be a fetchAsset per guid, which selects `thumbnail` AND `properties` —
    // every PNG and every property blob in the catalog held in memory at once
    // before the first yield (hundreds of MB on a real library), for two
    // integers' worth of decision. `missingOnly` also asks SQL for the rows
    // with no thumbnail instead of decoding every stored PNG to find out.
    QVector<Database::AssetThumbnailState> rows;
    if (options.projectOnly && project && !project->getProjectGuid().isEmpty()) {
        for (const AssetRecord &record : db->fetchProjectPinnedAssets(project->getProjectGuid()))
            rows.append({ record.guid, record.type, !record.thumbnail.isEmpty() });
    } else {
        // EVERY ROW, whatever its view filter. A thumbnail is shown wherever
        // an asset is shown, and the filters name WHERE: 2/3 are the Assets
        // page's library tiles, 1 is the editor tray's (a shader graph created
        // from the materials module is an Editor row). A sweep written against
        // the page's own query — which is where this started — silently
        // skipped every graph and every editor-scope row, i.e. most of what
        // has no thumbnail. `rebuildOne` decides per TYPE what can be drawn.
        rows = db->fetchAssetThumbnailStates(options.missingOnly);
    }

    // The yield is not free either (it pumps the event loop), so a healthy
    // library does not pay one per row — but it must pay them REGULARLY, or a
    // sweep that finds nothing to do is still one long stall.
    const int kYieldEvery = 16;
    int since = 0;
    auto stopped = [&options] {
        return stopRequested() || (options.cancelled && options.cancelled());
    };

    for (const auto &row : rows) {
        if (stopped()) { result.cancelled = true; break; }
        if (options.limit > 0 && result.rebuilt + result.failed.size() >= options.limit) break;
        if (!typeHasAThumbnail(static_cast<ModelTypes>(row.type))) continue;
        ++result.considered;

        bool rebuilt = false;
        // The cheap test first (SQL said whether the blob is EMPTY); the full
        // decode only for a row that claims to have one, because an undecodable
        // blob is a grey tile too.
        if (!options.missingOnly || !row.hasThumbnail || thumbnailIsMissing(db, row.guid)) {
            const Outcome outcome = rebuildOne(db, project, row.guid, engine);
            if (outcome.ok) { ++result.rebuilt; result.rebuiltGuids.append(row.guid); rebuilt = true; }
            else if (outcome.nothingToDraw) ++result.skipped;
            else result.failed.append({ row.guid, outcome.reason });
        }

        // BETWEEN ASSETS, NOT INSIDE ONE: each rebuild is a synchronous engine
        // render, and a library of hundreds must not hold the UI thread for the
        // whole sweep. The renderer is not borrowed here — a queued thumbnail
        // tick that lands in the yield gets it, finishes, and gives it back.
        // A YIELD CAN DELIVER THE WINDOW'S CLOSE: the stop check at the top of
        // the loop is what stops us before the next render (F1).
        if (yield && (rebuilt || ++since >= kYieldEvery)) { since = 0; yield(); }
    }
    // No trailing re-check: a stop that arrives after the last row did not stop
    // anything — the sweep finished — and asking the caller's predicate once
    // more would only make "how often am I asked" unanswerable.
    return result;
}

}   // namespace thumbrebuild
