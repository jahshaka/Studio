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

    bool commit()
    {
        if (!active) return true;
        active = false;
        return db.commit();
    }

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
    QStringList deleteFolderAndDependencies(const QString &guid);
    QStringList deleteAssetAndDependencies(const QString &guid);
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
    /// Guids of LIBRARY rows — view_filter IN (2,3): AssetsView + Effects
    /// (preflight §1.6 — Effects rows ARE library tiles; any store scan that
    /// forgets filter 3 silently skips most of a real library).
    QStringList fetchLibraryAssetGuids();
    QVector<AssetRecord> fetchChildAssets(const QString &parent, const QString &projectGuid, int filter = -1, bool showDependencies = true);
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
    QString fetchAssetGUIDByName(const QString &name, const QString &projectGuid);
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

    QString copyAsset(const ModelTypes &jafType,
                      const QString &guid,
                      const QMap<QString, QString> &newNames,
                      QVector<AssetRecord> &oldAssetRecords,
                      const QString &parent,
					  AssetViewFilter view_filter_to,
					  const QString &projectGuid);

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
	void updateGlobalDependencyDepender(const int &type, const QString &depender, const QString &dependee);
	void updateGlobalDependencyDependee(const int &type, const QString &depender, const QString &dependee);

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
    QSqlDatabase getDb() { return db; }

private:
    bool checkIfVersionSupported(const QString& pathToDb, const QString& table_name);

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
