#ifndef DATABASE_H
#define DATABASE_H

#include <QSqlDatabase>
#include <QSqlDriver>
#include <QSqlError>
#include <QSqlQuery>
#include <QJsonArray>
#include <QCryptographicHash>

#include "../project.h"

// Note that some functions that operate with projects don't accept anything, this is because
// they use the globally available getProjectGuid() which is always set to the current project
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
    void createAllTables();

    // INSERT ===============================================================================
    bool createProject(const QString &guid,
                       const QString &name,
                       const QByteArray &sceneBlob = QByteArray(),
                       const QByteArray &thumbnail = QByteArray());
    bool createFolder(const QString &folderName, const QString &parentFolder, const QString &guid, bool visible = true);
    QString createAssetEntry(const QString &guid,
                             const QString &assetname,
                             const int &type,
                             const QString &parentFolder,
                             const QString &license = QString(),
                             const QString &author = QString(),
                             const QByteArray &thumbnail = QByteArray(),
                             const QByteArray &properties = QByteArray(),
                             const QByteArray &tags = QByteArray(),
                             const QByteArray &asset = QByteArray());
    bool createDependency(const int &dependerType,
                          const int &dependeeType,
                          const QString &depender,
                          const QString &dependee,
                          const QString &projectGuid = QString());

    // DELETE ===============================================================================
    bool deleteProject();
	bool destroyTable(const QString &table);
	void wipeDatabase();
    bool deleteAsset(const QString &guid);
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
    bool renameAsset(const QString &guid, const QString &newName);
    bool updateProject(const QByteArray &sceneBlob, const QByteArray &thumbnail);
    bool updateAssetThumbnail(const QString &guid, const QByteArray &thumbnail);
    bool updateAssetAsset(const QString &guid, const QByteArray &asset);
    bool updateAssetMetadata(const QString &guid, const QString &name, const QByteArray &tags);
    bool updateAssetProperties(const QString &guid, const QByteArray &asset);

    // FETCH ================================================================================
    AssetRecord fetchAsset(const QString &guid);
    QVector<AssetRecord> fetchAssets();
    QVector<AssetRecord> fetchChildAssets(const QString &parent, bool showDependencies = true);
    QVector<AssetRecord> fetchAssetsByCollection(const int &collection_id);
    QVector<AssetRecord> fetchAssetsByType(const int &type);
    QVector<AssetRecord> fetchFilteredAssets(const QString &guid, const int &type);
    QVector<AssetRecord> fetchThumbnails();
    QVector<CollectionRecord> fetchCollections();
    QVector<ProjectTileData> fetchProjects();
    QVector<FolderRecord> fetchChildFolders(const QString &parent);
    QVector<FolderRecord> fetchCrumbTrail(const QString &parent);
    QVector<AssetRecord> fetchAssetThumbnails(const QStringList &guids);
    QByteArray fetchAssetData(const QString &guid) const;

    QByteArray fetchCachedThumbnail(const QString& name) const;
    QStringList fetchFolderNameByParent(const QString &guid);
    QStringList fetchAssetNameByParent(const QString &guid);
    QStringList fetchFolderAndChildFolders(const QString &guid);
    QStringList fetchChildFolderAssets(const QString &guid);
    QStringList fetchAssetGUIDAndDependencies(const QString &guid, bool appendSelf = true);
    QVector<DependencyRecord> fetchAssetDependencies(const AssetRecord &record);
    QStringList fetchAssetDependenciesByType(const QString &guid, const ModelTypes&);
    QStringList fetchAssetAndDependencies(const QString &guid);
    QString fetchAssetGUIDByName(const QString &name);
    QString fetchObjectMesh(const QString &guid, const int ertype, const int eetype);
    QString fetchMeshObject(const QString &guid, const int ertype, const int eetype);

    QStringList hasMultipleDependers(const QString &guid);
    bool hasDependencies(const QString &guid);

    // IMPORT ===============================================================================
    bool importProject(const QString &inFilePath, const QString &newGuid, QString &worldName, QMap<QString, QString> &assetGuids);
    QString importAsset(const ModelTypes &jafType,
                        const QString &pathToDb,
                        const QMap<QString, QString> &newNames,
                        QMap<QString, QString> &outGuids,
                        QVector<AssetRecord> &assetRecords,
                        const QString &parent = QString());

    QString copyAsset(const ModelTypes &jafType,
                      const QString &guid,
                      const QMap<QString, QString> &newNames,
                      QVector<AssetRecord> &oldAssetRecords,
                      const QString &parent);

    // EXPORT ===============================================================================
    void createExportScene(const QString& outTempFilePath);
    void createExportNode(const ModelTypes &type, const QString& objectGuid, const QString& outTempFilePath);
    void createExportNodes(const ModelTypes &type, const QStringList& objectGuids, const QString& outTempFilePath);

    int getTableCount();
    bool checkIfTableExists(const QString &tableName);

    QString getVersion();

    QByteArray getSceneBlobGlobal() const;
	void updateGlobalDependencyDepender(const int &type, const QString &depender, const QString &dependee);
	void updateGlobalDependencyDependee(const int &type, const QString &depender, const QString &dependee);

	QString getDependencyByType(const int &type, const QString &depender);

    // MISC
	void updateAuthorInfo(const QString &author_name);
	bool isAuthorInfoPresent();
	QString getAuthorName();
    void insertCollectionGlobal(const QString &collectionName);
    bool switchAssetCollection(const int, const QString&);
    void insertThumbnailGlobal(const QString &world_guid,
                               const QString &name,
                               const QByteArray &thumbnail,
							   const QString &thumbnail_guid);
    bool hasCachedThumbnail(const QString& name);

	bool checkIfRecordExists(const QString &record, const QVariant &value, const QString &table);
    bool checkIfDependencyExists(const QString &depender, const ModelTypes &type);

    QSqlDatabase getDb() { return db; }

private:
    QString projectsTableSchema;
    QString thumbnailsTableSchema;
    QString collectionsTableSchema;
    QString assetsTableSchema;
    QString dependenciesTableSchema;
    QString authorTableSchema;
    QString foldersTableSchema;
    QString metadataTableSchema;

    QSqlDatabase db;
};

#endif // DATABASE_H
