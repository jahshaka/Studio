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
//   6. THE KEY (BAKEKEY-1). The fingerprint's producer term hashes only the
//      files that WRITE a bake; the document-side classes it used to hash ride
//      the hand-bumped format version instead. This section proves the
//      narrowing is real and testable WITHOUT a rebuild: the C++
//      re-implementation of CMake's hash reproduces the compiled-in term on
//      the real tree, an edit to a file OUTSIDE the list cannot change the
//      key, an edit to meshbake.cpp's text does, and a bake carrying the
//      PREVIOUS key is refused (header-level and through the store).
//
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
#include <QElapsedTimer>
#include <cmath>
#include <vector>
#include <QTemporaryDir>
#include <cstdio>
#include <string>

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
#include "jahshaka/engine/Types.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/document/assets/skeleton.h"
#include "irisgl/document/assets/vertexbuffer.h"
#include "irisgl/document/assets/vertexlayout.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/import/graphicshelper.h"
#include "irisgl/import/importflags.h"
#include "irisgl/import/importsettings.h"
#include "irisgl/import/meshbake.h"
#include "irisgl/import/modelsceneinfo.h"

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
                   && (x->parentBone.isNull() || x->parent()->name == y->parent()->name)
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
    auto mat = iris::PbrMaterial::create();
    mat->setValue("baseColor", data.diffuseColor);
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

    // SURFACE CARDS survive the blob byte for byte (SURFACE-CACHE phase 1).
    // The PARSED side has none by design — cards are a product of the bake, as
    // the LOD chain is — so the comparison that means something is built
    // against read.
    {
        bool identical = built.meshes.size() == read.meshes.size();
        int totalCards = 0;
        for (int i = 0; identical && i < built.meshes.size(); ++i) {
            const iris::MeshPtr &a = built.meshes.at(i);
            const iris::MeshPtr &b = read.meshes.at(i);
            if (a.isNull() || b.isNull()) { identical = false; break; }
            identical = a->cards.size() == b->cards.size()
                        && a->cardCoverage == b->cardCoverage;
            totalCards += int(a->cards.size());
            for (int c = 0; identical && c < a->cards.size(); ++c) {
                const iris::MeshCard &x = a->cards.at(c), &y = b->cards.at(c);
                identical = x.axis == y.axis && x.lodLevel == y.lodLevel
                            && x.origin.x() == y.origin.x() && x.origin.y() == y.origin.y()
                            && x.origin.z() == y.origin.z()
                            && x.halfU == y.halfU && x.halfV == y.halfV
                            && x.halfDepth == y.halfDepth && x.coverage == y.coverage;
            }
        }
        CHECK_LOUD(identical,
                   qUtf8Printable(relPath + QStringLiteral(": every surface card survives the "
                                                           "blob byte for byte (%1 cards)")
                                                .arg(totalCards)));
    }

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

    // 3b. THE SETTINGS TERM (IMPORT-1, SPECS/IMPORT_DIALOG_SPEC.md §4.4). An
    // asset's import settings are half the key, because the same source bytes
    // legitimately produce different geometry under different settings. An
    // ABSENT record — every row imported before the import dialog, every
    // shipped sample, every .jaf archive — is IDENTITY and must key exactly as
    // a fully-defaulted record does, or an upgrade would orphan every bake in
    // every library for a second reason.
    {
        const QString oid = QStringLiteral("deadbeef").repeated(8);
        const QString identity = iris::ImportSettings::identityHash();
        CHECK_LOUD(iris::MeshBake::fingerprintFor(oid)
                       == iris::MeshBake::fingerprintFor(oid, identity),
                   "an ABSENT settings record keys exactly as an identity one");
        CHECK_LOUD(iris::MeshBake::fingerprintFor(oid)
                       == iris::MeshBake::fingerprintFor(
                              oid, iris::ImportSettings::hashOf(QJsonObject())),
                   "and so does an EMPTY {} record");
        iris::ImportSettings twice;
        twice.scale = 2.0;
        CHECK_LOUD(iris::MeshBake::fingerprintFor(oid)
                       != iris::MeshBake::fingerprintFor(oid, twice.hash()),
                   "different settings are a different bake");
        CHECK_LOUD(iris::MeshBake::fileNameFor(oid)
                       != iris::MeshBake::fileNameFor(oid, twice.hash()),
                   "…and a different bake FILE, so the two can coexist in one store");
        CHECK_LOUD(iris::MeshBake::fileNameFor(oid).endsWith(identity + QStringLiteral(".jmb")),
                   "the bake's name carries the settings hash");

        // An identity TRANSFORM must also produce byte-identical geometry to no
        // transform at all: the choke point writes assimp's scale property on
        // every call, so "identity" has to mean identity.
        const QByteArray plain = bakeBlob(path, fp);
        QTemporaryDir scratch;
        iris::MeshBake::Model withIdentity = iris::MeshBake::buildFromFile(
            path, fp, scratch.path(), iris::ImportSettings().transform());
        CHECK_LOUD(withIdentity.valid && iris::MeshBake::serialize(withIdentity) == plain,
                   "an identity import transform bakes byte-identical geometry");
    }

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
// 6. the key: what is hashed, what is hand-bumped, and what a stale key does
// ---------------------------------------------------------------------------

/// The fingerprint a build ONE FORMAT VERSION AGO produced for `oid`, composed
/// exactly the way MeshBake::producerId composes today's — the honest stand-in
/// for "a bake already in the user's library", which is what a version bump
/// must invalidate.
static QString previousGenerationFingerprint(const QString &oid)
{
    const QString producer = QStringLiteral("v%1|%2|assimp%3|flags%4")
                                 .arg(iris::MeshBake::formatVersion() - 1)
                                 .arg(iris::MeshBake::producerHash())
                                 .arg(iris::ModelSceneInfo::importerVersion())
                                 .arg(quint64(iris::ImportFlags::Canonical));
    return QString::fromLatin1(QCryptographicHash::hash(
        (producer + QLatin1Char('|') + oid).toUtf8(), QCryptographicHash::Sha256).toHex());
}

static bool copyInto(const QString &from, const QString &to)
{
    QDir().mkpath(QFileInfo(to).absolutePath());
    QFile::remove(to);
    return QFile::copy(from, to);
}

static bool appendComment(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::Append)) return false;
    f.write("\n// a comment that changes no behaviour at all\n");
    return true;
}

static void theKey()
{
    // 6a. THE LIST. The three files that write a bake are hashed; the
    // document-side classes that only describe what it holds are not.
    const QStringList hashed = iris::MeshBake::hashedSources();
    CHECK_LOUD(!hashed.isEmpty(),
               "the build reports which files its producer term hashes");
    CHECK_LOUD(hashed.contains(QStringLiteral("import/meshbake.cpp")) &&
                   hashed.contains(QStringLiteral("import/meshbake.h")),
               "the builder/serializer pair is hashed (it writes every byte)");
    CHECK_LOUD(hashed.contains(QStringLiteral("import/materialhelper.cpp")),
               "materialhelper.cpp is hashed (extractMaterialData fills every material record)");

    const QStringList versionCovered = {
        QStringLiteral("document/assets/mesh.cpp"),   QStringLiteral("document/assets/mesh.h"),
        QStringLiteral("document/assets/skeleton.cpp"), QStringLiteral("document/assets/skeleton.h"),
        QStringLiteral("document/scenegraph/meshnode.cpp"),
        QStringLiteral("core/geometry/trimesh.cpp"),
        QStringLiteral("import/graphicshelper.cpp"),
        QStringLiteral("import/importflags.h"),       QStringLiteral("import/importflags.cpp")};
    QStringList leaked;
    for (const QString &rel : versionCovered)
        if (hashed.contains(rel)) leaked.append(rel);
    CHECK_LOUD(leaked.isEmpty(),
               "no version-covered file is in the producer hash "
               "(they ride kFormatVersion + source.bake_key_guard)");
    if (!leaked.isEmpty())
        std::printf("info: still hashed: %s\n", qUtf8Printable(leaked.join(", ")));

    // The version really is the key's first term, so a hand bump invalidates.
    CHECK_LOUD(iris::MeshBake::producerId().startsWith(
                   QStringLiteral("v%1|").arg(iris::MeshBake::formatVersion())),
               "the format version is the leading term of the producer id");

    // 6b. THE ALGORITHM. producerHashOf is the same hash CMake computed, or the
    // rest of this section proves nothing about the real key.
    const QString irisRoot = QStringLiteral(JAHSHAKA_TEST_SOURCE_DIR "/irisgl");
    QStringList realPaths;
    for (const QString &rel : hashed) realPaths.append(irisRoot + QLatin1Char('/') + rel);
    const QString recomputed = iris::MeshBake::producerHashOf(realPaths);
    if (iris::MeshBake::producerHash() == QStringLiteral("dev")) {
        std::printf("info: this build carries no CMake producer term ('dev') — "
                    "skipping the CMake/C++ agreement check\n");
    } else {
        CHECK_LOUD(!recomputed.isEmpty() && recomputed == iris::MeshBake::producerHash(),
                   "producerHashOf reproduces the producer term CMake compiled in");
    }

    // 6c. NARROWNESS, measured on copies: an edit to a file OUTSIDE the list
    // cannot move the key; an edit to meshbake.cpp's text must.
    QTemporaryDir scratch;
    if (!scratch.isValid()) { std::printf("FAIL: no scratch dir for the key test\n"); ++failures; return; }
    QStringList copies;
    bool copied = true;
    for (const QString &rel : hashed) {
        const QString to = scratch.filePath(rel);
        copied = copyInto(irisRoot + QLatin1Char('/') + rel, to) && copied;
        copies.append(to);
    }
    // A file that is NOT hashed, copied in beside them: mesh.cpp is the class
    // that actually builds the vertex data, and it is deliberately out.
    const QString meshCopy = scratch.filePath(QStringLiteral("document/assets/mesh.cpp"));
    copied = copyInto(irisRoot + QStringLiteral("/document/assets/mesh.cpp"), meshCopy) && copied;
    CHECK_LOUD(copied, "the hashed sources copied into the scratch tree");

    const QString scratchId = iris::MeshBake::producerHashOf(copies);
    CHECK_LOUD(!scratchId.isEmpty() && scratchId == recomputed,
               "the same bytes in another directory hash to the same producer term");

    CHECK_LOUD(appendComment(meshCopy), "the non-hashed copy was edited");
    CHECK_LOUD(iris::MeshBake::producerHashOf(copies) == scratchId,
               "a comment-only edit to document/assets/mesh.cpp does NOT change the key");

    CHECK_LOUD(appendComment(scratch.filePath(QStringLiteral("import/meshbake.cpp"))),
               "the hashed copy was edited");
    CHECK_LOUD(iris::MeshBake::producerHashOf(copies) != scratchId,
               "an edit to import/meshbake.cpp's text DOES change the key");

    // 6d. A BAKE UNDER THE PREVIOUS KEY IS IGNORED — at the header, which is
    // all the resolver reads.
    const QString oid = QStringLiteral("abcdef01").repeated(8);
    const QString current = iris::MeshBake::fingerprintFor(oid);
    const QString previous = previousGenerationFingerprint(oid);
    CHECK_LOUD(!current.isEmpty() && current != previous,
               "bumping the format version changes the fingerprint of every source");

    const QString path = fixture(QStringLiteral("tests/importer/fixtures/textured_pbr_quad.glb"));
    QTemporaryDir staging;
    iris::MeshBake::Model stale =
        iris::MeshBake::buildFromFile(path, previous, staging.path());
    CHECK_LOUD(stale.valid, "a bake was built carrying the previous generation's fingerprint");
    const QByteArray staleBlob = iris::MeshBake::serialize(stale);
    CHECK_LOUD(!iris::MeshBake::deserialize(staleBlob, current).valid,
               "a bake made under the previous key is refused by this build");

    const QString staleFile = scratch.filePath(QStringLiteral("stale.jmb"));
    QString writeError;
    CHECK(iris::MeshBake::write(staleFile, stale, &writeError), "the stale bake was written");
    CHECK_LOUD(!iris::MeshBake::headerMatches(staleFile, current),
               "the header probe rejects the previous generation without reading the payload");
    CHECK_LOUD(!iris::MeshBake::read(staleFile, current).valid,
               "reading a previous-generation bake is a clean miss, so the source is re-baked");

    // And the FORMAT VERSION field itself is enforced, not merely reflected in
    // the fingerprint: a blob from one version ago is refused even if its
    // recorded fingerprint were to match.
    QByteArray oneVersionBack = iris::MeshBake::serialize(
        iris::MeshBake::buildFromFile(path, current, staging.path()));
    CHECK(oneVersionBack.size() > 8, "a current blob to patch");
    if (oneVersionBack.size() > 8) {
        const qint32 back = qint32(iris::MeshBake::formatVersion() - 1);
        // configure(): little endian, so the version's four bytes sit at 4..7.
        for (int i = 0; i < 4; ++i)
            oneVersionBack[4 + i] = char((quint32(back) >> (8 * i)) & 0xFFu);
        CHECK_LOUD(!iris::MeshBake::deserialize(oneVersionBack, current).valid,
                   "a blob stamped with the previous format version is refused");
    }
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

    // THE UPGRADE PATH (BAKEKEY-1): a bake from the previous generation is not
    // corrupt — it is a perfectly good blob under a key this build no longer
    // accepts, recorded as its OWN object (the bake's display name derives from
    // the SOURCE oid, so generations coexist under one name). It must be
    // ignored, land back on assets.bakeAll's list, and re-bake.
    {
        const QString sourceOid = QFileInfo(sourcePath).completeBaseName().toLower();
        const QString freshOid = QFileInfo(plan.bakePath).completeBaseName().toLower();
        const QString previous = previousGenerationFingerprint(sourceOid);

        QTemporaryDir staging;
        iris::MeshBake::Model stale =
            iris::MeshBake::buildFromFile(sourcePath, previous, staging.path());
        const QString stalePath = staging.filePath(iris::MeshBake::fileNameFor(sourceOid));
        QString writeError;
        CHECK(stale.valid && iris::MeshBake::write(stalePath, stale, &writeError),
              "a previous-generation bake was built and written");

        // The library BEFORE this build ran: the stale generation is recorded,
        // the new one is not.
        bool ingested = true;
        for (const QString &guid : {result.assetGuid, result.meshGuid}) {
            QString casError;
            ingested = AssetCas::ingestFile(conn, storeRoot.path(), stalePath, guid,
                                            iris::MeshBake::casRole(),
                                            iris::MeshBake::fileNameFor(sourceOid),
                                            nullptr, &casError) && ingested;
        }
        QSqlQuery drop(conn);
        drop.prepare("DELETE FROM asset_files WHERE role = ? AND oid = ?");
        drop.addBindValue(iris::MeshBake::casRole());
        drop.addBindValue(freshOid);
        CHECK(ingested && drop.exec(), "the library was rewound to the previous generation");

        CHECK_LOUD(!MeshBakeStore::isFresh(conn, storeRoot.path(), sourcePath),
                   "a bake from the previous key is not fresh for this build");
        CHECK_LOUD(!MeshBakeStore::modelSourcesNeedingBake(conn, storeRoot.path()).isEmpty(),
                   "a previous-key bake puts the asset back on assets.bakeAll's list");

        // THROUGH THE VERB'S OWN CALLS: assets.bakeAll (and Preferences ->
        // Assets -> Bake All, which runs the same implementation) walks
        // modelSourcesNeedingBake and calls bakeSource on each — not the
        // per-asset entry point the corrupt case above exercised.
        QString rerror;
        const bool reok = MeshBakeStore::bakeSource(conn, storeRoot.path(), sourcePath, &rerror);
        CHECK_LOUD(reok, "assets.bakeAll's per-source re-bake ran");
        if (!reok) std::printf("info: upgrade rebake error: %s\n", qUtf8Printable(rerror));
        MeshBakeStore::clear();
        CHECK_LOUD(MeshBakeStore::isFresh(conn, storeRoot.path(), sourcePath),
                   "the re-baked asset is fresh under the new key, with the stale "
                   "generation still in the catalog");
        CHECK_LOUD(MeshBakeStore::modelSourcesNeedingBake(conn, storeRoot.path()).isEmpty(),
                   "assets.bakeAll converges: nothing is left needing a bake");
    }

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

// ---------------------------------------------------------------------------
// 7. THE ATOM LOD CHAIN (SPECS/NANITE_SPEC.md §7).
//
// Three separate claims, and they fail in different ways:
//   (a) the POLICY — which meshes get a chain and what it looks like;
//   (b) the SERIALIZER — the chain survives the blob byte for byte, because a
//       level that comes back subtly different draws holes at a distance and
//       nothing else in the tree would notice;
//   (c) the CONTRACT the LOD levels are consumed through, MeshData::
//       lodForWorldError — Photon's voxelizer and its far-field proxy pick a
//       level with it, and "the coarsest level whose error is below this cell
//       size" has to mean exactly that at both ends of the range.
// ---------------------------------------------------------------------------

static void lodChain()
{
    // (a) THE POLICY, on a mesh that has one: the shipped gizmo sphere.
    const QString spherePath = fixture(QStringLiteral("app/models/axis_sphere.obj"));
    QTemporaryDir staging;
    iris::MeshBake::Model model = iris::MeshBake::buildFromFile(
        spherePath, iris::MeshBake::fingerprintFor(QStringLiteral("lodchain")), staging.path());
    CHECK_LOUD(model.valid && !model.meshes.isEmpty(), "the LOD fixture baked");
    if (!model.valid || model.meshes.isEmpty()) return;

    const iris::MeshPtr &mesh = model.meshes.first();
    CHECK_LOUD(mesh->lodIndices.size() >= 2, "a 960-triangle mesh gets a chain of at least two levels");
    CHECK_LOUD(mesh->lodIndices.size() == mesh->lodErrors.size(),
               "every level carries exactly one error");
    const int baseTris = mesh->getIndexBuffer() ? mesh->getIndexBuffer()->dataSize / 12 : 0;
    int previousTris = baseTris;
    float previousError = 0.0f;
    bool ordered = baseTris > 0;
    for (int i = 0; i < mesh->lodIndices.size(); ++i) {
        const int tris = int(mesh->lodIndices.at(i).size()) / 3;
        if (tris >= previousTris || mesh->lodErrors.at(i) <= previousError) ordered = false;
        // A level indexes the ORIGINAL vertex buffer — that is what keeps every
        // level of a mesh inside one draw call, and a level naming a vertex the
        // mesh does not have would draw garbage the moment the camera backs off.
        for (quint32 v : mesh->lodIndices.at(i))
            if (int(v) >= mesh->numVerts) { ordered = false; break; }
        previousTris = tris;
        previousError = mesh->lodErrors.at(i);
    }
    CHECK_LOUD(ordered, "levels get coarser and their errors grow, monotonically");
    CHECK_LOUD(mesh->lodErrors.first() > 0.0f, "the first level's error is a real length, not zero");

    // THE CHAIN ENDS ON A RULE ABOUT THE CONTENT, NOT ON A LEVEL COUNT (ATOM-3
    // A6). `kMaxLevels` used to stop every chain at four levels — 1/16 of the
    // triangles, whatever the asset — so no model had a far-field proxy. The
    // chain now halves until the TRIANGLE FLOOR (kMinTriangles = 128, which is
    // also Nanite's root cluster), until a level cannot shed 15 %, or until the
    // error passes 5 % of the extent. This fixture is small enough that the
    // floor is what stops it: its coarsest level cannot be halved again.
    const int coarsestTris = mesh->lodIndices.isEmpty()
                                 ? 0 : int(mesh->lodIndices.last().size()) / 3;
    CHECK_LOUD(coarsestTris > 0 && coarsestTris < 2 * 128,
               ("the chain ran down to the triangle floor, not to a level count (coarsest level " +
                std::to_string(coarsestTris) + " triangles)").c_str());

    // (b) THE SERIALIZER: through the blob and back, byte for byte.
    const QString blob = QDir(staging.path()).filePath(QStringLiteral("lod.jmb"));
    QString err;
    CHECK_LOUD(iris::MeshBake::write(blob, model, &err), "the chain serialized");
    iris::MeshBake::Model read = iris::MeshBake::read(blob);
    CHECK_LOUD(read.valid && !read.meshes.isEmpty(), "and read back");
    if (read.valid && !read.meshes.isEmpty()) {
        const iris::MeshPtr &back = read.meshes.first();
        bool identical = back->lodIndices.size() == mesh->lodIndices.size() &&
                         back->lodErrors.size() == mesh->lodErrors.size();
        for (int i = 0; identical && i < mesh->lodIndices.size(); ++i)
            identical = back->lodIndices.at(i) == mesh->lodIndices.at(i) &&
                        back->lodErrors.at(i) == mesh->lodErrors.at(i);
        CHECK_LOUD(identical, "every level survives the blob byte for byte");
    }

    // (a2) A MESH THAT MUST NOT GET ONE. The axis cube is far below the point
    // where a level saves anything, and a chain there would cost a buffer, a
    // VAO and a switch for nothing.
    iris::MeshBake::Model cube = iris::MeshBake::buildFromFile(
        fixture(QStringLiteral("app/models/axis_cube.obj")),
        iris::MeshBake::fingerprintFor(QStringLiteral("lodcube")), staging.path());
    if (cube.valid && !cube.meshes.isEmpty()) {
        CHECK_LOUD(cube.meshes.first()->lodIndices.isEmpty(),
                   "a mesh too small to simplify gets NO chain (not a fake level)");
    }

    // (a3) THE CHAIN BUILDER IS IDEMPOTENT: asking twice must not append a
    // second chain to the same mesh.
    const int before = int(mesh->lodIndices.size());
    iris::MeshBake::buildLodChain(mesh);
    CHECK_LOUD(int(mesh->lodIndices.size()) == before,
               "buildLodChain replaces the chain, it does not append to it");

    // (c) THE CONSUMER CONTRACT (the hand-off Photon reads): the coarsest level
    // whose MEASURED BOUND is below a world-space cell size. The rule reads
    // `lodBounds` and not `lodErrors` since ATOM-BAKE-1 (AT-A5) — which this
    // case now proves by giving the two arrays DIFFERENT numbers and asserting
    // the answers follow the bounds.
    jahshaka::engine::MeshData data;
    data.positions.assign(9, 0.0f);
    data.indices = { 0, 1, 2 };
    data.lodIndices = { { 0, 1, 2 }, { 0, 1, 2 }, { 0, 1, 2 } };
    data.lodBounds = { 0.01f, 0.05f, 0.20f };
    data.lodErrors = { 1.0f, 2.0f, 3.0f };   // deliberately absurd: nothing may read these
    CHECK_LOUD(data.lodForWorldError(0.005f) == 0,
               "a cell finer than every level's bound asks for the finest level");
    CHECK_LOUD(data.lodForWorldError(0.02f) == 1, "a 2 cm cell takes the 1 cm level");
    CHECK_LOUD(data.lodForWorldError(0.10f) == 2, "a 10 cm cell takes the 5 cm level");
    CHECK_LOUD(data.lodForWorldError(10.0f) == 3, "a cell coarser than every level takes the coarsest");
    CHECK_LOUD(data.lodForWorldError(0.0f) == 0 && data.lodForWorldError(-1.0f) == 0,
               "a non-positive cell size means the finest, never a wrap-around");
    jahshaka::engine::MeshData plain;
    plain.indices = { 0, 1, 2 };
    CHECK_LOUD(plain.lodForWorldError(100.0f) == 0,
               "a mesh with no chain is level 0 at every cell size");
}

// ---------------------------------------------------------------------------
// 8. SURFACE CARDS (SPECS/SURFACE_CACHE_ASSESSMENT.md §2/§7 phase 1) — the
//    `meshbake.cards` suite.
//
// The card list is what phases 2 and 4 will SPEND: the capture's cost is fixed
// per card (SURFACE-CACHE-0 measured 0.042-0.057 ms of CPU and 352 KB at 128^2
// each), so a generator that authors the wrong cards is not a cosmetic problem,
// it is the cache's budget wrong forever. Four claims, and they fail in
// different ways:
//
//   (a) COVERAGE — the fraction of the mesh's own surface the list can see. It
//       is measured WITH OCCLUSION by the generator (a surfel hidden behind
//       nearer surface of the same mesh is not covered), so it is a real
//       number and not 1.0 by construction.
//   (b) THE 6-FACE BOX on a convex mesh: the cube's list IS the box, exactly —
//       six cards, one per axis, each the exact 2x2 face.
//   (c) DETERMINISM — the same mesh must produce the same cards twice, because
//       the bake's bytes are content-addressed (assets.gc / assets.verify /
//       assets.checkConsistency all compare oids). The generator uses no random
//       numbers at all: the surfel positions come from a van der Corput pair on
//       the surfel INDEX and the K-means seeding is farthest-point.
//   (d) THE BUDGET — `maxCards` is honoured, 0 means none, and more budget can
//       never make the answer worse (it did once: the 6-face box was measured
//       only when the clustered list FAILED, so a torus went from 0.983 at six
//       cards to 0.909 at seven — the torus's own numbers have moved since, but
//       the shape of that defect is why this case exists).
//
// Bake TIME per mesh is printed, not asserted — the assessment lacks the
// number and a threshold on this box would be a flake on another.
// ---------------------------------------------------------------------------

namespace {

struct CardSubject
{
    const char *path;
    float minCoverage;
    const char *note;
};

/// The primitive table's meshes (src/data/primitives.h), by FILE — the table's
/// own paths are Qt resources, which a document-side suite does not link.
/// Every primitive the samples are built from is here; the four that are not in
/// the table (arrow, endlessplane, hp_sphere, teapot) are here too because they
/// ship and they are loaded through the same function.
const CardSubject kCardSubjects[] = {
    { "app/content/primitives/plane.obj",        0.90f, nullptr },
    { "app/content/primitives/cube.obj",         0.90f, nullptr },
    { "app/content/primitives/sphere.obj",       0.90f, nullptr },
    { "app/content/primitives/hemisphere.obj",   0.90f, nullptr },
    { "app/content/primitives/cylinder.obj",     0.90f, nullptr },
    { "app/content/primitives/tube.obj",         0.90f, nullptr },
    { "app/content/primitives/cone.obj",         0.90f, nullptr },
    { "app/content/primitives/pyramid.obj",      0.90f, nullptr },
    { "app/content/primitives/torus.obj",        0.90f, nullptr },
    { "app/content/primitives/capsule.obj",      0.90f, nullptr },
    { "app/content/primitives/wedge.obj",        0.90f, nullptr },
    // THE ONE EXCEPTION, and the budget is NOT what is short: a five-point star
    // extruded in Z hides most of its side faces behind its own arms from every
    // one of the six axes. Measured 0.848 at NINE cards — and swept, it is
    // 0.848 at a budget of 12, of 16, of 24 and of 48: the generator retires
    // every bucket because no further axis-aligned split reaches the surface,
    // so the remaining sixth is not a card this list failed to buy, it is
    // surface no axis-aligned card can see. Epic's cards have exactly this
    // limit. (It was 0.844 at eight cards before the added centre was seeded at
    // the centroid of a bucket's UNSEEN surfels instead of by farthest point,
    // which is worth 0.004 here and nothing anywhere else — the fix is right,
    // the star is simply at the representation's ceiling.) The floor below is a
    // REGRESSION guard on that number, not an endorsement of it.
    { "app/content/primitives/star.obj",         0.80f,
      "self-occluding: axis-aligned cards cannot see inside its own arms" },
    { "app/models/ground.obj",                   0.90f, nullptr },
    { "app/content/primitives/endlessplane.obj", 0.90f, nullptr },
    { "app/content/primitives/hp_sphere.obj",    0.90f, nullptr },
    { "app/content/primitives/teapot.obj",       0.90f, nullptr },
    { "app/content/primitives/arrow.obj",        0.90f, nullptr },
};

}   // namespace

/// A synthetic "crumpled sheet": an n x n grid of quads with a deterministic
/// height, built by hand so it has a real surface, a real silhouette — and NO
/// LOD CHAIN, which is the case the coverage raster's ceiling exists for.
static iris::MeshPtr crumpledGrid(int n)
{
    std::vector<float> pos;
    pos.reserve(size_t(n + 1) * size_t(n + 1) * 3);
    for (int z = 0; z <= n; ++z)
        for (int x = 0; x <= n; ++x) {
            const float fx = float(x) / float(n) * 4.0f - 2.0f;
            const float fz = float(z) / float(n) * 4.0f - 2.0f;
            pos.push_back(fx);
            pos.push_back(0.35f * std::sin(fx * 3.1f) * std::cos(fz * 2.7f));
            pos.push_back(fz);
        }
    std::vector<unsigned> idx;
    idx.reserve(size_t(n) * size_t(n) * 6);
    for (int z = 0; z < n; ++z)
        for (int x = 0; x < n; ++x) {
            const unsigned a = unsigned(z * (n + 1) + x), b = a + 1;
            const unsigned c = unsigned((z + 1) * (n + 1) + x), d = c + 1;
            idx.push_back(a); idx.push_back(c); idx.push_back(b);
            idx.push_back(b); idx.push_back(c); idx.push_back(d);
        }
    auto mesh = iris::MeshPtr(new iris::Mesh());
    mesh->setPrimitiveMode(iris::PrimitiveMode::Triangles);
    mesh->usesIndexBuffer = true;
    mesh->numVerts = int(pos.size() / 3);
    mesh->numFaces = int(idx.size() / 3);
    iris::VertexLayout layout;
    layout.addAttrib(iris::VertexAttribUsage::Position, 0x1406 /*GL_FLOAT*/, 3,
                     int(sizeof(float) * 3));
    auto vb = iris::VertexBuffer::create(layout);
    vb->setData(reinterpret_cast<char *>(pos.data()), unsigned(pos.size() * sizeof(float)));
    mesh->addVertexBuffer(vb);
    auto ib = iris::IndexBuffer::create();
    ib->setData(reinterpret_cast<char *>(idx.data()), unsigned(idx.size() * sizeof(unsigned)));
    mesh->setIndexBuffer(ib);
    return mesh;
}

static void surfaceCards()
{
    // (a) COVERAGE + the bake TIME, over the primitive table and the shipped
    // meshes the sample scenes are built from.
    std::printf("      %-44s %7s %6s %8s %9s %9s\n", "mesh", "tris", "cards", "coverage",
                "cards ms", "load ms");
    for (const CardSubject &subject : kCardSubjects) {
        const QString path = fixture(QLatin1String(subject.path));
        if (!QFileInfo::exists(path)) {
            std::printf("FAIL: card fixture missing: %s\n", subject.path);
            ++failures;
            continue;
        }
        // Mesh::loadMesh is the PRIMITIVE's own path — it builds the cards at
        // creation, which is the thing being tested for these meshes.
        iris::Mesh::clearLoadCache();
        QElapsedTimer timer;
        timer.start();
        const iris::MeshPtr mesh = iris::Mesh::loadMesh(path);
        const double loadMs = double(timer.nsecsElapsed()) / 1e6;
        if (mesh.isNull()) {
            std::printf("FAIL: could not load %s\n", subject.path);
            ++failures;
            continue;
        }
        // THE GENERATOR'S OWN TIME, apart from the assimp parse that dominates
        // the load of a big mesh — the number the assessment lacked. The
        // generator is deterministic, so re-running it costs nothing but time.
        timer.restart();
        iris::MeshBake::buildCards(mesh, iris::kDefaultMaxCards);
        const double cardsMs = double(timer.nsecsElapsed()) / 1e6;
        const iris::IndexBufferPtr ib = mesh->getIndexBuffer();
        std::printf("      %-44s %7d %6d %8.3f %9.2f %9.2f%s%s\n", subject.path,
                    ib ? ib->dataSize / 12 : 0, int(mesh->cards.size()),
                    double(mesh->cardCoverage), cardsMs, loadMs,
                    subject.note ? "   " : "", subject.note ? subject.note : "");
        CHECK(mesh->cards.size() > 0 && mesh->cards.size() <= iris::kDefaultMaxCards,
              qUtf8Printable(QStringLiteral("%1: a card list inside the budget")
                                 .arg(QLatin1String(subject.path))));
        CHECK(mesh->cardCoverage >= subject.minCoverage,
              qUtf8Printable(QStringLiteral("%1: coverage %2 >= %3")
                                 .arg(QLatin1String(subject.path))
                                 .arg(double(mesh->cardCoverage), 0, 'f', 3)
                                 .arg(double(subject.minCoverage), 0, 'f', 2)));
        // Every card has to be a capture somebody can actually run.
        bool wellFormed = true;
        for (const iris::MeshCard &card : mesh->cards)
            wellFormed = wellFormed && card.axis < iris::MeshCard::kAxisCount
                         && card.halfU > 0.0f && card.halfV > 0.0f && card.halfDepth > 0.0f
                         && card.coverage >= 0.0f && card.coverage <= 1.0f
                         && int(card.lodLevel) <= mesh->lodIndices.size();
        CHECK(wellFormed, qUtf8Printable(QStringLiteral("%1: every card is a runnable capture")
                                             .arg(QLatin1String(subject.path))));
    }
    std::printf("ok:   every shipped mesh's card list covers its surface\n");
    ++checks;

    // (b) THE 6-FACE BOX, EXACT, on the cube: six cards, one per axis, each the
    // exact face of the 2 x 2 x 2 mesh. A card is grown by a margin and then
    // clamped to the mesh's own box in its plane, which is what makes this
    // exact rather than "the box plus a margin".
    {
        iris::Mesh::clearLoadCache();
        const iris::MeshPtr cube =
            iris::Mesh::loadMesh(fixture(QStringLiteral("app/content/primitives/cube.obj")));
        CHECK_LOUD(!cube.isNull() && cube->cards.size() == 6,
                   "the cube gets exactly six cards — the 6-face box, not two per face");
        if (!cube.isNull() && cube->cards.size() == 6) {
            bool oneEach = true;
            bool exact = true;
            QVector<int> perAxis(iris::MeshCard::kAxisCount, 0);
            const iris::Vec3 lo = cube->aabb.getMin(), hi = cube->aabb.getMax();
            const iris::Vec3 size = hi - lo;
            for (const iris::MeshCard &card : cube->cards) {
                ++perAxis[card.axis];
                // The card's rectangle is the face: its u and v sizes are the
                // box's own extents along axisU / axisV.
                const float wantU = std::fabs(iris::Vec3::dotProduct(size,
                                        iris::MeshCard::axisU(card.axis)));
                const float wantV = std::fabs(iris::Vec3::dotProduct(size,
                                        iris::MeshCard::axisV(card.axis)));
                if (std::fabs(card.halfU * 2.0f - wantU) > 1e-4f) exact = false;
                if (std::fabs(card.halfV * 2.0f - wantV) > 1e-4f) exact = false;
            }
            for (int a = 0; a < iris::MeshCard::kAxisCount; ++a)
                if (perAxis[a] != 1) oneEach = false;
            CHECK_LOUD(oneEach, "one card per axis, all six");
            CHECK_LOUD(exact, "each card's rectangle is the box's face, exactly");
            CHECK_LOUD(cube->cardCoverage >= 0.999f,
                       "and the six of them see the whole cube");
        }
    }

    // (c) DETERMINISM. Two independent loads of one mesh, and the same mesh
    // built through the BAKE, must agree card for card. The bake's bytes are
    // content-addressed, so a generator with a random seed would make one
    // model's bake a different object on every import.
    {
        const QString path = fixture(QStringLiteral("app/content/primitives/torus.obj"));
        iris::Mesh::clearLoadCache();
        const iris::MeshPtr first = iris::Mesh::loadMesh(path);
        iris::Mesh::clearLoadCache();
        const iris::MeshPtr second = iris::Mesh::loadMesh(path);
        bool same = !first.isNull() && !second.isNull()
                    && first->cards.size() == second->cards.size()
                    && first->cardCoverage == second->cardCoverage;
        for (int i = 0; same && i < first->cards.size(); ++i) {
            const iris::MeshCard &a = first->cards.at(i), &b = second->cards.at(i);
            same = a.axis == b.axis && a.lodLevel == b.lodLevel
                   && a.origin.x() == b.origin.x() && a.origin.y() == b.origin.y()
                   && a.origin.z() == b.origin.z()
                   && a.halfU == b.halfU && a.halfV == b.halfV && a.halfDepth == b.halfDepth
                   && a.coverage == b.coverage;
        }
        CHECK_LOUD(same, "the same mesh builds the same cards twice, bit for bit (no seed exists)");

        // And the GENERATOR is idempotent: asking twice must replace the list,
        // never append to it.
        if (!first.isNull()) {
            const int before = int(first->cards.size());
            iris::MeshBake::buildCards(first, iris::kDefaultMaxCards);
            CHECK_LOUD(int(first->cards.size()) == before,
                       "buildCards replaces the list, it does not append to it");
        }
    }

    // (d) THE BUDGET, and monotonicity in it.
    {
        const QString path = fixture(QStringLiteral("app/content/primitives/star.obj"));
        iris::Mesh::clearLoadCache();
        const iris::MeshPtr mesh = iris::Mesh::loadMesh(path);
        if (!mesh.isNull()) {
            iris::MeshBake::buildCards(mesh, 0);
            CHECK_LOUD(mesh->cards.isEmpty() && mesh->cardCoverage == 0.0f,
                       "maxCards 0 authors no cards at all — and says so, rather than a box");
            float previous = -1.0f;
            bool monotone = true;
            bool capped = true;
            for (int budget : { 1, 2, 6, 8, 12, 24 }) {
                iris::MeshBake::buildCards(mesh, budget);
                if (mesh->cards.size() > budget) capped = false;
                if (mesh->cardCoverage < previous - 1e-6f) monotone = false;
                previous = mesh->cardCoverage;
            }
            CHECK_LOUD(capped, "the card count never exceeds maxCards");
            CHECK_LOUD(monotone, "raising maxCards never lowers the coverage");
            iris::MeshBake::buildCards(mesh, 1000);
            CHECK_LOUD(mesh->cards.size() <= 64,
                       "an absurd budget is clamped to the format's ceiling of 64");
        }
    }

    // (e) A SKINNED mesh gets NONE — a card baked against a bind pose is a lie,
    // and the ray hit on a skinned surface keeps reading the voxel fallback
    // (Epic states the same limit for skeletal meshes).
    {
        QTemporaryDir scratch;
        iris::MeshBake::Model rigged = iris::MeshBake::buildFromFile(
            fixture(QStringLiteral("tests/importer/fixtures/ticks_anim.glb")),
            iris::MeshBake::fingerprintFor(QStringLiteral("cardskin")), scratch.path());
        if (rigged.valid && !rigged.meshes.isEmpty()) {
            bool anySkinned = false, skinnedHaveNoCards = true;
            for (const iris::MeshPtr &mesh : rigged.meshes) {
                if (mesh.isNull() || mesh->getSkeleton().isNull()) continue;
                anySkinned = true;
                if (!mesh->cards.isEmpty()) skinnedHaveNoCards = false;
            }
            if (anySkinned)
                CHECK_LOUD(skinnedHaveNoCards, "a skinned mesh gets no cards");
        }
    }

    // (f) THE IMPORT SETTING. `maxCards` is a recorded, validated, hashed part
    // of the import record — API-first: the verb and its refusals before any
    // dialog row (SPECS/SCRIPTING_SPEC.md §2.3).
    {
        QString error;
        QJsonObject record;
        record[QStringLiteral("maxCards")] = 6;
        const iris::ImportSettings six = iris::ImportSettings::fromJson(record, &error);
        CHECK_LOUD(error.isEmpty() && six.maxCards == 6, "assets import settings read maxCards");
        CHECK_LOUD(six.transform().maxCards == 6, "and carry it to the builder");
        CHECK_LOUD(six.toJson().value(QStringLiteral("maxCards")).toInt() == 6,
                   "and write it back in the complete record");

        for (const QJsonValue &bad : { QJsonValue(-1), QJsonValue(65), QJsonValue(3.5),
                                       QJsonValue(QStringLiteral("twelve")) }) {
            QJsonObject wrong;
            wrong[QStringLiteral("maxCards")] = bad;
            QString why;
            iris::ImportSettings::fromJson(wrong, &why);
            CHECK(!why.isEmpty(),
                  qUtf8Printable(QStringLiteral("a refused maxCards value fails the import: %1")
                                     .arg(bad.toVariant().toString())));
        }
        std::printf("ok:   maxCards is validated the same way every other import setting is\n");
        ++checks;

        // IT IS PART OF THE BAKE KEY: two imports of one file at different card
        // budgets are two different bakes, exactly as two scales are.
        iris::ImportSettings twelve;
        CHECK_LOUD(twelve.hash() == iris::ImportSettings::identityHash()
                       && twelve.maxCards == iris::kDefaultMaxCards,
                   "the default budget is 12 and keys as IDENTITY (an old row re-bakes once, "
                   "by the format bump, not by a renamed key)");
        CHECK_LOUD(six.hash() != iris::ImportSettings::identityHash(),
                   "a different budget is a different bake key");
    }

    // (g) THE COVERAGE RASTER'S CEILING — A MESH WITH NO CHAIN (F1).
    //
    // The raster is charged per TRIANGLE PER CARD PER ROUND, so an unbounded
    // one is quadratic in the wrong place: at twelve cards and a dozen rounds a
    // 2 M-triangle mesh is 144 x 2 M triangle setups. The ceiling took the
    // COARSEST BAKED LEVEL, which misses the two classes that have no chain at
    // all — a mesh whose topology stopped the simplifier, and EVERY mesh born
    // through Mesh::loadMesh — and those are exactly the meshes a user's import
    // and Preferences' bake-all hand to the UI thread.
    //
    // WHAT IS ASSERTED, and why it is shaped this way: the claim is a BOUND, so
    // the assertion is that the cost stops tracking the triangle count —
    // quadrupling the triangles must not quadruple the time — plus a generous
    // absolute ceiling that a "seconds" regression trips and a loaded box does
    // not. A tight wall-clock threshold here would be the flake class CLAUDE.md
    // names; the measured numbers are PRINTED so a reader sees the real shape.
    // Measured on this box (Debug): 65 k tris 100 ms, 259 k 190 ms, 2 M 632 ms
    // — against 115 / 414 ms and rising linearly with the ceiling removed.
    //
    // AND COVERAGE MUST NOT PAY FOR THE BOUND. Both halves of this were found
    // by measurement and both were wrong first: a uniform stride aliased
    // against the grid's own period (1.000 -> 0.399), and a centre-sampled
    // raster leaves a texel empty whenever every triangle in it is finer than a
    // texel, which any thinning then makes worse. The golden-ratio selection
    // and the centroid splat are those two fixes, and this case is what holds
    // them: the same surface at five densities must read the same coverage.
    {
        std::printf("      %-44s %9s %9s\n", "synthetic chainless grid", "triangles", "cards ms");
        double previousMs = 0.0;
        int previousTris = 0;
        bool bounded = true, covered = true, underCeiling = true;
        for (int n : { 120, 180, 360, 700 }) {
            const iris::MeshPtr mesh = crumpledGrid(n);
            CHECK(mesh->lodIndices.isEmpty(), "the synthetic grid carries no LOD chain");
            QElapsedTimer timer;
            timer.start();
            iris::MeshBake::buildCards(mesh, iris::kDefaultMaxCards);
            const double ms = double(timer.nsecsElapsed()) / 1e6;
            std::printf("      %-44s %9d %9.1f   cov %.3f\n", "", mesh->numFaces, ms,
                        double(mesh->cardCoverage));
            if (mesh->cardCoverage < 0.95f) covered = false;
            if (ms > 4000.0) underCeiling = false;
            // The bound: 4x the triangles must not cost 4x the time. Compared
            // against the FIRST measured pair only, and with a wide factor,
            // because this is a shape assertion and not a benchmark.
            if (previousTris > 0) {
                const double triRatio = double(mesh->numFaces) / double(previousTris);
                const double msRatio = previousMs > 0.5 ? ms / previousMs : 1.0;
                if (msRatio > triRatio) bounded = false;
            }
            previousMs = ms;
            previousTris = mesh->numFaces;
        }
        CHECK_LOUD(covered,
                   "the same surface reads the same coverage at every density — the raster's "
                   "ceiling does not buy its bound with coverage");
        CHECK_LOUD(bounded,
                   "and the cost stops tracking the triangle count: 4x the triangles is less "
                   "than 4x the time, at every step");
        CHECK_LOUD(underCeiling,
                   "no density takes seconds (the unbounded raster did, on the UI thread)");
    }
}


// ---------------------------------------------------------------------------
// 9. THE MEASURED BOUND (ATOM P1's AT-A5) — the `atom.error_bound` suite.
//
// THE CLAIM UNDER TEST, in one sentence: for every level of every shipped mesh,
// no point of that level's surface lies further from level 0's surface than the
// length the bake stored — measured INDEPENDENTLY of the bake's own sampler.
//
// WHAT THIS IS, SAID ACCURATELY (the lane's audit corrected the first wording).
// It is a REGRESSION CHECK, not an independent derivation. The bake's maximum comes
// from the REMOVED BASE VERTICES, computed EXACTLY — `meshopt_simplify` only ever
// removes vertices, so the worst case sits on the ones the level no longer has, and
// there is nothing to sample there. A check that walked them too would reproduce
// that term bit for bit and pass by construction, which is what the first cut did:
// its worst measured/stored came out at 0.8000 = 1/1.25 on seven of eight subjects,
// i.e. it was reading back the bake's own maximum and dividing by the margin.
//
// So the dense set here is AREA-ONLY, eight times as dense, at strata the bake
// never used (a different sample count moves every stratum, and the van der Corput
// index is the sample number). That leaves the SAMPLING-GAP MARGIN exposed, which
// is the one thing a margin should be judged on, and it exercises the whole
// pipeline — sampler, grid resolution, precision floor, monotonicity — against
// numbers the bake did not produce.
//
// AND WHAT IT MEASURES, STATED SO NOBODY READS MORE INTO A PASS: area-only at 8x
// density NEVER EXCEEDS the stored bound on any shipped mesh, and on eight of ten
// subjects it lands at exactly 0.8000 of it — which is 1/1.25, the margin, meaning
// the dense sampling found precisely the same maximum the bake's 4096 samples did.
// For smooth simplification the area maximum has already CONVERGED at 4096, so the
// margin is headroom and not a necessity. `axis_sphere.obj` is the one subject where
// density found more (0.8094, i.e. 1.2 % past the bake's own figure), and
// `ground.obj` reads 0.2037 because its bound is the precision floor. So this suite
// is a REGRESSION CHECK on a converged number, and the term that actually carries
// the maximum on a hard mesh is the removed-vertex walk — measured at 13.08x the
// area-only figure on `endlessplane.obj` (JAH_BAKE_BOUND_TERMS in meshbake.cpp
// prints the split), which is exactly why that walk is in the bake and not here.
//
// AND `lodBounds[k] >= lodErrors[k]` IS DELIBERATELY NOT ASSERTED. The quadric can
// over-state as easily as it under-states — it is a different quantity, not a
// looser version of this one — so the suite REPORTS the ratio and pins nothing.
// The ratio table is the evidence AT-A5 was missing.
static void errorBound()
{
    struct Row { QString path; };
    const QVector<Row> subjects = {
        { QStringLiteral("app/content/primitives/sphere.obj") },
        { QStringLiteral("app/content/primitives/hp_sphere.obj") },
        { QStringLiteral("app/content/primitives/capsule.obj") },
        { QStringLiteral("app/content/primitives/torus.obj") },
        { QStringLiteral("app/content/primitives/hemisphere.obj") },
        { QStringLiteral("app/content/primitives/teapot.obj") },
        { QStringLiteral("app/content/primitives/tube.obj") },
        { QStringLiteral("app/content/primitives/endlessplane.obj") },
        { QStringLiteral("app/models/ground.obj") },
        { QStringLiteral("app/models/axis_sphere.obj") },
        { QStringLiteral("tests/importer/fixtures/scaled_two_meshes.glb") },
    };

    int chained = 0;
    std::printf("      %-34s %5s %9s %12s %12s %6s\n",
                "mesh", "level", "tris", "quadric", "bound", "ratio");
    for (const Row &r : subjects) {
        iris::MeshBake::Model m =
            iris::MeshBake::buildFromFile(fixture(r.path), QStringLiteral("atom-error-bound"));
        if (!m.valid || m.meshes.isEmpty()) {
            CHECK_LOUD(false, qUtf8Printable(r.path + ": bakes at all"));
            continue;
        }
        for (const iris::MeshPtr &mesh : m.meshes) {
            if (mesh.isNull() || mesh->lodIndices.isEmpty()) continue;
            ++chained;
            CHECK_LOUD(mesh->lodBounds.size() == mesh->lodIndices.size() &&
                           mesh->lodErrors.size() == mesh->lodIndices.size(),
                       qUtf8Printable(r.path + ": one bound and one quadric per level"));
            // Monotone, because the one rule stops at the first level it cannot
            // afford and that is only correct for a sorted array.
            bool monotone = true;
            for (int i = 1; i < mesh->lodBounds.size(); ++i)
                if (mesh->lodBounds.at(i) < mesh->lodBounds.at(i - 1)) monotone = false;
            CHECK_LOUD(monotone, qUtf8Printable(r.path + ": the bounds are non-decreasing"));

            double worstRatio = 0.0;
            const bool ok = iris::MeshBake::checkLodBounds(mesh, 8, &worstRatio);
            CHECK_LOUD(ok, qUtf8Printable(
                               QStringLiteral("%1: EVERY level is inside its stored bound under "
                                              "an independent sampling 8x as dense (worst "
                                              "measured/stored %2)")
                                   .arg(r.path).arg(worstRatio, 0, 'f', 4)));
            for (int i = 0; i < mesh->lodBounds.size(); ++i) {
                const float q = mesh->lodErrors.at(i), b = mesh->lodBounds.at(i);
                std::printf("      %-34s %5d %9d %12.6f %12.6f %6.2f\n",
                            qUtf8Printable(QFileInfo(r.path).fileName()), i + 1,
                            int(mesh->lodIndices.at(i).size() / 3), double(q), double(b),
                            q > 0.0f ? double(b) / double(q) : 0.0);
            }
        }
    }
    CHECK_LOUD(chained >= 8, "the subject list really does carry chained meshes");
}

// ---------------------------------------------------------------------------
// 10. THE SIGNED DISTANCE FIELD (ATOM P2 / SUB-S5-SDF) — the `meshbake.sdf` suite.
//
// WHAT A DISTANCE FIELD HAS TO GET RIGHT, and what each check is for:
//
//   * THE ZERO CROSSING IS THE SURFACE. A cell whose stored distance is d must
//     really be about d from level 0's geometry — checked against the same exact
//     nearest-surface query the bake's own grid answers, to WITHIN ONE CELL,
//     which is the accuracy the design asks for and the accuracy the exact band
//     is there to deliver.
//   * THE SIGN. A closed convex mesh must read NEGATIVE at its centre and
//     POSITIVE in the padded corner. That is the check that would have caught the
//     first cut of this code, which derived the sign from the seed direction and
//     therefore read every far cell as outside — losing the entire interior of
//     anything thicker than the exact band.
//   * THE RESOLUTION IS NOT FINER THAN THE GEOMETRY IS HONEST: the cell is at
//     least four times level 1's measured bound whenever there is a chain.
//   * IT SURVIVES THE FORMAT. Serialise, read back, compare every byte.
//   * AND A SKINNED MESH GETS NONE, because a field baked against a bind pose is
//     a lie, exactly as a card is.
static void signedDistanceField()
{
    // CONE AND PYRAMID ARE HERE FOR THE SIGN, and they are the shapes that would
    // have caught the first cut of it: both have a convex feature sharper than a
    // right angle (an apex, and in the wedge's case a spine), where the nearest
    // point of an EXTERIOR cell is ON that feature and a single face normal there
    // can face away from the cell — signing an outside point INSIDE. The fix is the
    // angle-weighted pseudonormal (`surface::angleWeightAt`); the check is
    // `checkSdfExteriorSign` below, because `checkSdfAgainstSurface` compares
    // MAGNITUDES (|stored| against the exact distance) and is blind to it.
    const QStringList subjects = {
        QStringLiteral("app/content/primitives/sphere.obj"),
        QStringLiteral("app/content/primitives/cube.obj"),
        QStringLiteral("app/content/primitives/hp_sphere.obj"),
        QStringLiteral("app/content/primitives/torus.obj"),
        QStringLiteral("app/content/primitives/cone.obj"),
        QStringLiteral("app/content/primitives/pyramid.obj"),
        QStringLiteral("app/content/primitives/wedge.obj"),
    };
    std::printf("      %-28s %12s %10s %10s %9s\n", "mesh", "dims", "cell", "scale", "bytes");
    for (const QString &path : subjects) {
        iris::MeshBake::Model m =
            iris::MeshBake::buildFromFile(fixture(path), QStringLiteral("atom-sdf"));
        if (!m.valid || m.meshes.isEmpty()) {
            CHECK_LOUD(false, qUtf8Printable(path + ": bakes at all"));
            continue;
        }
        const iris::MeshPtr mesh = m.meshes.first();
        const iris::MeshSdf &f = mesh->sdf;
        CHECK_LOUD(!f.isEmpty(), qUtf8Printable(path + ": carries a field"));
        if (f.isEmpty()) continue;
        std::printf("      %-28s %4dx%3dx%3d %10.5f %10.5f %9d\n",
                    qUtf8Printable(QFileInfo(path).fileName()), f.dim[0], f.dim[1], f.dim[2],
                    double(f.cell), double(f.scale), f.values.size());
        CHECK_LOUD(f.values.size() == f.cellCount(),
                   qUtf8Printable(path + ": one byte per cell, exactly"));
        CHECK_LOUD(f.dim[0] <= iris::MeshSdf::kMaxDim && f.dim[1] <= iris::MeshSdf::kMaxDim &&
                       f.dim[2] <= iris::MeshSdf::kMaxDim,
                   qUtf8Printable(path + ": inside the format's ceiling"));
        if (!mesh->lodBounds.isEmpty())
            CHECK_LOUD(f.cell >= mesh->lodBounds.first() * 4.0f * 0.999f,
                       qUtf8Printable(path + ": the cell is no finer than the geometry is honest"));

        // THE ZERO CROSSING, against the geometry itself.
        double worstCells = 0.0;
        int probed = 0;
        const bool accurate = iris::MeshBake::checkSdfAgainstSurface(mesh, &worstCells, &probed);
        CHECK_LOUD(probed > 50, qUtf8Printable(path + ": the band really has cells in it"));
        CHECK_LOUD(accurate,
                   qUtf8Printable(QStringLiteral("%1: the field agrees with level 0's surface to "
                                                 "within one cell (worst %2 cells over %3 probes)")
                                      .arg(path).arg(worstCells, 0, 'f', 3).arg(probed)));

        // THE SIGN. The padded corner is outside by construction; the centre of a
        // closed mesh is inside.
        CHECK_LOUD(f.distanceAt(0, 0, 0) > 0.0f,
                   qUtf8Printable(path + ": the padded corner reads OUTSIDE"));
        // The box centre is inside a SOLID convex mesh and not otherwise: a torus
        // centre is in its hole, and a WEDGE's is ON its diagonal face (the wedge is
        // the half of a 2x2x2 cube cut through opposite edges, so the cube's centre
        // lies in that plane and its true signed distance is zero — an honest
        // ambiguity, not a defect). Both are excluded by name and by reason.
        if (!path.contains(QStringLiteral("torus")) && !path.contains(QStringLiteral("wedge")))
            CHECK_LOUD(f.distanceAt(f.dim[0] / 2, f.dim[1] / 2, f.dim[2] / 2) < 0.0f,
                       qUtf8Printable(path + ": the centre of a solid convex mesh reads INSIDE"));

        // ...AND EVERY EXTERIOR CELL READS POSITIVE. This is the assertion the
        // pseudonormal exists for, and the one the magnitude check cannot make: a
        // cell whose nearest point is on a sharp convex edge or an apex is where a
        // face-normal sign flips. "Exterior" is decided WITHOUT the field — by ray
        // parity through level 0's own triangles — so the field is judged against
        // the geometry rather than against itself.
        int outsideProbed = 0, outsideWrong = 0;
        const bool signOk = iris::MeshBake::checkSdfExteriorSign(mesh, &outsideProbed,
                                                                &outsideWrong);
        CHECK_LOUD(outsideProbed > 100,
                   qUtf8Printable(path + ": the exterior probe really has cells in it"));
        CHECK_LOUD(signOk,
                   qUtf8Printable(QStringLiteral("%1: EVERY exterior cell reads POSITIVE "
                                                 "(%2 wrong of %3 probed)")
                                      .arg(path).arg(outsideWrong).arg(outsideProbed)));

        // THE FORMAT.
        const QByteArray blob = iris::MeshBake::serialize(m);
        const iris::MeshBake::Model back = iris::MeshBake::deserialize(blob);
        CHECK_LOUD(back.valid && !back.meshes.isEmpty(),
                   qUtf8Printable(path + ": the bake with a field round-trips"));
        if (back.valid && !back.meshes.isEmpty()) {
            const iris::MeshSdf &g = back.meshes.first()->sdf;
            CHECK_LOUD(g.dim[0] == f.dim[0] && g.dim[1] == f.dim[1] && g.dim[2] == f.dim[2] &&
                           g.cell == f.cell && g.scale == f.scale && g.values == f.values,
                       qUtf8Printable(path + ": every field byte survives the format"));
        }
    }

    // A SKINNED MESH GETS NO FIELD (the same rule the cards obey).
    iris::MeshBake::Model rig = iris::MeshBake::buildFromFile(
        fixture(QStringLiteral("tests/importer/fixtures/ticks_anim.glb")),
        QStringLiteral("atom-sdf-skinned"));
    if (rig.valid && !rig.meshes.isEmpty()) {
        bool anySkinned = false, anyField = false;
        for (const iris::MeshPtr &mesh : rig.meshes) {
            if (mesh.isNull() || !mesh->hasSkeleton()) continue;
            anySkinned = true;
            if (!mesh->sdf.isEmpty()) anyField = true;
        }
        if (anySkinned)
            CHECK_LOUD(!anyField, "a skinned mesh gets NO field (a bind-pose field is a lie)");
    }
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // `--cards` is the meshbake.cards suite; with no argument the binary runs
    // everything meshbake.roundtrip has always run (plus the card SERIALIZER
    // check inside the round trip itself).
    if (argc > 1 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--cards")) {
        std::printf("== 8. surface cards ==\n");
        surfaceCards();
        if (failures) std::printf("FAILED: %d of %d check(s)\n", failures, checks);
        else          std::printf("ALL %d CHECKS PASSED\n", checks);
        return failures ? 1 : 0;
    }
    // `--error-bound` is atom.error_bound; `--sdf` is meshbake.sdf. Their own
    // suites because they are their own claims (and each prints a table the
    // design asked for).
    if (argc > 1 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--error-bound")) {
        std::printf("== 9. the measured bound ==\n");
        errorBound();
        if (failures) std::printf("FAILED: %d of %d check(s)\n", failures, checks);
        else          std::printf("ALL %d CHECKS PASSED\n", checks);
        return failures ? 1 : 0;
    }
    if (argc > 1 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--sdf")) {
        std::printf("== 10. the signed distance field ==\n");
        signedDistanceField();
        if (failures) std::printf("FAILED: %d of %d check(s)\n", failures, checks);
        else          std::printf("ALL %d CHECKS PASSED\n", checks);
        return failures ? 1 : 0;
    }

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

    std::printf("== 6. the key ==\n");
    theKey();

    std::printf("== 5. the store ==\n");
    storeIntegration();

    std::printf("== 7. the ATOM LOD chain ==\n");
    lodChain();

    if (failures) std::printf("FAILED: %d of %d check(s)\n", failures, checks);
    else          std::printf("ALL %d CHECKS PASSED\n", checks);
    return failures ? 1 : 0;
}
