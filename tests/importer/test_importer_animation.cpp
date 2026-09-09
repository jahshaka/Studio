// ANIMATION-ASSET suite (ModelTypes::Animation — the clip file as a library
// type). The owner's finding: 10 of his 23 Mixamo downloads are animation-only
// FBX ("without skin"), and every one of them was refused by the library with
// "may be corrupt or use ... Draco mesh compression" — a guess that was wrong
// about a file that parses perfectly and simply has no geometry.
//
// Sections:
//   1. CLASSIFICATION (animfile::shapeOf) — the structural sniff, with no
//      assimp parse: GLB/glTF by JSON header, binary FBX by the Objects
//      record walk (synthetic containers, both offset widths), COLLADA by the
//      streaming scan, .bvh by format, obj/ply/stl by "that format cannot
//      carry animation", an ASCII FBX by falling through to Unknown.
//   2. READ — the one parse: clip table, clip lengths in seconds, and the
//      clip's own RIG SIGNATURE (two clips on different rigs must not share
//      a rigId).
//   3. IMPORT through the ONE pipeline: an animation-only GLB becomes an
//      Animation row with its source file stored, a pose-strip thumbnail, a
//      metadata block and an "animation" determinism record.
//   4. The CONTROL: the same rig WITH its mesh stays an Object + Mesh member.
//      A file with meshes and animations is scenery that moves.
//   5. .bvh — mocap, which before this type could not be imported AT ALL.
//   6. The REFUSALS, which are the point of the lane: each of the three
//      failure shapes gets a message that says what is actually wrong.
//
// Also runs as a classifier CLI (`--classify FILE...`) so the structural
// sniff can be checked against real user content that is not, and must not
// become, a suite fixture.
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QtEndian>
#include <cstdio>

#include "data/constants.h"
#include "data/database/database.h"
#include "services/animationfile.h"
#include "services/assetcas.h"
#include "services/assetmetadata.h"
#include "services/assetstorepaths.h"
#include "services/import/assetimportservice.h"
#include "services/import/importtypes.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

static const char *shapeName(animfile::Shape shape)
{
    switch (shape) {
    case animfile::Shape::NotAModel: return "NotAModel";
    case animfile::Shape::Geometry: return "Geometry";
    case animfile::Shape::AnimationOnly: return "AnimationOnly";
    case animfile::Shape::Unknown: return "Unknown";
    }
    return "?";
}

static QString fixture(const char *relative)
{
    return QString(JAHSHAKA_TEST_SOURCE_DIR) + QLatin1String(relative);
}

static void writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    file.open(QIODevice::WriteOnly);
    file.write(bytes);
    file.close();
}

// A .glb whose JSON says: no meshes, no animations. Structurally Unknown (it
// is neither), and the import must refuse it by SAYING that.
static QByteArray glbWith(const QByteArray &json)
{
    QByteArray chunk = json;
    while (chunk.size() % 4) chunk.append(' ');
    QByteArray out("glTF");
    QByteArray header(8, '\0');
    qToLittleEndian<quint32>(2, header.data());
    qToLittleEndian<quint32>(quint32(12 + 8 + chunk.size()), header.data() + 4);
    out.append(header);
    QByteArray chunkHeader(8, '\0');
    qToLittleEndian<quint32>(quint32(chunk.size()), chunkHeader.data());
    qToLittleEndian<quint32>(0x4E4F534Au, chunkHeader.data() + 4);
    out.append(chunkHeader);
    out.append(chunk);
    return out;
}

// ---- synthetic binary FBX containers -------------------------------------
//
// The walker reads record HEADERS only, so a container with the right shape is
// all it takes to test it — and a synthetic one can carry exactly the object
// classes a case is about (no 5 MB of real character to ship).
static QByteArray fbxRecord(const QByteArray &name, const QByteArray &children, bool wide)
{
    // [endOffset, numProperties, propertyListLen, nameLen, name, children]
    const int headerBytes = wide ? 25 : 13;
    QByteArray out(headerBytes, '\0');
    out.append(name);
    out.append(children);
    return out;   // end offsets are patched by the assembler below
}

static QByteArray buildFbx(const QStringList &objectClasses, bool wide, quint32 version)
{
    QByteArray file("Kaydara FBX Binary  ");
    file.append('\0');
    file.append(char(0x1A));
    file.append('\0');
    QByteArray versionBytes(4, '\0');
    qToLittleEndian<quint32>(version, versionBytes.data());
    file.append(versionBytes);

    const int headerBytes = wide ? 25 : 13;
    auto writeHeader = [&](QByteArray &into, qint64 endOffset, const QByteArray &name) {
        QByteArray header(headerBytes, '\0');
        if (wide) {
            qToLittleEndian<quint64>(quint64(endOffset), header.data());
            header[24] = char(name.size());
        } else {
            qToLittleEndian<quint32>(quint32(endOffset), header.data());
            header[12] = char(name.size());
        }
        into.append(header);
        into.append(name);
    };

    // One "Objects" record, one child record per class, then the null record
    // that terminates each list. Offsets are absolute, so the record bodies
    // are assembled back to front.
    const qint64 objectsStart = file.size();
    QByteArray objects;
    qint64 cursor = objectsStart + headerBytes + strlen("Objects");
    QByteArray children;
    for (const QString &klass : objectClasses) {
        const QByteArray name = klass.toUtf8();
        const qint64 end = cursor + headerBytes + name.size();
        QByteArray child;
        writeHeader(child, end, name);
        children.append(child);
        cursor = end;
    }
    children.append(QByteArray(headerBytes, '\0'));   // null record ends the child list
    cursor += headerBytes;
    writeHeader(objects, cursor, QByteArrayLiteral("Objects"));
    objects.append(children);
    file.append(objects);
    file.append(QByteArray(headerBytes, '\0'));       // null record ends the top level
    return file;
}

static int runClassifier(int argc, char **argv)
{
    for (int i = 2; i < argc; ++i) {
        const QString path = QString::fromLocal8Bit(argv[i]);
        std::printf("%-14s %s\n", shapeName(animfile::shapeOf(path)),
                    qPrintable(QFileInfo(path).fileName()));
    }
    return 0;
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication app(argc, argv);
    if (argc > 1 && QLatin1String(argv[1]) == QLatin1String("--classify"))
        return runClassifier(argc, argv);

    const QString cwd = QDir::currentPath();
    const QString animGlb = fixture("/tests/avatar/fixtures/rig2_walk_anim.glb");
    const QString mismatchGlb = fixture("/tests/avatar/fixtures/rig_mismatch_anim.glb");
    const QString riggedGlb = fixture("/tests/avatar/fixtures/rig2.glb");
    const QString walkBvh = fixture("/tests/avatar/fixtures/rig2_walk.bvh");
    const QString asciiFbx = fixture("/tests/importer/fixtures/unit_cube_m.fbx");
    CHECK(QFileInfo::exists(animGlb) && QFileInfo::exists(riggedGlb)
              && QFileInfo::exists(walkBvh) && QFileInfo::exists(asciiFbx),
          "fixtures present (animation-only GLB, rigged GLB, .bvh, ASCII FBX)");

    // ---- 1. classification, structural ------------------------------------
    {
        CHECK(animfile::shapeOf(animGlb) == animfile::Shape::AnimationOnly,
              "GLB with 0 meshes + 1 animation classifies AnimationOnly (JSON header)");
        CHECK(animfile::shapeOf(riggedGlb) == animfile::Shape::Geometry,
              "GLB with a mesh classifies Geometry (it stays an Object)");
        CHECK(animfile::shapeOf(walkBvh) == animfile::Shape::AnimationOnly,
              ".bvh classifies AnimationOnly by format (assimp synthesises a mesh for one)");
        CHECK(animfile::shapeOf(fixture("/tests/importer/fixtures/tetra_normals.stl"))
                  == animfile::Shape::Geometry,
              ".stl short-circuits to Geometry (the format carries no animation)");
        CHECK(animfile::shapeOf(fixture("/tests/importer/fixtures/colored_quad.ply"))
                  == animfile::Shape::Geometry,
              ".ply short-circuits to Geometry");
        CHECK(animfile::shapeOf(fixture("/tests/importer/fixtures/textured_pbr_quad.glb"))
                  == animfile::Shape::Geometry,
              "a textured GLB is Geometry");
        CHECK(animfile::shapeOf(fixture("/tests/importer/fixtures/ticks_anim.glb"))
                  == animfile::Shape::Geometry,
              "a GLB with a mesh AND an animation is Geometry (an Object that moves)");
        CHECK(animfile::shapeOf(asciiFbx) == animfile::Shape::Unknown,
              "an ASCII FBX is Unknown (the structural reader declines; read() decides)");
        CHECK(!animfile::isAnimationFile(asciiFbx),
              "... and the parse fallback correctly says it is not a clip file");

        // binary FBX, both offset widths
        const QString fbxGeom = cwd + "/synthetic_geometry.fbx";
        const QString fbxAnim = cwd + "/synthetic_clip.fbx";
        const QString fbxAnimLegacy = cwd + "/synthetic_clip_7400.fbx";
        writeFile(fbxGeom, buildFbx({ "Geometry", "Model", "AnimationStack" }, true, 7700));
        writeFile(fbxAnim, buildFbx({ "Model", "AnimationStack", "AnimationCurve" }, true, 7700));
        writeFile(fbxAnimLegacy, buildFbx({ "Model", "AnimationStack" }, false, 7400));
        CHECK(animfile::shapeOf(fbxGeom) == animfile::Shape::Geometry,
              "binary FBX with a Geometry object is Geometry (record walk, 64-bit offsets)");
        CHECK(animfile::shapeOf(fbxAnim) == animfile::Shape::AnimationOnly,
              "binary FBX with AnimationStack and no Geometry is AnimationOnly");
        CHECK(animfile::shapeOf(fbxAnimLegacy) == animfile::Shape::AnimationOnly,
              "... and the same holds for pre-7500 (32-bit offset) containers");

        // COLLADA, streaming
        const QString daeClip = cwd + "/clip.dae";
        const QString daeModel = cwd + "/model.dae";
        writeFile(daeClip, QByteArrayLiteral(
            "<?xml version=\"1.0\"?><COLLADA><library_animations><animation id=\"a\"/>"
            "</library_animations></COLLADA>"));
        writeFile(daeModel, QByteArrayLiteral(
            "<?xml version=\"1.0\"?><COLLADA><library_geometries><geometry id=\"g\"/>"
            "</library_geometries><library_animations><animation id=\"a\"/>"
            "</library_animations></COLLADA>"));
        CHECK(animfile::shapeOf(daeClip) == animfile::Shape::AnimationOnly,
              "COLLADA with animations and no geometry is AnimationOnly");
        CHECK(animfile::shapeOf(daeModel) == animfile::Shape::Geometry,
              "COLLADA with a <geometry> is Geometry");

        // not a model at all
        CHECK(animfile::shapeOf(cwd + "/nothing.png") == animfile::Shape::NotAModel,
              "a non-model extension is NotAModel (no file read at all)");
    }

    // ---- 2. read(): the clip table and the clip's rig signature -----------
    animfile::Contents walk;
    {
        walk = animfile::read(animGlb);
        CHECK(walk.parsed && walk.isAnimationOnly(),
              "read(): the animation-only GLB parses and reports 0 meshes, >=1 animation");
        CHECK(walk.clips.size() == 1 && walk.clips.first().length > 0.0,
              "read(): one clip with a positive length in SECONDS");
        CHECK(!walk.clips.first().name.isEmpty(),
              "read(): the clip has a display name (junk names fall back to the file's)");
        CHECK(!walk.boneChannelNames.isEmpty() && !walk.rigId.isEmpty(),
              "read(): the clip advertises the bones it drives, and a rig id over them");
        const animfile::Contents foreign = animfile::read(mismatchGlb);
        CHECK(foreign.parsed && !foreign.rigId.isEmpty() && foreign.rigId != walk.rigId,
              "a clip authored on ANOTHER rig gets a different rig id");

        QImage strip;
        animfile::read(animGlb, &strip, 192, 96);
        CHECK(!strip.isNull() && strip.width() == 192 && strip.height() == 96,
              "read(): the pose strip renders at the requested size");
        bool anyLit = false;
        for (int y = 0; y < strip.height() && !anyLit; ++y)
            for (int x = 0; x < strip.width(); ++x)
                if (strip.pixelColor(x, y) != QColor(24, 24, 28)) { anyLit = true; break; }
        CHECK(anyLit, "... and it draws the skeleton, not an empty swatch");
    }

    // ---- the store, for everything below ----------------------------------
    const QString dbPath = cwd + "/importer_animation.db";
    const QString root = cwd + "/importer_animation_store";
    QFile::remove(dbPath);
    QDir(root).removeRecursively();
    QDir().mkpath(root);
    Database db;
    CHECK(db.initializeDatabase(dbPath), "fresh database opened");
    db.createAllTables();
    AssetStorePaths::setRootOverride(root);
    QSqlDatabase conn = QSqlDatabase::database();
    AssetImportService service(&db, nullptr);

    // ---- 3. the import ----------------------------------------------------
    QString clipGuid;
    {
        ImportRequest request;
        request.sourcePath = animGlb;
        const ImportResult result = service.import(request);
        CHECK(result.ok(), "an animation-only GLB imports (it was refused outright before)");
        clipGuid = result.assetGuid;

        const AssetRecord record = db.fetchAsset(clipGuid);
        CHECK(record.type == static_cast<int>(ModelTypes::Animation),
              "the row is an ANIMATION asset, not an Object");
        CHECK(record.name == QFileInfo(animGlb).completeBaseName(),
              "the row takes the file's base name (what binds a clip to a role)");
        CHECK(!record.thumbnail.isEmpty(), "the row carries the pose-strip thumbnail");

        const QString source = AssetCas::resolveSource(conn, root, clipGuid);
        CHECK(!source.isEmpty() && QFileInfo::exists(source),
              "the SOURCE FILE itself is stored (content-addressed, no intermediate)");

        const QJsonObject meta = AssetMetadata::ensure(&db, clipGuid, root);
        CHECK(meta.value("kind").toString() == QLatin1String("animation"),
              "metadata block kind = animation");
        CHECK(meta.value("clips").toArray().size() == 1
                  && meta.value("duration").toDouble() > 0.0,
              "metadata carries the clip table and the duration");
        CHECK(meta.value("rigId").toString() == walk.rigId,
              "metadata carries the clip's rig id (the join key with a rigged model)");

        const QJsonObject record2 = service.importSettings(clipGuid);
        CHECK(record2.value("importer").toString() == QLatin1String("animation"),
              "the determinism record names the animation importer");
        // (the assets.* type NAME is asserted by scripting.e2e.animation_assets,
        // which reads it back through the verb rather than the enum — this
        // target deliberately does not link the scripting module.)
    }

    // ---- 4. the control: meshes + clips stays an Object --------------------
    {
        ImportRequest request;
        request.sourcePath = riggedGlb;
        const ImportResult result = service.import(request);
        CHECK(result.ok(), "the rigged GLB (mesh + 2 clips) still imports");
        const AssetRecord record = db.fetchAsset(result.assetGuid);
        CHECK(record.type == static_cast<int>(ModelTypes::Object),
              "a file with meshes AND animations is still an Object (unchanged)");
        CHECK(!result.meshGuid.isEmpty()
                  && db.fetchAsset(result.meshGuid).type == static_cast<int>(ModelTypes::Mesh),
              "... with its Mesh member row");
        CHECK(service.importSettings(result.assetGuid).value("importer").toString()
                  == QLatin1String("mesh"),
              "... imported by the mesh importer, not the animation one");
    }

    // ---- 5. mocap ---------------------------------------------------------
    {
        ImportRequest request;
        request.sourcePath = walkBvh;
        const ImportResult result = service.import(request);
        CHECK(result.ok(), "a .bvh imports (before this type, no importer sniffed one at all)");
        CHECK(db.fetchAsset(result.assetGuid).type == static_cast<int>(ModelTypes::Animation),
              "... as an Animation asset");
    }

    // ---- 6. the refusals --------------------------------------------------
    {
        const QString emptyGlb = cwd + "/empty.glb";
        writeFile(emptyGlb, glbWith(QByteArrayLiteral(
            "{\"asset\":{\"version\":\"2.0\"},\"scenes\":[{\"nodes\":[]}],\"scene\":0}")));
        ImportRequest request;
        request.sourcePath = emptyGlb;
        const ImportResult result = service.import(request);
        CHECK(!result.ok(), "a model file with neither geometry nor animation is refused");
        CHECK(result.error.contains("no geometry and no animation"),
              "... and the message says exactly that (not 'may be corrupt ... Draco')");

        const QString brokenGlb = cwd + "/broken.glb";
        writeFile(brokenGlb, QByteArrayLiteral("glTFnot-a-chunk-at-all\x01\x02\x03"));
        ImportRequest brokenRequest;
        brokenRequest.sourcePath = brokenGlb;
        const ImportResult broken = service.import(brokenRequest);
        CHECK(!broken.ok() && broken.error.contains("could not be read"),
              "a genuinely unreadable file is refused as unreadable, with assimp's reason");
        CHECK(broken.error.contains("Draco"),
              "... and THAT is where the Draco hint belongs");

        // The type hint can still force the mesh importer; the diagnosis then
        // names the right route instead of guessing.
        ImportRequest forced;
        forced.sourcePath = animGlb;
        forced.typeHint = static_cast<int>(ModelTypes::Mesh);
        const ImportResult forcedResult = service.import(forced);
        CHECK(!forcedResult.ok() && forcedResult.error.contains("Animation asset"),
              "forcing the mesh importer on a clip file points at the animation route");
    }

    std::printf(failures ? "\nFAILURES: %d\n" : "\nall checks passed (%d failures)\n", failures);
    return failures ? 1 : 0;
}
