// MESH BAKE — the gate (MESH_BAKE_SPEC.md phase 1).
//
// The bake exists to make opening a world a LOAD instead of an assimp parse.
// That is only allowed if a baked load is INDISTINGUISHABLE from a fresh
// build, so this suite does not check that the bake "works": it builds both
// sides and compares them field by field.
//
//   1. Round trip. For every fixture: parse with assimp, build the document
//      the old way (testmesh::fromScene +
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
#include <QCryptographicHash>

#include "../support/testmesh.h"
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
#include <algorithm>
#include <cfloat>
#include <map>
#include <cmath>
#include <vector>
#include <QTemporaryDir>
#include <QSet>
#include <atomic>
#include <QEventLoop>
#include <QThread>
#include <QBuffer>
#include <cstdio>
#include <functional>
#include <string>

#include "assimp/Importer.hpp"
#include "assimp/scene.h"
#include "assimp/ProgressHandler.hpp"
#include "assimp/postprocess.h"

#include "data/database/database.h"
#include "data/project.h"
#include "services/assetcas.h"
#include "services/assetstorepaths.h"
#include "services/import/assetimportservice.h"
#include "services/meshbakestore.h"
#include "irisgl/import/meshprewarm.h"
#include "irisgl/import/parsecensus.h"
#include "services/primitiveassets.h"
#include "data/primitives.h"
#include "../support/testmesh.h"
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
#include "irisgl/import/importflags.h"
#include "irisgl/import/importsettings.h"
#include "irisgl/import/meshbake.h"
#include "irisgl/import/modelsceneinfo.h"
#include "irisgl/import/scenesource.h"
#include "irisgl/import/clipfileinfo.h"
#include "../support/timingbars.h"
#include "cutcurve_stock.h"

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
        testmesh::fromScene(scene);
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
            testmesh::fromScene(scene);
        CHECK_LOUD(parsedMeshes.size() == loaded.meshes.size(),
                   "the stored bake has the same mesh count as a parse of the stored source");
        for (int i = 0; i < qMin(parsedMeshes.size(), loaded.meshes.size()); ++i)
            compareMesh(parsedMeshes[i], loaded.meshes[i],
                        QStringLiteral("stored mesh %1").arg(i));
    }

    // bakeAll has nothing to do — the import already did it.
    CHECK_LOUD(MeshBakeStore::modelSourcesNeedingBake(conn, storeRoot.path()).isEmpty(),
               "assets.bakeAll's dry run reports nothing left to bake after an import");

    // A CORRUPT bake on disk must make the resolver refuse it (FORWARD-ONLY-1:
    // the open shows the model missing — it never parses in its place).
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
    // THE REFUSAL, through the open's own reader: no model, and no parse.
    {
        MeshBakeStore::clear();
        const int parsesBefore = iris::ParseCensus::snapshot().workerParses
                                 + iris::ParseCensus::snapshot().mainThreadParses;
        CHECK_LOUD(MeshBakeStore::load(sourcePath) == nullptr,
                   "MeshBakeStore::load REFUSES the stale bake (null, no geometry)");
        CHECK_LOUD(iris::ParseCensus::snapshot().workerParses
                           + iris::ParseCensus::snapshot().mainThreadParses == parsesBefore,
                   "...and nothing parsed the source in its place");
        iris::MeshPrewarm prewarm;
        prewarm.parse(MeshBakeStore::planFor(conn, storeRoot.path(), sourcePath));
        CHECK_LOUD(!prewarm.baked(sourcePath) && prewarm.contains(sourcePath),
                   "the open's prewarm records a MISS for it (bakes only, no parse behind it)");
    }

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
//    `meshbake.cards` (archived: archive/tests/meshbake) suite.
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
    { "tests/meshbake/fixtures/flat_grid_100m.obj",                   0.90f, nullptr },
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
        // The cards are built HERE, from the parse, exactly as the BAKE builds
        // them (ATOM P2 deleted the creation-time generator that used to run
        // inside Mesh::loadMesh; the cards a primitive carries are its bake's).
        QElapsedTimer timer;
        timer.start();
        const iris::MeshPtr mesh = testmesh::load(path);
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
        const iris::MeshPtr cube =
            testmesh::load(fixture(QStringLiteral("app/content/primitives/cube.obj")));
        // The cards are built HERE now: nothing builds them at creation since
        // ATOM P2 deleted Mesh::loadMesh, and a primitive's real cards are its
        // BAKE's (meshbake.primitives_baked (archived: archive/tests/meshbake)).
        if (!cube.isNull()) iris::MeshBake::buildCards(cube, iris::kDefaultMaxCards);
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
        const iris::MeshPtr first = testmesh::load(path);
        const iris::MeshPtr second = testmesh::load(path);
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
            iris::MeshBake::buildCards(first, iris::kDefaultMaxCards);
            const int before = int(first->cards.size());
            iris::MeshBake::buildCards(first, iris::kDefaultMaxCards);
            CHECK_LOUD(int(first->cards.size()) == before,
                       "buildCards replaces the list, it does not append to it");
        }
    }

    // (d) THE BUDGET, and monotonicity in it.
    {
        const QString path = fixture(QStringLiteral("app/content/primitives/star.obj"));
        const iris::MeshPtr mesh = testmesh::load(path);
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
    // outside an import — and those are exactly the meshes a user's import
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
        // Both wall-clock bars are STAGE-CLOSE (lane D6B-GATE-SHAPE; tests/support/timingbars.h):
        // meshbake.cards.timing (archived: archive/tests/meshbake) arms them on a quiet box; this row asserts the coverage.
        JAH_TIMING_CHECK("meshbake.cards", bounded,
                   "and the cost stops tracking the triangle count: 4x the triangles is less "
                   "than 4x the time, at every step");
        JAH_TIMING_CHECK("meshbake.cards", underCeiling,
                   "no density takes seconds (the unbounded raster did, on the UI thread)");
    }
}


// ---------------------------------------------------------------------------
// 9. THE MEASURED BOUND (ATOM P1's AT-A5) — an arm of the `atom.lod_bound_bar` (archived: archive/tests/atom) suite
// (`--bound-bar` runs it after the bar, section 14).
//
// THE CLAIM UNDER TEST, in one sentence: for every level of every shipped mesh,
// no point of that level's surface lies further from level 0's surface than the
// length the bake stored — measured INDEPENDENTLY of the bake's own sampler.
//
// WHAT THIS IS, SAID ACCURATELY. It is a REGRESSION CHECK, not an independent
// derivation: `checkLodBounds` re-measures at 8x the area samples, at strata the bake
// never used, plus the SAME exact removed-vertex walk, under the same island caps and
// without the margin. Where the exact vertex term carries the maximum (the dragon,
// the endless plane, most levels since ATOM-LOD-BOUND-1 stopped multiplying that
// exact term by the sampling margin) the check reproduces the stored number and reads
// 1.0000; where the sampled term carries it, the ratio is the dense samples against
// 1.25x the bake's (0.80-0.90 on the shipped meshes), which is the margin being judged.
// `flat_grid_100m.obj` (the old 100 m ground plane, kept here as a test subject) reads ~0.20 because its bound is the precision floor. The physics bar —
// bound >= the dense reference AND <= 2x the simplifier's error — is
// atom.lod_bound_bar (archived: archive/tests/atom); `test_mesh_bake --bound-terms <file>` prints a chain's terms.
//
// WHAT IT ADDS TO THE BAR: the same dense re-measure through the BAKE's own route
// (`MeshBake::buildFromFile`, where the bar loads through testmesh::parseFile), one bound and
// one quadric per level, the bounds non-decreasing, and the ratio table.
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
        { QStringLiteral("tests/meshbake/fixtures/flat_grid_100m.obj") },
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
//
// A FRESH LIBRARY, seeded exactly as a launch seeds one (PrimitiveAssets::
// seedAll, which is what shell/mainwindow.cpp calls when it opens a library),
// and then the four products the design owes for every row: a LOD CHAIN of at
// least two levels for every mesh above 256 triangles, CARDS, an SDF, and a
// measured lodBound per level. Plus the half that is easy to lose: a SECOND
// seed of the same library bakes NOTHING.
static void primitivesBaked()
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

    PrimitiveAssets::clearCache();
    CHECK_LOUD(!PrimitiveAssets::allSeeded(&db), "a fresh library has seeded nothing");

    QStringList errors;
    QElapsedTimer timer;
    timer.start();
    const int created = PrimitiveAssets::seedAll(&db, &errors);
    const double seedMs = double(timer.nsecsElapsed()) / 1e6;
    for (const QString &line : errors) std::printf("info: seed error: %s\n", qUtf8Printable(line));
    CHECK_LOUD(errors.isEmpty(), "the seed reported no failure");
    CHECK_LOUD(created == primitives::all().size(),
               qUtf8Printable(QStringLiteral("every seed row was created (%1 of %2) in %3 ms")
                                  .arg(created).arg(primitives::all().size())
                                  .arg(seedMs, 0, 'f', 0)));
    CHECK_LOUD(PrimitiveAssets::allSeeded(&db), "…and allSeeded agrees");

    std::printf("      %-12s %7s %6s %6s %6s %-9s %s\n", "seed", "tris", "levels", "cards",
                "dag", "bound[1]", "guid");
    for (const primitives::Def &def : primitives::all()) {
        const QString name = QString::fromLatin1(def.name);
        const QString guid = QString::fromLatin1(def.guid);

        // THE ROW IS THE RESERVED ONE (a favourite, a tile's drop payload and
        // `assets.builtins` all name it).
        const AssetRecord row = db.fetchAsset(guid);
        CHECK(row.guid == guid || !row.name.isEmpty(),
              qUtf8Printable(name + ": its library row is the reserved guid " + guid));
        CHECK(QJsonDocument::fromJson(row.properties).object()
                  .value(QStringLiteral("type")).toString() == QStringLiteral("platform"),
              qUtf8Printable(name + ": the row is marked platform furniture"));

        const iris::MeshPtr mesh = PrimitiveAssets::mesh(name, &db);
        CHECK_LOUD(!mesh.isNull(),
                   qUtf8Printable(name + ": the baked asset resolves to a mesh"));
        if (!mesh) continue;

        const int tris = mesh->numFaces;
        const int levels = int(mesh->lodIndices.size()) + 1;
        std::printf("      %-12s %7d %6d %6d %6s %-9.6g %s\n", qUtf8Printable(name), tris, levels,
                    int(mesh->cards.size()), mesh->clusterDag.isEmpty() ? "no" : "yes",
                    mesh->lodBounds.isEmpty() ? 0.0 : double(mesh->lodBounds.first()),
                    qUtf8Printable(guid.right(4)));

        // THE CHAIN, for a mesh big enough to have one. The bake's own floor is
        // `kMinTriangles` x 2 = 512 (a level must be able to halve down to 128
        // and still shed 15 %), so the design's "above 256 triangles" is
        // asserted where the bake can honour it and the rest are reported.
        if (tris > 512)
            CHECK_LOUD(levels >= 2,
                       qUtf8Printable(QStringLiteral("%1: %2 triangles -> a chain of %3 levels")
                                          .arg(name).arg(tris).arg(levels)));
        // Every level's bound is a real length and the list is non-decreasing.
        CHECK(mesh->lodBounds.size() == mesh->lodIndices.size(),
              qUtf8Printable(name + ": one measured bound per level"));
        for (int L = 0; L < mesh->lodBounds.size(); ++L) {
            CHECK(mesh->lodBounds[L] > 0.0f,
                  qUtf8Printable(name + ": every bound is a positive length"));
            if (L > 0)
                CHECK(mesh->lodBounds[L] >= mesh->lodBounds[L - 1],
                      qUtf8Printable(name + ": the bounds are non-decreasing"));
        }
        // CARDS and the cluster DAG: every shipped mesh gets both (each refuses
        // only a skinned mesh, and none of these is skinned).
        CHECK_LOUD(!mesh->cards.isEmpty(), qUtf8Printable(name + ": it carries surface cards"));
        for (const iris::MeshCard &card : mesh->cards)
            CHECK(int(card.lodLevel) < levels,
                  qUtf8Printable(name + ": every card names a level the chain HAS"));
        CHECK_LOUD(!mesh->clusterDag.isEmpty(), qUtf8Printable(name + ": it carries a cluster DAG"));
    }

    // A SECOND SEED BAKES NOTHING. `seedAll` answers with how many rows it
    // CREATED, and the bake is re-checked per row against this build — so a zero
    // here is the whole idempotence claim, and it is also what a second launch
    // of the app on the same library does.
    PrimitiveAssets::clearCache();
    QStringList again;
    const int secondPass = PrimitiveAssets::seedAll(&db, &again);
    CHECK_LOUD(secondPass == 0 && again.isEmpty(),
               qUtf8Printable(QStringLiteral("a second seed of the same library creates nothing "
                                             "(%1 created, %2 error(s))")
                                  .arg(secondPass).arg(again.size())));

    AssetStorePaths::setRootOverride(QString());
    PrimitiveAssets::clearCache();
}

// ---------------------------------------------------------------------------
// 12. THE NEXT GENERATION WINS — `meshbake.rebake_generation` (archived: archive/tests/meshbake) (BAKE-RETIRE-1)
// ---------------------------------------------------------------------------
//
// A bake's file NAME is `<sourceOid16>-<settingsHash>.jmb`: it names the CONTENT
// and the SETTINGS and says NOTHING about the producer, because the producer is
// a fingerprint inside the file. `asset_files`' primary key is
// (asset_guid, role, name) and the store ingests with INSERT OR IGNORE — so
// before this lane, a re-bake after a `kFormatVersion` bump was SILENTLY DROPPED
// on any library that already had one: the stale oid stayed linked, the reader
// refused its fingerprint, and a baked asset with no parse to fall back on (every
// shipped primitive since ATOM P2) drew NOTHING, forever, while the seeder rebuilt
// it into an unlinked object on every boot. No suite saw it because every test
// home is fresh, which is exactly why this one plants the previous generation by
// hand.
static void rebakeGeneration()
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
    request.sourcePath = fixture(QStringLiteral("app/models/axis_cube.obj"));
    const ImportResult imported = service.import(request);
    CHECK_LOUD(imported.ok(), "the fixture imported through the ONE pipeline");
    if (!imported.ok()) {
        std::printf("info: import error: %s\n", qUtf8Printable(imported.error));
        AssetStorePaths::setRootOverride(QString());
        return;
    }

    const auto bakeRows = [&](const QString &guid) {
        QSqlQuery q(conn);
        q.prepare("SELECT oid, name FROM asset_files WHERE asset_guid = ? AND role = 'bake'");
        q.addBindValue(guid);
        QVector<QPair<QString, QString>> rows;
        if (q.exec()) while (q.next())
            rows.append({ q.value(0).toString(), q.value(1).toString() });
        return rows;
    };
    const auto refcountOf = [&](const QString &oid) {
        QSqlQuery q(conn);
        q.prepare("SELECT refcount FROM files WHERE oid = ?");
        q.addBindValue(oid);
        return (q.exec() && q.next()) ? q.value(0).toInt() : -1;
    };

    auto rows = bakeRows(imported.assetGuid);
    CHECK_LOUD(rows.size() == 1, "the import left exactly one bake row on the Object");
    if (rows.isEmpty()) { AssetStorePaths::setRootOverride(QString()); return; }
    const QString currentOid = rows.first().first;
    const QString bakeName = rows.first().second;
    const QString source = AssetCas::resolveSource(conn, storeRoot.path(), imported.assetGuid);
    CHECK_LOUD(!source.isEmpty(), "the source object resolves");

    // PLANT THE PREVIOUS GENERATION, UNDER EVERY OWNER. A real library holds it
    // that way — `recordBake` writes the bake under the Object AND the Mesh
    // member row — and it has to be both here, because `planFor` looks bake rows
    // up BY NAME ACROSS OWNERS (it walks every generation newest first and lets
    // the fingerprint decide): a fixture that spoiled only the Object row would
    // still be served the fresh bake through the mesh row's.
    //
    // Rewriting the row rather than the file is the honest fixture: the name is
    // generation-INDEPENDENT, which is the defect. The bytes are real so the
    // refcount trigger has something to count, and the name is swapped in two
    // statements — DELETE the current row FIRST, then rename the planted one, or
    // the rename collides with the very key it is taking over.
    const QString stalePath = QDir(home.path()).filePath(QStringLiteral("stale.jmb"));
    {
        QFile f(stalePath);
        CHECK_LOUD(f.open(QIODevice::WriteOnly), "the stale generation's bytes were written");
        f.write(QByteArray(4096, 'S'));
    }
    QString staleOid;
    QString casError;
    const QStringList owners{ imported.assetGuid, imported.meshGuid };
    for (const QString &owner : owners) {
        CHECK(AssetCas::ingestFile(conn, storeRoot.path(), stalePath, owner,
                                   QStringLiteral("bake"), QStringLiteral("stale-generation.jmb"),
                                   &staleOid, &casError),
              "the stale generation was ingested as a bake object");
        QSqlQuery drop(conn);
        drop.prepare("DELETE FROM asset_files WHERE asset_guid = ? AND role = 'bake' AND name = ?");
        drop.addBindValue(owner);
        drop.addBindValue(bakeName);
        CHECK(drop.exec(), "the current generation's row was removed");
        QSqlQuery rename(conn);
        rename.prepare("UPDATE asset_files SET name = ? WHERE asset_guid = ? AND role = 'bake' "
                       "AND name = ?");
        rename.addBindValue(bakeName);
        rename.addBindValue(owner);
        rename.addBindValue(QStringLiteral("stale-generation.jmb"));
        CHECK(rename.exec(), "…and the planted one took its key");
    }
    rows = bakeRows(imported.assetGuid);
    CHECK_LOUD(rows.size() == 1 && rows.first().first == staleOid
                   && rows.first().second == bakeName,
               "every owner now holds the PREVIOUS generation's bake under the current name");

    // "CANNOT READ" IS `load` ANSWERING NULL, not `isFresh` answering false:
    // freshness is a CATALOG question (is there a bake row under the name this
    // content and these settings imply) and the planted row answers it YES —
    // which is precisely why the silent drop was invisible. The fingerprint lives
    // in the FILE, and that is what refuses.
    MeshBakeStore::clear();
    CHECK_LOUD(!MeshBakeStore::load(source, imported.assetGuid),
               "…and this build cannot READ that bake (exactly a format bump)");

    // THE RE-BAKE MUST SUPERSEDE IT.
    QString bakeError;
    CHECK_LOUD(MeshBakeStore::bakeSource(conn, storeRoot.path(), source, &bakeError,
                                        imported.assetGuid),
               "the re-bake ran");
    if (!bakeError.isEmpty()) std::printf("info: bake error: %s\n", qUtf8Printable(bakeError));

    for (const QString &owner : owners) {
        rows = bakeRows(owner);
        for (const auto &r : rows)
            std::printf("      after the re-bake: %s | %s | %s\n", qUtf8Printable(owner.left(8)),
                        qUtf8Printable(r.first.left(12)), qUtf8Printable(r.second));
        CHECK_LOUD(rows.size() == 1,
                   qUtf8Printable(QStringLiteral("ONE bake row survives the re-bake on %1 (got %2)")
                                      .arg(owner.left(8)).arg(rows.size())));
        CHECK_LOUD(!rows.isEmpty() && rows.first().first != staleOid,
                   "…and it is the NEW generation's object, not the stale one");
    }
    CHECK_LOUD(MeshBakeStore::isFresh(conn, storeRoot.path(), source, imported.assetGuid),
               "…so this build can read the bake again");

    // AND THE SUPERSEDED OBJECT IS REAPABLE: nothing links it any more, which is
    // what hands it to `assets.gc` (the refcount triggers are AFTER INSERT and
    // AFTER DELETE on asset_files — an UPDATE would have left it inflated).
    QSqlQuery stillLinked(conn);
    stillLinked.prepare("SELECT COUNT(*) FROM asset_files WHERE oid = ?");
    stillLinked.addBindValue(staleOid);
    int links = -1;
    if (stillLinked.exec() && stillLinked.next()) links = stillLinked.value(0).toInt();
    CHECK_LOUD(links == 0, qUtf8Printable(QStringLiteral("no asset_files row links the superseded "
                                                         "object any more (got %1)").arg(links)));
    CHECK_LOUD(refcountOf(staleOid) <= 0,
               qUtf8Printable(QStringLiteral("…and its refcount is back to zero for gc (got %1)")
                                  .arg(refcountOf(staleOid))));

    AssetStorePaths::setRootOverride(QString());
}

// ---------------------------------------------------------------------------
// 13. THE HEMISPHERE'S CAP IS WOUND THE RIGHT WAY — `meshbake.hemisphere_winding` (archived: archive/tests/meshbake)
//     (HEMISPHERE-CAP-1)
// ---------------------------------------------------------------------------
//
// The cap's 32 faces were wound so their geometric normal pointed +Y while every
// one of their vertex normals declared (0, -1, 0): the bottom of the hemisphere
// faced INTO the object. Nothing in the bake fixes a wound-backwards face — the
// card generator's coverage raster, the SDF's angle-weighted pseudonormal and any
// back-face cull all read the winding — so the fix is the FILE, and this is the
// check that keeps it fixed. It is a property of every shipped mesh, so every one
// of them is measured, not just the hemisphere.
static void windingAgreesWithNormals()
{
    for (const primitives::Def &def : primitives::all()) {
        const QString name = QString::fromLatin1(def.name);
        // The seed key is a Qt RESOURCE and this binary links no .qrc, so the
        // same file is read from the source tree: ":/content/primitives/x.obj"
        // ships as "app/content/primitives/x.obj" — one mapping, the one the
        // app folder IS.
        const QString seed = QString::fromLatin1(def.mesh);
        const QString path = seed.startsWith(QLatin1Char(':'))
                                 ? fixture(QStringLiteral("app") + seed.mid(1))
                                 : fixture(seed);
        const iris::MeshPtr mesh = testmesh::load(path, QString());
        CHECK_LOUD(!mesh.isNull(), qUtf8Printable(name + ": the shipped mesh parses"));
        if (!mesh) continue;

        // Positions and normals out of the vertex buffers, then the geometric
        // normal of every triangle against the average of its three declared
        // vertex normals. A face whose winding disagrees with its own normals is
        // a face the renderer and the bake will read two different ways.
        QVector<iris::Vec3> pos, nrm;
        for (const iris::VertexBufferPtr &vb : mesh->getVertexBuffers()) {
            const auto &attribs = vb->vertexLayout.getAttribs();
            if (attribs.size() != 1) continue;
            const int count = vb->dataSize / int(sizeof(float) * 3);
            const float *f = reinterpret_cast<const float *>(vb->data);
            if (attribs[0].usage == iris::VertexAttribUsage::Position) {
                pos.resize(count);
                for (int i = 0; i < count; ++i) pos[i] = iris::Vec3(f[i * 3], f[i * 3 + 1], f[i * 3 + 2]);
            } else if (attribs[0].usage == iris::VertexAttribUsage::Normal) {
                nrm.resize(count);
                for (int i = 0; i < count; ++i) nrm[i] = iris::Vec3(f[i * 3], f[i * 3 + 1], f[i * 3 + 2]);
            }
        }
        if (pos.isEmpty() || nrm.size() != pos.size()) {
            std::printf("info: %s: no position/normal pair to measure\n", qUtf8Printable(name));
            continue;
        }
        const iris::IndexBufferPtr ib = mesh->getIndexBuffer();
        if (!ib || ib->dataSize <= 0) {
            std::printf("info: %s: no index buffer\n", qUtf8Printable(name));
            continue;
        }
        const quint32 *idx = reinterpret_cast<const quint32 *>(ib->data);
        const int tris = ib->dataSize / int(sizeof(quint32) * 3);
        int disagreeing = 0;
        for (int t = 0; t < tris; ++t) {
            const quint32 a = idx[t * 3], b = idx[t * 3 + 1], c = idx[t * 3 + 2];
            if (int(a) >= pos.size() || int(b) >= pos.size() || int(c) >= pos.size()) continue;
            const iris::Vec3 geo = iris::Vec3::crossProduct(pos[b] - pos[a], pos[c] - pos[a]);
            const iris::Vec3 declared = nrm[a] + nrm[b] + nrm[c];
            if (geo.lengthSquared() < 1e-20f || declared.lengthSquared() < 1e-12f) continue;
            if (iris::Vec3::dotProduct(geo, declared) <= 0.0f) ++disagreeing;
        }
        CHECK_LOUD(disagreeing == 0,
                   qUtf8Printable(QStringLiteral("%1: %2 of %3 faces wound against their own "
                                                 "declared normal")
                                      .arg(name).arg(disagreeing).arg(tris)));
    }
}

// ---------------------------------------------------------------------------
// 14. THE LOD BOUND'S PHYSICS BAR (ATOM-LOD-BOUND-1) — the `atom.lod_bound_bar` (archived: archive/tests/atom)
// suite, and the scan stand-in `atom.lod_switch` (archived: archive/tests/atom) imports.
//
// THE DEFECT IT GUARDS. A scan's debris islands (1-6 triangle components floating
// round the surface) were collapsed away by the simplifier and then measured
// against the nearest SURVIVING surface metres off, so the owner's temple stored a
// level-1 bound 100-115x its simplifier error and never left level 0 inside 1 km.
// The bake now measures REPRESENTED surface (a dropped island costs its own
// extent) and locks what would be displaced past twice the level's own error
// (irisgl/import/meshbake.cpp, `Islands` and the displacement lock).
//
// THE BAR, per level of every chained mesh:
//   (a) HONEST: the stored bound is at least a dense reference of the two-sided
//       distance (MeshBake::checkLodBounds at 8x the samples, exact vertex walk, no
//       margin) — on a one-component mesh that reference IS the sampled Hausdorff;
//   (b) TIGHT: the stored bound is at most twice the simplifier's own error (or the
//       precision floor, where the level is exact);
//   (c) on the stand-in, every island a level dropped is no bigger than the level's
//       bound (the cap is the island's size, never more), and level 1 is reached by
//       150 m on a 1080-line, 45-degree view — the owner's bar for the temple.

namespace standin {

/// A 60 m scanned-ground stand-in: a gently bumped floor grid, eight columns and
/// 600 single-triangle debris islands floating 0.5-5 m above it — the owner's
/// scan's shape (one big surface, a few big parts, hundreds of islands) at a
/// fixture's size. Deterministic (a fixed LCG); written as a positions-only OBJ.
inline float floorHeight(float x, float z)
{
    return 0.15f * std::sin(x * 0.7f) * std::cos(z * 0.9f) + 0.05f * std::sin(x * 2.3f + z * 1.7f);
}

inline bool write(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    QByteArray out;
    int base = 1;
    const auto v = [&](float x, float y, float z) {
        out += QByteArray("v ") + QByteArray::number(x, 'f', 5) + ' ' + QByteArray::number(y, 'f', 5)
               + ' ' + QByteArray::number(z, 'f', 5) + '\n';
    };
    const auto face = [&](int a, int b, int c) {
        out += QByteArray("f ") + QByteArray::number(a) + ' ' + QByteArray::number(b) + ' '
               + QByteArray::number(c) + '\n';
    };
    out += "# ATOM-LOD-BOUND-1 scan stand-in (tests/meshbake/test_mesh_bake.cpp standin::write)\n";
    // The floor: 100 x 100 cells over 60 m, facing +Y.
    const int N = 100;
    const float half = 30.0f, cell = 2.0f * half / float(N);
    for (int j = 0; j <= N; ++j)
        for (int i = 0; i <= N; ++i) {
            const float x = -half + cell * float(i), z = -half + cell * float(j);
            v(x, floorHeight(x, z), z);
        }
    for (int j = 0; j < N; ++j)
        for (int i = 0; i < N; ++i) {
            const int a = base + j * (N + 1) + i, b = a + 1, c = a + (N + 1), d = c + 1;
            face(a, c, b);
            face(b, c, d);
        }
    base += (N + 1) * (N + 1);
    // Eight columns: open cylinders r 0.9 m, 8 m tall, on an 18 m ring.
    const int segs = 24, rings = 12;
    for (int k = 0; k < 8; ++k) {
        const float cx = 18.0f * std::cos(float(k) * 0.785398f), cz = 18.0f * std::sin(float(k) * 0.785398f);
        const float y0 = floorHeight(cx, cz);
        for (int r = 0; r <= rings; ++r)
            for (int s = 0; s < segs; ++s) {
                const float t = float(s) * 6.2831853f / float(segs);
                v(cx + 0.9f * std::cos(t), y0 + 8.0f * float(r) / float(rings), cz + 0.9f * std::sin(t));
            }
        for (int r = 0; r < rings; ++r)
            for (int s = 0; s < segs; ++s) {
                const int a = base + r * segs + s, b = base + r * segs + (s + 1) % segs;
                const int c = a + segs, d = b + segs;
                face(a, c, b);
                face(b, c, d);
            }
        base += (rings + 1) * segs;
    }
    // 600 debris islands: one triangle each, 0.1-0.6 m, 0.5-5 m above the floor.
    quint32 seed = 0x9E3779B9u;
    const auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return float(seed >> 8) / 16777216.0f; };
    for (int k = 0; k < 600; ++k) {
        const float x = -28.0f + 56.0f * rnd(), z = -28.0f + 56.0f * rnd();
        const float y = floorHeight(x, z) + 0.5f + 4.5f * rnd();
        const float s = 0.1f + 0.5f * rnd();
        for (int c = 0; c < 3; ++c) {
            float dx = rnd() - 0.5f, dy = rnd() - 0.5f, dz = rnd() - 0.5f;
            const float l = std::sqrt(dx * dx + dy * dy + dz * dz) + 1e-6f;
            v(x + s * dx / l, y + s * dy / l, z + s * dz / l);
        }
        face(base, base + 1, base + 2);
        base += 3;
    }
    return f.write(out) == out.size();
}

}   // namespace standin

/// The largest axis extent of a mesh's positions (meshoptimizer's simplifyScale:
/// what the bake's precision floor is a fraction of) and its box half-diagonal
/// (the radius the GPU level rule subtracts from the view distance).
static void meshExtents(const iris::MeshPtr &mesh, float *maxAxis, float *halfDiagonal)
{
    iris::Vec3 lo(FLT_MAX, FLT_MAX, FLT_MAX), hi(-FLT_MAX, -FLT_MAX, -FLT_MAX);
    for (const iris::VertexBufferPtr &vb : mesh->getVertexBuffers()) {
        const auto &attribs = vb->vertexLayout.getAttribs();
        if (attribs.isEmpty() || attribs[0].usage != iris::VertexAttribUsage::Position) continue;
        const int comps = attribs[0].count > 0 ? attribs[0].count : 3;
        const int count = vb->dataSize / int(sizeof(float) * comps);
        const float *p = reinterpret_cast<const float *>(vb->data);
        for (int i = 0; i < count; ++i) {
            const iris::Vec3 q(p[i * comps], p[i * comps + 1], p[i * comps + 2]);
            lo = iris::Vec3(std::min(lo.x(), q.x()), std::min(lo.y(), q.y()), std::min(lo.z(), q.z()));
            hi = iris::Vec3(std::max(hi.x(), q.x()), std::max(hi.y(), q.y()), std::max(hi.z(), q.z()));
        }
        break;
    }
    const iris::Vec3 size = hi - lo;
    if (maxAxis) *maxAxis = std::max(std::max(size.x(), size.y()), size.z());
    if (halfDiagonal) *halfDiagonal = 0.5f * size.length();
}

/// The view distance at which a level whose bound is `bound` becomes affordable at
/// one pixel on a `lines`-line view with a `fovDeg` vertical field of view: the GPU
/// rule (JahCullTest_cs.glsl) affords bound <= (d - radius) * 2 / (P * lines).
static float switchDistance(float bound, float radius, float fovDeg = 45.0f, float lines = 1080.0f)
{
    const float P = 1.0f / std::tan(0.5f * fovDeg * 3.14159265f / 180.0f);
    return bound * P * lines * 0.5f + radius;
}

/// Prints one mesh's chain as the bound's terms (the test-time window that
/// replaced the JAH_BAKE_BOUND_TERMS latch).
static void printTerms(const QString &label, const QVector<iris::MeshBake::LodLevelTerms> &terms,
                       float radius)
{
    std::printf("      %-22s %5s %7s %9s %9s %9s %9s %9s %5s %6s %5s %4s %8s %8s\n", "mesh", "level",
                "tris", "quadric", "area", "vertex", "facet", "bound", "x", "drop", "lock", "pass",
                "dropExt", "L@1080");
    for (int i = 0; i < terms.size(); ++i) {
        const auto &t = terms.at(i);
        std::printf("      %-22s %5d %7d %9.5f %9.5f %9.5f %9.5f %9.5f %5.2f %6d %5d %4d %8.4f %7.0fm\n",
                    qUtf8Printable(label), i + 1, t.triangles, double(t.quadric), double(t.areaTerm),
                    double(t.vertexTerm), double(t.facetTerm), double(t.bound),
                    t.quadric > 0.0f ? double(t.bound / t.quadric) : 0.0, t.islandsDropped,
                    t.verticesLocked, t.passes, double(t.droppedMaxExtent),
                    double(switchDistance(t.bound, radius)));
    }
}

/// `--bound-terms <model>`: the chain of every mesh of any model file, as terms.
/// The tool the lead re-runs on a file nobody can ship (the owner's scan).
static int boundTerms(const QString &path)
{
    const QList<iris::MeshPtr> meshes = testmesh::parseFile(path);
    if (meshes.isEmpty()) { std::printf("FAIL: %s parses\n", qUtf8Printable(path)); return 1; }
    for (int i = 0; i < meshes.size(); ++i) {
        QVector<iris::MeshBake::LodLevelTerms> terms;
        QElapsedTimer t; t.start();
        iris::MeshBake::buildLodChain(meshes.at(i), &terms);
        float radius = 0.0f;
        meshExtents(meshes.at(i), nullptr, &radius);
        printTerms(QStringLiteral("%1#%2").arg(QFileInfo(path).fileName()).arg(i), terms, radius);
        std::printf("      (chain %lld ms, radius %.2f m)\n", qint64(t.elapsed()), double(radius));
    }
    return 0;
}

// ---- bake.dag_reaches_root (ATOM-6 stage 2c) ---------------------------------------------------
//
// THE CHAIN REACHES ITS ROOT ON CONTENT MADE OF SMALL PIECES. An edge collapse never removes a connected
// component, so before ATOM-6 a group of leaf cards or closed boxes could not halve: it was terminal at depth 0
// (or fell to the sloppy fallback) and the chain ended there (the Bistro's cypresses at 6,656 triangles). With
// the bake's own prune (clusterlod patch 0004: the group's unlocked components removed whole, smallest island
// first, charged their full extent) a CARD SOUP (2,000 quads in a 2 m sphere) and a KIT-BASH (200 closed
// 12-triangle boxes on a grid) bake to DAGs whose coarsest cut is EMPTY or one cluster (<= 128 triangles), whose
// stored errors are monotone up the DAG and under kMaxRelBound x the extent, and in which no group is terminal
// for lack of a target (only the root may be). RED at the base: terminal at depth 0, the coarsest in the
// thousands. The area per level is printed (the coverage, parked for the area-preserving item).
namespace dagroot {

iris::MeshPtr meshFromArrays(const std::vector<float> &positions, const std::vector<float> &normals,
                             const std::vector<float> &uvs, const std::vector<unsigned> &indices)
{
    iris::MeshPtr m = iris::Mesh::create();
    const auto add = [&](iris::VertexAttribUsage usage, const std::vector<float> &v, int comps) {
        iris::VertexLayout layout;
        layout.addAttrib(usage, iris::AttribTypeFloat, comps, comps * int(sizeof(float)));
        auto vb = iris::VertexBuffer::create(layout);
        vb->setData(const_cast<float *>(v.data()), unsigned(v.size() * sizeof(float)));
        m->addVertexBuffer(vb);
    };
    add(iris::VertexAttribUsage::Position, positions, 3);
    add(iris::VertexAttribUsage::Normal, normals, 3);
    add(iris::VertexAttribUsage::TexCoord0, uvs, 2);
    auto ib = iris::IndexBuffer::create();
    ib->setData(const_cast<unsigned *>(indices.data()), unsigned(indices.size() * sizeof(unsigned)));
    m->setIndexBuffer(ib);
    m->usesIndexBuffer = true;
    m->numVerts = int(positions.size() / 3);
    m->numFaces = int(indices.size() / 3);
    m->primitiveMode = iris::PrimitiveMode::Triangles;
    return m;
}

/// 2,000 two-triangle cards, 10 cm, scattered and turned inside a sphere of radius 1 m (deterministic).
iris::MeshPtr cardSoup()
{
    std::vector<float> p, n, uv;
    std::vector<unsigned> idx;
    unsigned seed = 2024u;
    const auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return float(seed >> 8) / float(1u << 24); };
    for (int c = 0; c < 2000; ++c) {
        float cx, cy, cz;
        do { cx = rnd() * 2.0f - 1.0f; cy = rnd() * 2.0f - 1.0f; cz = rnd() * 2.0f - 1.0f; }
        while (cx * cx + cy * cy + cz * cz > 1.0f);
        // two orthonormal in-plane axes from a random normal
        float nx = rnd() * 2.0f - 1.0f, ny = rnd() * 2.0f - 1.0f, nz = rnd() * 2.0f - 1.0f;
        float nl = std::sqrt(nx * nx + ny * ny + nz * nz); if (nl < 1e-3f) { nx = 0; ny = 1; nz = 0; nl = 1; }
        nx /= nl; ny /= nl; nz /= nl;
        float ax = std::fabs(nx) < 0.9f ? 1.0f : 0.0f, ay = ax > 0.5f ? 0.0f : 1.0f, az = 0.0f;
        float ux = ay * nz - az * ny, uy = az * nx - ax * nz, uz = ax * ny - ay * nx;
        const float ul = std::sqrt(ux * ux + uy * uy + uz * uz); ux /= ul; uy /= ul; uz /= ul;
        const float vx = ny * uz - nz * uy, vy = nz * ux - nx * uz, vz = nx * uy - ny * ux;
        const float h = 0.05f;
        const unsigned b = unsigned(p.size() / 3);
        const float corners[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
        for (const auto &k : corners) {
            p.insert(p.end(), { cx + h * (k[0] * ux + k[1] * vx), cy + h * (k[0] * uy + k[1] * vy), cz + h * (k[0] * uz + k[1] * vz) });
            n.insert(n.end(), { nx, ny, nz });
            uv.insert(uv.end(), { k[0] * 0.5f + 0.5f, k[1] * 0.5f + 0.5f });
        }
        idx.insert(idx.end(), { b, b + 1, b + 2, b, b + 2, b + 3 });
    }
    return meshFromArrays(p, n, uv, idx);
}

/// 200 closed boxes (12 triangles each, 24 vertices: a face's own normal), 10 cm, on a 20 x 10 grid 30 cm apart.
iris::MeshPtr kitBash()
{
    std::vector<float> p, n, uv;
    std::vector<unsigned> idx;
    const float h = 0.05f;
    const int faces[6][3] = { { 0, 0, 1 }, { 0, 0, -1 }, { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 } };
    for (int gy = 0; gy < 10; ++gy)
        for (int gx = 0; gx < 20; ++gx) {
            const float c[3] = { gx * 0.3f, gy * 0.3f, 0.0f };
            for (const auto &f : faces) {
                // the face's two in-plane axes
                int a = f[0] ? 1 : 0, bAxis = f[2] ? 1 : 2;
                if (f[1]) { a = 0; bAxis = 2; }
                const unsigned base = unsigned(p.size() / 3);
                const float corners[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
                for (const auto &k : corners) {
                    float v[3] = { c[0] + h * float(f[0]), c[1] + h * float(f[1]), c[2] + h * float(f[2]) };
                    v[a] += h * k[0];
                    v[bAxis] += h * k[1];
                    p.insert(p.end(), { v[0], v[1], v[2] });
                    n.insert(n.end(), { float(f[0]), float(f[1]), float(f[2]) });
                    uv.insert(uv.end(), { k[0] * 0.5f + 0.5f, k[1] * 0.5f + 0.5f });
                }
                // winding is irrelevant to the DAG; two triangles per face
                idx.insert(idx.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
            }
        }
    return meshFromArrays(p, n, uv, idx);
}

/// A rough sheet (ATOM-7 stage 2): a 96 x 96 grid of 5 cm cells, random diagonals, a height field of random 1-30 cm
/// steps, every cell faceted (its own normal: an attribute seam on every cell edge, the flat-shaded kit-bash) and a UV
/// chart per 8 x 8 block — one welded component with long group borders and many sliver fans — deterministic.
iris::MeshPtr roughSheet()
{
    std::vector<float> p, n, uv;
    std::vector<unsigned> idx;
    unsigned seed = 4242u;
    const auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return float(seed >> 8) / float(1u << 24); };
    const int N = 96;
    std::vector<float> h((N + 1) * (N + 1));
    for (float &z : h) z = 0.01f + 0.29f * rnd();
    // per cell its own four corners (a UV chart per 8 x 8 block: twins along the block lines)
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) {
            const unsigned base = unsigned(p.size() / 3);
            const int cx[4] = { x, x + 1, x + 1, x }, cy[4] = { y, y, y + 1, y + 1 };
            for (int k = 0; k < 4; ++k) {
                p.insert(p.end(), { cx[k] * 0.05f, cy[k] * 0.05f, h[size_t(cy[k] * (N + 1) + cx[k])] });
                // FACETED: every cell its own normal (a flat-shaded kit-bash: an attribute seam on every cell edge)
                const float sx = h[size_t(y * (N + 1) + x + 1)] - h[size_t(y * (N + 1) + x)];
                const float sy = h[size_t((y + 1) * (N + 1) + x)] - h[size_t(y * (N + 1) + x)];
                const float nl = std::sqrt(sx * sx + sy * sy + 0.0025f);
                n.insert(n.end(), { -sx / nl, -sy / nl, 0.05f / nl });
                const float u = float(cx[k] - (x / 8) * 8) / 8.0f + float(x / 8), v = float(cy[k] - (y / 8) * 8) / 8.0f;
                uv.insert(uv.end(), { u, v });
            }
            if (rnd() < 0.5f) idx.insert(idx.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
            else              idx.insert(idx.end(), { base, base + 1, base + 3, base + 1, base + 2, base + 3 });
        }
    return meshFromArrays(p, n, uv, idx);
}

}   // namespace dagroot

// ---- THE CUT CURVE (ATOM-UPGRADE stage 0; SPECS/ATOM_UPGRADE_SPEC.md) --------------------------------------------
//
// THE INSTRUMENT THE UPGRADE IS JUDGED BY: the triangles a cut of a mesh's DAG draws when every group whose stored
// error is under E is affordable — the runtime's own rule (JahClusterCut.glsl: a cluster is drawn iff its own group is
// NOT affordable and it is a leaf or its producer IS), with no frustum and no occlusion, i.e. what the screen pays at
// the distance where E mesh units project to the tier's pixel (A §2c: E = 16 at 1 px of 1080 rows = 334 m on Bistro's
// 0.016 node scale). Three arms: SHIPPED (the bake as it writes), POSONLY (the same bake on a copy of the mesh with
// its positions alone: no attributes for the simplifier, clusterlod's estimate the position quadric) and STOCK
// (upstream clusterlod unpatched, positions only, its quadric stored — a reference, never a product path:
// cutcurve_stock.cpp). "top" = the cut once every finite group is affordable: 0 when the chain ends in an empty
// group, else its terminal clusters (a DAG-less mesh draws level 0 at every E).
namespace cutcurve
{

using Dag = StockDag;

Dag fromMesh(const iris::MeshClusterDag &dag)
{
    Dag d;
    for (const auto &g : dag.groups) { d.groupError.push_back(g.error); d.groupDepth.push_back(g.depth); }
    for (const auto &c : dag.clusters) {
        d.clusterGroup.push_back(c.group);
        d.clusterRefined.push_back(c.refined);
        d.clusterTriangles.push_back(int(c.triangleCount));
    }
    return d;
}

/// The triangles drawn when a group is affordable iff `afford(error)`.
template <typename F> long long cut(const Dag &d, F afford)
{
    long long tris = 0;
    for (size_t c = 0; c < d.clusterGroup.size(); ++c) {
        const int g = d.clusterGroup[c], r = d.clusterRefined[c];
        if (g < 0 || size_t(g) >= d.groupError.size() || afford(d.groupError[size_t(g)])) continue;
        if (r < 0 || (size_t(r) < d.groupError.size() && afford(d.groupError[size_t(r)]))) tris += d.clusterTriangles[c];
    }
    return tris;
}
long long cutAt(const Dag &d, float E) { return cut(d, [E](float e) { return e < E; }); }
long long cutTop(const Dag &d) { return cut(d, [](float e) { return e != FLT_MAX; }); }

const float *positionsOf(const iris::MeshPtr &m, size_t *nv, int *comps)
{
    for (const iris::VertexBufferPtr &vb : m->getVertexBuffers()) {
        if (!vb || !vb->data || vb->dataSize <= 0) continue;
        const QList<iris::VertexAttribute> a = vb->vertexLayout.getAttribs();
        if (a.isEmpty() || a.first().usage != iris::VertexAttribUsage::Position) continue;
        if (a.size() != 1 || a.first().count < 3) return nullptr;   // interleaved: not a layout the bake reads
        *comps = a.first().count;
        *nv = size_t(vb->dataSize) / (sizeof(float) * size_t(*comps));
        return reinterpret_cast<const float *>(vb->data);
    }
    return nullptr;
}

/// The POSONLY arm's subject: the mesh's positions and indices, nothing else.
iris::MeshPtr positionsOnly(const iris::MeshPtr &src)
{
    size_t nv = 0;
    int pc = 3;
    const float *p = positionsOf(src, &nv, &pc);
    const iris::IndexBufferPtr sib = src->getIndexBuffer();
    if (!p || sib.isNull() || !sib->data) return iris::MeshPtr();
    std::vector<float> xyz(nv * 3);
    for (size_t v = 0; v < nv; ++v) for (int k = 0; k < 3; ++k) xyz[v * 3 + size_t(k)] = p[v * size_t(pc) + size_t(k)];
    iris::MeshPtr m = iris::Mesh::create();
    iris::VertexLayout layout;
    layout.addAttrib(iris::VertexAttribUsage::Position, iris::AttribTypeFloat, 3, 12);
    auto vb = iris::VertexBuffer::create(layout);
    vb->setData(xyz.data(), unsigned(xyz.size() * sizeof(float)));
    m->addVertexBuffer(vb);
    auto ib = iris::IndexBuffer::create();
    ib->setData(sib->data, unsigned(sib->dataSize));
    m->setIndexBuffer(ib);
    m->usesIndexBuffer = true;
    m->numVerts = int(nv);
    m->numFaces = int(size_t(sib->dataSize) / 12u);
    m->primitiveMode = iris::PrimitiveMode::Triangles;
    return m;
}

enum class Arm { Shipped, PosOnly, Stock };

/// One mesh through one arm. `st` (shipped and posonly) carries the bake's report (with its per-group records).
Dag build(const iris::MeshPtr &mesh, Arm arm, iris::MeshBake::ClusterDagStats *st, double *ms)
{
    QElapsedTimer t; t.start();
    Dag d;
    if (arm == Arm::Stock) {
        size_t nv = 0; int pc = 3;
        const float *p = positionsOf(mesh, &nv, &pc);
        const iris::IndexBufferPtr ib = mesh->getIndexBuffer();
        const size_t ni = (!ib.isNull() && ib->data) ? size_t(ib->dataSize) / sizeof(unsigned) : 0u;
        // the bake's own floor: a mesh under two leaves gets no DAG in any arm (draws level 0)
        if (p && ni / 3 >= 256) stockBuild(p, nv, size_t(pc) * sizeof(float), reinterpret_cast<const unsigned *>(ib->data), ni, d);
    } else {
        const iris::MeshPtr subject = arm == Arm::PosOnly ? positionsOnly(mesh) : mesh;
        if (!subject.isNull()) {
            iris::MeshBake::ClusterDagOptions opt;
            opt.wantGroups = true;
            iris::MeshBake::buildClusterDag(subject, st, opt);
            d = fromMesh(subject->clusterDag);
        }
    }
    *ms = double(t.elapsed());
    return d;
}

/// The E ladder of the verb, in mesh units (A §2c's columns).
const float kLadder[] = { 2.0f, 4.0f, 8.0f, 16.0f, 32.0f, 64.0f, 128.0f };

/// 30 OVERLAPPING BOXES WITH HARD UV SEAMS (the spec's kit-bashed box set): 12 triangles each, every face its own
/// four vertices, its own normal and its own UV chart (a seam on every edge), sizes 0.2-1 m, centres inside a 1.5 m
/// cube so the boxes interpenetrate like kit-bashed facade pieces; deterministic. 360 triangles: one DAG group of
/// three leaves at depth 0.
iris::MeshPtr kitBashedBoxSet()
{
    std::vector<float> p, n, uv;
    std::vector<unsigned> idx;
    unsigned seed = 3030u;
    const auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return float(seed >> 8) / float(1u << 24); };
    const int faces[6][3] = { { 0, 0, 1 }, { 0, 0, -1 }, { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 } };
    for (int b = 0; b < 30; ++b) {
        const float c[3] = { 1.5f * rnd(), 1.5f * rnd(), 1.5f * rnd() };
        const float h[3] = { 0.1f + 0.4f * rnd(), 0.1f + 0.4f * rnd(), 0.1f + 0.4f * rnd() };
        for (int f = 0; f < 6; ++f) {
            const int *fa = faces[f];
            const int axis = fa[0] ? 0 : (fa[1] ? 1 : 2);
            const int a = (axis + 1) % 3, bb = (axis + 2) % 3;
            const unsigned base = unsigned(p.size() / 3);
            const float corners[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
            const float chartU = float((b * 6 + f) % 16), chartV = float((b * 6 + f) / 16);
            for (const auto &k : corners) {
                float v[3] = { c[0], c[1], c[2] };
                v[axis] += h[axis] * float(fa[axis]);
                v[a] += h[a] * k[0];
                v[bb] += h[bb] * k[1];
                p.insert(p.end(), { v[0], v[1], v[2] });
                n.insert(n.end(), { float(fa[0]), float(fa[1]), float(fa[2]) });
                uv.insert(uv.end(), { chartU + 0.5f + 0.45f * k[0], chartV + 0.5f + 0.45f * k[1] });
            }
            idx.insert(idx.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
        }
    }
    return dagroot::meshFromArrays(p, n, uv, idx);
}

}   // namespace cutcurve

#include "atom2_rows.h"

/// `--dag-reaches-root` (the SHIP row bake.dag_reaches_root, rewritten to ATOM-2's builder; its law stands): the card
/// soup (2,000 quads in a 2 m sphere) and the kit-bash (200 closed 12-triangle boxes) bake to DAGs that END — the top
/// cut draws nothing (an empty top over a non-empty root), the stored errors are monotone up the DAG, no group carries
/// an infinite error, and the builder never hit its depth cap.
static int dagReachesRoot()
{
    struct Case { const char *name; iris::MeshPtr mesh; };
    const Case cases[] = { { "card soup (2,000 quads, 4,000 tris)", dagroot::cardSoup() },
                           { "kit-bash (200 closed boxes, 2,400 tris)", dagroot::kitBash() } };
    for (const Case &cs : cases) {
        iris::MeshBake::ClusterDagStats st;
        QElapsedTimer t; t.start();
        iris::MeshBake::buildClusterDag(cs.mesh, &st);
        const iris::MeshClusterDag &dag = cs.mesh->clusterDag;
        std::printf("== %s: %d clusters, %d groups, depth %d, %d levels, %lld ms; %d reductions, %d carried, %d retries, "
                    "%d pieces pruned; root %d tris, top error %.4g\n",
                    cs.name, int(dag.clusters.size()), int(dag.groups.size()), st.depth, st.levels, qint64(t.elapsed()),
                    st.reductions, st.carried, st.retries, st.prunedPieces, st.rootTriangles, double(st.rootError));
        CHECK_LOUD(!dag.isEmpty(), "the mesh has a DAG");
        if (dag.isEmpty()) continue;
        bool monotone = true, finite = true;
        for (const auto &c : dag.clusters)
            if (c.refined >= 0 && dag.groups.at(c.group).error < dag.groups.at(c.refined).error) monotone = false;
        for (const auto &g : dag.groups) if (!(g.error < FLT_MAX)) finite = false;
        const long long top = cutcurve::cutTop(cutcurve::fromMesh(dag));
        char msg[200];
        std::snprintf(msg, sizeof(msg), "%s: the top cut draws nothing (%lld)", cs.name, top);
        CHECK_LOUD(top == 0, msg);
        std::snprintf(msg, sizeof(msg), "%s: the root is non-empty (%d triangles)", cs.name, st.rootTriangles);
        CHECK_LOUD(st.rootTriangles > 0, msg);
        std::snprintf(msg, sizeof(msg), "%s: the stored errors are monotone up the DAG", cs.name);
        CHECK_LOUD(monotone, msg);
        std::snprintf(msg, sizeof(msg), "%s: no group is terminal (every error finite)", cs.name);
        CHECK_LOUD(finite, msg);
        std::snprintf(msg, sizeof(msg), "%s: the chain stayed under the depth cap", cs.name);
        CHECK_LOUD(!st.depthCapHit, msg);
    }
    return atom2::finish();
}

/// `--locked-positions-kept` (the SHIP row bake.locked_positions_kept): A LOCKED POSITION SURVIVES — the card soup,
/// the kit-bash and the rough sheet bake with no group losing a position its level locked, recomputed from the DAG
/// itself (a group's external positions: its members' edges twinned by a pool cluster outside it).
static int lockedPositionsKept()
{
    return atom2::lockedPositionsKeptLaw({ { "card soup (2,000 quads)", dagroot::cardSoup() },
                                           { "kit-bash (200 closed boxes)", dagroot::kitBash() },
                                           { "rough sheet (96 x 96, UV charts)", dagroot::roughSheet() } });
}

/// `--dag-levels <model> <out.tsv> [threads] [--mesh i,j]`: THE DAG PER MESH AND PER LEVEL (the lead's measuring tool,
/// rewritten for ATOM-2's builder): every mesh of the file through the bake's own DAG build; per level the groups
/// formed (reductions / not simplified / carried / the empty top), the level-d surface's triangles (the uniform cut
/// at d), the stored errors (max, median), the worst step against its budget, the retries and the pieces pruned; per
/// mesh the root's triangles and the top's error; for the file the totals, the level-to-level growth of the stored
/// maximum (the empty top excluded: it is a vanishing, not a level), the depth-0 estimate against the measured step,
/// and the wall time. A measurement — nothing is written but the table.
static int dagLevels(const QString &path, const QString &outPath, int threads, const QSet<int> &only)
{
    iris::MeshBake::setBakeThreads(threads);
    Assimp::Importer importer;
    const aiScene *scene = iris::readSceneFile(importer, path, iris::ImportFlags::Canonical);
    const QList<iris::MeshPtr> meshes = testmesh::fromScene(scene);
    if (meshes.isEmpty()) { std::printf("FAIL: %s parses\n", qUtf8Printable(path)); return 1; }
    std::vector<QString> names(size_t(meshes.size()));
    std::function<void(const aiNode *)> walk = [&](const aiNode *n) {
        for (unsigned k = 0; k < n->mNumMeshes; ++k)
            if (n->mMeshes[k] < names.size() && names[n->mMeshes[k]].isEmpty())
                names[n->mMeshes[k]] = QString::fromUtf8(n->mName.C_Str());
        for (unsigned c = 0; c < n->mNumChildren; ++c) walk(n->mChildren[c]);
    };
    walk(scene->mRootNode);
    QFile out(outPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) { std::printf("FAIL: %s\n", qUtf8Printable(outPath)); return 1; }
    out.write("mesh\tname\tl0\tlevel\tgroups\treductions\tnotSimplified\tcarried\ttop\ttris\terrMax\terrMed\t"
              "stepOverBudgetMax\tbudgetMax\tretries\tpruned\tislandsDropped\tweightNormalMed\tweightUvMed\n");
    long long l0All = 0, rootAll = 0, topAll = 0, dagLess = 0, dagLessTris = 0;
    int dagMeshes = 0, carried = 0, retries = 0, reductions = 0, allCarriedLevels = 0, capHits = 0, depthMax = 0;
    int g2 = 0, g4 = 0, g8 = 0, pairs = 0;
    double msSum = 0.0;
    long long dagBytes = 0;
    std::map<int, int> depthHistogram;
    std::map<int, long long> attemptHist;   // groups attempted (every pass) by their simplify count
    std::map<int, long long> grantedHist;   // DAG reductions by ceil(log2(granted / children's stored)); -1 = leaves
    long long countCarries = 0, budgetCarries = 0;
    std::vector<double> d0ratio;
    QElapsedTimer wall; wall.start();
    for (int i = 0; i < meshes.size(); ++i) {
        if (!only.isEmpty() && !only.contains(i)) continue;
        const iris::MeshPtr &m = meshes.at(i);
        const iris::IndexBufferPtr ib = m->getIndexBuffer();
        const long long l0 = (!ib.isNull() && ib->data) ? (long long)(size_t(ib->dataSize) / sizeof(unsigned) / 3u) : 0;
        l0All += l0;
        iris::MeshBake::ClusterDagStats st;
        iris::MeshBake::ClusterDagOptions opt;
        opt.wantGroups = true;
        iris::MeshBake::buildClusterDag(m, &st, opt);
        const iris::MeshClusterDag &dag = m->clusterDag;
        if (st.depthCapHit) ++capHits;
        if (dag.isEmpty()) { ++dagLess; dagLessTris += l0; topAll += l0; continue; }
        ++dagMeshes;
        msSum += st.msTotal;
        // THE DAG's SIZE as the blob writes it (meshbake.cpp writeClusterDag) and the depth of its deepest group
        dagBytes += 4 + 4 + 4LL * iris::atomdag::configRecord().size() + 4 + 32LL * dag.groups.size() +
                    40LL * dag.clusters.size() + 4 + 4LL * dag.vertices.size() + 4 + dag.triangles.size();
        ++depthHistogram[st.depth];
        carried += st.carried; retries += st.retries; reductions += st.reductions; allCarriedLevels += st.levelsAllCarried;
        depthMax = std::max(depthMax, st.depth);
        rootAll += st.rootTriangles;
        topAll += cutcurve::cutTop(cutcurve::fromMesh(dag));
        const atom2::Reading rd(dag);
        struct Lv { int groups = 0, red = 0, ns = 0, car = 0, top = 0, retries = 0, pruned = 0, islands = 0;
                    long long tris = 0; std::vector<float> err, wn, wu; float over = 0.0f, budget = 0.0f; };
        std::map<int, Lv> lv;
        for (int c = 0; c < dag.clusters.size(); ++c)
            for (int d = rd.born[size_t(c)]; d <= rd.dies[size_t(c)]; ++d) lv[d].tris += dag.clusters.at(c).triangleCount;
        for (const iris::atomdag::GroupRecord &r : st.records) {
            Lv &L = lv[r.level];
            ++L.groups;
            if (r.kind == 0) ++L.red; else if (r.kind == 1) ++L.car; else if (r.kind == 2) ++L.top; else ++L.ns;
            L.retries += std::max(0, r.attempts - 1);
            L.pruned += r.pruned;
            L.islands += r.islandsDropped;
            if (r.kind == 0 || r.kind == 3) L.err.push_back(r.stored);
            if (r.kind == 0 || r.kind == 1) ++attemptHist[std::min(r.attempts, 12)];
            if (r.kind == 1) (r.countCarry ? countCarries : budgetCarries) += 1;
            if (r.kind == 0)
                ++grantedHist[r.childStored > 0.0f
                                  ? std::max(1, int(std::ceil(std::log2(double(r.budget / r.childStored)) - 1e-4)))
                                  : -1];
            if (r.kind == 0) {
                if (r.budget > 0.0f) L.over = std::max(L.over, r.step / r.budget);
                L.budget = std::max(L.budget, r.budget);
                L.wn.push_back(r.weightNormal);
                L.wu.push_back(r.weightUv);
                if (r.level == 0 && r.depth0Estimate > 0.0f && r.step > 0.0f) d0ratio.push_back(double(r.depth0Estimate / r.step));
            }
        }
        const auto med = [](std::vector<float> v) { if (v.empty()) return 0.0f; std::sort(v.begin(), v.end()); return v[v.size() / 2]; };
        float prevMax = -1.0f;
        for (auto &kv : lv) {
            const Lv &L = kv.second;
            const float errMax = L.err.empty() ? 0.0f : *std::max_element(L.err.begin(), L.err.end());
            out.write(QStringLiteral("%1\t%2\t%3\t%4\t%5\t%6\t%7\t%8\t%9\t%10\t%11\t%12\t%13\t%14\t%15\t%16\t%17\t%18\t%19\n")
                          .arg(i).arg(names[size_t(i)]).arg(l0).arg(kv.first).arg(L.groups).arg(L.red).arg(L.ns).arg(L.car)
                          .arg(L.top).arg(L.tris).arg(double(errMax), 0, 'g', 5).arg(double(med(L.err)), 0, 'g', 5)
                          .arg(double(L.over), 0, 'f', 3).arg(double(L.budget), 0, 'g', 5).arg(L.retries).arg(L.pruned)
                          .arg(L.islands).arg(double(med(L.wn)), 0, 'g', 4).arg(double(med(L.wu)), 0, 'g', 4)
                          .toUtf8());
            if (!L.err.empty()) {
                if (prevMax > 0.0f) {
                    ++pairs;
                    const float q = errMax / prevMax;
                    g2 += q > 2.0f; g4 += q > 4.0f; g8 += q > 8.0f;
                }
                prevMax = errMax;
            }
        }
    }
    std::sort(d0ratio.begin(), d0ratio.end());
    std::printf("dag-levels %s (atomdag, %d threads): %d meshes with a DAG, %lld without (%lld tris); level 0 %lld\n",
                qUtf8Printable(QFileInfo(path).fileName()), threads, dagMeshes, dagLess, dagLessTris, l0All);
    std::printf("  TOP CUT TOTAL %lld (every finite group affordable; DAG-less meshes at level 0); ROOT LEVEL TOTAL %lld "
                "(the last cluster of every DAG, before its empty top)\n", topAll, rootAll);
    std::printf("  reductions %d, carried %d (levels where every group carried %d), retries %d, depth max %d, depth-cap "
                "hits %d\n", reductions, carried, allCarriedLevels, retries, depthMax, capHits);
    std::printf("  level-to-level growth of the stored max (the empty top excluded): %d pairs, >2x %d, >4x %d, >8x %d\n",
                pairs, g2, g4, g8);
    if (!d0ratio.empty())
        std::printf("  depth-0 position-quadric estimate / measured step: median %.3f, p10 %.3f, p90 %.3f over %zu groups\n",
                    d0ratio[d0ratio.size() / 2], d0ratio[d0ratio.size() / 10], d0ratio[d0ratio.size() * 9 / 10], d0ratio.size());
    std::printf("  carried groups by cause (every pass): cluster count %lld (asked the same budget), budget %lld (asked 2x)\n",
                countCarries, budgetCarries);
    std::printf("  groups attempted by simplify count (every pass, every shrink; 12 = 12+):");
    long long aN = 0, aSum = 0;
    for (const auto &kv : attemptHist) { std::printf(" %d:%lld", kv.first, kv.second); aN += kv.second; aSum += kv.second * kv.first; }
    std::printf("  (mean %.2f)\n", aN ? double(aSum) / double(aN) : 0.0);
    std::printf("  reductions by granted / children's stored, ceil(log2) (1 = within 2x, 2 = 4x, ...; L = leaves at 2x the "
                "estimate):");
    for (const auto &kv : grantedHist) {
        if (kv.first < 0) std::printf(" L:%lld", kv.second); else std::printf(" %d:%lld", kv.first, kv.second);
    }
    std::printf("\n");
    std::printf("  DAG blob bytes %lld; meshes by their deepest group's depth:", dagBytes);
    for (const auto &kv : depthHistogram) std::printf(" d%d %d", kv.first, kv.second);
    std::printf("\n");
    std::printf("  DAG build wall %.0f ms (summed over meshes); process wall %.1f s\n", msSum, double(wall.elapsed()) / 1000.0);
    return 0;
}

/// `--dag-groups <model> <mesh> [--threads N]`: every group the builder attempted for one mesh (D11's per-group
/// record; replaces the old --dag-terms): level, kind (reduction / carried / the empty top / not simplified), members,
/// triangles in and out, the children's error, the budget, the measured step and its terms, the attempts, the pieces
/// pruned, the islands dropped and the two attribute weights. TSV on stdout.
static int dagGroupsTool(const QString &path, int meshIndex, int threads)
{
    iris::MeshBake::setBakeThreads(threads);
    Assimp::Importer importer;
    const aiScene *scene = iris::readSceneFile(importer, path, iris::ImportFlags::Canonical);
    const QList<iris::MeshPtr> meshes = testmesh::fromScene(scene);
    if (meshIndex < 0 || meshIndex >= meshes.size()) { std::printf("FAIL: mesh %d of %s\n", meshIndex, qUtf8Printable(path)); return 1; }
    iris::MeshBake::ClusterDagStats st;
    iris::MeshBake::ClusterDagOptions opt;
    opt.wantGroups = true;
    iris::MeshBake::buildClusterDag(meshes.at(meshIndex), &st, opt);
    static const char *kinds[] = { "reduce", "carried", "top", "asis" };
    std::printf("level\tkind\tgroup\tmembers\tin\tout\tchildren\tgranted\tstep\tstored\tremovedV\tlostF\taddedF\tisland\tlostLocked\t"
                "attempts\tpruned\tislands\twN\twUV\tdepth0Est\testimate\n");
    for (const iris::atomdag::GroupRecord &r : st.records)
        std::printf("%d\t%s\t%d\t%d\t%d\t%d\t%.5g\t%.5g\t%.5g\t%.5g\t%.5g\t%.5g\t%.5g\t%.5g\t%.5g\t%d\t%d\t%d\t%.4g\t%.4g\t%.5g\t%.5g\n",
                    r.level, kinds[std::clamp(r.kind, 0, 3)], r.dagGroup, r.members, r.inTris, r.outTris, double(r.childStored),
                    double(r.budget), double(r.step), double(r.stored), double(r.removedVertex), double(r.lostFacet),
                    double(r.addedFacet), double(r.droppedIsland), double(r.lostLocked), r.attempts, r.pruned,
                    r.islandsDropped, double(r.weightNormal), double(r.weightUv), double(r.depth0Estimate), double(r.estimate));
    std::printf("# %d clusters, %d groups, %d levels, %.0f ms (leaves %.0f, groups %.0f, reduce %.0f; simplify %.0f, measure %.0f "
                "of which exact %.0f, summed)\n", st.clusters, st.groups, st.levels, st.msTotal, st.msLeaves, st.msGroups,
                st.msReduce, st.msSimplify, st.msMeasure, st.msExact);
    return 0;
}

/// `--drift <model> [--meshes <list|i,j>] [--threads N]`: D11's diagnostic — per mesh and depth, the uniform cut's
/// drift from level 0 (MeshBake::dagDrift) beside the largest stored error it carries; never stored.
static int driftTool(const QString &path, const QString &meshesArg, int threads)
{
    iris::MeshBake::setBakeThreads(threads);
    Assimp::Importer importer;
    const aiScene *scene = iris::readSceneFile(importer, path, iris::ImportFlags::Canonical);
    const QList<iris::MeshPtr> meshes = testmesh::fromScene(scene);
    if (meshes.isEmpty()) { std::printf("FAIL: %s parses\n", qUtf8Printable(path)); return 1; }
    std::vector<int> items;
    if (meshesArg.isEmpty()) for (int i = 0; i < meshes.size(); ++i) items.push_back(i);
    else if (QFileInfo(meshesArg).isFile()) {
        QFile f(meshesArg);
        if (!f.open(QIODevice::ReadOnly)) return 1;
        for (const QByteArray &raw : f.readAll().split('\n')) {
            const QString line = QString::fromUtf8(raw).trimmed();
            if (!line.isEmpty() && !line.startsWith(QLatin1Char('#'))) items.push_back(line.split(QLatin1Char('\t')).at(0).toInt());
        }
    } else for (const QString &s : meshesArg.split(QLatin1Char(','))) items.push_back(s.toInt());
    std::printf("mesh\tdepth\ttris\tdrift\tstored\tdrift/stored\n");
    for (int i : items) {
        if (i < 0 || i >= meshes.size()) continue;
        iris::MeshBake::buildClusterDag(meshes.at(i));
        for (const auto &r : iris::MeshBake::dagDrift(meshes.at(i)))
            std::printf("%d\t%d\t%d\t%.6g\t%.6g\t%.3f\n", i, r.depth, r.triangles, double(r.drift), double(r.stored),
                        r.stored > 0.0f ? double(r.drift / r.stored) : 0.0);
        std::fflush(stdout);
    }
    return 0;
}

/// `--cut-curve <model> [--meshes <list file | i,j,...>] [--arm shipped|posonly|stock] [--threads N]`: THE CUT CURVE (ATOM-UPGRADE stage 0) — per mesh (every mesh of the file, or the list's: a file of
/// `index<TAB>class<TAB>name` lines, '#' comments, e.g. spikes/atom-upgrade/spenders.txt), the triangles a cut draws at
/// E = 2, 4, 8, 16, 32, 64, 128 mesh units and at the top (every finite group affordable), summed per class and over
/// all; posonly = the same builder on the positions alone, stock = upstream clusterlod.h unpatched (meshoptimizer's
/// demo/clusterlod.h, the reference arm). TSV on stdout; a measurement — nothing is written.
static int cutCurveTool(const QString &path, const QString &meshesArg, const QString &armName, int threads)
{
    using namespace cutcurve;
    const Arm arm = armName == QStringLiteral("posonly") ? Arm::PosOnly : armName == QStringLiteral("stock") ? Arm::Stock
                  : Arm::Shipped;
    if (armName != QStringLiteral("shipped") && armName != QStringLiteral("posonly") && armName != QStringLiteral("stock")) {
        std::printf("FAIL: --arm %s (shipped|posonly|stock)\n", qUtf8Printable(armName));
        return 1;
    }
    iris::MeshBake::setBakeThreads(threads);
    Assimp::Importer importer;
    const aiScene *scene = iris::readSceneFile(importer, path, iris::ImportFlags::Canonical);
    const QList<iris::MeshPtr> meshes = testmesh::fromScene(scene);
    if (meshes.isEmpty()) { std::printf("FAIL: %s parses\n", qUtf8Printable(path)); return 1; }
    std::vector<QString> names(size_t(meshes.size()));
    std::function<void(const aiNode *)> walk = [&](const aiNode *nd) {
        for (unsigned k = 0; k < nd->mNumMeshes; ++k)
            if (nd->mMeshes[k] < names.size() && names[nd->mMeshes[k]].isEmpty())
                names[nd->mMeshes[k]] = QString::fromUtf8(nd->mName.C_Str());
        for (unsigned c = 0; c < nd->mNumChildren; ++c) walk(nd->mChildren[c]);
    };
    walk(scene->mRootNode);
    struct Item { int index; QString cls; };
    std::vector<Item> items;
    if (meshesArg.isEmpty()) {
        for (int i = 0; i < meshes.size(); ++i) items.push_back({ i, QStringLiteral("all") });
    } else if (QFileInfo(meshesArg).isFile()) {
        QFile f(meshesArg);
        if (!f.open(QIODevice::ReadOnly)) { std::printf("FAIL: %s\n", qUtf8Printable(meshesArg)); return 1; }
        for (const QByteArray &raw : f.readAll().split('\n')) {
            const QString line = QString::fromUtf8(raw).trimmed();
            if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
            const QStringList cols = line.split(QLatin1Char('\t'));
            items.push_back({ cols.at(0).toInt(), cols.size() > 1 ? cols.at(1) : QStringLiteral("listed") });
        }
    } else {
        for (const QString &s : meshesArg.split(QLatin1Char(','))) items.push_back({ s.toInt(), QStringLiteral("listed") });
    }
    constexpr int kE = int(sizeof(kLadder) / sizeof(kLadder[0]));
    std::printf("# cut-curve %s arm %s, %d threads; a group is affordable at E iff its stored error < E (mesh units)\n",
                qUtf8Printable(QFileInfo(path).fileName()), qUtf8Printable(armName), threads);
    std::printf("mesh\tclass\tname\tl0\tgroups\tdepth");
    for (float E : kLadder) std::printf("\tE%g", double(E));
    std::printf("\ttop\tms\n");
    std::map<QString, std::vector<long long>> sums;
    QElapsedTimer wall; wall.start();
    for (const Item &it : items) {
        if (it.index < 0 || it.index >= meshes.size()) { std::printf("FAIL: mesh %d out of range\n", it.index); return 1; }
        const iris::MeshPtr &m = meshes.at(it.index);
        const iris::IndexBufferPtr ib = m->getIndexBuffer();
        const long long l0 = (!ib.isNull() && ib->data) ? (long long)(size_t(ib->dataSize) / sizeof(unsigned) / 3u) : 0;
        iris::MeshBake::ClusterDagStats st;
        double ms = 0.0;
        const Dag d = build(m, arm, &st, &ms);
        int maxDepth = -1;
        for (int g : d.groupDepth) maxDepth = std::max(maxDepth, g);
        std::vector<long long> row;
        for (float E : kLadder) row.push_back(d.clusterGroup.empty() ? l0 : cutAt(d, E));
        row.push_back(d.clusterGroup.empty() ? l0 : cutTop(d));
        std::printf("%d\t%s\t%s\t%lld\t%zu\t%d", it.index, qUtf8Printable(it.cls), qUtf8Printable(names[size_t(it.index)]), l0,
                    d.groupError.size(), maxDepth + 1);
        for (long long v : row) std::printf("\t%lld", v);
        std::printf("\t%.0f\n", ms);
        for (const QString &key : { it.cls, QStringLiteral("all") }) {
            std::vector<long long> &s = sums[key];
            if (s.empty()) s.assign(size_t(kE) + 2, 0);
            s[0] += l0;
            for (size_t k = 0; k < row.size(); ++k) s[k + 1] += row[k];
            if (key == it.cls && it.cls == QStringLiteral("all")) break;
        }
        std::fflush(stdout);
    }
    for (const auto &kv : sums) {
        std::printf("SUM\t%s\t-\t%lld\t-\t-", qUtf8Printable(kv.first), kv.second[0]);
        for (size_t k = 1; k < kv.second.size(); ++k) std::printf("\t%lld", kv.second[k]);
        std::printf("\t%lld\n", qint64(wall.elapsed()));
    }
    return 0;
}

/// `--cut-curve-law` (the row bake.cut_curve, ATOM-UPGRADE stage 0): THE CUT CURVE OF A MESH IS MONOTONE AND ENDS
/// EMPTY — on the kit-bashed box set (30 overlapping boxes, hard UV seams) the shipped DAG's cut at E = extent x 2^-k
/// (k = 12 .. 0) never grows as E grows, and once every finite group is affordable it draws nothing (the empty top:
/// a thing under a pixel stops drawing — the spec's law, stage 5's). The curve is printed.
static int cutCurveLaw()
{
    using namespace cutcurve;
    const iris::MeshPtr mesh = kitBashedBoxSet();
    float lo[3] = { FLT_MAX, FLT_MAX, FLT_MAX }, hi[3] = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
    size_t nv = 0; int pc = 3;
    const float *p = positionsOf(mesh, &nv, &pc);
    for (size_t v = 0; p && v < nv; ++v)
        for (int k = 0; k < 3; ++k) { lo[k] = std::min(lo[k], p[v * 3 + size_t(k)]); hi[k] = std::max(hi[k], p[v * 3 + size_t(k)]); }
    const float extent = std::sqrt((hi[0] - lo[0]) * (hi[0] - lo[0]) + (hi[1] - lo[1]) * (hi[1] - lo[1]) +
                                   (hi[2] - lo[2]) * (hi[2] - lo[2]));
    iris::MeshBake::ClusterDagStats st;
    double ms = 0.0;
    const Dag d = build(mesh, Arm::Shipped, &st, &ms);
    const long long l0 = (long long)(nv ? mesh->getIndexBuffer()->dataSize / 12 : 0);
    std::printf("== kit-bashed box set: %lld tris, extent %.3f, %zu groups, %zu clusters, %.0f ms\n", l0, double(extent),
                d.groupError.size(), d.clusterGroup.size(), ms);
    std::printf("   cut curve (E / extent -> triangles):");
    bool monotone = true;
    long long prev = -1;
    for (int k = 12; k >= 0; --k) {
        const float E = extent / float(1 << k);
        const long long t = d.clusterGroup.empty() ? l0 : cutAt(d, E);
        std::printf(" 2^-%d %lld", k, t);
        if (prev >= 0 && t > prev) monotone = false;
        prev = t;
    }
    const long long top = d.clusterGroup.empty() ? l0 : cutTop(d);
    if (prev >= 0 && top > prev) monotone = false;
    std::printf(" | top %lld\n", top);
    char msg[200];
    CHECK_LOUD(!d.clusterGroup.empty(), "the box set has a DAG");
    std::snprintf(msg, sizeof(msg), "the cut curve never grows as E grows");
    CHECK_LOUD(monotone, msg);
    std::snprintf(msg, sizeof(msg), "the curve ends at the empty top (top cut %lld triangles == 0)", top);
    CHECK_LOUD(top == 0, msg);
    if (failures) std::printf("FAILED: %d of %d check(s)\n", failures, checks);
    else          std::printf("ALL %d CHECKS PASSED\n", checks);
    return failures ? 1 : 0;
}

/// `--dag-depths <model>...`: THE DAG'S TRIANGLES PER DEPTH (ENGINE-POLISH-ATOM-1 stage 6, the owner's "far = near
/// polygons"): every mesh of every file through the bake's own DAG build (MeshBake::buildClusterDag - a mesh under its
/// floor gets none: the engine draws such a mesh from a flat DAG over level 0, its every triangle at depth 0), the
/// triangles its clusters hold at each depth (a cluster's depth is its group's), summed over the files, and the ten
/// largest meshes' own rows. A measurement of the bake as it is - nothing baked differently.
static int dagDepths(const QStringList &paths)
{
    std::map<int, unsigned long long> total;
    unsigned long long noDagTriangles = 0ull, level0 = 0ull;
    unsigned meshesNoDag = 0u, meshesDag = 0u;
    struct Row { QString name; unsigned long long tris; std::map<int, unsigned long long> perDepth; };
    std::vector<Row> rows;
    for (const QString &path : paths) {
        const QList<iris::MeshPtr> meshes = testmesh::parseFile(path);
        if (meshes.isEmpty()) { std::printf("FAIL: %s parses\n", qUtf8Printable(path)); return 1; }
        for (int i = 0; i < meshes.size(); ++i) {
            const iris::MeshPtr &m = meshes.at(i);
            iris::MeshBake::buildClusterDag(m);
            const iris::MeshClusterDag &dag = m->clusterDag;
            Row r;
            r.name = QStringLiteral("%1#%2").arg(QFileInfo(path).fileName()).arg(i);
            r.tris = 0ull;
            if (dag.clusters.isEmpty()) {
                const iris::IndexBufferPtr ib = m->getIndexBuffer();
                const unsigned long long t =
                    (!ib.isNull() && ib->data) ? (unsigned long long)(size_t(ib->dataSize) / sizeof(unsigned) / 3u) : 0ull;
                noDagTriangles += t;
                level0 += t;
                ++meshesNoDag;
                continue;
            }
            ++meshesDag;
            for (const iris::MeshClusterDag::Cluster &c : dag.clusters) {
                const int d = (c.group >= 0 && c.group < dag.groups.size()) ? dag.groups.at(c.group).depth : 0;
                r.perDepth[d] += c.triangleCount;
                total[d] += c.triangleCount;
                if (c.refined < 0) { r.tris += c.triangleCount; level0 += c.triangleCount; }
            }
            rows.push_back(std::move(r));
        }
    }
    std::printf("dag-depths: %u meshes with a DAG, %u without (under the floor: %llu triangles, all at depth 0); "
                "level 0 %llu triangles in all\n", meshesDag, meshesNoDag, noDagTriangles, level0);
    std::printf("  depth  triangles (DAG meshes)   + the DAG-less meshes at depth 0\n");
    for (const auto &kv : total)
        std::printf("  %5d  %12llu%s\n", kv.first, kv.second,
                    kv.first == 0 ? qUtf8Printable(QStringLiteral("   + %1").arg(noDagTriangles)) : "");
    std::sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) { return a.tris > b.tris; });
    for (size_t k = 0; k < rows.size() && k < 10u; ++k) {
        std::printf("  %-28s level 0 %8llu:", qUtf8Printable(rows[k].name), rows[k].tris);
        for (const auto &kv : rows[k].perDepth) std::printf(" d%d %llu", kv.first, kv.second);
        std::printf("\n");
    }
    return 0;
}

/// `--bake-blob <model>`: the whole bake of any model file — its sha256, its size
/// and its wall time. The blob is a pure function of the model (serialize), so two
/// builds, two thread counts or two optimisation levels that print the same sha
/// baked the same bytes: the tool IMPORT-SPEED-1 proves "bake-output: unchanged"
/// with against the base binary on a file nobody can ship (the owner's scan).
/// `--bake-blob <model> [threads] [out.jmb]` bakes on that many threads (0 = the
/// hardware's), prints the per-stage table the import's log line carries and the
/// producer id the fingerprint was keyed on (so a blob can be compared with one
/// another build wrote: they differ in the fingerprint alone when the payload is
/// the same), and writes the blob when asked.
static int bakeBlobTool(const QString &path, int threads, const QString &outPath)
{
    iris::MeshBake::setBakeThreads(threads);
    const QString fp = iris::MeshBake::fingerprintFor(QStringLiteral("deadbeef").repeated(8));
    QElapsedTimer t; t.start();
    QTemporaryDir scratch;
    const iris::MeshBake::Model model = iris::MeshBake::buildFromFile(path, fp, scratch.path());
    const QByteArray blob = model.valid ? iris::MeshBake::serialize(model) : QByteArray();
    const qint64 ms = t.elapsed();
    if (blob.isEmpty()) { std::printf("FAIL: %s bakes\n", qUtf8Printable(path)); return 1; }
    std::printf("bake-blob %s  sha256 %s  bytes %lld  wall %lld ms\n  %s\n  producer %s  settings %s\n",
                qUtf8Printable(QFileInfo(path).fileName()),
                QCryptographicHash::hash(blob, QCryptographicHash::Sha256).toHex().constData(),
                qint64(blob.size()), ms, qUtf8Printable(model.stageSummary()),
                qUtf8Printable(iris::MeshBake::producerId()), qUtf8Printable(iris::ImportSettings::identityHash()));
    if (!outPath.isEmpty()) {
        QFile f(outPath);
        if (!f.open(QIODevice::WriteOnly) || f.write(blob) != blob.size()) return 1;
    }
    return 0;
}

/// bake.determinism (IMPORT-SPEED-1): THE BAKE IS A FUNCTION OF THE MODEL, NOT OF
/// THE THREADS. The same file baked on ONE thread (every chunk of every parallel
/// loop walked in order by the caller — the serial answer) and on every hardware
/// thread (the meshes concurrently, each mesh's chain and DAG as two tasks, every
/// query loop chunked across the pool) must give byte-identical blobs. Subjects
/// that reach every parallel path: a MULTI-MESH model (two copies of the scan
/// stand-in as two objects — concurrent meshes, the displacement lock's passes,
/// island caps, the DAG's waves) and the Stanford dragon (normals + UVs in the
/// attribute metric, a 68k-triangle DAG, the SDF's band). Each hardware bake runs
/// TWICE, so a race that happens to reproduce the serial bytes once is still
/// asked again.
/// `part` (TEST-CUT-1: three rows, each at most 60 s): "standin" (the multi-mesh model), "dragon" (the Stanford
/// dragon), "throws" (the throw-in-every-unit cases on the multi-mesh model); empty runs all three.
static void bakeDeterminism(const QString &part = QString())
{
    QTemporaryDir tmp;
    const QString standinPath = tmp.path() + QStringLiteral("/standin.obj");
    CHECK_LOUD(standin::write(standinPath), "the scan stand-in is written");
    // Two objects: the stand-in, and the stand-in again 100 m along +X.
    const QString twoPath = tmp.path() + QStringLiteral("/two_standins.obj");
    {
        QFile in(standinPath);
        CHECK_LOUD(in.open(QIODevice::ReadOnly), "the stand-in reads back");
        const QList<QByteArray> lines = in.readAll().split('\n');
        int vertices = 0;
        for (const QByteArray &l : lines) if (l.startsWith("v ")) ++vertices;
        QByteArray out = "o first\n";
        for (const QByteArray &l : lines) if (l.startsWith("v ") || l.startsWith("f ")) out += l + '\n';
        out += "o second\n";
        for (const QByteArray &l : lines) {
            const QList<QByteArray> t = l.split(' ');
            if (l.startsWith("v ") && t.size() == 4)
                out += "v " + QByteArray::number(t[1].toDouble() + 100.0, 'f', 5) + ' ' + t[2] + ' ' + t[3] + '\n';
            else if (l.startsWith("f ") && t.size() == 4)
                out += "f " + QByteArray::number(t[1].toInt() + vertices) + ' ' + QByteArray::number(t[2].toInt() + vertices)
                       + ' ' + QByteArray::number(t[3].toInt() + vertices) + '\n';
        }
        QFile f(twoPath);
        CHECK_LOUD(f.open(QIODevice::WriteOnly) && f.write(out) == out.size(), "the two-object model is written");
    }
    const QString fp = iris::MeshBake::fingerprintFor(QStringLiteral("deadbeef").repeated(8));
    // the hardware's width, asked for explicitly: the default is 4 (BAKE-WIDTH-2), the proof wants the widest
    const int hardware = [] { iris::MeshBake::setBakeThreads(1 << 16); return iris::MeshBake::bakeThreads(); }();
    std::printf("  hardware threads: %d\n", hardware);
    CHECK_LOUD(hardware >= 2, "the box has more than one thread (else this suite proves nothing)");
    QStringList subjects;
    if (part.isEmpty() || part == QStringLiteral("standin")) subjects << twoPath;
    if (part.isEmpty() || part == QStringLiteral("dragon")) subjects << QStringLiteral(JAH_CLUSTER_FIXTURE_DIR "/matcaps_dragon.obj");
    const bool throws = part.isEmpty() || part == QStringLiteral("throws");
    CHECK_LOUD(!subjects.isEmpty() || throws, qUtf8Printable(QStringLiteral("the part '%1' is one of standin, dragon, throws").arg(part)));
    for (const QString &path : subjects) {
        const QString name = QFileInfo(path).fileName();
        QByteArray blob[3];
        qint64 ms[3] = { 0, 0, 0 };
        int meshes = 0, chained = 0, dags = 0;
        for (int run = 0; run < 3; ++run) {
            iris::MeshBake::setBakeThreads(run == 0 ? 1 : hardware);
            QTemporaryDir scratch;
            QElapsedTimer t; t.start();
            const iris::MeshBake::Model model = iris::MeshBake::buildFromFile(path, fp, scratch.path());
            ms[run] = t.elapsed();
            blob[run] = model.valid ? iris::MeshBake::serialize(model) : QByteArray();
            if (run == 0) {
                meshes = model.meshes.size();
                for (const iris::MeshPtr &m : model.meshes) {
                    if (!m->lodIndices.isEmpty()) ++chained;
                    if (!m->clusterDag.groups.isEmpty()) ++dags;
                }
            }
        }
        iris::MeshBake::setBakeThreads(0);
        std::printf("  %-20s %d mesh(es), %d chained, %d DAGs: 1 thread %lld ms, %d threads %lld / %lld ms, "
                    "sha256 %s\n", qUtf8Printable(name), meshes, chained, dags, ms[0], hardware, ms[1], ms[2],
                    QCryptographicHash::hash(blob[0], QCryptographicHash::Sha256).toHex().left(16).constData());
        CHECK_LOUD(!blob[0].isEmpty() && chained == meshes && dags == meshes,
                   qUtf8Printable(name + QStringLiteral(": bakes, every mesh with a chain and a DAG")));
        CHECK_LOUD(blob[1] == blob[0] && blob[2] == blob[0],
                   qUtf8Printable(name + QStringLiteral(": 1 thread and %1 threads (twice) bake byte-identical blobs")
                                             .arg(hardware)));
    }
    CHECK_LOUD(subjects.size() == (part.isEmpty() ? 2 : (throws ? 0 : 1)), "the part's subjects ran");
    if (!throws)
        return;

    // AND A BAKE THAT THROWS IS NO BAKE — NOT A DEAD APP (IMPORT-SPEED-1 F1). An
    // exception in any unit of work (the test hook throws in the n-th: early = the
    // caller's own first chunks, later = worker threads and nested jobs inside a
    // mesh's stages) must come back to buildFromScene as an invalid model — before,
    // a worker's throw was std::terminate — and the pool must then bake the same
    // file whole, byte for byte — AFTER EACH THROW (a breakage one throw leaves must not be repaired by the next
    // throw's bake). Under the 60 s law by fewer WIDTHS, never fewer comparisons (the stage-2 read): the reference at
    // one thread, the five throws on the pool at the box's width (the worker threads and the nested jobs, where a
    // throw used to be std::terminate; the caller's own chunks run there too), each followed by a whole bake
    // byte-identical to the reference.
    {
        QTemporaryDir scratch;
        iris::MeshBake::setBakeThreads(1);
        const QByteArray reference = iris::MeshBake::serialize(iris::MeshBake::buildFromFile(twoPath, fp, scratch.path()));
        iris::MeshBake::setBakeThreads(hardware);
        for (const int n : { 1, 3, 40, 400, 2000 }) {
            iris::MeshBake::failBakeAfterChunksForTest(n);
            const iris::MeshBake::Model failed = iris::MeshBake::buildFromFile(twoPath, fp, scratch.path());
            iris::MeshBake::failBakeAfterChunksForTest(0);
            CHECK_LOUD(!failed.valid, qUtf8Printable(QStringLiteral(
                "%1 threads: a throw in unit %2 of the bake answers 'no bake' (and the process lives)").arg(hardware).arg(n)));
            const QByteArray again = iris::MeshBake::serialize(iris::MeshBake::buildFromFile(twoPath, fp, scratch.path()));
            CHECK_LOUD(!reference.isEmpty() && again == reference, qUtf8Printable(QStringLiteral(
                "%1 threads: ...and after the throw in unit %2 the next bake is whole and byte-identical to one thread's")
                .arg(hardware).arg(n)));
        }
        iris::MeshBake::setBakeThreads(0);
    }
}

static void boundBar()
{
    QTemporaryDir tmp;
    const QString standinPath = tmp.path() + QStringLiteral("/lod_standin.obj");
    CHECK_LOUD(standin::write(standinPath), "the scan stand-in is written");

    struct Subject { QString path; bool standin; };
    const QVector<Subject> subjects = {
        { fixture(QStringLiteral("app/content/primitives/sphere.obj")), false },
        { fixture(QStringLiteral("app/content/primitives/hp_sphere.obj")), false },
        { fixture(QStringLiteral("app/content/primitives/capsule.obj")), false },
        { fixture(QStringLiteral("app/content/primitives/torus.obj")), false },
        { fixture(QStringLiteral("app/content/primitives/hemisphere.obj")), false },
        { fixture(QStringLiteral("app/content/primitives/teapot.obj")), false },
        { fixture(QStringLiteral("app/content/primitives/tube.obj")), false },
        { fixture(QStringLiteral("app/content/primitives/endlessplane.obj")), false },
        { fixture(QStringLiteral("tests/meshbake/fixtures/flat_grid_100m.obj")), false },
        { fixture(QStringLiteral("app/models/axis_sphere.obj")), false },
        { QStringLiteral(JAH_CLUSTER_FIXTURE_DIR "/matcaps_dragon.obj"), false },
        { standinPath, true },
    };
    int chained = 0;
    for (const Subject &s : subjects) {
        const QString name = s.standin ? QStringLiteral("scan stand-in") : QFileInfo(s.path).fileName();
        const QList<iris::MeshPtr> meshes = testmesh::parseFile(s.path);
        CHECK_LOUD(!meshes.isEmpty(), qUtf8Printable(name + ": parses"));
        for (const iris::MeshPtr &mesh : meshes) {
            QVector<iris::MeshBake::LodLevelTerms> terms;
            iris::MeshBake::buildLodChain(mesh, &terms);
            if (mesh->lodIndices.isEmpty()) continue;
            ++chained;
            float maxAxis = 0.0f, radius = 0.0f;
            meshExtents(mesh, &maxAxis, &radius);
            printTerms(name, terms, radius);

            double worst = 0.0;
            QVector<float> reference;
            const bool honest = iris::MeshBake::checkLodBounds(mesh, 8, &worst, &reference);
            CHECK_LOUD(honest, qUtf8Printable(QStringLiteral(
                "%1: (a) every stored bound >= the per-facet two-sided reference (worst reference/stored %2)")
                .arg(name).arg(worst, 0, 'f', 4)));

            // (b) 2x where the level dropped no island — the displacement lock's
            // budget on the exact term, which on these subjects also bounds the
            // sampled one. Where islands WERE dropped the sampled term may carry an
            // island's capped distance (<= the budget) times the sampling margin, so
            // the arithmetic ceiling of the rule there is 2 x 1.25.
            bool tight = true;
            QString tightWhy;
            const float floorBound = maxAxis * 1e-5f;
            for (int k = 0; k < terms.size(); ++k) {
                const float factor = terms.at(k).islandsDropped > 0 ? 2.5f : 2.0f;
                const float allowed = std::max(factor * terms.at(k).quadric, floorBound * 1.0001f);
                if (terms.at(k).bound > allowed * (1.0f + 1e-6f)) {
                    tight = false;
                    tightWhy += QStringLiteral(" L%1 %2 > %4 x %3").arg(k + 1)
                                    .arg(double(terms.at(k).bound), 0, 'f', 5)
                                    .arg(double(terms.at(k).quadric), 0, 'f', 5).arg(double(factor));
                }
            }
            CHECK_LOUD(tight, qUtf8Printable(name + QStringLiteral(": (b) every bound <= 2x the "
                                                                   "simplifier's error (2.5x on a level "
                                                                   "that dropped islands)") + tightWhy));
            if (!s.standin) continue;

            bool capped = true;
            int droppedAny = 0;
            for (const auto &t : terms) {
                droppedAny += t.islandsDropped;
                if (t.droppedMaxExtent > t.bound * (1.0f + 1e-6f)) capped = false;
            }
            CHECK_LOUD(capped && droppedAny > 0, qUtf8Printable(QStringLiteral(
                "%1: (c) the levels drop islands (%2 in all) and none is bigger than its level's bound")
                .arg(name).arg(droppedAny)));
            const float d1 = switchDistance(terms.first().bound, radius);
            CHECK_LOUD(d1 <= 150.0f, qUtf8Printable(QStringLiteral(
                "%1: (c) level 1 (bound %2 m) is affordable from %3 m on a 1080-line 45-degree view "
                "(bar: 150 m)").arg(name).arg(double(terms.first().bound), 0, 'f', 4).arg(double(d1), 0, 'f', 0)));
        }
    }
    CHECK_LOUD(chained >= 10, "the subject list really does carry chained meshes");
}

/// CULL-MODE-1: A TWO-SIDED SOURCE MATERIAL IS BAKED. glTF's `doubleSided` arrives
/// through assimp as AI_MATKEY_TWOSIDED; the import stores it on the baked material
/// (MeshMaterialData::twoSided, bake format v16) and it survives the blob.
static void twoSidedReachesTheNode()
{
    const QString rel = QStringLiteral("tests/importer/fixtures/double_sided_quads.gltf");
    const QString path = fixture(rel);
    Assimp::Importer importer;
    const aiScene *scene = importer.ReadFile(path.toStdString().c_str(), iris::ImportFlags::Canonical);
    CHECK_LOUD(scene != nullptr, "the two-quad glTF parses");
    if (!scene) return;
    QTemporaryDir scratch;
    const QString fingerprint = iris::MeshBake::fingerprintFor(
        QStringLiteral("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));
    iris::MeshBake::Model built = iris::MeshBake::buildFromScene(scene, path, fingerprint, scratch.path());
    CHECK_LOUD(built.valid, "the two-quad glTF bakes");
    if (!built.valid) return;
    int twoSidedMaterials = 0;
    for (const iris::MeshMaterialData &m : built.materials) twoSidedMaterials += m.twoSided ? 1 : 0;
    const iris::MeshBake::Model read = iris::MeshBake::deserialize(iris::MeshBake::serialize(built), fingerprint);
    int readTwoSided = 0;
    for (const iris::MeshMaterialData &m : read.materials) readTwoSided += m.twoSided ? 1 : 0;
    CHECK_LOUD(twoSidedMaterials == 1 && readTwoSided == 1,
               qUtf8Printable(QStringLiteral("one of the two materials is two-sided, through the blob (%1 built, %2 read)")
                                  .arg(twoSidedMaterials).arg(readTwoSided)));
    // ATOM-TWO-SIDED-1: THE IMPORT'S FACE CULLING overrides the file's flag where the
    // importer reads it — the record the verbs and the dialog write (`faceCulling`:
    // "file" by default, "single", "double"), in the bake key.
    {
        const struct { const char *name; iris::FaceCullingImport mode; int twoSided; } arms[] = {
            { "file", iris::FaceCullingImport::File, 1 },
            { "single", iris::FaceCullingImport::Single, 0 },
            { "double", iris::FaceCullingImport::Double, 2 },
        };
        QStringList hashes;
        for (const auto &arm : arms) {
            QJsonObject record;
            record[QStringLiteral("faceCulling")] = QString::fromLatin1(arm.name);
            QString error;
            const iris::ImportSettings parsed = iris::ImportSettings::fromJson(record, &error);
            CHECK_LOUD(error.isEmpty() && parsed.faceCulling == arm.mode &&
                           parsed.toJson().value(QStringLiteral("faceCulling")).toString() == QLatin1String(arm.name),
                       qUtf8Printable(QStringLiteral("faceCulling '%1' reads, carries and writes back").arg(arm.name)));
            hashes << parsed.hash();
            const iris::MeshBake::Model m =
                iris::MeshBake::buildFromScene(scene, path, fingerprint, scratch.path(), parsed.transform());
            int n = 0;
            for (const iris::MeshMaterialData &d : m.materials) n += d.twoSided ? 1 : 0;
            CHECK_LOUD(m.valid && n == arm.twoSided,
                       qUtf8Printable(QStringLiteral("faceCulling '%1': %2 of the two materials two-sided (want %3)")
                                          .arg(arm.name).arg(n).arg(arm.twoSided)));
        }
        CHECK_LOUD(hashes[0] == iris::ImportSettings::identityHash() && hashes[1] != hashes[0] &&
                       hashes[2] != hashes[0] && hashes[2] != hashes[1],
                   "\"file\" keys as identity; \"single\" and \"double\" each move the settings hash (a re-bake)");
        QJsonObject wrong;
        wrong[QStringLiteral("faceCulling")] = QStringLiteral("auto");
        QString why;
        iris::ImportSettings::fromJson(wrong, &why);
        CHECK_LOUD(!why.isEmpty(), "an unknown faceCulling ('auto') is refused");
    }
    // (The NODE half — the fragment's MeshNode culls nothing — needs an Ogre::Root: a
    // fragment's children live in the scene graph, which this suite has none of. It is
    // mirror.document_to_engine's arm, through the same buildFromFile + buildFragment.)
}

// ---------------------------------------------------------------------------
// 17. THE CLIP BAKE and the DESCRIBE block (SHIPPED-BAKES-1)
// ---------------------------------------------------------------------------
//
// A clip file's readers read its CLIP bake, never a parse: its facts (the
// ClipFileInfo the strip and the metadata come from) and its keys must survive
// the round trip exactly, its bytes must be deterministic, a wrong key or a
// truncated blob must be refused, and the rig's uniform factor must reach the
// POSITION keys and nothing else. A model bake carries the facts of its parse
// (the metadata backfill reads them).
static void clipBake()
{
    const QString walk = fixture(QStringLiteral("tests/avatar/fixtures/rig2_walk_anim.glb"));
    const QVector<double> strip = iris::ClipFileInfo::stripFractions();
    const iris::MeshBake::Clip clip = iris::MeshBake::buildClipFromFile(walk, QStringLiteral("fp"));
    CHECK_LOUD(clip.valid && clip.info.parsed, "the clip bakes");
    CHECK_LOUD(!clip.animations.isEmpty(), "...with its animation");
    CHECK_LOUD(clip.info.poses.size() == strip.size(),
               "...and the pose strip's poses (one per ClipFileInfo::stripFractions)");

    const QByteArray a = iris::MeshBake::serializeClip(clip);
    const QByteArray b = iris::MeshBake::serializeClip(
        iris::MeshBake::buildClipFromFile(walk, QStringLiteral("fp")));
    CHECK_LOUD(!a.isEmpty() && a == b, "the clip bake is deterministic (two builds, same bytes)");

    const iris::MeshBake::Clip back = iris::MeshBake::deserializeClip(a, QStringLiteral("fp"));
    CHECK_LOUD(back.valid, "it reads back");
    CHECK_LOUD(iris::MeshBake::serializeClip(back) == a, "...to the same bytes (a lossless round trip)");
    CHECK(back.info.clips.size() == clip.info.clips.size()
              && back.info.nodeNames == clip.info.nodeNames
              && back.info.nodeParents == clip.info.nodeParents,
          "the clip table and the hierarchy survive");
    CHECK(back.declaredUnitScale == clip.declaredUnitScale, "the file's declared unit survives");
    CHECK_LOUD(!iris::MeshBake::deserializeClip(a, QStringLiteral("other")).valid,
               "a clip bake under another key is REFUSED (a stale producer rebuilds)");
    CHECK_LOUD(!iris::MeshBake::deserializeClip(a.left(a.size() / 2)).valid,
               "a truncated clip bake is refused");
    CHECK_LOUD(!iris::MeshBake::deserialize(a).valid && !iris::MeshBake::deserializeClip(
                   iris::MeshBake::serialize(iris::MeshBake::buildFromFile(
                       fixture(QStringLiteral("tests/importer/fixtures/ticks_anim.glb")),
                       QStringLiteral("fp")))).valid,
               "the two KINDS never read as each other");

    // THE PARSE IT REPLACES, compared: the facts of the identity ClipNamesOnly
    // read are the bake's facts.
    const iris::ClipFileInfo parsed = iris::ClipFileInfo::read(walk, strip);
    bool sameFacts = parsed.parsed && parsed.clips.size() == back.info.clips.size()
                     && parsed.nodeNames == back.info.nodeNames
                     && parsed.poses.size() == back.info.poses.size();
    for (int i = 0; sameFacts && i < parsed.clips.size(); ++i)
        sameFacts = parsed.clips[i].name == back.info.clips[i].name
                    && parsed.clips[i].channelNames == back.info.clips[i].channelNames
                    && parsed.clips[i].lengthSeconds == back.info.clips[i].lengthSeconds;
    for (int i = 0; sameFacts && i < parsed.poses.size(); ++i)
        sameFacts = parsed.poses[i].positions == back.info.poses[i].positions;
    CHECK_LOUD(sameFacts, "the bake's facts ARE the parse's (names, channels, lengths, poses)");

    // THE RIG'S FACTOR: identity hands the keys over untouched; a rig imported
    // at 2x gets every POSITION key doubled and its rotations and scales as
    // authored (what assimp's ScaleProcess did to a key read under that rig).
    const auto same = iris::MeshBake::clipAnimations(back, iris::ImportTransform(), walk);
    iris::ImportTransform twice;
    twice.scale = 2.0;
    const auto doubled = iris::MeshBake::clipAnimations(back, twice, walk);
    bool identityExact = same.size() == back.animations.size();
    bool scaledExact = doubled.size() == back.animations.size();
    int posKeys = 0;
    for (auto it = back.animations.constBegin(); it != back.animations.constEnd(); ++it) {
        const auto &from = it.value();
        const auto &id = same.value(it.key());
        const auto &x2 = doubled.value(it.key());
        if (!id || !x2) { identityExact = scaledExact = false; break; }
        identityExact = identityExact && id->source == walk && x2->source == walk;
        for (auto b = from->boneAnimations.constBegin(); b != from->boneAnimations.constEnd(); ++b) {
            const auto f = b.value();
            const auto i1 = id->boneAnimations.value(b.key());
            const auto i2 = x2->boneAnimations.value(b.key());
            if (!i1 || !i2) { identityExact = scaledExact = false; continue; }
            for (int k = 0; k < f->posKeys->keys.size(); ++k, ++posKeys) {
                const iris::Vec3 v = f->posKeys->keys[k]->value;
                identityExact = identityExact && i1->posKeys->keys[k]->value == v
                                && i1->posKeys->keys[k]->time == f->posKeys->keys[k]->time;
                scaledExact = scaledExact && i2->posKeys->keys[k]->value == v * 2.0f;
            }
            for (int k = 0; k < f->rotKeys->keys.size(); ++k)
                scaledExact = scaledExact
                              && i2->rotKeys->keys[k]->value == f->rotKeys->keys[k]->value;
        }
    }
    CHECK_LOUD(posKeys > 0, qUtf8Printable(QStringLiteral("the clip has position keys (%1)").arg(posKeys)));
    CHECK_LOUD(identityExact, "an identity rig gets the clip's keys exactly, on fresh objects");
    CHECK_LOUD(scaledExact, "a 2x rig gets every position key doubled, rotations as authored");
    CHECK_LOUD(same.constBegin().value() != back.animations.constBegin().value(),
               "...and every caller is handed its OWN objects");

    // THE FACTOR AGAINST THE PARSE IT REPLACES, under a UNIT OVERRIDE, on an
    // FBX that declares its own unit: the bake read with a rig's recipe must
    // hand over the keys the old parse under that recipe produced — not a
    // multiply compared with the same multiply.
    {
        // pivot_rig.fbx is authored in METRES (UnitScaleFactor 100); its
        // CENTIMETRE twin — the Mixamo unit — is the same ASCII with the one
        // declaration changed, written here rather than committed.
        QTemporaryDir unitDir;
        const QString cmRig = QDir(unitDir.path()).filePath(QStringLiteral("pivot_rig_cm.fbx"));
        {
            QFile in(fixture(QStringLiteral("tests/skeletal/fixtures/pivot_rig.fbx")));
            QFile out(cmRig);
            if (in.open(QIODevice::ReadOnly) && out.open(QIODevice::WriteOnly)) {
                QByteArray text = in.readAll();
                text.replace("\"UnitScaleFactor\", \"double\", \"Number\", \"\",100",
                             "\"UnitScaleFactor\", \"double\", \"Number\", \"\",1");
                out.write(text);
            }
        }
        QString chosen;
        iris::MeshBake::Clip fbx;
        for (const QString &path : { cmRig }) {
            const QString rel = QFileInfo(path).fileName();
            iris::MeshBake::Clip c = iris::MeshBake::buildClipFromFile(path, QStringLiteral("fp"));
            bool hasPos = false;
            for (const auto &a : c.animations)
                for (const auto &bone : a->boneAnimations)
                    hasPos = hasPos || !bone->posKeys->keys.isEmpty();
            std::printf("info: %s: declared unit %.4f m, %d clip(s), position keys %s\n",
                        qUtf8Printable(rel), c.declaredUnitScale, int(c.animations.size()),
                        hasPos ? "yes" : "no");
            if (c.valid && hasPos && c.declaredUnitScale != 1.0 && chosen.isEmpty()) {
                chosen = path;
                fbx = c;
            }
        }
        CHECK_LOUD(!chosen.isEmpty(), "an FBX clip fixture that DECLARES a non-metre unit");
        if (!chosen.isEmpty()) {
            iris::ImportTransform rig;
            rig.scale = 1.3;
            rig.unitOverride = 0.5;   // the user says: half a metre per unit
            const auto baked = iris::MeshBake::clipAnimations(fbx, rig.keysOnly(), chosen);
            Assimp::Importer importer;
            const aiScene *scene =
                iris::readSceneFile(importer, chosen, iris::ImportFlags::ClipNamesOnly, rig.keysOnly());
            const auto parsed = scene ? iris::Mesh::extractAnimations(scene, chosen)
                                      : QMap<QString, iris::SkeletalAnimationPtr>();
            bool same = !parsed.isEmpty() && parsed.size() == baked.size();
            int compared = 0;
            float worst = 0.0f;
            for (auto it = parsed.constBegin(); same && it != parsed.constEnd(); ++it) {
                const auto b = baked.value(it.key());
                if (!b) { same = false; break; }
                for (auto bone = it.value()->boneAnimations.constBegin();
                     same && bone != it.value()->boneAnimations.constEnd(); ++bone) {
                    const auto mine = b->boneAnimations.value(bone.key());
                    if (!mine || mine->posKeys->keys.size() != bone.value()->posKeys->keys.size()) {
                        same = false;
                        break;
                    }
                    for (int k = 0; k < bone.value()->posKeys->keys.size(); ++k, ++compared) {
                        const iris::Vec3 p = bone.value()->posKeys->keys[k]->value;
                        const iris::Vec3 q = mine->posKeys->keys[k]->value;
                        const float tol = 1e-4f * std::max(1.0f, p.length());
                        worst = std::max(worst, (p - q).length());
                        same = same && (p - q).length() <= tol;
                    }
                    for (int k = 0; same && k < bone.value()->rotKeys->keys.size(); ++k)
                        same = same && mine->rotKeys->keys.size() == bone.value()->rotKeys->keys.size();
                }
            }
            CHECK_LOUD(same && compared > 0,
                       qUtf8Printable(QStringLiteral("under a unit override (0.5 m/unit, scale 1.3) the "
                                                     "clip bake's keys ARE the parse's: %1 position keys, "
                                                     "worst %2").arg(compared).arg(double(worst))));
        }
    }

    // THE DESCRIBE BLOCK: a model bake carries the facts of its parse.
    const QString ticks = fixture(QStringLiteral("tests/importer/fixtures/ticks_anim.glb"));
    const iris::MeshBake::Model model = iris::MeshBake::buildFromFile(ticks, QStringLiteral("fp"));
    iris::SceneSource source;
    CHECK_LOUD(source.read(ticks), "the fixture parses (the comparison's own read)");
    const iris::ModelSceneInfo facts = iris::ModelSceneInfo::fromSource(source);
    const iris::MeshBake::Model read = iris::MeshBake::deserialize(iris::MeshBake::serialize(model));
    const iris::ModelSceneInfo &d = read.describe;
    CHECK_LOUD(read.valid && d.parsed, "a model bake reads back WITH its describe block");
    CHECK_LOUD(d.vertices == facts.vertices && d.triangles == facts.triangles
                   && d.meshes == facts.meshes && d.materials == facts.materials
                   && d.boneNames == facts.boneNames && d.nodeNames == facts.nodeNames
                   && d.textureReferences == facts.textureReferences
                   && d.animations.size() == facts.animations.size()
                   && d.extentX == facts.extentX && d.extentY == facts.extentY
                   && d.extentZ == facts.extentZ && d.extentValid == facts.extentValid
                   && d.declaredUnitScale == facts.declaredUnitScale,
               "...and its facts ARE the parse's (counts, names, clips, extent, unit)");

    // THE HEADER READ: the describe block alone, off the file, no geometry.
    QTemporaryDir dir;
    const QString bakePath = QDir(dir.path()).filePath(QStringLiteral("ticks.jmb"));
    CHECK_LOUD(iris::MeshBake::write(bakePath, model, nullptr), "the model bake is written");
    iris::ModelSceneInfo header;
    CHECK_LOUD(iris::MeshBake::readDescribe(bakePath, &header)
                   && header.vertices == facts.vertices && header.triangles == facts.triangles
                   && header.nodeNames == facts.nodeNames && header.extentX == facts.extentX,
               "readDescribe answers the same facts from the HEADER alone");
    CHECK_LOUD(!iris::MeshBake::readDescribe(bakePath, &header, QStringLiteral("other")),
               "...and refuses a bake under another key");
    {
        // Cut the file right after the header: the geometry is GONE and the
        // header read still answers — it never reached for it.
        QFile f(bakePath);
        QByteArray all;
        if (f.open(QIODevice::ReadOnly)) { all = f.readAll(); f.close(); }
        // Where the header ends: stream magic, version, key and facts the way
        // the container lays them, and take the stream's position.
        QBuffer buffer(&all);
        buffer.open(QIODevice::ReadOnly);
        QDataStream h(&buffer);
        h.setVersion(QDataStream::Qt_6_0);
        h.setByteOrder(QDataStream::LittleEndian);
        quint32 magic = 0;
        qint32 version = 0;
        QString key;
        QByteArray factsBlob;
        h >> magic >> version >> key >> factsBlob;
        const qint64 headerBytes = buffer.pos();
        CHECK_LOUD(h.status() == QDataStream::Ok && headerBytes < all.size() / 2,
                   qUtf8Printable(QStringLiteral("the header is %1 of the bake's %2 bytes")
                                      .arg(headerBytes).arg(all.size())));
        const QString cut = QDir(dir.path()).filePath(QStringLiteral("cut.jmb"));
        QFile out(cut);
        if (out.open(QIODevice::WriteOnly)) { out.write(all.left(int(headerBytes))); out.close(); }
        iris::ModelSceneInfo cutInfo;
        CHECK_LOUD(iris::MeshBake::readDescribe(cut, &cutInfo) && cutInfo.triangles == header.triangles,
                   "a bake cut right after its header still describes (the geometry is never read)");
        CHECK_LOUD(!iris::MeshBake::read(cut).valid, "...while the full read of it is refused");
    }
}


// ---------------------------------------------------------------------------
// 18. THE IMPORT BAKES' LANDING — `meshbake.async_landing` (archived: archive/tests/meshbake) (STAGE-CLOSE-TRIAGE-1 rounds 3-4)
// ---------------------------------------------------------------------------
//
// In process, against a real fixture store: the bake an import lands off the UI thread.
//   F5  a cancelled bake stops at its cancel points: the parse (assimp's progress handler), and
//       runBake before the write and the stage — invalid model, error "cancelled", nothing staged;
//   F6  the landing names EVERY row over the baked file (the Object AND its Mesh member), not the
//       one row the by-content dedupe kept;
//   F7  a landing dropped by a reset/shutdown (the epoch) still calls `done` with its reason —
//       what ends the avatar module's busy job;
//   F8  a landing whose store root moved while the worker staged is refused, with its reason.
static void asyncLanding()
{
    QTemporaryDir home;
    QTemporaryDir storeRoot;
    QTemporaryDir otherRoot;
    if (!home.isValid() || !storeRoot.isValid() || !otherRoot.isValid()) {
        std::printf("FAIL: could not create the fixture store\n");
        ++failures;
        return;
    }
    AssetStorePaths::setRootOverride(storeRoot.path());
    Database db;
    CHECK_LOUD(db.initializeDatabase(QDir(home.path()).filePath("assets.db")), "fixture database opened");
    db.createAllTables();
    QSqlDatabase conn = QSqlDatabase::database();
    AssetCas::ensureCasSchema(conn);

    Project project;
    AssetImportService service(&db, &project);
    ImportRequest request;
    request.sourcePath = fixture(QStringLiteral("tests/importer/fixtures/scaled_two_meshes.glb"));
    const ImportResult imported = service.import(request);
    CHECK_LOUD(imported.ok() && !imported.meshGuid.isEmpty(), "the fixture imported (an Object and a Mesh member)");
    if (!imported.ok()) { AssetStorePaths::setRootOverride(QString()); return; }
    const QString source = AssetCas::resolveSource(conn, storeRoot.path(), imported.assetGuid);
    CHECK_LOUD(MeshBakeStore::isFresh(conn, storeRoot.path(), source, imported.assetGuid), "its bake is current");

    // A STALE BAKE: the bake rows go (every owner), so a landing has something to build.
    const auto dropBakes = [&]() {
        QSqlQuery q(conn);
        q.exec(QStringLiteral("DELETE FROM asset_files WHERE role = 'bake'"));
        MeshBakeStore::clear();
    };
    const auto pumpUntilLanded = [&]() {
        for (int i = 0; i < 6000 && MeshBakeStore::pendingBakeCount() > 0; ++i) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            QThread::msleep(1);
        }
        QCoreApplication::processEvents();
    };
    QObject context;
    QVector<MeshBakeStore::LandedBake> landed;
    MeshBakeStore::addLandedListener(&context, [&](const QVector<MeshBakeStore::LandedBake> &rows) {
        landed += rows;
    });

    // ---- F5: the cancel points ------------------------------------------------------------
    {
        // THE PARSE'S CANCEL POINT IS ASSIMP'S OWN (patch 0010): an Importer whose progress handler
        // answers false aborts after the format's read — before any post-processing step — frees the
        // partial scene and names the cancel. Driven directly, so the check cannot pass through the
        // mesh cancel point behind it.
        {
            struct Refuse : Assimp::ProgressHandler
            {
                int calls = 0;
                bool Update(float) override { ++calls; return false; }
            };
            Assimp::Importer importer;
            auto *refuse = new Refuse;   // the importer owns it
            importer.SetProgressHandler(refuse);
            const aiScene *scene = importer.ReadFile(source.toStdString(),
                                                     aiProcess_Triangulate | aiProcess_GenSmoothNormals |
                                                     aiProcess_JoinIdenticalVertices);
            const QString why = QString::fromStdString(importer.GetErrorString());
            CHECK_LOUD(!scene && why.contains(QStringLiteral("cancelled")),
                       qUtf8Printable(QStringLiteral("F5: a progress handler answering false CANCELS assimp's "
                                                     "import — no scene, the reason named ('%1')").arg(why)));
            CHECK_LOUD(refuse->calls <= 3,
                       qUtf8Printable(QStringLiteral("F5: ...at the first cancel point after the read, not after "
                                                     "every post-processing step (%1 Update call(s))").arg(refuse->calls)));
        }
        std::atomic<bool> cancel { true };
        const iris::MeshBake::Model model =
            iris::MeshBake::buildFromFile(source, QStringLiteral("fp"), QString(), iris::ImportTransform(), &cancel);
        CHECK_LOUD(!model.valid, "F5: a bake cancelled before its parse answers an invalid model");
        std::atomic<bool> go { false };
        const iris::MeshBake::Model whole =
            iris::MeshBake::buildFromFile(source, QStringLiteral("fp"), QString(), iris::ImportTransform(), &go);
        CHECK_LOUD(whole.valid, "F5: ...and the same bake with the flag down is whole (a cancel point changes nothing)");
        const QVector<MeshBakeStore::BakeJob> jobs = MeshBakeStore::staleJobsFor(conn, storeRoot.path(), { source });
        dropBakes();
        const QVector<MeshBakeStore::BakeJob> stale = MeshBakeStore::staleJobsFor(conn, storeRoot.path(), { source });
        CHECK_LOUD(!stale.isEmpty(), "F5: with its bake rows gone the model has a rebuild job");
        if (!stale.isEmpty()) {
            const MeshBakeStore::BakeResult r = MeshBakeStore::runBake(stale.first(), &cancel);
            CHECK_LOUD(r.error == QStringLiteral("cancelled") && r.path.isEmpty() && r.staged.srcPath.isEmpty(),
                       qUtf8Printable(QStringLiteral("F5: runBake under a raised flag answers 'cancelled' and stages "
                                                     "nothing (error '%1')").arg(r.error)));
        }
        Q_UNUSED(jobs);
    }

    // ---- F6: every row over the baked file --------------------------------------------------
    landed.clear();
    const int started = MeshBakeStore::bakeLandedAsync({ imported.assetGuid, imported.meshGuid });
    CHECK_LOUD(started == 1, qUtf8Printable(QStringLiteral("F6: ONE bake for the two rows over one content "
                                                           "and settings (started %1)").arg(started)));
    pumpUntilLanded();
    QSet<QString> rowsLanded;
    for (const auto &row : landed) if (row.ok) rowsLanded.insert(row.assetGuid);
    CHECK_LOUD(rowsLanded.contains(imported.assetGuid) && rowsLanded.contains(imported.meshGuid),
               qUtf8Printable(QStringLiteral("F6: the landing names BOTH rows, the Object and its Mesh member "
                                             "(%1 row(s) landed)").arg(rowsLanded.size())));
    CHECK_LOUD(MeshBakeStore::isFresh(conn, storeRoot.path(), source, imported.assetGuid), "F6: ...and the bake is current");

    // ---- F7: a landing dropped by the epoch still says so -------------------------------------
    dropBakes();
    bool called = false, fresh = true;
    QString dropped;
    const bool began = MeshBakeStore::ensureFreshAsync(source, imported.assetGuid, &context,
        [&](bool f, const QString &why) { called = true; fresh = f; dropped = why; });
    CHECK_LOUD(began, "F7: a stale model's rebuild began asynchronously");
    MeshBakeStore::stopBackgroundRebuild();   // a reset / a shutdown: the epoch moves
    pumpUntilLanded();
    CHECK_LOUD(called && !fresh && dropped.contains(QStringLiteral("reset")),
               qUtf8Printable(QStringLiteral("F7: the dropped landing still called back, not fresh, with its reason "
                                             "('%1')").arg(dropped)));
    CHECK_LOUD(!MeshBakeStore::isFresh(conn, storeRoot.path(), source, imported.assetGuid),
               "F7: ...and committed nothing");

    // ---- F8: the store root moved while the worker staged ------------------------------------
    landed.clear();
    called = false; dropped.clear();
    const bool began8 = MeshBakeStore::ensureFreshAsync(source, imported.assetGuid, &context,
        [&](bool f, const QString &why) { called = true; fresh = f; dropped = why; });
    CHECK_LOUD(began8, "F8: a rebuild began under the store root (its job staged into that root)");
    AssetStorePaths::setRootOverride(otherRoot.path());   // the store moves before the landing
    pumpUntilLanded();
    AssetStorePaths::setRootOverride(storeRoot.path());
    CHECK_LOUD(called && dropped.contains(QStringLiteral("store moved")),
               qUtf8Printable(QStringLiteral("F8: the landing into a moved store is refused with its reason ('%1')")
                                  .arg(dropped)));
    bool failedRow = false;
    for (const auto &row : landed) if (!row.ok) failedRow = true;
    CHECK_LOUD(failedRow, "F8: ...and the landed listeners are told the rows failed");
    CHECK_LOUD(!MeshBakeStore::isFresh(conn, storeRoot.path(), source, imported.assetGuid),
               "F8: ...and nothing was committed into either root");

    MeshBakeStore::stopBackgroundRebuild();
    AssetStorePaths::setRootOverride(QString());
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // `--cards` is the meshbake.cards (archived: archive/tests/meshbake) suite; with no argument the binary runs
    // everything meshbake.roundtrip (archived: archive/tests/meshbake) has always run (plus the card SERIALIZER
    // check inside the round trip itself).
    if (argc > 1 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--cards")) {
        std::printf("== 8. surface cards ==\n");
        surfaceCards();
        if (failures) std::printf("FAILED: %d of %d check(s)\n", failures, checks);
        else          std::printf("ALL %d CHECKS PASSED\n", checks);
        return failures ? 1 : 0;
    }
    // ATOM P2's three: the seed's products, the generation the re-bake must
    // supersede, and the winding every shipped mesh owes its own normals.
    if (argc > 1 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--primitives-baked")) {
        std::printf("== 11. every shipped mesh is a baked library asset ==\n");
        primitivesBaked();
        if (failures) std::printf("FAILED: %d of %d check(s)\n", failures, checks);
        else          std::printf("ALL %d CHECKS PASSED\n", checks);
        return failures ? 1 : 0;
    }
    if (argc > 1 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--rebake-generation")) {
        std::printf("== 12. the next generation wins ==\n");
        rebakeGeneration();
        if (failures) std::printf("FAILED: %d of %d check(s)\n", failures, checks);
        else          std::printf("ALL %d CHECKS PASSED\n", checks);
        return failures ? 1 : 0;
    }
    // ATOM-LOD-BOUND-1: the bar, the stand-in the app suite imports, and the terms tool.
    if (argc > 1 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--bound-bar")) {
        std::printf("== 14. the LOD bound's physics bar ==\n");
        boundBar();
        std::printf("== 9. the measured bound, through the bake's own route ==\n");
        errorBound();
        if (failures) std::printf("FAILED: %d of %d check(s)\n", failures, checks);
        else          std::printf("ALL %d CHECKS PASSED\n", checks);
        return failures ? 1 : 0;
    }
    if (argc > 2 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--write-standin"))
        return standin::write(QString::fromLocal8Bit(argv[2])) ? 0 : 1;
    if (argc > 2 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--bound-terms"))
        return boundTerms(QString::fromLocal8Bit(argv[2]));
    if (argc > 1 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--cut-curve-law")) return cutCurveLaw();
    if (argc > 2 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--cut-curve")) {
        QString meshesArg, armName = QStringLiteral("shipped");
        int threads = 4;
        for (int a = 3; a < argc; ++a) {
            const QString arg = QString::fromLocal8Bit(argv[a]);
            if (arg == QStringLiteral("--meshes") && a + 1 < argc) meshesArg = QString::fromLocal8Bit(argv[++a]);
            else if (arg == QStringLiteral("--arm") && a + 1 < argc) armName = QString::fromLocal8Bit(argv[++a]);
            else if (arg == QStringLiteral("--threads") && a + 1 < argc) threads = QString::fromLocal8Bit(argv[++a]).toInt();
            else { std::printf("FAIL: --cut-curve: unknown argument %s\n", qUtf8Printable(arg)); return 1; }
        }
        return cutCurveTool(QString::fromLocal8Bit(argv[2]), meshesArg, armName, threads);
    }
    if (argc > 1 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--dag-reaches-root")) return dagReachesRoot();
    if (argc > 1 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--locked-positions-kept")) return lockedPositionsKept();
    if (argc > 3 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--dag-levels")) {
        // [threads] [--mesh i,j,...]
        int threads = 4;
        QSet<int> only;
        for (int a = 4; a < argc; ++a) {
            const QString arg = QString::fromLocal8Bit(argv[a]);
            if (arg == QStringLiteral("--mesh") && a + 1 < argc)
                for (const QString &n : QString::fromLocal8Bit(argv[++a]).split(QLatin1Char(','))) only.insert(n.toInt());
            else threads = arg.toInt();
        }
        return dagLevels(QString::fromLocal8Bit(argv[2]), QString::fromLocal8Bit(argv[3]), threads, only);
    }
    if (argc > 2 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--drift")) {
        QString meshesArg;
        int threads = 4;
        for (int a = 3; a < argc; ++a) {
            const QString arg = QString::fromLocal8Bit(argv[a]);
            if (arg == QStringLiteral("--meshes") && a + 1 < argc) meshesArg = QString::fromLocal8Bit(argv[++a]);
            else if (arg == QStringLiteral("--threads") && a + 1 < argc) threads = QString::fromLocal8Bit(argv[++a]).toInt();
        }
        return driftTool(QString::fromLocal8Bit(argv[2]), meshesArg, threads);
    }
    if (argc > 3 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--dag-groups")) {
        int threads = 4;
        for (int a = 4; a < argc; ++a)
            if (QString::fromLocal8Bit(argv[a]) == QStringLiteral("--threads") && a + 1 < argc) threads = QString::fromLocal8Bit(argv[++a]).toInt();
        return dagGroupsTool(QString::fromLocal8Bit(argv[2]), QString::fromLocal8Bit(argv[3]).toInt(), threads);
    }
    if (argc > 2 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--atom2")) return atom2::run(QString::fromLocal8Bit(argv[2]));
    if (argc > 2 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--dag-depths")) {
        QStringList paths;
        for (int a = 2; a < argc; ++a) paths << QString::fromLocal8Bit(argv[a]);
        return dagDepths(paths);
    }
    if (argc > 2 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--bake-blob"))
        return bakeBlobTool(QString::fromLocal8Bit(argv[2]), argc > 3 ? QString::fromLocal8Bit(argv[3]).toInt() : 0,
                            argc > 4 ? QString::fromLocal8Bit(argv[4]) : QString());
    if (argc > 1 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--determinism")) {
        std::printf("== 15. the bake does not depend on its threads ==\n");
        bakeDeterminism(argc > 2 ? QString::fromLocal8Bit(argv[2]) : QString());
        if (failures) std::printf("FAILED: %d of %d check(s)\n", failures, checks);
        else          std::printf("ALL %d CHECKS PASSED\n", checks);
        return failures ? 1 : 0;
    }
    if (argc > 1 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--async-landing")) {
        std::printf("== 18. the import bakes' landing ==\n");
        asyncLanding();
        if (failures) std::printf("FAILED: %d of %d check(s)\n", failures, checks);
        else          std::printf("ALL %d CHECKS PASSED\n", checks);
        return failures ? 1 : 0;
    }
    if (argc > 1 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--hemisphere-winding")) {
        std::printf("== 13. winding against declared normals ==\n");
        windingAgreesWithNormals();
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

    std::printf("== 16. a two-sided source material reaches the node ==\n");
    twoSidedReachesTheNode();

    std::printf("== 2-4. determinism, staleness, corruption ==\n");
    determinismAndFailureModes();

    std::printf("== 6. the key ==\n");
    theKey();

    std::printf("== 5. the store ==\n");
    storeIntegration();

    std::printf("== 7. the ATOM LOD chain ==\n");
    lodChain();

    std::printf("== 17. the clip bake and the describe block ==\n");
    clipBake();

    if (failures) std::printf("FAILED: %d of %d check(s)\n", failures, checks);
    else          std::printf("ALL %d CHECKS PASSED\n", checks);
    return failures ? 1 : 0;
}
