/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/previewservice.h"

#include <algorithm>

#include <QFileInfo>
#include <QImage>
#include <QPixmap>
#include <QSqlDatabase>

#include "bridge/enginethumbnailrenderer.h"
#include "bridge/previewenvironment.h"
#include "data/database/database.h"
#include "data/project.h"
#include "io/materialreader.h"
#include "irisgl/core/irisutils.h"
#include "services/jahlog.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/import/meshbake.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "jahshaka/engine/Engine.h"
#include "services/animationfile.h"
#include "services/assetcas.h"
#include "services/assetstorepaths.h"
#include "services/avatarassets.h"
#include "services/iesprofile.h"
#include "services/libraryassetnode.h"
#include "services/materialbundle.h"
#include "services/meshbakestore.h"
#include "services/previewkey.h"
#include "services/previewwriter.h"
#include "services/thumbnailmanager.h"
#include "services/videoutils.h"

using jahshaka::engine::Engine;

namespace {

QString storeFileFor(const QString &guid)
{
    return AssetCas::resolveSource(QSqlDatabase::database(), AssetStorePaths::root(), guid);
}

PreviewService::Outcome failure(const QString &why, bool nothing = false)
{
    PreviewService::Outcome out;
    out.reason = why;
    out.nothingToDraw = nothing;
    return out;
}

QString projectGuidOf(Project *project)
{
    return project ? project->getProjectGuid() : QString();
}

}   // namespace

PreviewService::PreviewService(Database *db, EngineProvider engine)
    : mDb(db), mEngine(std::move(engine))
{
    if (mDb && !mDb->filePath().isEmpty()) mWriter = std::make_unique<previews::Writer>(mDb->filePath());
}

// The writer drains its queue and joins here: before the library closes (ShellServices::destroyPlain).
PreviewService::~PreviewService() = default;

const char *PreviewService::stateName(State state)
{
    switch (state) {
    case State::Ok: return "ok";
    case State::Missing: return "missing";
    case State::Stale: return "stale";
    case State::None: break;
    }
    return "none";
}

// ---- the per-type pictures (the bodies of thumbrebuild::rebuildOne and assetthumb::renderObject) ----

QImage PreviewService::renderPicture(Project *project, const QString &guid, int type, QSize size,
                                     Outcome *out)
{
    const auto engineOrNull = [this]() { return mEngine ? mEngine() : std::shared_ptr<Engine>(); };
    const auto say = [out](const QString &why) { *out = failure(why); return QImage(); };
    // A TRANSIENT failure (previewservice.h "THE FAILURES"): the cause can change with no change to the row.
    const auto later = [out](const QString &why) { *out = failure(why); out->transient = true; return QImage(); };
    const QSize cpu = size.isEmpty() ? QSize(256, 256) : size;
    const QSize studio = size.isEmpty() ? QSize(512, 512) : size;

    // A MODEL through the one thumbnail renderer, framed to its bounds. ALWAYS from the stored blob:
    // the session-registered import node carries texture paths into a staging directory the commit
    // deleted (rendering it gave a white, untextured tile).
    const auto renderModel = [&](const QString &modelGuid) -> QImage {
        iris::SceneNodePtr node = libraryasset::fromLibrary(mDb, project, modelGuid);
        if (!node) {
            if (mDb->fetchAsset(modelGuid).guid.isEmpty())
                return say(QStringLiteral("the model '%1' is no longer in the library").arg(modelGuid));
            return say(QStringLiteral("the stored model for '%1' could not be read: its bytes are missing, "
                                      "or its blob carries no geometry").arg(modelGuid));
        }
        const auto engine = engineOrNull();
        if (!engine) return later(QStringLiteral("the engine is not running (a model preview is a render)"));
        auto loan = EngineThumbnailRenderer::borrow(engine, "the model preview");
        if (!loan) return later(loan.reason());
        const QImage image = loan->renderNode(node, studio);
        if (image.isNull()) return say(loan->lastFailure());
        return image;
    };

    switch (static_cast<ModelTypes>(type)) {
    case ModelTypes::Texture: {
        // THE ONE IMAGE DECODE of the CPU kinds (SKY-LIGHT-1 adds `.hdr` here).
        auto thumb = ThumbnailManager::createThumbnail(storeFileFor(guid), cpu.width(), cpu.height());
        if (!thumb || thumb->thumb.isNull()) return say(QStringLiteral("the image could not be read"));
        return thumb->thumb;
    }
    case ModelTypes::Music:
        return QImage(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-file-music.png"));
    case ModelTypes::Video:
        // The first-second frame; VideoUtils falls back to the film icon when decode fails.
        return VideoUtils::thumbnailFor(storeFileFor(guid)).toImage();
    case ModelTypes::Animation: {
        // The POSE STRIP, from the CLIP BAKE (SHIPPED-BAKES-1), never a parse: a stale or missing
        // bake is rebuilt from the source on a worker first.
        QImage strip;
        if (const iris::BakedClipPtr clip = MeshBakeStore::ensureClip(storeFileFor(guid)))
            animfile::describe(clip->info, QFileInfo(mDb->fetchAsset(guid).name).completeBaseName(), &strip,
                               cpu.width(), cpu.height());
        if (strip.isNull()) return say(QStringLiteral("the animation's clip bake could not be read"));
        return strip;
    }
    case ModelTypes::File:
        return QImage(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-file-72.png"));
    case ModelTypes::LightProfile: {
        // The candela lobe: two .ies files look identical as names and completely different as light.
        const IesProfile profile = IesProfile::parse(storeFileFor(guid));
        if (!profile.ok)
            return say(QStringLiteral("the photometric profile could not be read: %1").arg(profile.error));
        return profile.polarThumbnail(cpu.width());
    }
    case ModelTypes::Object:
    case ModelTypes::ParticleSystem:
        return renderModel(guid);
    case ModelTypes::Mesh: {
        // THE MESH PRESET: the row's BAKE (the geometry its Object draws; the scene reader's own
        // MeshBakeStore::load of the row's source under its guid), every mesh of it in ONE neutral
        // material - iris::PbrMaterial's documented default (defaultmaterial: grey, roughness 0.75,
        // metalness 0) - framed to its bounds in the studio. Never a parse: no bake, no picture.
        const QString source = storeFileFor(guid);
        const iris::BakedModelPtr baked = source.isEmpty() ? iris::BakedModelPtr() : MeshBakeStore::load(source, guid);
        if (!baked || !baked->valid) return say(QStringLiteral("the mesh's bake could not be read (re-import it)"));
        const iris::MaterialPtr neutral = iris::PbrMaterial::create();
        iris::SceneNodePtr node = iris::MeshBake::buildFragment(
            *baked, source, [neutral](iris::MeshPtr, iris::MeshMaterialData &) { return neutral; });
        if (!node) return say(QStringLiteral("the mesh's bake holds no geometry"));
        node->update(0.0f);
        const auto engine = engineOrNull();
        if (!engine) return later(QStringLiteral("the engine is not running (a mesh preview is a render)"));
        auto loan = EngineThumbnailRenderer::borrow(engine, "the mesh preview");
        if (!loan) return later(loan.reason());
        const QImage image = loan->renderNode(node, studio);
        if (image.isNull()) return say(loan->lastFailure());
        return image;
    }
    case ModelTypes::Avatar: {
        // An avatar LOOKS like its character: its preview is its model's, rendered now.
        AvatarAssets::Loaded loaded = AvatarAssets::load(guid, AvatarAssets::Scope::Library, mDb, project);
        if (!loaded.ok() && project)
            loaded = AvatarAssets::load(guid, AvatarAssets::Scope::Project, mDb, project);
        if (!loaded.ok())
            return say(QStringLiteral("the avatar definition could not be read: %1").arg(loaded.error));
        if (loaded.definition.modelAsset.isEmpty()) return say(QStringLiteral("this avatar names no model to draw"));
        const QImage image = renderModel(loaded.definition.modelAsset);
        if (image.isNull()) out->reason = QStringLiteral("the avatar's model could not be rendered: %1").arg(out->reason);
        return image;
    }
    case ModelTypes::Material: {
        // THE BUNDLE'S DEFINITION, pin-first (MATERIAL_BUNDLE_SPEC D-2): a tile shows the version the
        // open project holds. An EMPTY definition is a named failure, never a default ball.
        const QJsonObject definition = MaterialBundle::read(mDb, guid, project);
        if (definition.isEmpty()) return say(QStringLiteral("the material has no definition to draw"));
        const auto engine = engineOrNull();
        if (!engine) return later(QStringLiteral("the engine is not running (a material preview is a render)"));
        auto loan = EngineThumbnailRenderer::borrow(engine, "the material preview");
        if (!loan) return later(loan.reason());
        MaterialReader reader;
        reader.setProject(project);
        const QImage image = loan->renderMaterial(reader.parseMaterialTyped(definition, mDb), studio);
        if (image.isNull()) return say(loan->lastFailure());
        return image;
    }
    default:
        break;
    }
    *out = failure(QStringLiteral("this asset type (%1) has no preview").arg(type), true);
    return QImage();
}

// ---- the request --------------------------------------------------------------------------------

bool PreviewService::keyFor(Project *project, const QString &guid, int *typeOut, QString *keyOut,
                            Outcome *refused)
{
    if (!mDb) { *refused = failure(QStringLiteral("there is no library in this session")); return false; }
    const QVector<Database::PreviewRow> rows = mDb->fetchPreviewRows(projectGuidOf(project), false, -1, { guid });
    if (rows.isEmpty()) { *refused = failure(QStringLiteral("no asset with guid '%1'").arg(guid)); return false; }
    const Database::PreviewRow &row = rows.first();
    if (!previews::presetFor(row.type)) {
        *refused = failure(QStringLiteral("this asset type (%1) has no preview").arg(row.type), true);
        return false;
    }
    if (previews::nothingToDraw(row)) {
        *refused = failure(row.platform
                               ? QStringLiteral("'%1' is platform furniture (the app's own content, shown in "
                                                "no tile grid): nothing to draw").arg(guid)
                               : QStringLiteral("'%1' stores no model definition (a builtin primitive's row "
                                                "has nothing to draw)").arg(guid), true);
        return false;
    }
    *typeOut = row.type;
    *keyOut = previews::key(previews::sourceOf(row), previewenv::kVersion);
    return true;
}

void PreviewService::submit(const QString &guid, const QImage &picture, const QString &key, Done done)
{
    if (!mWriter) {
        if (done) done(failure(QStringLiteral("this session has no preview writer")));
        return;
    }
    QString refused;
    const std::shared_ptr<LandingFn> landing = mLanding;
    const bool queued = mWriter->submit(guid, picture, key, [done, landing](const previews::Writer::Landed &landed) {
        Outcome out;
        out.ok = landed.ok;
        out.reason = landed.reason;
        out.superseded = landed.superseded;
        if (done) done(out);
        if (*landing) (*landing)();
    }, &refused);
    if (!queued && done) done(failure(refused));
}

void PreviewService::request(Project *project, const QString &guid, Done done, QSize size)
{
    const auto finish = [&done](const Outcome &outcome) { if (done) done(outcome); };
    int type = 0;
    QString key;
    Outcome refused;
    if (!keyFor(project, guid, &type, &key, &refused)) return finish(refused);
    // THE KEY AT THE REQUEST, written with the bytes: a definition that moves in flight reads stale.
    Outcome outcome;
    const QImage image = renderPicture(project, guid, type, size, &outcome);
    if (image.isNull()) {
        if (outcome.reason.isEmpty()) outcome.reason = QStringLiteral("the render produced no image");
        return finish(outcome);   // A FAILED DELIVERY CHANGES NOTHING
    }
    submit(guid, image, key, std::move(done));
}

void PreviewService::store(Project *project, const QString &guid, const QImage &picture, Done done)
{
    int type = 0;
    QString key;
    Outcome refused;
    if (!keyFor(project, guid, &type, &key, &refused)) {
        if (done) done(refused);
        return;
    }
    submit(guid, picture, key, std::move(done));
}

int PreviewService::pendingWrites() const
{
    return mWriter ? mWriter->pending() : 0;
}

PreviewService::LandingFn PreviewService::setLandingListener(LandingFn listener)
{
    LandingFn previous = std::move(*mLanding);
    *mLanding = std::move(listener);
    return previous;
}

void PreviewService::mint(Project *project, const QString &guid, const char *who, Done then)
{
    if (guid.isEmpty()) return;
    const QString door = QString::fromUtf8(who ? who : "a material");
    const auto engine = mEngine ? mEngine() : std::shared_ptr<Engine>();
    // Headless (or with no engine) "no picture" is the expected answer and the fallback tile stands:
    // saying so once per minted material would be noise. What is said is a render that COULD have
    // happened and did not - a refused borrow above all (the renderer busy with another render).
    const bool couldRender = engine && !engine->isHeadless();
    request(project, guid, [door, guid, couldRender, then](const Outcome &outcome) {
        if (!outcome.ok && couldRender)
            JahLog::write(JahLog::legacy, JahLog::Level::Warning, QStringLiteral("%1: no preview stored for %2 - %3").arg(door, guid, outcome.reason));
        if (then) then(outcome);
    });
}

// ---- the state ----------------------------------------------------------------------------------

QVector<PreviewService::Row> PreviewService::state(Project *project, bool projectOnly, int type,
                                                   const QStringList &guids)
{
    QVector<Row> out;
    if (!mDb) return out;
    const QVector<Database::PreviewRow> rows = mDb->fetchPreviewRows(projectGuidOf(project), projectOnly, type, guids);
    out.reserve(rows.size());
    for (const Database::PreviewRow &row : rows) {
        Row r;
        r.guid = row.guid;
        r.type = row.type;
        r.storedKey = row.storedKey;
        if (previews::presetFor(row.type) && !previews::nothingToDraw(row)) {
            r.key = previews::key(previews::sourceOf(row), previewenv::kVersion);
            r.state = !row.hasBytes ? State::Missing : (row.storedKey == r.key ? State::Ok : State::Stale);
        }
        out.append(r);
    }
    return out;
}

PreviewService::Counts PreviewService::counts(Project *project, bool projectOnly)
{
    Counts out;
    for (const Row &row : state(project, projectOnly)) {
        switch (row.state) {
        case State::Ok: ++out.ok; break;
        case State::Missing: ++out.missing; break;
        case State::Stale: ++out.stale; break;
        case State::None: ++out.none; break;
        }
    }
    return out;
}

QImage PreviewService::emptyStudio(int type, QSize size, bool fresh, QString *reason)
{
    const auto say = [reason](const QString &why) { if (reason) *reason = why; return QImage(); };
    const previews::Preset *preset = previews::presetFor(type);
    if (!preset || !preset->studioLit)
        return say(QStringLiteral("this asset type (%1) is not drawn in the studio: its preview has no empty-studio "
                                  "control").arg(type));
    using View = EngineThumbnailRenderer::StudioView;
    const View view = static_cast<ModelTypes>(type) == ModelTypes::Material ? View::Material : View::Asset;
    const QString cacheKey = QStringLiteral("%1|%2|%3|%4").arg(view == View::Material ? 0 : 1)
                                 .arg(size.width()).arg(size.height()).arg(previewenv::kVersion);
    if (!fresh) {
        const auto kept = mEmptyStudio.constFind(cacheKey);
        if (kept != mEmptyStudio.constEnd()) return kept.value();
    }
    const auto engine = mEngine ? mEngine() : std::shared_ptr<Engine>();
    if (!engine) return say(QStringLiteral("the engine is not running (the empty studio is a render)"));
    auto loan = EngineThumbnailRenderer::borrow(engine, "the empty studio");
    if (!loan) return say(loan.reason());
    const QImage image = loan->renderStudio(view, size);
    if (image.isNull()) return say(loan->lastFailure());
    if (!fresh) mEmptyStudio.insert(cacheKey, image);
    return image;
}

// ---- the sweep ----------------------------------------------------------------------------------

PreviewService::SweepResult PreviewService::sweep(Project *project, const SweepOptions &options,
                                                  const std::function<void(const QString &, const Outcome &)> &each,
                                                  const std::function<void()> &yield)
{
    SweepResult result;
    if (!mDb) return result;
    // A SWEEP IN FLIGHT is what app.resetLibrary refuses on: it deletes exactly the rows this writes.
    ++mSweeps;
    const QVector<Row> rows = state(project, options.projectOnly);
    // The yield is not free (it pumps the event loop), so a healthy library does not pay one a row;
    // but it pays them REGULARLY, or a sweep that finds nothing to do is still one long stall.
    const int kYieldEvery = 16;
    int since = 0;
    for (const Row &row : rows) {
        if (mStop || (options.cancelled && options.cancelled())) { result.cancelled = true; break; }
        if (options.limit > 0 && result.requested >= options.limit) break;
        if (!previews::presetFor(row.type)) continue;
        ++result.considered;
        bool rendered = false;
        if (row.state == State::None) {
            ++result.skipped;
        } else if (!options.staleOnly || row.state != State::Ok) {
            // AT THE WRITER'S CAP the sweep waits for a landing (the door's bounded wait) - never a
            // queue that holds every row's picture until the sweep ends.
            bool full = mWriter && mWriter->pending() >= previews::Writer::kQueueCap;
            while (full && options.waitForLanding && options.waitForLanding())
                full = mWriter->pending() >= previews::Writer::kQueueCap;
            if (full) { result.stalled = true; break; }
            ++result.requested;
            const QString guid = row.guid;
            request(project, guid, [each, guid](const Outcome &outcome) { if (each) each(guid, outcome); });
            if (mWriter) result.peakPending = std::max(result.peakPending, mWriter->pending());
            rendered = true;
        }
        // BETWEEN ROWS, NOT INSIDE ONE: each request is a synchronous render. A YIELD CAN DELIVER THE
        // WINDOW'S CLOSE: the stop check at the top of the loop stops us before the next render.
        if (yield && (rendered || ++since >= kYieldEvery)) { since = 0; yield(); }
    }
    --mSweeps;
    return result;
}
