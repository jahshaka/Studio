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
#include "jahshaka/engine/Types.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/document/assets/skeleton.h"
#include "irisgl/document/assets/vertexbuffer.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/import/graphicshelper.h"
#include "irisgl/import/importflags.h"
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
//       lodForCellSize — Photon's voxelizer and its far-field proxy pick a
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
    // whose error is below a world-space cell size.
    jahshaka::engine::MeshData data;
    data.positions.assign(9, 0.0f);
    data.indices = { 0, 1, 2 };
    data.lodIndices = { { 0, 1, 2 }, { 0, 1, 2 }, { 0, 1, 2 } };
    data.lodErrors = { 0.01f, 0.05f, 0.20f };
    CHECK_LOUD(data.lodLevelCount() == 4, "lodLevelCount counts level 0 too");
    CHECK_LOUD(&data.lodLevelIndices(0) == &data.indices, "level 0 IS the mesh's own index list");
    CHECK_LOUD(data.lodForCellSize(0.005f) == 0,
               "a cell finer than every level's error asks for the finest level");
    CHECK_LOUD(data.lodForCellSize(0.02f) == 1, "a 2 cm cell takes the 1 cm level");
    CHECK_LOUD(data.lodForCellSize(0.10f) == 2, "a 10 cm cell takes the 5 cm level");
    CHECK_LOUD(data.lodForCellSize(10.0f) == 3, "a cell coarser than every level takes the coarsest");
    CHECK_LOUD(data.lodForCellSize(0.0f) == 0 && data.lodForCellSize(-1.0f) == 0,
               "a non-positive cell size means the finest, never a wrap-around");
    jahshaka::engine::MeshData plain;
    plain.indices = { 0, 1, 2 };
    CHECK_LOUD(plain.lodLevelCount() == 1 && plain.lodForCellSize(100.0f) == 0,
               "a mesh with no chain has exactly one level at every cell size");
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
