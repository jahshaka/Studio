/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef DATABASE_H
#define DATABASE_H

#include <QSqlDatabase>
#include <QSqlDriver>
#include <QSqlError>
#include <QSqlQuery>
#include <QJsonArray>
#include <QCryptographicHash>

#include "data/project.h"

#include "irisgl/irisglfwd.h"

// RAII transaction over one QSqlDatabase connection (ASSET_PIPELINE_SPEC
// phase 0): begin on construction, roll back on destruction unless commit()
// was called — so every early-return path inside a multi-statement DB
// operation unwinds cleanly instead of leaving half the rows behind (and a
// .jaf import stops paying one fsync per row).  If the driver can't start a
// transaction (nested, or non-transactional driver) the guard degrades to a
// no-op: statements simply autocommit as before.
class DbTransaction
{
public:
    explicit DbTransaction(QSqlDatabase database)
        : db(database), active(database.transaction())
    {
    }

    ~DbTransaction()
    {
        if (active) db.rollback();
    }

    // On a FAILED commit the guard rolls back itself, so the connection is
    // never left mid-transaction and a caller's post-failure cleanup reads
    // COMMITTED state (the import rollback depended on that and never got
    // it — deep audit 2026-09, area 6).
    bool commit()
    {
        if (!active) return true;
        active = false;
        if (db.commit()) return true;
        db.rollback();
        return false;
    }

    // Explicit early rollback: ends the transaction NOW instead of at
    // destruction. The one caller that needs it is failure cleanup that has to
    // query the database — inside an open transaction such a query sees the
    // caller's own uncommitted rows, which is exactly how the failed-import
    // object cleanup became a no-op. Idempotent; a no-op on a degraded guard.
    void rollback()
    {
        if (!active) return;
        active = false;
        db.rollback();
    }

    /// True while this guard owns a live transaction (false when it degraded
    /// to a no-op because the connection was already in one, or closed).
    bool isActive() const { return active; }

    DbTransaction(const DbTransaction &) = delete;
    DbTransaction &operator=(const DbTransaction &) = delete;

private:
    QSqlDatabase db;
    bool active;
};

// Every project-scoped function takes the project guid explicitly — the data
// layer no longer reads the app's ambient current project (SCRIPTING_SPEC
// §1.6.1 / APP_ARCHITECTURE_AUDIT §2.5; callers pass
// Globals::project->getProjectGuid() until the hub dissolves).
// Also note there are some general functions such as deleteRecord(...) that can delete a record
// from any table however there will always exist the explicit function which is preferred
// The general variants are better for use with in memory databases and followup queries
class Database
{
public:
    Database();
    ~Database();

    bool executeAndCheckQuery(QSqlQuery&, const QString&);

    // MANAGE ===============================================================================
    bool initializeDatabase(const QString &pathToBlob);
    void closeDatabase();

    // CREATE ===============================================================================
    bool createProjectsTable();
    bool createThumbnailsTable();
    bool createCollectionsTable();
    bool createAssetsTable();
    bool createDependenciesTable();
    bool createAuthorTable();
    bool createFoldersTable();
    bool createMetadataTable();
    bool createFavoritesTable();
    void createAllTables();
    void createIndexes();
    void createCasTables();

    // INSERT ===============================================================================
    bool createProject(const QString &guid,
                       const QString &name,
                       const QByteArray &sceneBlob = QByteArray(),
                       const QByteArray &thumbnail = QByteArray());
    bool createFolder(const QString &folderName, const QString &parentFolder, const QString &guid, const QString &projectGuid, bool visible = true);
    QString createAssetEntry(const QString &guid,
                             const QString &assetname,
                             const int &type,
                             const QString &parentFolder,
                             const QString &projectGuid,
                             const QString &license = QString(),
                             const QString &author = QString(),
                             const QByteArray &thumbnail = QByteArray(),
                             const QByteArray &properties = QByteArray(),
                             const QByteArray &tags = QByteArray(),
                             const QByteArray &asset = QByteArray(),
							 const AssetViewFilter view_filter = AssetViewFilter::Editor);

	QString createAssetEntry(const QString &projectGuid,
							 const QString &guid,
							 const QString &assetname,
							 const int &type,
							 const QByteArray &asset = QByteArray(),
							 const QByteArray &properties = QByteArray(),
							 const AssetViewFilter view_filter = AssetViewFilter::Editor);

    bool createDependency(const int &dependerType,
                          const int &dependeeType,
                          const QString &depender,
                          const QString &dependee,
                          const QString &projectGuid = QString());

    bool addFavorite(const QString &guid);
    bool removeFavorite(const QString &guid);

    /// Creates a drawer (ASSET_DRAWERS_SPEC §2). parent -1 = top level; any
    /// other parent must be an existing drawer. Returns the new drawer's id,
    /// or -1 on failure.
    int createCollection(const QString &collectionName, const int parent = -1);

    // DELETE ===============================================================================
    bool deleteProject(const QString &guid);
	bool destroyTable(const QString &table);
	void wipeDatabase();
    bool deleteAsset(const QString &guid);
    /// Deletes a drawer AND its sub-drawers; every asset of the subtree moves
    /// to Uncategorized (0). Ids <= 0 (the root and Uncategorized) are refused.
    bool deleteCollection(const int &collectionId);
    bool deleteFolder(const QString &guid);
    bool deleteDependency(const QString &dependee);
    bool deleteDependency(const QString &depender, const QString &dependee);
    bool removeDependenciesByType(const QString &depender, const ModelTypes &type);
    /// The returned list is the on-disk files the caller should unlink. The
    /// optional `ok` reports whether every row actually went: a delete that
    /// could not run (closed connection, failed statement) must never look
    /// like a successful one to the caller.
    QStringList deleteFolderAndDependencies(const QString &guid, bool *ok = nullptr);
    QStringList deleteAssetAndDependencies(const QString &guid, bool *ok = nullptr);
    bool deleteRecord(const QString &table, const QString &row, const QVariant &value);

    // UPDATE ===============================================================================
    bool renameProject(const QString &guid, const QString &newName);
    bool renameFolder(const QString &guid, const QString &newName);
    bool renameCollection(const int &collectionId, const QString &newName);
    /// Reparents a drawer (parent -1 = top level). Refuses ids <= 0, unknown
    /// drawers/parents, and cycles (a drawer cannot move under its own subtree).
    bool setCollectionParent(const int collectionId, const int parent);
    bool renameAsset(const QString &guid, const QString &newName);
    bool updateProject(const QByteArray &sceneBlob, const QByteArray &thumbnail, const QString &projectGuid);
    bool updateProjectBlob(const QByteArray &sceneBlob, const QString &projectGuid);
    bool updateAssetThumbnail(const QString &guid, const QByteArray &thumbnail);
    bool updateAssetAsset(const QString &guid, const QByteArray &asset);
    bool updateSceneThumbnail(const QString &guid, const QByteArray &asset);
    bool updateAssetMetadata(const QString &guid, const QString &name, const QByteArray &tags);
    bool updateAssetProperties(const QString &guid, const QByteArray &asset);
	bool updateAssetViewFilter(const QString& guid, const int& filter);
	bool updateProjectDesktop(const QString &guid, int desktop);
	bool updateProjectPosition(const QString &guid, float x, float y);
	// slider mode (DESKTOP_SLIDER_SPEC.md): filmstrip {row, orderIndex}
	bool updateProjectSliderPos(const QString &guid, int row, int index);
	void updateSchema();
	bool updateMetadataVersion(const QString &version);

    // FETCH ================================================================================
    AssetRecord fetchAsset(const QString &guid);
    QVector<AssetRecord> fetchAssetsForAssetView();
    /// Stored bytes per asset (sum of its linked CAS objects' sizes), for the
    /// Assets page's list view Size column. guid → bytes; assets with no
    /// stored content are absent.
    QMap<QString, qint64> fetchAssetFileSizes();
    /// Guids of LIBRARY rows — view_filter IN (2,3): AssetsView + Effects
    /// (preflight §1.6 — Effects rows ARE library tiles; any store scan that
    /// forgets filter 3 silently skips most of a real library).
    QStringList fetchLibraryAssetGuids();
    QVector<AssetRecord> fetchChildAssets(const QString &parent, const QString &projectGuid, int filter = -1, bool showDependencies = true);
    /// Reference-with-pin membership (ASSET_PIPELINE_SPEC §3.1.5): the LIBRARY
    /// assets this project pinned (project_assets rows), as full catalog
    /// records. `includeDependencies` false drops rows that exist only as a
    /// dependency of another asset (matching fetchChildAssets' semantics).
    /// This is THE source assets.list({scope:'project'}) and the editor's
    /// project panel share.
    QVector<AssetRecord> fetchProjectPinnedAssets(const QString &projectGuid, bool includeDependencies = true);
    QVector<AssetRecord> fetchAssetsFromParent(const QString &guid);
	QVector<AssetRecord> fetchAssetsByType(const int &type, const QString &projectGuid);
	QVector<AssetRecord> fetchAssetsByViewFilter(const AssetViewFilter& filter);
    QVector<AssetRecord> fetchFilteredAssets(const QString &guid, const int &type);
    QVector<AssetRecord> fetchThumbnails();
    QVector<AssetRecord> fetchFavorites();
    QVector<CollectionRecord> fetchCollections();
    /// The drawer plus all its descendants (breadth-first); empty when the id
    /// names no drawer row.
    QVector<int> fetchCollectionSubtree(const int collectionId);
    /// How many asset rows live in these drawers (the delete confirm).
    int countAssetsInCollections(const QVector<int> &collectionIds);
    // desktop <= 0 fetches every project (legacy behaviour); desktop 1..N filters
    // to that desktop, treating an absent/NULL desktop column value as Desktop 1.
    QVector<ProjectTileData> fetchProjects(int desktop = 0);
    QVector<FolderRecord> fetchChildFolders(const QString &parent, const QString &projectGuid);
    QVector<FolderRecord> fetchCrumbTrail(const QString &parent, const QString &projectGuid);
    QVector<AssetRecord> fetchAssetThumbnails(const QStringList &guids);
    QByteArray fetchAssetData(const QString &guid) const;

    QByteArray fetchCachedThumbnail(const QString& name) const;
    QStringList fetchFolderNameByParent(const QString &guid);
    QStringList fetchAssetNameByParent(const QString &guid);
    QStringList fetchFolderAndChildFolders(const QString &guid);
    QStringList fetchChildFolderAssets(const QString &guid);
    QStringList fetchAssetGUIDAndDependencies(const QString &guid, bool appendSelf = true);
    QStringList fetchAssetAndAllDependencies(const QString &guid);
    QVector<DependencyRecord> fetchAssetDependencies(const AssetRecord &record);
    QStringList fetchAssetDependeesByType(const QString &guid, const ModelTypes&);
    QStringList fetchAssetAndDependencies(const QString &guid);
    /// STATIC because it always was one in fact — it runs a query on the
    /// default connection and touches no member. It is also called through a
    /// NULL Database* today (SceneWriter's `handle` static is unset in the
    /// preset-apply path), which was undefined behaviour that happened to work;
    /// as a static member that call is merely pointless, not undefined.
    static QString fetchAssetGUIDByName(const QString &name, const QString &projectGuid);
    QString fetchObjectMesh(const QString &guid, const int ertype, const int eetype);
    QString fetchMeshObject(const QString &guid, const int ertype, const int eetype);

    QStringList hasMultipleDependers(const QString &guid);
    bool hasDependencies(const QString &guid);
	DatabaseMetadataRecord getDbMetadata();

    // IMPORT ===============================================================================
    bool importProject(const QString &inFilePath, const QString &newGuid, QString &worldName, QMap<QString, QString> &assetGuids);
    QString importAsset(const ModelTypes &jafType,
                        const QString &pathToDb,
                        const QMap<QString, QString> &newNames,
                        QMap<QString, QString> &outGuids,
                        QVector<AssetRecord> &assetRecords,
						AssetViewFilter view_filter_to,
                        const QString &projectGuid,
                        const QString &parent = QString());

    QString importAssetBundle(const QString &pathToDb,
                             const QMap<QString, QString> &newNames,
                             QMap<QString, QString> &outGuids,
                             QVector<AssetRecord> &assetRecords,
                             const QString &projectGuid,
                             const QString &parent = QString());


    // EXPORT ===============================================================================
    bool createBlobFromNode(const iris::SceneNodePtr &node, const QString &writePath);
    bool createBlobFromAsset(const QString &guid, const QString &writePath);

    void createExportScene(const QString& outTempFilePath, const QString &projectGuid);
    void createExportBundle(const QStringList& objectGuids, const QString& outTempFilePath);

    int getTableCount();
    bool checkIfTableExists(const QString &tableName);
    bool checkIfColumnExists(const QString &tableName, const QString &columnName);
    // Guarded, idempotent schema evolution for the projects table (desktops feature).
    // Runs on every startup via createAllTables; ALTERs only when a column is missing.
    void migrateProjectsTable();
    // Same contract for the collections table (asset drawers: parent column).
    void migrateCollectionsTable();
    void migrateAssetsTable();

    QString getVersion();

    QByteArray getSceneBlobGlobal(const QString &projectGuid) const;
	/// Both return false when the UPDATE did not run (they used to be void and
	/// had never worked — positional SQL bound by name, see the .cpp).
	bool updateGlobalDependencyDepender(const int &type, const QString &depender, const QString &dependee);
	bool updateGlobalDependencyDependee(const int &type, const QString &depender, const QString &dependee);

	QString getDependencyByType(const int &type, const QString &depender);

    // MISC
	void updateAuthorInfo(const QString &author_name);
	bool isAuthorInfoPresent();
	QString getAuthorName();
    bool switchAssetCollection(const int, const QString&);
    void insertThumbnailGlobal(const QString &world_guid,
                               const QString &name,
                               const QByteArray &thumbnail,
							   const QString &thumbnail_guid);
    bool hasCachedThumbnail(const QString& name);

	bool checkIfRecordExists(const QString &record, const QVariant &value, const QString &table, bool perProject = false, const QString &projectGuid = QString());
    bool checkIfDependencyExists(const QString &depender, const ModelTypes &type);
	bool checkIfDependencyExists(const QString& depender, const QString& dependee);
    bool checkIfProjectVersionSupported(const QString& pathToDb);
    bool checkIfJafModelVersionSupported(const QString& pathToDb);

    /// Is a `.jaf`/project archive's recorded CONTENT_VERSION string ("0.9.1b",
    /// "1.0.0", …) new enough to open? Encodes MIN_JAF_VERSION's historic
    /// major*10 + minor packing as an ordered (major, minor) compare, parsed
    /// with integers — the old code ran the first three characters through
    /// toFloat() and multiplied by 10, which happened to land on the right
    /// integer only because 0.9f*10 rounds up to exactly 9.0f, and rejected
    /// every 0.10.x archive outright ("0.1" → 1). Static + public so the gate
    /// is unit-testable without a database file (deep audit 2026-09, area 6).
    static bool jafVersionAccepted(const QString &version);

    QSqlDatabase getDb() { return db; }

private:
    bool checkIfVersionSupported(const QString& pathToDb, const QString& table_name);

    /// SIDECAR LIFECYCLE (invariant I2 — deep audit 2026-09, area 6).
    ///
    /// <store>/sidecar/<guid>.json is the record rebuildCatalog reconstructs
    /// the library from, so it has to track the catalog, not just its birth:
    /// sidecars used to be written at IMPORT ONLY, which left 41 of the
    /// owner's 86 naming deleted assets and every rename/retag/refile invisible
    /// to recovery. Every catalog mutation that changes a field the sidecar
    /// records now refreshes it, and deleteAsset removes it.
    ///
    /// Both are best-effort and deliberately quiet: an offline store or an
    /// asset with no sidecar must never turn a successful database write into
    /// a failure (the row is the truth; the sidecar is its backup).
    /// Does the ACTIVE store root belong with THIS catalog for this guid?
    /// (A sidecar already there, or objects this catalog records present
    /// under it.) Guards both calls below — see the definition for why a
    /// guid-only key would have every unit test write into the developer's
    /// live library.
    bool sidecarBelongsHere(const QString &guid);
    void refreshSidecar(const QString &guid);
    void dropSidecar(const QString &guid);

    QString projectsTableSchema;
    QString thumbnailsTableSchema;
    QString collectionsTableSchema;
    QString assetsTableSchema;
    QString dependenciesTableSchema;
    QString authorTableSchema;
    QString foldersTableSchema;
    QString metadataTableSchema;
    QString favoritesTableSchema;

	QString version080SchemaUpdate;
	QString version080SchemaDowngrade;

    QSqlDatabase db;
};

#endif // DATABASE_H
