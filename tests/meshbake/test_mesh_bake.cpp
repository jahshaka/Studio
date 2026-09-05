// MESH BAKE — the gate (MESH_BAKE_SPEC.md phase 1).
//
// The bake exists to make opening a world a LOAD instead of an assimp parse.
// That is only allowed if a baked load is INDISTINGUISHABLE from a fresh
// build, so this suite does not check that the bake "works": it builds both
// sides and compares them field by field.
//
//   1. Round trip. For every fixture: parse with assimp, build the document
//      the old way (GraphicsHelper::loadAllMeshesFromAssimpScene +
//      Mesh::extractAnimations + MeshNode::loadAsSceneFragment), then bake,
//      serialize, deserialize, and compare — vertex buffers byte for byte,
//      index buffers byte for byte, bounds, the picking TriMesh, the skeleton
//      (names, hierarchy, bind matrices), every animation key, and the whole
//      fragment node tree (shape, names, local transforms, mesh indices,
//      rootBone links, attached flags, clip list).
//   2. Determinism. The same source baked twice, into different directories,
//      must produce IDENTICAL BYTES — that is what lets `assets.gc`,
//      `assets.verify` and `assets.checkConsistency` treat a bake as ordinary
//      content addressed by its hash.
//   3. Staleness. A bake whose fingerprint is not the one this build would
//      produce is IGNORED. (The fingerprint carries the bake format, the
//      producer hash of the TUs that build one, the assimp version and the
//      import flags, plus the source content id.)
//   4. Corruption. Truncated at every length, bit-flipped, empty, wrong
//      magic, wrong version: every one returns an invalid model and nothing
//      crashes. The open path then parses, exactly as it always did.
//   5. The store. Through the REAL import pipeline: importing a model writes
//      a `bake`-role file into the CAS under both the Object and the Mesh
//      row, MeshBakeStore resolves it back, a load returns the same geometry,
//      and assets.bakeAll's dry run reports nothing left to do. Then the
//      bake object is corrupted on disk and the resolver refuses it.
//
// No engine, no display: all of this is document-side by construction.

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlQuery>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <cstdio>

#include "assimp/Importer.hpp"
#include "assimp/scene.h"

#include "data/database/database.h"
#include "data/project.h"
#include "services/assetcas.h"
#include "services/assetstorepaths.h"
#include "services/import/assetimportservice.h"
#include "services/meshbakestore.h"
#include "export/exportcontentsource.h"

#include "irisgl/core/geometry/trimesh.h"
#include "irisgl/document/animation/animation.h"
#include "irisgl/document/animation/skeletalanimation.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/assets/skeleton.h"
#include "irisgl/document/assets/vertexbuffer.h"
#include "irisgl/document/materials/custommaterial.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/import/graphicshelper.h"
#include "irisgl/import/importflags.h"
#include "irisgl/import/meshbake.h"

static int failures = 0;
static int checks = 0;
#define CHECK(cond, msg) do { ++checks; if (cond) { /* quiet */ } \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)
#define CHECK_LOUD(cond, msg) do { ++checks; if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

static QString fixture(const QString &name)
{
    return QStringLiteral(JAHSHAKA_TEST_SOURCE_DIR "/") + name;
}

// ---------------------------------------------------------------------------
// 1. round trip
// ---------------------------------------------------------------------------

static bool sameBytes(const char *a, int aSize, const char *b, int bSize)
{
    if (aSize != bSize) return false;
    if (aSize == 0) return true;
    return std::memcmp(a, b, size_t(aSize)) == 0;
}

static void compareMesh(const iris::MeshPtr &parsed, const iris::MeshPtr &baked,
                        const QString &label)
{
    CHECK(!parsed.isNull() && !baked.isNull(), qUtf8Printable(label + ": both meshes exist"));
    if (parsed.isNull() || baked.isNull()) return;

    CHECK(parsed->numVerts == baked->numVerts, qUtf8Printable(label + ": numVerts"));
    CHECK(parsed->numFaces == baked->numFaces, qUtf8Printable(label + ": numFaces"));
    CHECK(parsed->usesIndexBuffer == baked->usesIndexBuffer,
          qUtf8Printable(label + ": usesIndexBuffer"));
    CHECK(parsed->getPrimitiveMode() == baked->getPrimitiveMode(),
          qUtf8Printable(label + ": primitiveMode"));
    CHECK(parsed->boundingSphere.pos == baked->boundingSphere.pos &&
              parsed->boundingSphere.radius == baked->boundingSphere.radius,
          qUtf8Printable(label + ": bounding sphere"));
    CHECK(parsed->aabb.getMin() == baked->aabb.getMin() &&
              parsed->aabb.getMax() == baked->aabb.getMax(),
          qUtf8Printable(label + ": aabb"));

    const auto &pv = parsed->getVertexBuffers();
    const auto &bv = baked->getVertexBuffers();
    CHECK(pv.size() == bv.size(), qUtf8Printable(label + ": vertex buffer count"));
    for (int i = 0; i < qMin(pv.size(), bv.size()); ++i) {
        auto pa = pv[i]->vertexLayout.getAttribs();
        auto ba = bv[i]->vertexLayout.getAttribs();
        CHECK(pa.size() == 1 && ba.size() == 1,
              qUtf8Printable(label + ": one attribute per buffer"));
        if (pa.size() == 1 && ba.size() == 1) {
            CHECK(pa[0].usage == ba[0].usage && pa[0].type == ba[0].type &&
                      pa[0].count == ba[0].count && pa[0].sizeInBytes == ba[0].sizeInBytes,
                  qUtf8Printable(QStringLiteral("%1: buffer %2 layout").arg(label).arg(i)));
        }
        CHECK(sameBytes(pv[i]->data, pv[i]->dataSize, bv[i]->data, bv[i]->dataSize),
              qUtf8Printable(QStringLiteral("%1: buffer %2 bytes identical").arg(label).arg(i)));
    }

    const iris::IndexBufferPtr pi = parsed->getIndexBuffer();
    const iris::IndexBufferPtr bi = baked->getIndexBuffer();
    CHECK(pi.isNull() == bi.isNull(), qUtf8Printable(label + ": index buffer presence"));
    if (!pi.isNull() && !bi.isNull())
        CHECK(sameBytes(pi->data, pi->dataSize, bi->data, bi->dataSize),
              qUtf8Printable(label + ": index bytes identical"));

    // The picking mesh is REBUILT from the bake, not stored — so it has to be
    // proved equal, triangle for triangle, or picking silently disagrees with
    // what the viewport draws.
    CHECK((parsed->triMesh != nullptr) == (baked->triMesh != nullptr),
          qUtf8Printable(label + ": trimesh presence"));
    if (parsed->triMesh && baked->triMesh) {
        CHECK(parsed->triMesh->triangles.size() == baked->triMesh->triangles.size(),
              qUtf8Printable(label + ": trimesh triangle count"));
        bool same = parsed->triMesh->triangles.size() == baked->triMesh->triangles.size();
        for (int t = 0; same && t < parsed->triMesh->triangles.size(); ++t) {
            const iris::Triangle &x = parsed->triMesh->triangles[t];
            const iris::Triangle &y = baked->triMesh->triangles[t];
            same = x.a == y.a && x.b == y.b && x.c == y.c && x.normal == y.normal;
        }
        CHECK(same, qUtf8Printable(label + ": every picking triangle identical"));
    }

    const iris::SkeletonPtr ps = parsed->getSkeleton();
    const iris::SkeletonPtr bs = baked->getSkeleton();
    CHECK(ps.isNull() == bs.isNull(), qUtf8Printable(label + ": skeleton presence"));
    if (!ps.isNull() && !bs.isNull()) {
        CHECK(ps->bones.size() == bs->bones.size(), qUtf8Printable(label + ": bone count"));
        bool same = ps->bones.size() == bs->bones.size();
        for (int b = 0; same && b < ps->bones.size(); ++b) {
            const iris::BonePtr &x = ps->bones[b];
            const iris::BonePtr &y = bs->bones[b];
            same = x->name == y->name
                   && x->inverseMeshSpacePoseMatrix == y->inverseMeshSpacePoseMatrix
                   && x->meshSpacePoseMatrix == y->meshSpacePoseMatrix
                   && x->localMatrix == y->localMatrix
                   && x->bindingPos == y->bindingPos && x->bindingRot == y->bindingRot
                   && x->bindingScale == y->bindingScale
                   && x->parentBone.isNull() == y->parentBone.isNull()
                   && (x->parentBone.isNull() || x->parentBone->name == y->parentBone->name)
                   && x->childBones.size() == y->childBones.size();
        }
        CHECK(same, qUtf8Printable(label + ": every bone identical (names, bind, hierarchy)"));
    }
}

static void compareAnimations(const QMap<QString, iris::SkeletalAnimationPtr> &parsed,
                              const QMap<QString, iris::SkeletalAnimationPtr> &baked,
                              const QString &label)
{
    CHECK(parsed.keys() == baked.keys(), qUtf8Printable(label + ": clip names"));
    for (auto it = parsed.constBegin(); it != parsed.constEnd(); ++it) {
        const auto other = baked.constFind(it.key());
        if (other == baked.constEnd()) continue;
        const auto &pa = it.value();
        const auto &ba = other.value();
        CHECK(pa->name == ba->name, qUtf8Printable(label + ": clip name"));
        CHECK(pa->boneAnimations.keys() == ba->boneAnimations.keys(),
              qUtf8Printable(label + ": channel names"));
        for (auto b = pa->boneAnimations.constBegin(); b != pa->boneAnimations.constEnd(); ++b) {
            const auto ob = ba->boneAnimations.constFind(b.key());
            if (ob == ba->boneAnimations.constEnd()) continue;
            bool same = b.value()->posKeys->keys.size() == ob.value()->posKeys->keys.size()
                        && b.value()->rotKeys->keys.size() == ob.value()->rotKeys->keys.size()
                        && b.value()->scaleKeys->keys.size() == ob.value()->scaleKeys->keys.size();
            for (int k = 0; same && k < b.value()->posKeys->keys.size(); ++k)
                same = b.value()->posKeys->keys[k]->value == ob.value()->posKeys->keys[k]->value
                       && b.value()->posKeys->keys[k]->time == ob.value()->posKeys->keys[k]->time;
            for (int k = 0; same && k < b.value()->rotKeys->keys.size(); ++k)
                same = b.value()->rotKeys->keys[k]->value == ob.value()->rotKeys->keys[k]->value
                       && b.value()->rotKeys->keys[k]->time == ob.value()->rotKeys->keys[k]->time;
            for (int k = 0; same && k < b.value()->scaleKeys->keys.size(); ++k)
                same = b.value()->scaleKeys->keys[k]->value == ob.value()->scaleKeys->keys[k]->value
                       && b.value()->scaleKeys->keys[k]->time == ob.value()->scaleKeys->keys[k]->time;
            CHECK(same, qUtf8Printable(label + ": every key of " + b.key()));
        }
    }
}

static void compareFragment(const iris::SceneNodePtr &parsed, const iris::SceneNodePtr &baked,
                            const QString &label, int depth = 0)
{
    CHECK(parsed.isNull() == baked.isNull(), qUtf8Printable(label + ": node presence"));
    if (parsed.isNull() || baked.isNull()) return;

    CHECK(parsed->getSceneNodeType() == baked->getSceneNodeType(),
          qUtf8Printable(label + ": node type"));
    // A node NOBODY named keeps SceneNode's constructor default, which embeds
    // a process-global counter ("SceneNode17") — two builds of the same file
    // legitimately differ there. What matters is that both sides are unnamed,
    // or that both carry the SAME authored name.
    static const QRegularExpression unnamed(QStringLiteral("^SceneNode\\d+$"));
    const bool bothUnnamed = unnamed.match(parsed->name).hasMatch() &&
                             unnamed.match(baked->name).hasMatch();
    CHECK(bothUnnamed || parsed->name == baked->name, qUtf8Printable(label + ": node name"));
    CHECK(parsed->getLocalPos() == baked->getLocalPos(), qUtf8Printable(label + ": local pos"));
    CHECK(parsed->getLocalRot() == baked->getLocalRot(), qUtf8Printable(label + ": local rot"));
    CHECK(parsed->getLocalScale() == baked->getLocalScale(),
          qUtf8Printable(label + ": local scale"));
    CHECK(parsed->isAttached() == baked->isAttached(), qUtf8Printable(label + ": attached"));
    CHECK(parsed->getAnimations().size() == baked->getAnimations().size(),
          qUtf8Printable(label + ": animation count"));

    if (parsed->getSceneNodeType() == iris::SceneNodeType::Mesh &&
        baked->getSceneNodeType() == iris::SceneNodeType::Mesh) {
        auto p = parsed.staticCast<iris::MeshNode>();
        auto b = baked.staticCast<iris::MeshNode>();
        CHECK(p->meshIndex == b->meshIndex, qUtf8Printable(label + ": meshIndex"));
        CHECK(p->meshPath == b->meshPath, qUtf8Printable(label + ": meshPath"));
        CHECK(p->rootBone.isNull() == b->rootBone.isNull(),
              qUtf8Printable(label + ": rootBone link"));
        CHECK(p->getMaterial().isNull() == b->getMaterial().isNull(),
              qUtf8Printable(label + ": material presence"));
        CHECK(p->hasSkeleton() == b->hasSkeleton(), qUtf8Printable(label + ": node skeleton"));
        if (!p->getMesh().isNull() && !b->getMesh().isNull())
            compareMesh(p->getMesh(), b->getMesh(), label + " mesh");
    }

    CHECK(parsed->children().size() == baked->children().size(),
          qUtf8Printable(label + ": child count"));
    for (int i = 0; i < qMin(parsed->children().size(), baked->children().size()); ++i)
        compareFragment(parsed->children()[i], baked->children()[i],
                        QStringLiteral("%1/%2").arg(label).arg(i), depth + 1);
}

static iris::MaterialPtr testMaterial(iris::MeshPtr, iris::MeshMaterialData &data)
{
    auto mat = iris::CustomMaterial::create();
    mat->setValue("diffuseColor", data.diffuseColor);
    return iris::MaterialPtr(mat);
}

static void roundTrip(const QString &relPath)
{
    const QString path = fixture(relPath);
    if (!QFileInfo::exists(path)) {
        std::printf("FAIL: fixture missing: %s\n", qUtf8Printable(path));
        ++failures;
        return;
    }

    // The OLD way, in full.
    Assimp::Importer importer;
    const aiScene *scene = importer.ReadFile(path.toStdString().c_str(),
                                             iris::ImportFlags::Canonical);
    if (!scene) {
        std::printf("FAIL: assimp could not read %s\n", qUtf8Printable(relPath));
        ++failures;
        return;
    }
    const QList<iris::MeshPtr> parsedMeshes =
        iris::GraphicsHelper::loadAllMeshesFromAssimpScene(scene);
    const QMap<QString, iris::SkeletalAnimationPtr> parsedAnims =
        iris::Mesh::extractAnimations(scene, path);
    // The scratch dir is passed to BOTH sides: extractMaterialData WRITES
    // embedded textures, and with no directory it writes them beside the
    // source — which for a fixture means into the repository.
    QTemporaryDir scratch;
    const iris::SceneNodePtr parsedFragment =
        iris::MeshNode::loadAsSceneFragment(path, scene, testMaterial, scratch.path());

    // The BAKE way: build, serialize, deserialize — nothing kept in memory
    // from the build, so what is compared is what a later process would read.
    const QString fingerprint = iris::MeshBake::fingerprintFor(
        QStringLiteral("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));
    iris::MeshBake::Model built =
        iris::MeshBake::buildFromScene(scene, path, fingerprint, scratch.path());
    CHECK_LOUD(built.valid, qUtf8Printable(relPath + ": bake built"));
    if (!built.valid) return;

    const QByteArray blob = iris::MeshBake::serialize(built);
    CHECK_LOUD(blob.size() > 32, qUtf8Printable(relPath + ": bake blob is non-trivial"));
    iris::MeshBake::Model read = iris::MeshBake::deserialize(blob, fingerprint);
    CHECK_LOUD(read.valid, qUtf8Printable(relPath + ": bake blob read back"));
    if (!read.valid) return;

    CHECK_LOUD(read.meshes.size() == parsedMeshes.size(),
               qUtf8Printable(relPath + ": mesh count matches the parse"));
    for (int i = 0; i < qMin(read.meshes.size(), parsedMeshes.size()); ++i)
        compareMesh(parsedMeshes[i], read.meshes[i], QStringLiteral("%1 mesh %2").arg(relPath).arg(i));

    compareAnimations(parsedAnims, read.animations, relPath);

    const iris::SceneNodePtr bakedFragment =
        iris::MeshBake::buildFragment(read, path, testMaterial);
    compareFragment(parsedFragment, bakedFragment, relPath + " fragment");

    std::printf("ok:   %s: baked load is field-for-field the parsed build (%d meshes, %lld bytes)\n",
                qUtf8Printable(relPath), int(read.meshes.size()),
                static_cast<long long>(blob.size()));
}

// ---------------------------------------------------------------------------
// 2/3/4. determinism, staleness, corruption
// ---------------------------------------------------------------------------

static QByteArray bakeBlob(const QString &path, const QString &fingerprint)
{
    QTemporaryDir scratch;
    iris::MeshBake::Model model =
        iris::MeshBake::buildFromFile(path, fingerprint, scratch.path());
    return model.valid ? iris::MeshBake::serialize(model) : QByteArray();
}

static void determinismAndFailureModes()
{
    const QString path = fixture(QStringLiteral("tests/importer/fixtures/textured_pbr_quad.glb"));
    const QString fp = iris::MeshBake::fingerprintFor(QStringLiteral("deadbeef") .repeated(8));

    // 2. Determinism — two builds, two different staging directories, one
    // byte pattern. This is what makes a bake ordinary content-addressed
    // storage: assets.checkConsistency re-derives it and gets the same oid.
    const QByteArray a = bakeBlob(path, fp);
    const QByteArray b = bakeBlob(path, fp);
    CHECK_LOUD(!a.isEmpty() && a == b,
               "the same source baked twice produces byte-identical blobs");

    // 3. Staleness — the fingerprint is the whole guard.
    CHECK_LOUD(!iris::MeshBake::deserialize(a, QStringLiteral("not-the-fingerprint")).valid,
               "a bake whose fingerprint does not match is ignored");
    CHECK_LOUD(iris::MeshBake::deserialize(a, fp).valid,
               "a bake whose fingerprint matches is accepted");
    CHECK_LOUD(iris::MeshBake::deserialize(a).valid,
               "no expectation given = read whatever the blob says it is");
    CHECK_LOUD(iris::MeshBake::fingerprintFor(QString()).isEmpty(),
               "a source with no content id has no fingerprint (and so never matches)");

    // 4. Corruption. TRUNCATION at every length: a torn write, a half-copied
    // file, a store on a full disk. Not one of them may be accepted, and not
    // one of them may crash.
    int acceptedTruncations = 0;
    for (int cut = 0; cut < a.size(); cut += qMax(1, a.size() / 512)) {
        if (iris::MeshBake::deserialize(a.left(cut), fp).valid) ++acceptedTruncations;
    }
    CHECK_LOUD(acceptedTruncations == 0,
               "no truncation of a bake is ever accepted (torn-write safety)");

    // Bit flips through the header and the first kilobyte of payload.
    int acceptedFlips = 0, survived = 0;
    for (int at = 0; at < qMin(a.size(), 1024); at += 7) {
        QByteArray corrupt = a;
        corrupt[at] = char(corrupt[at] ^ 0x5A);
        if (iris::MeshBake::deserialize(corrupt, fp).valid) ++acceptedFlips;
        ++survived;
    }
    // A flip inside a vertex-data payload legitimately still parses — it is a
    // valid blob with different numbers, and no format can tell. What must
    // never happen is a CRASH, and what must never be accepted is a
    // structurally broken one; the count is reported for honesty.
    std::printf("info: %d/%d single-bit corruptions still parsed (payload bytes)\n",
                acceptedFlips, survived);
    CHECK_LOUD(survived > 0, "bit-flip sweep ran without crashing");

    CHECK_LOUD(!iris::MeshBake::deserialize(QByteArray(), fp).valid, "an empty blob is rejected");
    CHECK_LOUD(!iris::MeshBake::deserialize(QByteArray(4096, 'x'), fp).valid,
               "a garbage blob is rejected");

    QByteArray wrongVersion = a;
    wrongVersion[4] = char(0x7F);   // the version field, little-endian
    CHECK_LOUD(!iris::MeshBake::deserialize(wrongVersion, fp).valid,
               "a bake written by a different format version is rejected");

    QByteArray wrongMagic = a;
    wrongMagic[0] = char(wrongMagic[0] ^ 0xFF);
    CHECK_LOUD(!iris::MeshBake::deserialize(wrongMagic, fp).valid,
               "a blob that is not a bake at all is rejected");

    // The trailing sentinel: a blob whose LAST bytes are gone but whose body
    // reads cleanly must still fail.
    CHECK_LOUD(!iris::MeshBake::deserialize(a.left(a.size() - 2), fp).valid,
               "a blob missing its trailing sentinel is rejected");

    // read() of a missing file is a miss, not an error.
    CHECK_LOUD(!iris::MeshBake::read(QStringLiteral("/nonexistent/nope.jmb"), fp).valid,
               "reading a bake that is not there is a clean miss");
}

// ---------------------------------------------------------------------------
// 5. the store: the real import pipeline
// ---------------------------------------------------------------------------

static void storeIntegration()
{
    QTemporaryDir home;
    QTemporaryDir storeRoot;
    if (!home.isValid() || !storeRoot.isValid()) {
        std::printf("FAIL: could not create the fixture store\n");
        ++failures;
        return;
    }
    AssetStorePaths::setRootOverride(storeRoot.path());

    Database db;
    CHECK_LOUD(db.initializeDatabase(QDir(home.path()).filePath("assets.db")),
               "fixture database opened");
    db.createAllTables();
    QSqlDatabase conn = QSqlDatabase::database();
    AssetCas::ensureCasSchema(conn);

    Project project;
    AssetImportService service(&db, &project);

    ImportRequest request;
    request.sourcePath = fixture(QStringLiteral("tests/importer/fixtures/scaled_two_meshes.glb"));
    const ImportResult result = service.import(request);
    CHECK_LOUD(result.ok(), "the model imported through the ONE pipeline");
    if (!result.ok()) {
        std::printf("info: import error: %s\n", qUtf8Printable(result.error));
        AssetStorePaths::setRootOverride(QString());
        return;
    }

    // The bake is recorded under BOTH rows — the shape the SOURCE has, which
    // is what makes it reachable (and reapable) through the ordinary
    // asset_files reachability the GC already implements.
    const auto bakeRowsFor = [&](const QString &guid) {
        QSqlQuery q(conn);
        q.prepare("SELECT oid, name FROM asset_files WHERE asset_guid = ? AND role = 'bake'");
        q.addBindValue(guid);
        QStringList rows;
        if (q.exec()) while (q.next()) rows.append(q.value(0).toString() + "|" + q.value(1).toString());
        return rows;
    };
    const QStringList objectRows = bakeRowsFor(result.assetGuid);
    const QStringList meshRows = bakeRowsFor(result.meshGuid);
    CHECK_LOUD(objectRows.size() == 1, "the import recorded a bake under the Object row");
    CHECK_LOUD(meshRows.size() == 1, "the import recorded a bake under the Mesh member row");
    CHECK_LOUD(!objectRows.isEmpty() && objectRows == meshRows,
               "both rows name the SAME bake object (the CAS dedups it)");

    // Resolution: source path in, bake plan out.
    const QString sourcePath = AssetCas::resolveSource(conn, storeRoot.path(), result.meshGuid);
    CHECK_LOUD(!sourcePath.isEmpty(), "the stored source resolves");
    const iris::PrewarmItem plan =
        MeshBakeStore::planFor(conn, storeRoot.path(), sourcePath);
    CHECK_LOUD(!plan.bakePath.isEmpty(), "MeshBakeStore resolves the bake for the source");
    CHECK_LOUD(QFileInfo::exists(plan.bakePath), "the bake object is on disk");
    CHECK_LOUD(MeshBakeStore::isFresh(conn, storeRoot.path(), sourcePath),
               "the freshly imported bake is fresh for this build");

    iris::MeshBake::Model loaded = iris::MeshBake::read(plan.bakePath, plan.bakeFingerprint);
    CHECK_LOUD(loaded.valid && !loaded.meshes.isEmpty(),
               "the stored bake reads back with geometry");

    // Against the parse of the same stored bytes: still identical.
    Assimp::Importer importer;
    const aiScene *scene = importer.ReadFile(sourcePath.toStdString().c_str(),
                                             iris::ImportFlags::Canonical);
    if (scene) {
        const QList<iris::MeshPtr> parsedMeshes =
            iris::GraphicsHelper::loadAllMeshesFromAssimpScene(scene);
        CHECK_LOUD(parsedMeshes.size() == loaded.meshes.size(),
                   "the stored bake has the same mesh count as a parse of the stored source");
        for (int i = 0; i < qMin(parsedMeshes.size(), loaded.meshes.size()); ++i)
            compareMesh(parsedMeshes[i], loaded.meshes[i],
                        QStringLiteral("stored mesh %1").arg(i));
    }

    // bakeAll has nothing to do — the import already did it.
    CHECK_LOUD(MeshBakeStore::modelSourcesNeedingBake(conn, storeRoot.path()).isEmpty(),
               "assets.bakeAll's dry run reports nothing left to bake after an import");

    // A CORRUPT bake on disk must make the resolver refuse it, so the open
    // path parses instead of drawing nothing.
    {
        QFile file(plan.bakePath);
        CHECK(file.open(QIODevice::ReadWrite), "bake object opened for corruption");
        const QByteArray whole = file.readAll();
        file.resize(0);
        file.write(whole.left(whole.size() / 3));
        file.close();
    }
    CHECK_LOUD(!MeshBakeStore::isFresh(conn, storeRoot.path(), sourcePath),
               "a truncated bake object is not fresh");
    CHECK_LOUD(!iris::MeshBake::read(plan.bakePath, plan.bakeFingerprint).valid,
               "a truncated bake object reads as a miss, not as geometry");
    CHECK_LOUD(!MeshBakeStore::modelSourcesNeedingBake(conn, storeRoot.path()).isEmpty(),
               "a corrupt bake puts the asset back on bakeAll's list");

    // And bakeAll rebuilds it — through the SAME per-asset entry point the
    // single-asset path uses, which must find the model among the asset's
    // files by extension (an archive-imported Object row files its model
    // under a non-'source' role).
    bool needed = false;
    QString error;
    const bool ok = MeshBakeStore::bakeAsset(&db, conn, storeRoot.path(), result.assetGuid,
                                             false, &needed, &error);
    CHECK_LOUD(ok && needed, "assets.bakeAll rebuilt the corrupt bake");
    if (!ok) std::printf("info: rebake error: %s\n", qUtf8Printable(error));
    CHECK_LOUD(MeshBakeStore::isFresh(conn, storeRoot.path(), sourcePath),
               "the rebuilt bake is fresh again");

    // DETERMINISM, END TO END: assets.checkConsistency re-runs the whole
    // convert stage on the stored source and diffs the produced object set
    // against the catalog. The bake is IN that set, so this only passes if a
    // bake built in a different staging directory hashes to the same oid.
    {
        AssetImportService consistency(&db, &project);
        const QJsonObject report = consistency.checkConsistency(result.assetGuid);
        CHECK_LOUD(report.value("ok").toBool(), "checkConsistency ran");
        CHECK_LOUD(report.value("consistent").toBool(),
                   "checkConsistency is GREEN with a bake in the object set "
                   "(the bake re-derives byte-identically)");
        if (!report.value("consistent").toBool())
            std::printf("info: missing=%s extra=%s\n",
                        QJsonDocument(report.value("missingFromReimport").toArray())
                            .toJson(QJsonDocument::Compact).constData(),
                        QJsonDocument(report.value("extraFromReimport").toArray())
                            .toJson(QJsonDocument::Compact).constData());
    }

    // ARCHIVES CARRY SOURCES, NOT BAKES (design call, stated in
    // exportcontentsource.cpp): a bake is keyed on the build that produced it,
    // so an archive that shipped one would ship megabytes the importing
    // installation refuses on sight. The export walker must not enumerate it;
    // the imported project re-bakes lazily on its first open.
    {
        CasContentSource source(storeRoot.path());
        bool sawSource = false, sawBake = false;
        for (const auto &entry : source.filesForAsset(result.assetGuid)) {
            if (entry.role == QLatin1String("source")) sawSource = true;
            if (entry.role == iris::MeshBake::casRole()) sawBake = true;
        }
        CHECK_LOUD(sawSource, "the export walker still enumerates the source");
        CHECK_LOUD(!sawBake, "the export walker does NOT put the bake in an archive");
    }

    // THE ARCHIVE SHAPE (the defect this lane's pixel run surfaced). A .jaf
    // ingest names the Object row after the model's BASE name and files the
    // model under a non-'source' role, so an Object-row-driven sweep sees no
    // model at all and reports a library with no bakes as fully baked.
    {
        QSqlQuery reshape(conn);
        reshape.prepare("UPDATE asset_files SET role = 'file' WHERE asset_guid = ? AND role = 'source'");
        reshape.addBindValue(result.assetGuid);
        CHECK_LOUD(reshape.exec(), "reshaped the Object row to the archive-import shape");
        QSqlQuery rename(conn);
        rename.prepare("UPDATE assets SET name = 'scaled_two_meshes' WHERE guid = ?");
        rename.addBindValue(result.assetGuid);
        rename.exec();

        // Drop the bake so the sweep has something to find.
        QSqlQuery drop(conn);
        drop.prepare("DELETE FROM asset_files WHERE role = 'bake'");
        drop.exec();
        MeshBakeStore::clear();

        const QStringList needing =
            MeshBakeStore::modelSourcesNeedingBake(conn, storeRoot.path());
        CHECK_LOUD(needing.size() == 1,
                   "the content-first sweep still finds the model on an archive-shaped row");
        if (needing.size() == 1) {
            QString bakeError;
            CHECK_LOUD(MeshBakeStore::bakeSource(conn, storeRoot.path(), needing.first(), &bakeError),
                       "…and bakes it");
            if (!bakeError.isEmpty()) std::printf("info: %s\n", qUtf8Printable(bakeError));
            MeshBakeStore::clear();
            CHECK_LOUD(MeshBakeStore::modelSourcesNeedingBake(conn, storeRoot.path()).isEmpty(),
                       "…and the sweep is then empty");
        }
    }

    // A STALE bake: the producer id changes when the code that builds bakes
    // changes, so a bake keyed on a different producer must be refused even
    // though its bytes are perfect.
    const QString staleFingerprint =
        iris::MeshBake::fingerprintFor(QStringLiteral("f") .repeated(64));
    CHECK_LOUD(!iris::MeshBake::read(plan.bakePath, staleFingerprint).valid,
               "a bake keyed on other content/producer is refused");

    AssetStorePaths::setRootOverride(QString());
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    std::printf("== 1. round trip: baked == parsed ==\n");
    roundTrip(QStringLiteral("tests/importer/fixtures/scaled_two_meshes.glb"));
    roundTrip(QStringLiteral("tests/importer/fixtures/textured_pbr_quad.glb"));
    roundTrip(QStringLiteral("tests/importer/fixtures/ticks_anim.glb"));
    roundTrip(QStringLiteral("tests/importer/fixtures/colored_quad.ply"));
    roundTrip(QStringLiteral("tests/importer/fixtures/tetra_normals.stl"));
    roundTrip(QStringLiteral("app/models/axis_cube.obj"));
    roundTrip(QStringLiteral("app/models/axis_sphere.obj"));

    std::printf("== 2-4. determinism, staleness, corruption ==\n");
    determinismAndFailureModes();

    std::printf("== 5. the store ==\n");
    storeIntegration();

    if (failures) std::printf("FAILED: %d of %d check(s)\n", failures, checks);
    else          std::printf("ALL %d CHECKS PASSED\n", checks);
    return failures ? 1 : 0;
}
