#ifndef DATABASE_H
#define DATABASE_H

#include <QSqlDatabase>
#include <QSqlDriver>
#include <QSqlError>
#include <QSqlQuery>
#include <QCryptographicHash>

#include "../project.h"

class Database
{
public:
    Database();
    ~Database();

    bool executeAndCheckQuery(QSqlQuery&, const QString&);
    void initializeDatabase(QString name);
    void closeDb();

    bool checkIfTableExists(const QString &tableName);

	void createGlobalDependencies();
	void insertGlobalDependency(const int &type, const QString &depender, const QString &dependee, const QString &project_id = QString());

	QString getDependencyByType(const int &type, const QString &depender);

    void createGlobalDb();
    void createGlobalDbThumbs();
    void createGlobalDbCollections();
	void createGlobalDbAssets();
	void createGlobalDbAuthor();
	void createGlobalDbFolders();
	void updateAuthorInfo(const QString &author_name);
	bool isAuthorInfoPresent();
	QString getAuthorName();
    void deleteProject();
    bool deleteAsset(const QString &guid);
	bool deleteFolder(const QString &guid);
    void renameProject(const QString&);
	void updateAssetThumbnail(const QString guid, const QByteArray &thumbnail);
	void updateAssetAsset(const QString guid, const QByteArray &asset);
	QString insertFolder(const QString&, const QString&, const QString&);
	QString insertAssetGlobal(const QString&, int type, const QString&, const QByteArray &thumbnail = QByteArray(),
		const QByteArray &properties = QByteArray(), const QByteArray &tags = QByteArray(),
		const QByteArray &asset = QByteArray(), const QString &author = QString());
	QString createAssetEntry(const QString&, const QString&, int type, const QString &, const QString&, const QByteArray &thumbnail = QByteArray(),
		const QByteArray &properties = QByteArray(), const QByteArray &tags = QByteArray(),
		const QByteArray &asset = QByteArray(), const QString &author = QString());
	void insertProjectAssetGlobal(const QString&, int type, const QByteArray &thumbnail,
							      const QByteArray &properties, const QByteArray &tags,
								  const QByteArray &asset, const QString &guid);
	QString insertMaterialGlobal(const QString &materialName, const QString &asset_guid, const QByteArray &material);
	QString insertProjectMaterialGlobal(const QString &materialName, const QString &asset_guid, const QByteArray &material);
    void insertSceneGlobal(const QString &world_guid, const QByteArray &sceneBlob, const QByteArray &thumb);
    void insertCollectionGlobal(const QString &collectionName);
    bool switchAssetCollection(const int, const QString&);
    void insertThumbnailGlobal(const QString &world_guid,
                               const QString &name,
                               const QByteArray &thumbnail,
							   const QString &thumbnail_guid);
	QByteArray getMaterialGlobal(const QString &guid) const;
	QByteArray getAssetMaterialGlobal(const QString &guid) const;
    bool hasCachedThumbnail(const QString& name);
	QVector<AssetData> fetchThumbnails();
	QVector<AssetData> fetchFilteredAssets(const QString &guid, int type);
    QVector<CollectionData> fetchCollections();
    QVector<ProjectTileData> fetchProjects();
	QVector<AssetTileData> fetchAssets();
	AssetTileData fetchAsset(const QString &guid);
	QVector<FolderData> fetchChildFolders(const QString &parent);
	QVector<FolderData> fetchCrumbTrail(const QString &parent);
	QVector<AssetTileData> fetchChildAssets(const QString &parent, bool showDependencies = true);
	QVector<AssetTileData> fetchAssetsByCollection(int collection_id);
	QVector<AssetData> fetchAssetThumbnails(const QStringList& guids);
    QByteArray getSceneBlobGlobal() const;
    QByteArray fetchCachedThumbnail(const QString& name) const;
	void updateAssetMetadata(const QString &guid, const QString &name, const QByteArray &tags);
    void updateSceneGlobal(const QByteArray &sceneBlob, const QByteArray &thumbnail);
    void createExportScene(const QString& outTempFilePath);
    bool importProject(const QString& inFilePath);

	QStringList fetchFolderAndChildFolders(const QString &guid);
	QStringList fetchChildFolderAssets(const QString &guid);

	bool deleteDependency(const QString &dependee);

	QStringList fetchAssetGUIDAndDependencies(const QString &guid);
	QStringList fetchAssetAndDependencies(const QString &guid);
	QStringList deleteFolderAndDependencies(const QString &guid);
	QStringList deleteAssetAndDependencies(const QString &guid);
	QString fetchAssetGUIDByName(const QString &name);

	QString fetchObjectMesh(const QString &guid, const int type);
	QString fetchMeshObject(const QString &guid, const int type);

	bool renameFolder(const QString &guid, const QString &newName);
	bool renameAsset(const QString &guid, const QString &newName);

    QSqlDatabase getDb() { return db; }

private:
    QSqlDatabase db;
};

#endif // DATABASE_H
