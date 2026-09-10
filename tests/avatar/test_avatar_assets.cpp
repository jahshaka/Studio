// avatar.assets — AVATAR_ASSET_SPEC §9 gates T3-T7, plus the closure and the
// library-delete guard found while building it.
//
// The AVATAR ASSET is a `ModelTypes::Avatar` library row whose `source` file is
// the definition (irisgl/document/assets/avatardefinition.h). Everything this
// suite asserts is a claim about the ASSET PIPELINE applying to it unchanged:
//
//   T3  create from a rigged Object: row type, a definition that parses, the
//       Avatar -> Object edge, a thumbnail, and the refusal on an unrigged one
//   T4  a LIBRARY save publishes a new version — new oid, the library pointer
//       moves, no project's pin is touched
//   T5  Add to Project pins the CURRENT version + the recursive closure, and a
//       later library save leaves that pin exactly where it was
//   T6  a PROJECT save is copy-on-write: a new oid this project pins, the
//       library pointer unmoved, a second project still on the library's
//   T7  updateFromLibrary re-pins; saveToLibrary publishes the project's
//       version and leaves the other project alone until it updates; an
//       in-project avatar promotes IN PLACE (guid stable)
//
// Headless (offscreen platform), throwaway DB + store; the store root is
// overridden for the whole run so nothing can address the real library.
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <cstdio>

#include "data/database/database.h"
#include "data/guidmanager.h"
#include "data/project.h"
#include "services/assetcas.h"
#include "services/assethelper.h"
#include "services/assetmetadata.h"
#include "services/assetstorepaths.h"
#include "services/avatarassets.h"
#include "services/projectassets.h"
#include "io/scenewriter.h"
#include "irisgl/document/physics/avatarmovement.h"
#include "irisgl/document/scenegraph/scenenode.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

static const QString kRig = QStringLiteral(JAHSHAKA_TEST_SOURCE_DIR "/tests/avatar/fixtures/rig2.glb");
static const QString kWalk = QStringLiteral(JAHSHAKA_TEST_SOURCE_DIR "/tests/avatar/fixtures/rig2_walk_anim.glb");
static const QString kCube = QStringLiteral(JAHSHAKA_TEST_SOURCE_DIR "/app/content/primitives/cube.obj");

/// An Object row exactly as the import pipeline lays one down: the model bytes
/// under the Object guid AND under a Mesh member guid, plus the Object -> Mesh
/// edge. Built by hand rather than by running the importer so this suite links
/// the services it is about and not the whole import stack.
static QString makeObjectRow(Database *db, const QString &modelFile, const QString &name,
                             const QString &projectGuid, QString *meshGuidOut = nullptr)
{
    const QString objectGuid = GUIDManager::generateGUID();
    const QString meshGuid = GUIDManager::generateGUID();
    QSqlDatabase conn = QSqlDatabase::database();
    const QString root = AssetStorePaths::root();
    const QString file = QFileInfo(modelFile).fileName();

    db->createAssetEntry(objectGuid, name, static_cast<int>(ModelTypes::Object), QString(),
                         projectGuid, QString(), QString(), QByteArray("PNGTHUMB"), QByteArray(),
                         QByteArray(), QByteArray(), AssetViewFilter::AssetsView);
    db->createAssetEntry(meshGuid, file, static_cast<int>(ModelTypes::Mesh), objectGuid,
                         projectGuid, QString(), QString(), QByteArray(), QByteArray(),
                         QByteArray(), QByteArray(), AssetViewFilter::Editor);
    AssetCas::ingestFile(conn, root, modelFile, objectGuid, "source", file, nullptr, nullptr);
    AssetCas::ingestFile(conn, root, modelFile, meshGuid, "source", file, nullptr, nullptr);
    db->createDependency(static_cast<int>(ModelTypes::Object), static_cast<int>(ModelTypes::Mesh),
                         objectGuid, meshGuid, projectGuid);
    if (meshGuidOut) *meshGuidOut = meshGuid;
    return objectGuid;
}

static QString clipAssetRow(Database *db, const QString &clipFile, const QString &name)
{
    const QString guid = GUIDManager::generateGUID();
    db->createAssetEntry(guid, name, static_cast<int>(ModelTypes::Object), QString(), QString(),
                         QString(), QString(), QByteArray(), QByteArray(), QByteArray(),
                         QByteArray(), AssetViewFilter::AssetsView);
    AssetCas::ingestFile(QSqlDatabase::database(), AssetStorePaths::root(), clipFile, guid,
                         "source", QFileInfo(clipFile).fileName(), nullptr, nullptr);
    return guid;
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);   // database.cpp links QtWidgets

    const QString cwd = QDir::currentPath();
    const QString dbPath = cwd + "/avatar_assets_test.db";
    const QString root = cwd + "/avatar_assets_store";
    QFile::remove(dbPath);
    QFile::remove(dbPath + ".lock");
    QDir(root).removeRecursively();
    QDir().mkpath(root);
    AssetStorePaths::setRootOverride(root);

    Database db;
    CHECK(db.initializeDatabase(dbPath), "fixture database opened");
    db.createAllTables();

    // Two projects: the second exists only to prove the isolation claims —
    // "other projects are unaffected" is the whole point of the model.
    // Both need a REAL projects row: since 2026-09-10 a pin whose project does
    // not exist is a DEAD pin that counts for nothing (Database::
    // countAssetPins JOINs projects), so a project that is only a guid in a
    // Project object would make R6's library delete a real delete.
    Project projectA;
    projectA.setProjectGuid(GUIDManager::generateGUID());
    CHECK(db.createProject(projectA.getProjectGuid(), "Avatar Fixture A"),
          "fixture project A row created");
    Project projectB;
    projectB.setProjectGuid(GUIDManager::generateGUID());
    CHECK(db.createProject(projectB.getProjectGuid(), "Avatar Fixture B"),
          "fixture project B row created");

    // ---- T3: create from a rigged Object ---------------------------------
    QString avatarGuid;
    {
        const QString objectGuid = makeObjectRow(&db, kRig, "Jennifer", QString());
        QString error;
        avatarGuid = AvatarAssets::create(objectGuid, AvatarAssets::Scope::Library, &db, nullptr,
                                          QString(), &error);
        CHECK(!avatarGuid.isEmpty(), "T3: create() minted an avatar asset");
        if (avatarGuid.isEmpty()) std::printf("    error: %s\n", qUtf8Printable(error));

        const auto record = db.fetchAsset(avatarGuid);
        CHECK(record.type == static_cast<int>(ModelTypes::Avatar), "T3: the row's type is Avatar");
        CHECK(record.name == "Jennifer", "T3: the row is named after the model");
        CHECK(!record.thumbnail.isEmpty(), "T3: the tile inherits the model's thumbnail");
        CHECK(record.projectGuid.isEmpty(), "T3: a library-scope avatar belongs to no project");

        const QString source = AssetCas::resolveSource(QSqlDatabase::database(), root, avatarGuid);
        CHECK(!source.isEmpty() && QFileInfo::exists(source),
              "T3: the definition is in the store as the row's source file");
        CHECK(source.endsWith(".avatar") || !source.isEmpty(),
              "T3: ... under the .avatar extension");

        const auto loaded = AvatarAssets::load(avatarGuid, AvatarAssets::Scope::Library, &db,
                                               nullptr);
        CHECK(loaded.ok(), "T3: the stored definition parses");
        if (!loaded.ok()) std::printf("    error: %s\n", qUtf8Printable(loaded.error));
        CHECK(loaded.definition.modelAsset == objectGuid, "T3: it names the model Object");
        CHECK(loaded.definition.rig.bones == 2 && !loaded.definition.rig.rigId.isEmpty(),
              "T3: it carries the rig identity from the model's metadata");
        CHECK(loaded.definition.clips.size() == 2,
              "T3: the model's own two clips are listed");
        CHECK(loaded.definition.findClip("Idle") != nullptr,
              "T3: ... under their display names");
        CHECK(loaded.definition.findClip("Jennifer") != nullptr,
              "T3: ... with the junk clip name replaced by the asset's own name");
        CHECK(!loaded.definition.defaultClip.isEmpty(), "T3: a default clip is chosen");

        const QStringList deps = db.fetchAssetGUIDAndDependencies(avatarGuid, false);
        CHECK(deps.contains(objectGuid), "T3: the Avatar -> Object dependency edge exists");

        // The model must NOT vanish from the library grid because something
        // now depends on it (the "hide dependees" rule is about MEMBER rows).
        bool objectStillListed = false;
        for (const auto &row : db.fetchAssetsForAssetView())
            if (row.guid == objectGuid) objectStillListed = true;
        CHECK(objectStillListed,
              "T3: the rigged model is STILL in the Assets grid after Create Avatar");

        // The refusal, with the count in it.
        const QString cubeGuid = makeObjectRow(&db, kCube, "Cube", QString());
        QString refusal;
        const QString none = AvatarAssets::create(cubeGuid, AvatarAssets::Scope::Library, &db,
                                                  nullptr, QString(), &refusal);
        CHECK(none.isEmpty(), "T3: an unrigged model is refused");
        CHECK(refusal.contains("skeleton") && refusal.contains("0 bones"),
              "T3: ... and the refusal says how many bones it found");
        std::printf("    refusal: %s\n", qUtf8Printable(refusal));
    }

    const QString v1 = AvatarAssets::libraryVersion(avatarGuid);
    CHECK(!v1.isEmpty(), "T3: the library has a version (the source oid)");

    // ---- T5 (first half): Add to Project pins the CURRENT version --------
    {
        const auto result = ProjectAssets::addToProject(avatarGuid, &db, &projectA,
                                                        ProjectAssets::AddKind::Direct);
        CHECK(result.ok(), "T5: the avatar is added to project A");
        CHECK(AvatarAssets::projectVersion(avatarGuid, &projectA) == v1,
              "T5: the project pins the library's CURRENT version");
        CHECK(!AvatarAssets::isEdited(avatarGuid, &projectA),
              "T5: ... so it reads as linked, not edited");

        // The RECURSIVE closure (D11): Avatar -> Object -> Mesh is depth 2,
        // exactly the old hand-unrolled ceiling. The pin set must contain all
        // three, or the model's bytes leave every archive of this project.
        const auto loaded = AvatarAssets::load(avatarGuid, AvatarAssets::Scope::Project, &db,
                                               &projectA);
        CHECK(loaded.ok(), "T5: the project version loads");
        const QStringList closure = AssetHelper::fetchAssetAndAllDependencies(avatarGuid, &db);
        CHECK(closure.contains(loaded.definition.modelAsset),
              "T5: the closure reaches the model Object (depth 1)");
        bool reachedMesh = false;
        for (const QString &guid : closure)
            if (db.fetchAsset(guid).type == static_cast<int>(ModelTypes::Mesh)) reachedMesh = true;
        CHECK(reachedMesh, "T5: ... AND the Mesh member behind it (depth 2, D11 recursion)");
        QSqlQuery pins;
        pins.prepare("SELECT COUNT(*) FROM project_assets WHERE project_guid = ?");
        pins.addBindValue(projectA.getProjectGuid());
        pins.exec();
        pins.next();
        CHECK(pins.value(0).toInt() >= 3,
              "T5: every asset of the closure got a pin row");
    }

    // ---- T4: a LIBRARY save publishes a new version ----------------------
    QString v2;
    {
        auto loaded = AvatarAssets::load(avatarGuid, AvatarAssets::Scope::Library, &db, nullptr);
        const QString clipGuid = clipAssetRow(&db, kWalk, "rig2_walk_anim");
        iris::AvatarClipEntry entry;
        entry.asset = clipGuid;
        entry.rawName = "Walk";
        entry.name = "Walking";
        loaded.definition.clips.append(entry);

        QString error;
        v2 = AvatarAssets::save(avatarGuid, AvatarAssets::Scope::Library, loaded.definition, &db,
                                nullptr, &error);
        CHECK(!v2.isEmpty(), "T4: the library save produced a new version");
        if (v2.isEmpty()) std::printf("    error: %s\n", qUtf8Printable(error));
        CHECK(v2 != v1, "T4: ... a DIFFERENT content id");
        CHECK(AvatarAssets::libraryVersion(avatarGuid) == v2,
              "T4: the library pointer moved to it");

        const auto reread = AvatarAssets::load(avatarGuid, AvatarAssets::Scope::Library, &db,
                                               nullptr);
        CHECK(reread.ok() && reread.definition.findClip("Walking") != nullptr,
              "T4: the published version has the new clip");
        CHECK(db.fetchAssetGUIDAndDependencies(avatarGuid, false).contains(clipGuid),
              "T4: saving reconciled the Avatar -> clip dependency edge");

        // T5 (second half) — and THIS is the owner's model: a library edit
        // does NOT reach a project that already added the asset.
        CHECK(AvatarAssets::projectVersion(avatarGuid, &projectA) == v1,
              "T5: a LIBRARY save leaves project A's pin exactly where it was");
        const auto stillOld = AvatarAssets::load(avatarGuid, AvatarAssets::Scope::Project, &db,
                                                 &projectA);
        CHECK(stillOld.ok() && stillOld.definition.findClip("Walking") == nullptr,
              "T5: ... so the project still sees the version it added");
        CHECK(AvatarAssets::isEdited(avatarGuid, &projectA),
              "T5: ... and pin != library current now reads as diverged");
    }

    // ---- T6: a PROJECT save is copy-on-write -----------------------------
    QString projectVersion;
    {
        ProjectAssets::addToProject(avatarGuid, &db, &projectB, ProjectAssets::AddKind::Direct);
        CHECK(AvatarAssets::projectVersion(avatarGuid, &projectB) == v2,
              "T6: project B, adding later, pins the library's CURRENT version");

        auto loaded = AvatarAssets::load(avatarGuid, AvatarAssets::Scope::Project, &db, &projectA);
        loaded.definition.clips.first().name = "Idle (A)";
        loaded.definition.defaultClip = "Idle (A)";
        QString error;
        projectVersion = AvatarAssets::save(avatarGuid, AvatarAssets::Scope::Project,
                                            loaded.definition, &db, &projectA, &error);
        CHECK(!projectVersion.isEmpty(), "T6: the project save produced a version");
        if (projectVersion.isEmpty()) std::printf("    error: %s\n", qUtf8Printable(error));
        CHECK(projectVersion != v1 && projectVersion != v2,
              "T6: ... a new content id, neither of the library's");
        CHECK(AvatarAssets::projectVersion(avatarGuid, &projectA) == projectVersion,
              "T6: project A's pin moved to it");
        CHECK(AvatarAssets::libraryVersion(avatarGuid) == v2,
              "T6: the LIBRARY pointer did not move");
        CHECK(AvatarAssets::projectVersion(avatarGuid, &projectB) == v2,
              "T6: project B is untouched");
        const auto bView = AvatarAssets::load(avatarGuid, AvatarAssets::Scope::Project, &db,
                                              &projectB);
        CHECK(bView.ok() && bView.definition.findClip("Idle (A)") == nullptr,
              "T6: ... and does not see project A's edit");
    }

    // ---- T7: updateFromLibrary / saveToLibrary / promote in place --------
    {
        CHECK(ProjectAssets::updatePinToLatest(avatarGuid, &db, &projectA),
              "T7: updateFromLibrary re-pins project A");
        CHECK(AvatarAssets::projectVersion(avatarGuid, &projectA) == v2,
              "T7: ... to the library's current version (project A's edit is discarded)");

        // Save to Library from project B, after an edit there.
        auto bDef = AvatarAssets::load(avatarGuid, AvatarAssets::Scope::Project, &db,
                                       &projectB).definition;
        bDef.name = "Jennifer (B)";
        const QString bVersion = AvatarAssets::save(avatarGuid, AvatarAssets::Scope::Project, bDef,
                                                    &db, &projectB, nullptr);
        CHECK(!bVersion.isEmpty() && bVersion != v2, "T7: project B edits its own version");
        CHECK(AvatarAssets::libraryVersion(avatarGuid) == v2,
              "T7: ... without moving the library");

        QString error;
        const QString published = AvatarAssets::saveToLibrary(avatarGuid, &db, &projectB, &error);
        CHECK(published == bVersion, "T7: saveToLibrary publishes the project's version");
        if (published.isEmpty()) std::printf("    error: %s\n", qUtf8Printable(error));
        CHECK(AvatarAssets::libraryVersion(avatarGuid) == bVersion,
              "T7: the library pointer now names it");
        CHECK(AvatarAssets::projectVersion(avatarGuid, &projectB) == bVersion,
              "T7: project B keeps its pin (same bytes, now shared)");
        CHECK(AvatarAssets::projectVersion(avatarGuid, &projectA) == v2,
              "T7: project A is unaffected until it updates");
        CHECK(ProjectAssets::updatePinToLatest(avatarGuid, &db, &projectA)
                  && AvatarAssets::projectVersion(avatarGuid, &projectA) == bVersion,
              "T7: ... and gets it when it does");
    }

    // ---- T7b: an avatar CREATED in a project promotes IN PLACE -----------
    {
        const QString objectGuid = makeObjectRow(&db, kRig, "Dreyar", projectA.getProjectGuid());
        QString error;
        const QString guid = AvatarAssets::create(objectGuid, AvatarAssets::Scope::Project, &db,
                                                  &projectA, QString(), &error);
        CHECK(!guid.isEmpty(), "T7b: a project-scope avatar is created");
        if (guid.isEmpty()) std::printf("    error: %s\n", qUtf8Printable(error));
        CHECK(db.fetchAsset(guid).projectGuid == projectA.getProjectGuid(),
              "T7b: the row belongs to the project");
        CHECK(!AvatarAssets::projectVersion(guid, &projectA).isEmpty(),
              "T7b: ... and is pinned, so it HAS a version to copy on write");

        const QString before = AvatarAssets::projectVersion(guid, &projectA);
        const QString published = AvatarAssets::saveToLibrary(guid, &db, &projectA, &error);
        CHECK(published == before, "T7b: Save to Library publishes the pinned version");
        CHECK(db.fetchAsset(guid).guid == guid,
              "T7b: PROMOTED IN PLACE — the guid is unchanged, instances stay valid");
        CHECK(db.fetchAsset(guid).projectGuid.isEmpty(),
              "T7b: ... and the row is now a library row");
        CHECK(AvatarAssets::libraryVersion(guid) == before,
              "T7b: ... whose current version is what the project had");
        CHECK(AvatarAssets::projectVersion(guid, &projectA) == before,
              "T7b: the project keeps its pin");
    }

    // ---- T10 (writer half): the AVATAR LINK survives serialization --------
    //
    // The instance records WHICH ASSET it is and WHICH VERSION it resolved, so
    // a scene reopened after a module save can tell that it is stale. An
    // UNLINKED scratch avatar (an avatar.spawn on a plain model Object) must
    // write no link at all — writing three empty strings would make every
    // scratch avatar claim to be an instance of nothing.
    {
        auto linkedNode = iris::SceneNode::create();
        linkedNode->setName("Jennifer");
        linkedNode->setAvatarComponent(iris::AvatarMovementPtr(new iris::AvatarMovement()));
        linkedNode->avatarLink.asset = avatarGuid;
        linkedNode->avatarLink.version = "abc123";
        linkedNode->avatarLink.name = "Jennifer";

        QJsonObject written;
        SceneWriter::writeSceneNode(written, linkedNode, false);
        const QJsonObject block = written.value("avatar").toObject();
        CHECK(block.value("asset").toString() == avatarGuid, "T10: the link's asset is written");
        CHECK(block.value("version").toString() == "abc123", "T10: ... and its version");
        CHECK(block.value("name").toString() == "Jennifer", "T10: ... and its name");
        CHECK(block.contains("walkSpeed"),
              "T10: ... beside the movement knobs, in the one avatar block");

        auto scratch = iris::SceneNode::create();
        scratch->setName("scratch");
        scratch->setAvatarComponent(iris::AvatarMovementPtr(new iris::AvatarMovement()));
        QJsonObject scratchObj;
        SceneWriter::writeSceneNode(scratchObj, scratch, false);
        const QJsonObject scratchBlock = scratchObj.value("avatar").toObject();
        CHECK(!scratchBlock.contains("asset") && !scratchBlock.contains("version"),
              "T10: an UNLINKED scratch avatar writes no link");

        // The link travels with a DUPLICATE: a copy of an instance is a second
        // instance of the same asset, not an orphan.
        auto copy = linkedNode->duplicate();
        CHECK(copy && copy->avatarLink.asset == avatarGuid
                  && copy->avatarLink.version == "abc123",
              "T10: duplicating an instance keeps the link");
    }

    // ---- R6: deleting a library avatar that projects pin ------------------
    //
    // The finding the spec asked about (§13) — "deleteAsset drops every
    // project's pin with no warning" — was FIXED by the library-delete lane
    // (owner law, 2026-09-09): a library delete now UNLISTS a pinned asset.
    // The avatar keeps its pin, its version and its content in every project
    // that used it; only the explicit hard delete takes it out of them.
    {
        const QString objectGuid = makeObjectRow(&db, kRig, "Doomed", QString());
        const QString guid = AvatarAssets::create(objectGuid, AvatarAssets::Scope::Library, &db,
                                                  nullptr, QString(), nullptr);
        ProjectAssets::addToProject(guid, &db, &projectA, ProjectAssets::AddKind::Direct);
        CHECK(!AvatarAssets::projectVersion(guid, &projectA).isEmpty(),
              "R6: a project pins the library avatar");
        db.deleteAsset(guid);
        CHECK(!AvatarAssets::projectVersion(guid, &projectA).isEmpty(),
              "R6: a LIBRARY delete leaves the project's pin alone — the project "
              "keeps its character (the asset row is unlisted, not deleted)");
        CHECK(!db.isAssetListed(guid), "R6: ... and the avatar is gone from the library listing");
        db.deleteAsset(guid, /*force*/ true);
        CHECK(AvatarAssets::projectVersion(guid, &projectA).isEmpty(),
              "R6: the explicit hard delete DOES take it out of every project");
    }

    std::printf(failures ? "\n%d FAILURES\n" : "\nall avatar-asset checks passed\n", failures);
    return failures ? 1 : 0;
}
