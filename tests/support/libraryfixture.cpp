/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "libraryfixture.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageWriter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QUuid>
#include <QVector>
#include <QtConcurrent/QtConcurrentMap>

#include "data/assethomekind.h"
#include "data/constants.h"
#include "data/database/database.h"
#include "data/project.h"
#include "io/scenewriter.h"
#include "irisgl/document/scenegraph/scene.h"
#include "services/assetcas.h"
#include "services/assetstorepaths.h"
#include "services/meshbakestore.h"
#include "services/memberstamp.h"

namespace libraryfixture {
namespace {

QString newGuid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

/// The library's next row guid: a name-based uuid of the running count, so two libraries grown by the same
/// calls hold the same guids in the same order — they differ only where `Options` makes them differ.
QString nextGuid(Library &lib)
{
    static const QUuid space = QUuid::fromString(QStringLiteral("{6b1f3a52-8c1e-4f0e-9a51-1a2b3c4d5e6f}"));
    return QUuid::createUuidV5(space, QString::number(lib.guids++)).toString(QUuid::WithoutBraces);
}

/// A flat colour with a gentle ramp and 1 bit of noise in its red channel: a PNG of a real picture's order of
/// bytes (deterministic by `seed`; ~42 KB at 326 x 256 on average, the reference import's three are 27-39 KB).
QByteArray noisyPng(int width, int height, int seed)
{
    QImage img(width, height, QImage::Format_RGB888);
    quint32 s = quint32(seed) * 2654435761u + 1u;
    const int r0 = (seed * 37) % 256, g0 = (seed * 91) % 256, b0 = (seed * 13) % 256;
    for (int y = 0; y < height; ++y) {
        uchar *line = img.scanLine(y);
        for (int x = 0; x < width; ++x) {
            s = s * 1664525u + 1013904223u;
            line[3 * x + 0] = uchar((r0 + x / 4 + (s >> 31)) & 255);
            line[3 * x + 1] = uchar((g0 + y / 4) & 255);
            line[3 * x + 2] = uchar((b0 + (x + y) / 8) & 255);
        }
    }
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    QImageWriter writer(&buf, "png");
    // The pool's encode is the generator's one fixed cost: at quality 60 its 72 encodes took ~0.8 s here, at the
    // default ~4.3 s (2026-11-22), for bytes of the same order.
    writer.setQuality(60);
    writer.write(img);
    return bytes;
}

/// 64 asset pictures 256 px high, widths 256..397 (the reference torus thumbnail is 397 x 256).
const QVector<QByteArray> &assetPool()
{
    static const QVector<QByteArray> pool = [] {
        QVector<QByteArray> out;
        for (int i = 0; i < 64; ++i) out.append(noisyPng(256 + (i * 37) % 142, 256, i));
        return out;
    }();
    return pool;
}

/// 8 project tiles at the size a save writes (ProjectService::tileFromFrame: TILE_SIZE x 2).
const QVector<QByteArray> &tilePool()
{
    static const QVector<QByteArray> pool = [] {
        QVector<QByteArray> out;
        const QSize tile = Constants::TILE_SIZE * 2;
        for (int i = 0; i < 8; ++i) out.append(noisyPng(tile.width(), tile.height(), 1000 + i));
        return out;
    }();
    return pool;
}

QByteArray json(const QJsonObject &o) { return QJsonDocument(o).toJson(); }

/// The store's id of `bytes` (AssetCas hashes a file's bytes with SHA-256): known before the row is written, so
/// the row carries its import record at its one INSERT, as an import's does.
QString oidOf(const QByteArray &bytes) { return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()); }

QJsonObject imageImport(const QString &oid)
{
    return { { "assimp", "libraryfixture" }, { "importer", "image" }, { "importerVersion", 1 },
             { "settings", QJsonObject() }, { "sourceOid", oid } };
}

QJsonObject imageMetadata(qint64 fileSize)
{
    return { { "fileSize", fileSize }, { "format", "png" }, { "height", 490 }, { "kind", "image" }, { "width", 760 } };
}

QJsonObject meshImport(const QString &oid)
{
    const QJsonObject settings{ { "axes", QJsonObject{ { "forward", "-Z" }, { "up", "+Y" } } }, { "clips", true },
                                { "faceCulling", "file" }, { "materials", "import" }, { "maxCards", 12 },
                                { "rotate", QJsonArray{ 0, 0, 0 } }, { "scale", 1 }, { "skeleton", true },
                                { "translate", QJsonArray{ 0, 0, 0 } }, { "units", "auto" }, { "version", 1 } };
    return { { "assimp", "libraryfixture" }, { "importer", "mesh" }, { "importerVersion", 1 },
             { "settings", settings }, { "sourceOid", oid } };
}

QJsonObject meshMetadata(qint64 fileSize, const QString &name)
{
    const QJsonObject summary{ { "animatedNodes", 0 }, { "cameras", 0 }, { "contentScale", 1 }, { "cutouts", 0 },
                               { "glass", 0 }, { "lights", 0 }, { "meshes", 1 }, { "missingMaps", QJsonArray() },
                               { "movable", 0 }, { "static", 1 } };
    return { { "animations", QJsonArray() }, { "boneNames", QJsonArray() }, { "bones", 0 },
             { "extent", QJsonObject{ { "x", 2.8 }, { "y", 2.8 }, { "z", 0.8 } } }, { "fileSize", fileSize },
             { "format", "obj" }, { "hasSkeleton", false }, { "importSummary", summary }, { "kind", "model" },
             { "materials", 1 }, { "meshes", 1 }, { "nodeNames", QJsonArray{ name, "Mesh" } }, { "rigId", "" },
             { "textures", 0 }, { "triangles", 1536 }, { "unitScale", 1 }, { "vertices", 1705 } };
}

/// The node JSON an import stores in an Object's `asset` column: the node, its material's values.
QByteArray objectNode(const QString &guid, const QString &meshGuid, const QString &name)
{
    QJsonObject values;
    for (const char *map : { "baseColorMap", "detail0Map", "detail0NormalMap", "detail1Map", "detail1NormalMap",
                             "detailWeightMap", "emissiveMap", "metallicMap", "normalMap", "reflectionMap",
                             "roughnessMap", "occlusionMap", "opacityMap", "clearCoatMap" }) {
        values.insert(QLatin1String(map), QString());
        values.insert(QLatin1String(map) + QStringLiteral("Address"), 0);
    }
    for (const char *scalar : { "alpha", "alphaCutoff", "alphaMode", "anisotropy", "brdf", "clearCoat",
                                "clearCoatRoughness", "detail0Blend", "detail0OffsetU", "detail0OffsetV",
                                "detail0ScaleU", "detail0ScaleV", "detail0Weight", "detail1Blend", "emissiveIntensity",
                                "ior", "metallic", "normalFactor", "refractionStrength", "roughness",
                                "roughnessLowerBound", "roughnessUpperBound", "shadingModel", "textureOffsetU",
                                "textureOffsetV", "textureScaleU", "textureScaleV" })
        values.insert(QLatin1String(scalar), 1);
    values.insert("baseColor", "#cbcbcb");
    values.insert("emissiveColor", "#000000");
    values.insert("specularColor", "#ffffff");
    const QJsonObject vec{ { "x", 0 }, { "y", 0 }, { "z", 0 } };
    const QJsonObject physics{ { "isStatic", true }, { "mass", 1 }, { "restitution", 0.1 }, { "type", "none" } };
    return QJsonDocument(QJsonObject{ { "activeAnimation", -1 }, { "animations", QJsonArray() },
                                      { "attached", false }, { "children", QJsonArray() },
                                      { "faceCullingMode", "material" }, { "guid", guid }, { "name", name },
                                      { "material", QJsonObject{ { "materialType", "pbr" }, { "values", values } } },
                                      { "mesh", meshGuid }, { "meshIndex", 0 }, { "physicsObject", physics },
                                      { "pickable", true }, { "pos", vec }, { "rot", vec },
                                      { "scale", QJsonObject{ { "x", 1 }, { "y", 1 }, { "z", 1 } } },
                                      { "scaleLock", false }, { "type", "mesh" }, { "visible", true } })
        .toJson(QJsonDocument::Compact);
}

/// A DISTINCT store object: its bytes written beside the store (one filesystem: the stage hardlinks, so no
/// flush), staged and committed for each of `guids` (an Object and its Mesh share their files, as an import
/// writes them). The object's id in `oidOut`.
bool storeObject(const QString &root, const QString &sources, const QStringList &guids, const QString &role,
                 const QString &name, const QByteArray &bytes, QString *oidOut, QString *error)
{
    const QString src = sources + QLatin1Char('/') + newGuid() + QLatin1Char('.') + QFileInfo(name).suffix();
    {
        QFile f(src);
        if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size()) {
            *error = QStringLiteral("libraryfixture: cannot write %1").arg(src);
            return false;
        }
    }
    AssetCas::Staged staged;
    staged.srcPath = src;
    staged.role = role;
    staged.name = name;
    if (!AssetCas::stage(root, staged)) { *error = staged.error; return false; }
    if (staged.oid != oidOf(bytes)) { *error = QStringLiteral("libraryfixture: the store's id is not SHA-256"); return false; }
    if (staged.copied) {
        QVector<AssetCas::Staged> batch{ staged };
        AssetCas::flushStaged(batch);
        staged = batch.first();
    }
    for (const QString &guid : guids)
        if (!AssetCas::commitStaged(QSqlDatabase::database(), root, guid, staged, error)) return false;
    QFile::remove(src);
    if (oidOut) *oidOut = staged.oid;
    return true;
}

/// One library row through the one door; false with the refusal.
bool row(Database *db, const QString &guid, const QString &name, ModelTypes type, const QString &parent,
         const assethome::Home &home, assethome::Origin origin, const QByteArray &thumbnail,
         const QByteArray &properties, const QByteArray &asset, QString *error)
{
    QString refused;
    if (db->createAssetEntry(guid, name, static_cast<int>(type), parent, home, origin, QString(), QString(), thumbnail,
                             properties, QByteArray(), asset, &refused)
            .isEmpty()) {
        *error = QStringLiteral("libraryfixture: the library refused '%1': %2").arg(name, refused);
        return false;
    }
    return true;
}

QByteArray sourceBytes(const QString &guid) { return QByteArrayLiteral("libraryfixture source ") + guid.toUtf8(); }

/// A model's source: a real OBJ (a tetrahedron), distinct per model by its comment line, so it parses and bakes
/// as an import's does and every model has a store object of its own.
QByteArray objBytes(const QString &guid)
{
    return QByteArrayLiteral("# libraryfixture model ") + guid.toUtf8()
           + QByteArrayLiteral("\nv 0 0 0\nv 1 0 0\nv 0 1 0\nv 0 0 1\nf 1 2 3\nf 1 2 4\nf 1 3 4\nf 2 3 4\n");
}

}   // namespace

bool grow(Database *db, const QString &storeRoot, int assets, int projects, Library &lib, const Options &options,
          QString *error)
{
    QString local;
    if (!error) error = &local;
    if (!db || assets % 100 != 0) { *error = QStringLiteral("libraryfixture: assets must be a multiple of 100"); return false; }
    const QString sources = storeRoot + QStringLiteral("-sources");
    if (!QDir().mkpath(storeRoot) || !QDir().mkpath(sources)) {
        *error = QStringLiteral("libraryfixture: cannot make %1").arg(storeRoot);
        return false;
    }
    const auto thumb = [&](int i) {
        if (!options.thumbnails) return QByteArray();
        const QByteArray &bytes = assetPool().at(i % assetPool().size());
        lib.thumbnailBytes += bytes.size();
        return bytes;
    };

    DbBatch batch(db);
    QVector<MeshBakeStore::BakeTarget> bakes;   // the models' sources, baked after the rows
    const bool first = lib.projects.isEmpty();
    if (first) lib.drawer = db->createCollection(QStringLiteral("Fixture drawer"));

    for (int block = 0; block < assets / 100; ++block) {
        for (int k = 0; k < 100; ++k) {
            const int i = lib.assets++;
            const QString tag = QStringLiteral("%1").arg(i, 5, 10, QLatin1Char('0'));
            const QString guid = nextGuid(lib);
            QString oid;
            if (k < 46) {   // the user's own picture
                const QString name = QStringLiteral("tex_%1.png").arg(tag);
                const QByteArray bytes = sourceBytes(guid);
                const QByteArray props = json({ { "import", imageImport(oidOf(bytes)) },
                                                { "metadata", imageMetadata(bytes.size()) } });
                if (!row(db, guid, name, ModelTypes::Texture, QString(), assethome::assets(), assethome::Origin::Import,
                         thumb(i), props, QByteArray(), error)
                    || !storeObject(storeRoot, sources, { guid }, QStringLiteral("source"), name, bytes, &oid, error))
                    return false;
                if (lib.textures.size() % 4 == 0) db->switchAssetCollection(lib.drawer, guid);
                lib.textures << guid;
            } else if (k < 60) {   // an Assets-home material and the picture picked into it
                const QString texGuid = nextGuid(lib);
                const QString matName = QStringLiteral("mat_%1").arg(tag);
                const QByteArray definition = QJsonDocument(QJsonObject{
                    { "materialType", "pbr" }, { "name", matName },
                    { "values", QJsonObject{ { "baseColorMap", texGuid } } }, { "version", 3 } })
                                                  .toJson(QJsonDocument::Compact);
                if (!row(db, guid, matName, ModelTypes::Material, QString(), assethome::assets(),
                         assethome::Origin::ExplicitSave, thumb(i), QByteArray(), definition, error)
                    || !storeObject(storeRoot, sources, { guid }, QStringLiteral("source"),
                                    QStringLiteral("material.json"), definition + guid.toUtf8(), nullptr, error))
                    return false;
                lib.assetsMaterials << guid;
                // ...its picture, minted by the picker (the Assets home's own import), stamped by it.
                const QString texName = QStringLiteral("picked_%1.png").arg(tag);
                const QByteArray bytes = sourceBytes(texGuid);
                const QByteArray props = json({ { "import", imageImport(oidOf(bytes)) },
                                                { "metadata", imageMetadata(bytes.size()) } });
                if (!row(db, texGuid, texName, ModelTypes::Texture, QString(), assethome::assets(),
                         assethome::Origin::Import, thumb(i), props, QByteArray(), error)
                    || !storeObject(storeRoot, sources, { texGuid }, QStringLiteral("source"), texName, bytes, &oid, error))
                    return false;
                db->createDependency(static_cast<int>(ModelTypes::Material), static_cast<int>(ModelTypes::Texture),
                                     guid, texGuid);
                memberstamp::stamp(db, texGuid, guid);
                lib.stampedTextures << texGuid;
                ++k;   // the picture is the block's next asset
                ++lib.assets;
            } else if (k < 80) {   // the Materials module's own material and the picture picked into it
                const QString texGuid = nextGuid(lib);
                const QString name = QStringLiteral("home_mat_%1").arg(tag);
                const QByteArray definition = QJsonDocument(QJsonObject{
                    { "materialType", "pbr" }, { "name", name },
                    { "values", QJsonObject{ { "baseColorMap", texGuid } } }, { "version", 3 } })
                                                  .toJson(QJsonDocument::Compact);
                if (!row(db, guid, name, ModelTypes::Material, QString(), assethome::materials(),
                         assethome::Origin::Create, thumb(i), QByteArray(), definition, error)
                    || !storeObject(storeRoot, sources, { guid }, QStringLiteral("source"),
                                    QStringLiteral("material.json"), definition + guid.toUtf8(), nullptr, error))
                    return false;
                lib.homeMaterials << guid;
                // ...minted in the material's home (the picker's rule: the material's home decides the picture's).
                const QString texName = QStringLiteral("home_picked_%1.png").arg(tag);
                const QByteArray bytes = sourceBytes(texGuid);
                const QByteArray props = json({ { "import", imageImport(oidOf(bytes)) },
                                                { "metadata", imageMetadata(bytes.size()) } });
                if (!row(db, texGuid, texName, ModelTypes::Texture, QString(), assethome::materials(),
                         assethome::Origin::Create, thumb(i), props, QByteArray(), error)
                    || !storeObject(storeRoot, sources, { texGuid }, QStringLiteral("source"), texName, bytes, &oid, error))
                    return false;
                db->createDependency(static_cast<int>(ModelTypes::Material), static_cast<int>(ModelTypes::Texture),
                                     guid, texGuid);
                memberstamp::stamp(db, texGuid, guid);
                lib.homePicked << texGuid;
                ++k;
                ++lib.assets;
            } else {   // a model: the Object and its Mesh member, one source and one bake shared by both
                const QString meshGuid = nextGuid(lib);
                const QString name = QStringLiteral("model_%1").arg(tag);
                const QString file = name + QStringLiteral(".obj");
                const QByteArray source = objBytes(guid);
                const QByteArray props = json({ { "import", meshImport(oidOf(source)) },
                                                { "metadata", meshMetadata(source.size(), file) } });
                if (!row(db, guid, name, ModelTypes::Object, QString(), assethome::assets(), assethome::Origin::Import,
                         thumb(i), props, objectNode(guid, meshGuid, name), error)
                    || !row(db, meshGuid, file, ModelTypes::Mesh, guid, assethome::assets(), assethome::Origin::Import,
                            QByteArray(), QByteArray(), QByteArray(), error)
                    || !storeObject(storeRoot, sources, { guid, meshGuid }, QStringLiteral("source"), file, source,
                                    &oid, error))
                    return false;
                bakes.append({ AssetStorePaths::objectPathIn(storeRoot, oid, QStringLiteral("obj")), guid, false });
                db->createDependency(static_cast<int>(ModelTypes::Object), static_cast<int>(ModelTypes::Mesh), guid,
                                     meshGuid);
                lib.objects << guid;
                lib.meshes << meshGuid;
                ++k;   // the mesh is the block's next asset
                ++lib.assets;
            }
        }
    }

    // THE MODELS' BAKES ARE THE REAL ONES — the bake sweep's own three steps (MeshBakeStore::prepareBake on this
    // connection, runBake on the pool's threads, commitBake here), under each model's import settings — so a
    // generated library boots with nothing for the sweep to rebuild. On the pool: a bake is ~10 ms of CPU.
    {
        QVector<MeshBakeStore::BakeJob> jobs;
        for (const MeshBakeStore::BakeTarget &target : std::as_const(bakes))
            jobs.append(MeshBakeStore::prepareBake(QSqlDatabase::database(), storeRoot, target));
        QVector<MeshBakeStore::BakeResult> results = QtConcurrent::blockingMapped(
            jobs, [](const MeshBakeStore::BakeJob &job) { return MeshBakeStore::runBake(job, nullptr); });
        for (MeshBakeStore::BakeResult &result : results)
            if (!MeshBakeStore::commitBake(QSqlDatabase::database(), storeRoot, result, error)) return false;
    }

    if (first) for (int i = 0; i < 5 && i < lib.textures.size(); ++i) db->addFavorite(lib.textures.at(i));

    SceneWriter writer;
    const QByteArray sceneBlob = writer.getSceneObject(QString(), iris::Scene::create(), nullptr);
    for (int p = 0; p < projects; ++p) {
        const QString guid = nextGuid(lib);
        const QByteArray tile = options.thumbnails ? tilePool().at(lib.projects.size() % tilePool().size()) : QByteArray();
        if (!db->createProject(guid, QStringLiteral("Fixture project %1").arg(lib.projects.size(), 4, 10, QLatin1Char('0')),
                               sceneBlob, tile)) {
            *error = QStringLiteral("libraryfixture: the library refused a project");
            return false;
        }
        lib.projects << guid;
    }

    // THE OPEN PROJECT: its own rows and pins, written once, never grown.
    if (first && !lib.projects.isEmpty()) {
        lib.openProject = lib.projects.first();
        for (int i = 0; i < 20; ++i) {
            const QString guid = nextGuid(lib);
            if (!row(db, guid, QStringLiteral("own_%1.png").arg(i), ModelTypes::Texture, QString(),
                     assethome::project(lib.openProject), assethome::Origin::Create, thumb(i), QByteArray(), QByteArray(),
                     error))
                return false;
        }
        QSqlDatabase conn = QSqlDatabase::database();
        for (int i = 0; i < 10 && i < lib.textures.size(); ++i)
            AssetCas::writePin(conn, lib.openProject, lib.textures.at(i), AssetCas::sourceOid(conn, lib.textures.at(i)));
        for (int i = 0; i < 5 && i < lib.homeMaterials.size(); ++i)
            AssetCas::writePin(conn, lib.openProject, lib.homeMaterials.at(i),
                               AssetCas::sourceOid(conn, lib.homeMaterials.at(i)));
    }
    return true;
}

}   // namespace libraryfixture
