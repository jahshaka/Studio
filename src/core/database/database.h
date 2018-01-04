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

    void createGlobalDb();
    void createGlobalDbThumbs();
    void createGlobalDbCollections();
	void createGlobalDbAssets();
	void createGlobalDbMaterials();
    void deleteProject();
    void renameProject(const QString&);
	QString insertAssetGlobal(const QString&, int type, const QByteArray &thumbnail);
	QString insertMaterialGlobal(const QString &materialName, const QString &asset_guid, const QByteArray &material);
    void insertSceneGlobal(const QString &world_guid, const QByteArray &sceneBlob);
    void insertCollectionGlobal(const QString &collectionName);
    bool switchAssetCollection(const int, const QString&);
    void insertThumbnailGlobal(const QString &world_guid,
                               const QString &name,
                               const QByteArray &thumbnail);
	QByteArray getMaterialGlobal(const QString &guid) const;
    bool hasCachedThumbnail(const QString& name);
	QVector<AssetData> fetchThumbnails();
    QVector<CollectionData> fetchCollections();
    QVector<ProjectTileData> fetchProjects();
	QVector<AssetTileData> fetchAssets();
    QByteArray getSceneBlobGlobal() const;
    QByteArray fetchCachedThumbnail(const QString& name) const;
    void updateSceneGlobal(const QByteArray &sceneBlob, const QByteArray &thumbnail);
    void createExportScene(const QString& outTempFilePath);
    bool importProject(const QString& inFilePath);

    QSqlDatabase getDb() { return db; }

private:
    QSqlDatabase db;
};

#endif // DATABASE_H
