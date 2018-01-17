#include "database.h"
#include "../../constants.h"
#include "../../irisgl/src/irisglfwd.h"
#include "../../irisgl/src/core/irisutils.h"
#include "../../globals.h"
#include "../guidmanager.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlRecord>
#include <QDateTime>
#include <QMessageBox>

Database::Database()
{
    if (!QSqlDatabase::isDriverAvailable(Constants::DB_DRIVER)) irisLog("DB driver not present!");

    db = QSqlDatabase::addDatabase(Constants::DB_DRIVER);
}

Database::~Database()
{
    auto connection = db.connectionName();
    db.close();
    db = QSqlDatabase();
    db.removeDatabase(connection);
}

bool Database::executeAndCheckQuery(QSqlQuery &query, const QString& name)
{
    if (!query.exec()) {
        irisLog(name + " + Query failed to execute: " + query.lastError().text());
        return false;
    }

    return true;
}

void Database::initializeDatabase(QString name)
{
    db.setDatabaseName(name);
    if (!db.open()) {
        irisLog( "Couldn't open a DB connection. " + db.lastError().text());
    }
}

void Database::closeDb()
{
    auto connection = db.connectionName();
    db.close();
    db = QSqlDatabase();
    db.removeDatabase(connection);
}

bool Database::checkIfTableExists(const QString &tableName)
{
    QSqlQuery query;
    query.prepare("SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name = ?;");
    query.addBindValue(tableName);

    if (query.exec()) {
        if (query.first()) {
            return query.value(0).toBool();
        }
    }
    else {
        irisLog("There was an error getting the material blob! " + query.lastError().text());
    }

    return false;
}

void Database::createGlobalDb() {
    QString schema = "CREATE TABLE IF NOT EXISTS " + Constants::DB_PROJECTS_TABLE + " ("
                     "    name              VARCHAR(64),"
                     "    thumbnail         BLOB,"
                     "    last_accessed     DATETIME,"
                     "    last_written      DATETIME,"
                     "    date_created      DATETIME DEFAULT CURRENT_TIMESTAMP,"
                     "    scene             BLOB,"
                     "    version           REAL,"
                     "    description       TEXT,"
                     "    url               TEXT,"
                     "    guid              VARCHAR(32) PRIMARY KEY"
                     ")";

    QSqlQuery query;
    query.prepare(schema);
    executeAndCheckQuery(query, "createGlobalDb");
}

void Database::createGlobalDbThumbs() {
    QString schema = "CREATE TABLE IF NOT EXISTS " + Constants::DB_THUMBS_TABLE + " ("
                     "    name              VARCHAR(128),"
                     "    world_guid        VARCHAR(32),"
                     "    thumbnail         BLOB,"
                     "    last_written      DATETIME,"
                     "    hash              VARCHAR(16),"
                     "    guid              VARCHAR(32) PRIMARY KEY"
                     ")";

    QSqlQuery query;
    query.prepare(schema);
    executeAndCheckQuery(query, "createGlobalDbThumbs");
}

void Database::createGlobalDbCollections()
{
    if (!checkIfTableExists(Constants::DB_COLLECT_TABLE)) {
        QString schema = "CREATE TABLE IF NOT EXISTS " + Constants::DB_COLLECT_TABLE + " ("
            "    name              VARCHAR(128),"
            "    date_created      DATETIME DEFAULT CURRENT_TIMESTAMP,"
            "    collection_id     INTEGER PRIMARY KEY"
            ")";

        QSqlQuery query;
        query.prepare(schema);
        executeAndCheckQuery(query, "createGlobalDbCollections");

        QSqlQuery query2;
        query.prepare("INSERT INTO " + Constants::DB_COLLECT_TABLE +
            " (name, date_created, collection_id)" +
            " VALUES (:name, datetime(), 0)");
        query.bindValue(":name", "Uncategorized");

        executeAndCheckQuery(query, "insertSceneCollection");
    }
}

/*
 *	properties is a json object that currently holds
 *  1. the camera orientation
 *  2. number of textures
 *	3. polygon count
 */
void Database::createGlobalDbAssets() {
	QString schema = "CREATE TABLE IF NOT EXISTS " + Constants::DB_ASSETS_TABLE + " ("
		"    name              VARCHAR(128),"
		"    extension		   VARCHAR(8),"
		"	 type			   INTEGER,"
		"	 collection		   INTEGER,"
		"	 times_used		   INTEGER,"	
		"    world_guid        VARCHAR(32),"
		"    thumbnail         BLOB,"
		"    date_created      DATETIME DEFAULT CURRENT_TIMESTAMP,"
		"    last_updated      DATETIME,"
		"	 author			   VARCHAR(128),"
		"    license		   VARCHAR(64),"
		"    hash              VARCHAR(16),"
		"    version           REAL,"
		"    tags			   BLOB,"
		"    properties        BLOB,"
		"    guid              VARCHAR(32) PRIMARY KEY"
		")";

	QSqlQuery query;
	query.prepare(schema);
	executeAndCheckQuery(query, "createGlobalDbAssets");
}

void Database::createGlobalDbMaterials()
{
	QString schema = "CREATE TABLE IF NOT EXISTS materials ("
		"    name              VARCHAR(128),"
		"	 type			   INTEGER,"
		"	 collection		   INTEGER,"
		"	 times_used		   INTEGER,"
		"	 model_guid		   VARCHAR(32),"
		"    world_guid        VARCHAR(32),"
		"    thumbnail         BLOB,"
		"    material		   BLOB,"
		"    date_created      DATETIME DEFAULT CURRENT_TIMESTAMP,"
		"    last_updated      DATETIME,"
		"    version           REAL,"
		"    asset_guid        VARCHAR(32),"
		"    guid              VARCHAR(32) PRIMARY KEY"
		")";

	QSqlQuery query;
	query.prepare(schema);
	executeAndCheckQuery(query, "createGlobalDbMaterials");
}

void Database::createGlobalDbAuthor()
{
	QString schema = "CREATE TABLE IF NOT EXISTS author ("
		"    name              VARCHAR(128),"
		"    default_license   VARCHAR(24),"
		"    date_created      DATETIME DEFAULT CURRENT_TIMESTAMP,"
		"    last_updated      DATETIME,"
		"    version           REAL"
		")";

	QSqlQuery query;
	query.prepare(schema);
	executeAndCheckQuery(query, "createGlobalDbAuthor");
}

void Database::updateAuthorInfo(const QString &author_name)
{
	QSqlQuery query1;
	query1.prepare("DELETE FROM author");
	executeAndCheckQuery(query1, "wipeTable");

	QSqlQuery query2;
	query2.prepare("INSERT INTO author (name, date_created, default_license) VALUES (:name, datetime(), :default_license)");
	query2.bindValue(":name", author_name);
	query2.bindValue(":default_license", "CCBY");
	executeAndCheckQuery(query2, "insertAuthorName");
}

bool Database::isAuthorInfoPresent()
{
	QSqlQuery query;
	query.prepare("SELECT COUNT(*) FROM author");
	executeAndCheckQuery(query, "authorCount");

	if (query.exec()) {
		if (query.first()) {
			return query.value(0).toBool();
		}
	}
	else {
		irisLog("There was an error getting the author count! " + query.lastError().text());
	}

	return false;
}

QString Database::getAuthorName()
{
	QSqlQuery query;
	query.prepare("SELECT name FROM author LIMIT 1");
	executeAndCheckQuery(query, "getAuthorName");

	if (query.exec()) {
		if (query.first()) {
			return query.value(0).toString();
		}
	}
	else {
		irisLog("There was an error getting the author count! " + query.lastError().text());
	}

	return QString();
}

QString Database::insertMaterialGlobal(const QString &materialName, const QString &asset_guid, const QByteArray &material)
{
	QSqlQuery query;
	auto guid = GUIDManager::generateGUID();
	query.prepare("INSERT INTO materials (name, date_created, material, asset_guid, guid) "
			      "VALUES (:name, datetime(), :material, :asset_guid, :guid)");
	query.bindValue(":name", materialName);
	query.bindValue(":material", material);
	query.bindValue(":asset_guid", asset_guid);
	query.bindValue(":guid", guid);

	executeAndCheckQuery(query, "insertMaterialGlobal");

	return guid;
}

void Database::deleteProject()
{
    QSqlQuery query;
    query.prepare("DELETE FROM " + Constants::DB_PROJECTS_TABLE + " WHERE guid = ?");
    query.addBindValue(Globals::project->getProjectGuid());
    executeAndCheckQuery(query, "deleteProject");
}

bool Database::deleteAsset(const QString &guid)
{
    QSqlQuery query;
    query.prepare("DELETE FROM " + Constants::DB_ASSETS_TABLE + " WHERE guid = ?");
    query.addBindValue(guid);

    QSqlQuery query2;
    query2.prepare("DELETE FROM materials WHERE asset_guid = ?");
    query2.addBindValue(guid);
    
    bool da = executeAndCheckQuery(query, "deleteAsset");
    bool dm = executeAndCheckQuery(query, "deleteMaterial");

    return da && dm;
}

void Database::renameProject(const QString &newName)
{
    QSqlQuery query;
    query.prepare("UPDATE " + Constants::DB_PROJECTS_TABLE + " SET name = ? WHERE guid = ?");
    query.addBindValue(newName);
    query.addBindValue(Globals::project->getProjectGuid());
    executeAndCheckQuery(query, "renameProject");
}

void Database::insertCollectionGlobal(const QString &collectionName)
{
    QSqlQuery query;
    auto guid = GUIDManager::generateGUID();
    query.prepare("INSERT INTO " + Constants::DB_COLLECT_TABLE +
        " (name, date_created)" +
        " VALUES (:name, datetime())");
    query.bindValue(":name", collectionName);

    executeAndCheckQuery(query, "insertSceneCollection");
}

bool Database::switchAssetCollection(const int id, const QString &guid)
{
    QSqlQuery query;
    query.prepare("UPDATE " + Constants::DB_ASSETS_TABLE + " SET collection = ?, last_updated = datetime() WHERE guid = ?");
    query.addBindValue(id);
    query.addBindValue(guid);

    return executeAndCheckQuery(query, "switchAssetCollection");
}

QString Database::insertAssetGlobal(const QString &assetName,
									int type,
									const QByteArray &thumbnail,
									const QByteArray &properties,
									const QByteArray &tags)
{
	QSqlQuery query;
	auto guid = GUIDManager::generateGUID();
	query.prepare("INSERT INTO " + Constants::DB_ASSETS_TABLE +
		" (name, extension, thumbnail, type, collection, version, date_created,"
		" last_updated, guid, properties, author, license, tags)" +
		" VALUES (:name, :extension, :thumbnail, :type, 0, :version, datetime(),"
		" datetime(), :guid, :properties, :author, :license, :tags)");

	QFileInfo assetInfo(assetName);

	query.bindValue(":name", assetInfo.baseName());
	query.bindValue(":extension", assetInfo.suffix());
	query.bindValue(":thumbnail", thumbnail);
	query.bindValue(":type", type);
	query.bindValue(":version", Constants::CONTENT_VERSION);
	query.bindValue(":guid", guid);
	query.bindValue(":properties", properties);

	query.bindValue(":author", getAuthorName());
	query.bindValue(":license", "CCBY");
	query.bindValue(":tags", tags);

	executeAndCheckQuery(query, "insertSceneAsset");

    return guid;
}

void Database::insertSceneGlobal(const QString &projectName, const QByteArray &sceneBlob, const QByteArray &thumb)
{
    QSqlQuery query;
    auto guid = GUIDManager::generateGUID();
    query.prepare("INSERT INTO " + Constants::DB_PROJECTS_TABLE                 +
                  " (name, scene, thumbnail, version, last_accessed, last_written, guid)"   +
                  " VALUES (:name, :scene, :thumb, :version, datetime(), datetime(), :guid)");
    query.bindValue(":name",    projectName);
    query.bindValue(":scene",   sceneBlob);
	query.bindValue(":thumb",	thumb);
    query.bindValue(":version", Constants::CONTENT_VERSION);
    query.bindValue(":guid",    guid);

    executeAndCheckQuery(query, "insertSceneGlobal");

    Globals::project->setProjectGuid(guid);
}

void Database::insertThumbnailGlobal(const QString &world_guid,
                                     const QString &name,
                                     const QByteArray &thumbnail)
{
    QSqlQuery query;
    query.prepare("INSERT INTO " + Constants::DB_THUMBS_TABLE + " (world_guid, name, thumbnail, guid)"
                  " VALUES (:world_guid, :name, :thumbnail, :guid)");
    query.bindValue(":world_guid",  world_guid);
    query.bindValue(":thumbnail",   thumbnail);
    query.bindValue(":name",        name);
    query.bindValue(":guid",        GUIDManager::generateGUID());

    executeAndCheckQuery(query, "insertThumbnailGlobal");
}

QByteArray Database::getMaterialGlobal(const QString &guid) const
{
	QSqlQuery query;
	query.prepare("SELECT material FROM materials WHERE asset_guid = ?");
	query.addBindValue(guid);

	if (query.exec()) {
		if (query.first()) {
			return query.value(0).toByteArray();
		}
	}
	else {
		irisLog("There was an error getting the material blob! " + query.lastError().text());
	}

	return QByteArray();
}

bool Database::hasCachedThumbnail(const QString &name)
{
    QSqlQuery query;
    query.prepare("SELECT EXISTS (SELECT 1 FROM " + Constants::DB_THUMBS_TABLE + " WHERE name = ? LIMIT 1)");
    query.addBindValue(name);

    if (query.exec()) {
        if (query.first()) {
            return query.record().value(0).toBool();
        }
    } else {
        irisLog("hasCachedThumbnail query failed! " + query.lastError().text());
    }

    return false;
}

QVector<AssetData> Database::fetchThumbnails()
{
	QSqlQuery query;
	query.prepare("SELECT name, thumbnail, guid, type, extension FROM " + Constants::DB_ASSETS_TABLE);
	executeAndCheckQuery(query, "fetchAssetData");

	QVector<AssetData> tileData;
	while (query.next()) {
		AssetData data;
		QSqlRecord record = query.record();
		for (int i = 0; i < record.count(); i++) {
			data.name		= record.value(0).toString();
			data.thumbnail	= record.value(1).toByteArray();
			data.guid		= record.value(2).toString();
			data.type		= record.value(3).toInt();
			data.extension  = record.value(4).toString();
		}

		tileData.push_back(data);
	}

	return tileData;
}

QVector<CollectionData> Database::fetchCollections()
{
    QSqlQuery query;
    query.prepare("SELECT name, collection_id FROM " + Constants::DB_COLLECT_TABLE + " ORDER BY name, date_created DESC");
    executeAndCheckQuery(query, "fetchCollections");

    QVector<CollectionData> tileData;
    while (query.next()) {
        CollectionData data;
        QSqlRecord record = query.record();
        for (int i = 0; i < record.count(); i++) {
            data.name = record.value(0).toString();
            data.id = record.value(1).toInt();
        }

        tileData.push_back(data);
    }

    return tileData;
}

QVector<ProjectTileData> Database::fetchProjects()
{
    QSqlQuery query;
    query.prepare("SELECT name, thumbnail, guid FROM " + Constants::DB_PROJECTS_TABLE + " ORDER BY last_written DESC");
    executeAndCheckQuery(query, "fetchProjects");

    QVector<ProjectTileData> tileData;
    while (query.next())  {
        ProjectTileData data;
        QSqlRecord record = query.record();
        for (int i = 0; i < record.count(); i++) {
            data.name       = record.value(0).toString();
            data.thumbnail  = record.value(1).toByteArray();
            data.guid       = record.value(2).toString();
        }

        tileData.push_back(data);
    }

    return tileData;
}

QVector<AssetTileData> Database::fetchAssets()
{
	QSqlQuery query;
	query.prepare("SELECT assets.name, (assets.guid || '.' || assets.extension) as full_filename,"
				  " assets.thumbnail, assets.guid, collections.name as collection_name,"
				  " assets.type, assets.collection, assets.properties, assets.author, assets.license, assets.tags"
				  " FROM assets"
                  " INNER JOIN " + Constants::DB_COLLECT_TABLE + " ON assets.collection = collections.collection_id ORDER BY assets.name DESC");
	executeAndCheckQuery(query, "fetchAssets");

	QVector<AssetTileData> tileData;
	while (query.next()) {
		AssetTileData data;
		QSqlRecord record = query.record();
		for (int i = 0; i < record.count(); i++) {
			data.name = record.value(0).toString();
			data.full_filename = record.value(1).toString();
			data.thumbnail = record.value(2).toByteArray();
			data.guid = record.value(3).toString();
            data.collection_name = record.value(4).toString();
			data.type = record.value(5).toInt();
			data.collection = record.value(6).toInt();
			data.properties = record.value(7).toByteArray();
			data.author = record.value(8).toString();
			data.license = record.value(9).toString();
			data.tags = record.value(10).toByteArray();
		}

		Globals::assetNames.insert(data.guid, data.name);

		tileData.push_back(data);
	}

	return tileData;
}

QVector<AssetTileData> Database::fetchAssetsByCollection(int collection_id)
{
	QSqlQuery query;
	query.prepare("SELECT assets.name, (assets.guid || '.' || assets.extension) as full_filename,"
		" assets.thumbnail, assets.guid, collections.name as collection_name, assets.type,"
		" assets.author, assets.license, assets.tags"
		" FROM assets"
		" INNER JOIN " + Constants::DB_COLLECT_TABLE + " ON assets.collection = collections.collection_id ORDER BY assets.name DESC WHERE assets.collection_id = ?");
	query.addBindValue(collection_id);
	executeAndCheckQuery(query, "fetchAssetsByCollection");

	QVector<AssetTileData> tileData;
	while (query.next()) {
		AssetTileData data;
		QSqlRecord record = query.record();
		for (int i = 0; i < record.count(); i++) {
			data.name = record.value(0).toString();
			data.full_filename = record.value(1).toString();
			data.thumbnail = record.value(2).toByteArray();
			data.guid = record.value(3).toString();
			data.collection_name = record.value(4).toString();
			data.type = record.value(5).toInt();
		}

		Globals::assetNames.insert(data.guid, data.name);

		tileData.push_back(data);
	}

	return tileData;
}

QByteArray Database::getSceneBlobGlobal() const
{
    QSqlQuery query;
    query.prepare("SELECT scene FROM " + Constants::DB_PROJECTS_TABLE + " WHERE guid = ?");
    query.addBindValue(Globals::project->getProjectGuid());

    if (query.exec()) {
        if (query.first()) {
            return query.value(0).toByteArray();
        }
    } else {
        irisLog("There was an error getting the scene blob! " + query.lastError().text());
    }

    return QByteArray();
}

QByteArray Database::fetchCachedThumbnail(const QString &name) const
{
    QSqlQuery query;
    query.prepare("SELECT thumbnail FROM " + Constants::DB_THUMBS_TABLE + " WHERE name = ?");
    query.addBindValue(name);

    if (query.exec()) {
        if (query.first()) {
            return query.value(0).toByteArray();
        }
    } else {
        irisLog(
            "There was an error fetching a thumbnail for a model (" + name + ")" + query.lastError().text()
        );
    }

    return QByteArray();
}

void Database::updateSceneGlobal(const QByteArray &sceneBlob, const QByteArray &thumbnail)
{
    QSqlQuery query;
    query.prepare("UPDATE " + Constants::DB_PROJECTS_TABLE + " SET scene = ?, last_written = datetime(), thumbnail = ? WHERE guid = ?");
    query.addBindValue(sceneBlob);
    query.addBindValue(thumbnail);
    query.addBindValue(Globals::project->getProjectGuid());

    executeAndCheckQuery(query, "updateSceneGlobal");
}

void Database::createExportScene(const QString &outTempFilePath)
{
    QSqlQuery query;
    query.prepare("SELECT name, scene, thumbnail, last_written, last_accessed, guid FROM " +
                  Constants::DB_PROJECTS_TABLE + " WHERE guid = ?");
    query.addBindValue(Globals::project->getProjectGuid());

    if (query.exec()) {
        query.next();
    } else {
        irisLog(
            "There was an error fetching a row to be exported " + query.lastError().text()
        );
    }

    auto sceneName  = query.value(0).toString();
    auto sceneBlob  = query.value(1).toByteArray();
    auto sceneThumb = query.value(2).toByteArray();
    auto sceneLastW = query.value(3).toDateTime();
    auto sceneLastA = query.value(4).toDateTime();
    auto sceneGuid  = query.value(5).toString();

    QSqlDatabase dbe = QSqlDatabase::addDatabase(Constants::DB_DRIVER, "myUniqueSQLITEConnection");
    dbe.setDatabaseName(QDir(outTempFilePath).filePath(Globals::project->getProjectName() + ".db"));
    dbe.open();

    QString schema = "CREATE TABLE IF NOT EXISTS " + Constants::DB_PROJECTS_TABLE + " ("
                     "    name              VARCHAR(64),"
                     "    thumbnail         BLOB,"
                     "    last_accessed     DATETIME,"
                     "    last_written      DATETIME,"
                     "    date_created      DATETIME DEFAULT CURRENT_TIMESTAMP,"
                     "    scene             BLOB,"
                     "    version           REAL,"
                     "    description       TEXT,"
                     "    url               TEXT,"
                     "    guid              VARCHAR(32) PRIMARY KEY"
                     ")";

    QSqlQuery query2(dbe);
    query2.prepare(schema);
    executeAndCheckQuery(query2, "createExportGlobalDb");

    QSqlQuery query3(dbe);
    query3.prepare("INSERT INTO " + Constants::DB_PROJECTS_TABLE +
                   " (name, scene, thumbnail, last_written, last_accessed, guid)" +
                   " VALUES (:name, :scene, :thumbnail, :last_written, :last_accessed, :guid)");
    query3.bindValue(":name",           sceneName);
    query3.bindValue(":scene",          sceneBlob);
    query3.bindValue(":thumbnail",      sceneThumb);
    query3.bindValue(":last_written",   sceneLastW);
    query3.bindValue(":last_accessed",  sceneLastA);
    query3.bindValue(":guid",           sceneGuid);

    executeAndCheckQuery(query3, "insertSceneGlobal");

    dbe.close();
}

bool Database::importProject(const QString &inFilePath)
{
    QSqlDatabase dbe = QSqlDatabase::addDatabase(Constants::DB_DRIVER, "myUniqueSQLITEImportConnection");
    dbe.setDatabaseName(inFilePath + ".db");
    dbe.open();

    QSqlQuery query(dbe);
    query.prepare("SELECT name, scene, thumbnail, last_written, last_accessed, guid FROM " + Constants::DB_PROJECTS_TABLE);

    if (query.exec()) {
        query.next();
    } else {
        irisLog(
            "There was an error fetching a record to be imported " + query.lastError().text()
        );
    }

    auto sceneName  = query.value(0).toString();
    auto sceneBlob  = query.value(1).toByteArray();
    auto sceneThumb = query.value(2).toByteArray();
    auto sceneLastW = query.value(3).toDateTime();
    auto sceneLastA = query.value(4).toDateTime();
    auto sceneGuid  = query.value(5).toString();

    dbe.close();

    QSqlQuery query2;
    query2.prepare("SELECT EXISTS (SELECT 1 FROM " + Constants::DB_PROJECTS_TABLE + " WHERE guid = ? LIMIT 1)");
    query2.addBindValue(sceneGuid);

    bool exists = false;
    if (query2.exec()) {
        if (query2.first()) {
            exists = query2.record().value(0).toBool();
        }
    } else {
        irisLog("hasExistingProject query failed! " + query2.lastError().text());
    }

    if (exists) {
        QMessageBox::StandardButton reply;
        reply = QMessageBox::question(Q_NULLPTR,
                                      "Project Exists",
                                      "This project already exists, Replace it?",
                                      QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
        if (reply == QMessageBox::Yes) {
            QSqlQuery query31;
            query31.prepare("UPDATE " + Constants::DB_PROJECTS_TABLE + " SET scene = ?, thumbnail = ? WHERE guid = ?");
            query31.addBindValue(sceneBlob);
            query31.addBindValue(sceneThumb);
            query31.addBindValue(sceneGuid);

            executeAndCheckQuery(query31, "updateinsertSceneGlobal");

            Globals::project->setProjectGuid(sceneGuid);
            return true;
        } else if (reply == QMessageBox::No) {
            return false;
        }
    } else {
        QSqlQuery query3;
        query3.prepare("INSERT INTO " + Constants::DB_PROJECTS_TABLE                    +
                       " (name, scene, thumbnail, last_written, last_accessed, guid)"   +
                       " VALUES (:name, :scene, :thumbnail, :last_written, :last_accessed, :guid)");
        query3.bindValue(":name",           sceneName);
        query3.bindValue(":scene",          sceneBlob);
        query3.bindValue(":thumbnail",      sceneThumb);
        query3.bindValue(":last_written",   sceneLastW);
        query3.bindValue(":last_accessed",  sceneLastA);
        query3.bindValue(":guid",           sceneGuid);

        executeAndCheckQuery(query3, "insertSceneGlobal");

        Globals::project->setProjectGuid(sceneGuid);
        return true;
    }

    return false;
}
