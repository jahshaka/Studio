/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "data/database/database.h"
#include "data/database/casschema.h"
#include "data/constants.h"
#include "irisgl/irisglfwd.h"
#include "data/guidmanager.h"
#include "io/assetmanager.h"
#include "services/assethelper.h"
#include "io/scenewriter.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSqlRecord>
#include <QDateTime>
#include <QMessageBox>
#include <QUuid>

Database::Database()
{
    projectsTableSchema =
        "CREATE TABLE IF NOT EXISTS projects ("
        "    name              VARCHAR(64),"
        "    last_accessed     DATETIME,"
        "    last_written      DATETIME,"
        "    date_created      DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "    version           VARCHAR(8),"
        "    description       TEXT,"
        "    url               TEXT,"
        "    guid              VARCHAR(32) PRIMARY KEY,"
        "    thumbnail         BLOB,"
        "    scene             BLOB,"
        "    desktop           INTEGER DEFAULT 1,"   // which desktop the tile lives on (1..4)
        "    desktop_x         REAL,"                // freeform position, normalized 0..1
        "    desktop_y         REAL"                 // NULL = never placed (cascade assigns)
        ")";

    thumbnailsTableSchema = 
        "CREATE TABLE IF NOT EXISTS thumbnails ("
        "    name              VARCHAR(128),"
        "    world_guid        VARCHAR(32),"
        "    last_written      DATETIME,"
        "    hash              VARCHAR(16),"
        "    guid              VARCHAR(32) PRIMARY KEY,"
        "    thumbnail         BLOB"
        ")";

    collectionsTableSchema =
        "CREATE TABLE IF NOT EXISTS collections ("
        "    name              VARCHAR(128),"
        "    date_created      DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "    collection_id     INTEGER PRIMARY KEY,"
        "    parent            INTEGER DEFAULT -1"     // drawer nesting; -1 = top level
        ")";

    assetsTableSchema = 
        "CREATE TABLE IF NOT EXISTS assets ("
        "    guid              VARCHAR(32) PRIMARY KEY,"
        "	 type			   INTEGER,"
        "    name              VARCHAR(128),"
        "	 collection		   INTEGER,"
        "	 times_used		   INTEGER,"
        "    project_guid      VARCHAR(32),"
        "    date_created      DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "    last_updated      DATETIME,"
        "	 author			   VARCHAR(128),"
        "    license		   VARCHAR(64),"
        "    hash              VARCHAR(16),"
        "    version           VARCHAR(8),"
        "    parent            VARCHAR(32),"
        "    thumbnail         BLOB,"
        "    asset             BLOB,"
        "    tags			   BLOB,"
        "    properties        BLOB,"
		"    view_filter       INTEGER"
        ")";

    dependenciesTableSchema =
        "CREATE TABLE IF NOT EXISTS dependencies ("
        "	 depender_type  INTEGER,"
        "	 dependee_type  INTEGER,"
        "    project_guid	VARCHAR(32),"
        "    depender		VARCHAR(32),"
        "    dependee		VARCHAR(32),"
        "    id				VARCHAR(32) PRIMARY KEY"
        ")";

    authorTableSchema =
        "CREATE TABLE IF NOT EXISTS author ("
        "    name              VARCHAR(128),"
        "    default_license   VARCHAR(24),"
        "    date_created      DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "    last_updated      DATETIME,"
        "    version           VARCHAR(8)"
        ")";

    foldersTableSchema = 
        "CREATE TABLE IF NOT EXISTS folders ("
        "    guid              VARCHAR(32) PRIMARY KEY,"
        "    parent            VARCHAR(32),"
        "    project_guid      VARCHAR(32),"
        "    name              VARCHAR(128),"
        "    date_created      DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "    last_updated      DATETIME,"
        "    hash              VARCHAR(16),"
        "    version           VARCHAR(8),"
        "    count			   INTEGER,"
        "    visible           INTEGER"
        ")";

	metadataTableSchema =
		"CREATE TABLE IF NOT EXISTS metadata ("
		"    date_created      DATETIME DEFAULT CURRENT_TIMESTAMP,"
		"    hash              VARCHAR(16),"
		"    version           VARCHAR(8),"
		"    data			   BLOB"
		")";

    favoritesTableSchema =
        "CREATE TABLE IF NOT EXISTS favorites ("
        "    asset_guid        VARCHAR(32) PRIMARY KEY,"
        "    date_created      DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "    version           VARCHAR(8),"
        "    name              VARCHAR(256),"
        "    hash              VARCHAR(16),"
        "    thumbnail		   BLOB"
        ")";

	// Schema updates
	version080SchemaUpdate = "ALTER TABLE assets ADD COLUMN view_filter INTEGER;";
	version080SchemaDowngrade = "ALTER TABLE assets DROP COLUMN view_filter;";
}

Database::~Database()
{
}

bool Database::executeAndCheckQuery(QSqlQuery &query, const QString& name)
{
    if (!query.exec()) {
        irisLog(QString("%1 query failed to execute! %2").arg(name, query.lastError().text()));
        return false;
    }

    return true;
}

// Note that this is the default connection, any queries called without
// an explicit connection will use this database
bool Database::initializeDatabase(const QString &pathToBlob)
{
    if (!QSqlDatabase::isDriverAvailable(Constants::DB_DRIVER)) {
        irisLog("DB driver not present!");
        return false;
    }

    db = QSqlDatabase::addDatabase(Constants::DB_DRIVER);
    db.setDatabaseName(pathToBlob);

    if (db.isValid()) {
        if (!db.open()) irisLog(QString("Couldn't open a database connection! %1").arg(db.lastError().text()));
        return db.isOpen();
    }
    else {
        irisLog(QString("The database connection is invalid! %1").arg(db.lastError().text()));
    }

    return false;
}

void Database::closeDatabase()
{
    if (db.isOpen()) db.close();
    db = QSqlDatabase(); // important that we make an invalid object
    QSqlDatabase::removeDatabase(db.connectionName());
}

int Database::getTableCount()
{
	QSqlQuery query;
	query.prepare("SELECT COUNT(*) FROM sqlite_master WHERE type = 'table'");
	if (query.exec()) {
		if (query.first()) return query.value(0).toInt();
	}
	else {
		irisLog(QString("There was an error getting the table count! ").arg(query.lastError().text()));
	}

	return 0;
}

bool Database::checkIfTableExists(const QString &tableName)
{
    QSqlQuery query;
    query.prepare("SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name = ?");
    query.addBindValue(tableName);

    if (query.exec()) {
        if (query.first()) return query.value(0).toBool();
    }
    else {
        irisLog(
            QString("There was an error checking if table %1 exists)").arg(tableName, query.lastError().text())
        );
    }

    return false;
}

bool Database::checkIfColumnExists(const QString &tableName, const QString &columnName)
{
    QSqlQuery query;
    // PRAGMA doesn't take bound parameters; table names here are compile-time constants
    query.prepare(QString("PRAGMA table_info(%1)").arg(tableName));

    if (query.exec()) {
        while (query.next()) {
            // table_info columns: cid, name, type, notnull, dflt_value, pk
            if (query.value(1).toString().compare(columnName, Qt::CaseInsensitive) == 0) return true;
        }
    }
    else {
        irisLog(
            QString("There was an error checking if column %1.%2 exists! %3")
                .arg(tableName, columnName, query.lastError().text())
        );
    }

    return false;
}

void Database::migrateProjectsTable()
{
    // Desktops feature (DESKTOPS_SPEC.md): existing libraries gain the columns in place.
    // Guarded by a column-existence check so this is idempotent and downgrade-safe —
    // ALTER TABLE ADD COLUMN with a DEFAULT means pre-existing rows read back as 1.
    if (!checkIfColumnExists("projects", "desktop")) {
        QSqlQuery query;
        query.prepare("ALTER TABLE projects ADD COLUMN desktop INTEGER DEFAULT 1");
        executeAndCheckQuery(query, "MigrateProjectsAddDesktop");
    }

    if (!checkIfColumnExists("projects", "desktop_x")) {
        QSqlQuery query;
        query.prepare("ALTER TABLE projects ADD COLUMN desktop_x REAL");
        executeAndCheckQuery(query, "MigrateProjectsAddDesktopX");
    }

    if (!checkIfColumnExists("projects", "desktop_y")) {
        QSqlQuery query;
        query.prepare("ALTER TABLE projects ADD COLUMN desktop_y REAL");
        executeAndCheckQuery(query, "MigrateProjectsAddDesktopY");
    }

    // Slider mode (DESKTOP_SLIDER_SPEC.md): filmstrip {row, orderIndex} per
    // tile. NULL = never assigned; the layout model seeds those on first show.
    if (!checkIfColumnExists("projects", "slider_row")) {
        QSqlQuery query;
        query.prepare("ALTER TABLE projects ADD COLUMN slider_row INTEGER");
        executeAndCheckQuery(query, "MigrateProjectsAddSliderRow");
    }

    if (!checkIfColumnExists("projects", "slider_index")) {
        QSqlQuery query;
        query.prepare("ALTER TABLE projects ADD COLUMN slider_index INTEGER");
        executeAndCheckQuery(query, "MigrateProjectsAddSliderIndex");
    }
}

void Database::migrateCollectionsTable()
{
    // Asset drawers (ASSET_DRAWERS_SPEC.md §2): collections gain nesting in
    // place. Additive and guarded like the desktops migration above — existing
    // user collections read back parent -1 and keep working at top level.
    if (!checkIfColumnExists("collections", "parent")) {
        QSqlQuery query;
        query.prepare("ALTER TABLE collections ADD COLUMN parent INTEGER DEFAULT -1");
        executeAndCheckQuery(query, "MigrateCollectionsAddParent");
    }
}

void Database::migrateAssetsTable()
{
    // Material-asset data heal: a shifted argument in the preset-apply
    // registration (fixed alongside the PBR-materials data-loss audit) wrote
    // every material definition into the TAGS column, leaving the ASSET
    // column empty — so saved materials hydrated broken and could never be
    // re-applied. Move the stranded JSON where it belongs. Idempotent: after
    // the move the WHERE clause matches nothing, and rows written correctly
    // (asset populated) are untouched.
    QSqlQuery query;
    query.prepare(
        "UPDATE assets SET asset = tags, tags = NULL "
        "WHERE type = ? AND (asset IS NULL OR length(asset) = 0) "
        "AND tags LIKE '{%'"
    );
    query.addBindValue(static_cast<int>(ModelTypes::Material));
    executeAndCheckQuery(query, "MigrateAssetsMaterialData");
}

QString Database::getVersion()
{
    //QSqlQuery pquery;
    //pquery.prepare("SELECT COUNT(*) FROM projects");
    //executeAndCheckQuery(pquery, "projectsCount");

    //bool getVersion = false;
    //if (pquery.exec()) {
    //    if (pquery.first()) {
    //        getVersion = pquery.value(0).toBool();
    //    }
    //}
    //else {
    //    irisLog("There was an error getting the projects count! " + pquery.lastError().text());
    //}

    //if (getVersion) {
    //    QSqlQuery query1;
    //    query1.prepare("SELECT version FROM projects LIMIT 1");

    //    if (query1.exec()) {
    //        if (query1.first()) {
    //            return query1.value(0).toString();
    //        }
    //    }
    //    else {
    //        irisLog("There was an error getting the db version! " + query1.lastError().text());
    //    }
    //}

    return QString();
}

void Database::updateGlobalDependencyDepender(const int &ertype, const QString & depender, const QString & dependee)
{
    QSqlQuery query;
    auto guid = GUIDManager::generateGUID();
    query.prepare("UPDATE dependencies SET depender = ? WHERE depender_type = ? AND dependee = ?");

    query.bindValue(":depender", depender);
    query.bindValue(":depender_type", ertype);
    query.bindValue(":dependee", dependee);

    executeAndCheckQuery(query, "updateGlobalDependencyDepender");
}

void Database::updateGlobalDependencyDependee(const int & ertype, const QString & depender, const QString & dependee)
{
    QSqlQuery query;
    auto guid = GUIDManager::generateGUID();
    query.prepare("UPDATE dependencies SET dependee = ? WHERE depender_type = ? AND depender = ?");

    query.bindValue(":depender", depender);
    query.bindValue(":depender_type", ertype);
    query.bindValue(":dependee", dependee);

    executeAndCheckQuery(query, "updateGlobalDependencyDependee");
}

QString Database::getDependencyByType(const int &ertype, const QString &depender)
{
	QSqlQuery query;
	query.prepare("SELECT dependee FROM dependencies WHERE depender_type = ? AND depender = ?");
	query.addBindValue(ertype);
	query.addBindValue(depender);

	if (query.exec()) {
		if (query.first()) {
			return query.value(0).toString();
		}
	}
	else {
		irisLog("There was an error getting the dependee id! " + query.lastError().text());
	}

	return QString();
}

bool Database::createProjectsTable() {
    QSqlQuery query;
    query.prepare(projectsTableSchema);
    return executeAndCheckQuery(query, "CreateProjectsTable");
}

bool Database::createThumbnailsTable() {
    QSqlQuery query;
    query.prepare(thumbnailsTableSchema);
    return executeAndCheckQuery(query, "CreateThumbnailsTable");
}

bool Database::createCollectionsTable()
{
    if (!checkIfTableExists("collections")) {
        QSqlQuery query;
        query.prepare(collectionsTableSchema);
        
        if (executeAndCheckQuery(query, "CreateCollectionsTable")) {
            QSqlQuery defaultCollQuery;
            defaultCollQuery.prepare(
                "INSERT INTO collections (name, date_created, collection_id) "
                "VALUES (:name, datetime(), 0)"
            );
            defaultCollQuery.bindValue(":name", "Uncategorized");
            return executeAndCheckQuery(defaultCollQuery, "InsertDefaultCollection");
        }
        
        return false;
    }

    // table already exists - nothing to do
    return true;
}

/*
 *	properties is a json object that currently holds
 *  1. the camera orientation
 *  2. number of textures
 *	3. polygon count
 */
bool Database::createAssetsTable() {
	QSqlQuery query;
	query.prepare(assetsTableSchema);
    return executeAndCheckQuery(query, "CreateAssetsTable");
}

bool Database::createDependenciesTable()
{
    QSqlQuery query;
    query.prepare(dependenciesTableSchema);
    return executeAndCheckQuery(query, "CreateDependenciesTable");
}

bool Database::createAuthorTable()
{
    QSqlQuery query;
    query.prepare(authorTableSchema);
    return executeAndCheckQuery(query, "CreateAuthorTable");
}

bool Database::createFoldersTable()
{
    QSqlQuery query;
    query.prepare(foldersTableSchema);
    return executeAndCheckQuery(query, "CreateFoldersTable");
}

bool Database::createMetadataTable()
{
	if (!checkIfTableExists("metadata")) {
		QSqlQuery query;
		query.prepare(metadataTableSchema);
		if (executeAndCheckQuery(query, "CreateMetadataTable")) {
			QSqlQuery defaultCollQuery;
			defaultCollQuery.prepare("INSERT INTO metadata (version) VALUES (?)");
			defaultCollQuery.addBindValue(Constants::CONTENT_VERSION);
			return executeAndCheckQuery(defaultCollQuery, "InsertDefaultMetadata");
		}

		return false;
	}

	// table already exists - nothing to do
	return true;
}

bool Database::createFavoritesTable()
{
    QSqlQuery query;
    query.prepare(favoritesTableSchema);
    return executeAndCheckQuery(query, "CreateFavoritesTable");
}

void Database::createAllTables()
{
    DbTransaction tx(db);
    if (!checkIfTableExists("projects"))        createProjectsTable();
    migrateProjectsTable();
    if (!checkIfTableExists("thumbnails"))      createThumbnailsTable();
    if (!checkIfTableExists("collections"))     createCollectionsTable();
    migrateCollectionsTable();
    if (!checkIfTableExists("assets"))          createAssetsTable();
    migrateAssetsTable();
    if (!checkIfTableExists("dependencies"))    createDependenciesTable();
    if (!checkIfTableExists("author"))          createAuthorTable();
    if (!checkIfTableExists("folders"))         createFoldersTable();
    if (!checkIfTableExists("metadata"))        createMetadataTable();
    if (!checkIfTableExists("favorites"))       createFavoritesTable();
    createIndexes();
    createCasTables();
    tx.commit();
}

void Database::createCasTables()
{
    // Content-addressed store catalog (ASSET_PIPELINE_SPEC §3.1.3, phase 2):
    // files (oid → size/ext/refcount) + asset_files (guid → content mapping)
    // + refcount triggers + PRAGMA user_version. Additive and idempotent —
    // pre-CAS binaries ignore these tables completely.
    QSqlQuery filesTable;
    filesTable.prepare(CasSchema::kFilesTable);
    executeAndCheckQuery(filesTable, "CreateFilesTable");

    QSqlQuery assetFilesTable;
    assetFilesTable.prepare(CasSchema::kAssetFilesTable);
    executeAndCheckQuery(assetFilesTable, "CreateAssetFilesTable");

    QSqlQuery oidIndex;
    oidIndex.prepare(CasSchema::kAssetFilesOidIndex);
    executeAndCheckQuery(oidIndex, "CreateAssetFilesOidIndex");

    QSqlQuery incTrigger;
    incTrigger.prepare(CasSchema::kRefcountInsertTrigger);
    executeAndCheckQuery(incTrigger, "CreateRefcountInsertTrigger");

    QSqlQuery decTrigger;
    decTrigger.prepare(CasSchema::kRefcountDeleteTrigger);
    executeAndCheckQuery(decTrigger, "CreateRefcountDeleteTrigger");

    // Reference-with-pin (phase 4): projects reference library assets and
    // pin the content they were built with.
    QSqlQuery projectAssetsTable;
    projectAssetsTable.prepare(CasSchema::kProjectAssetsTable);
    executeAndCheckQuery(projectAssetsTable, "CreateProjectAssetsTable");

    QSqlQuery versionQuery;
    versionQuery.exec("PRAGMA user_version");
    int current = 0;
    if (versionQuery.next()) current = versionQuery.value(0).toInt();
    if (current < CasSchema::kUserVersion) {
        QSqlQuery setVersion;
        setVersion.exec(QStringLiteral("PRAGMA user_version = %1").arg(CasSchema::kUserVersion));
    }
}

void Database::createIndexes()
{
    // The dependencies table was scanned unindexed on both edge directions
    // (ASSET_PIPELINE_SPEC §1.2 / phase 0). IF NOT EXISTS keeps this
    // idempotent across every startup and older libraries.
    QSqlQuery byDepender;
    byDepender.prepare("CREATE INDEX IF NOT EXISTS idx_dependencies_depender ON dependencies (depender)");
    executeAndCheckQuery(byDepender, "CreateDependenciesDependerIndex");

    QSqlQuery byDependee;
    byDependee.prepare("CREATE INDEX IF NOT EXISTS idx_dependencies_dependee ON dependencies (dependee)");
    executeAndCheckQuery(byDependee, "CreateDependenciesDependeeIndex");
}

bool Database::createProject(
    const QString &guid,
    const QString &projectName,
    const QByteArray &sceneBlob,
    const QByteArray &thumbnail
)
{
    QSqlQuery query;
    query.prepare(
        "INSERT INTO projects (name, scene, thumbnail, version, date_created, last_accessed, last_written, guid) "
        "VALUES (:name, :scene, :thumbnail, :version, datetime(), datetime(), datetime(), :guid)"
    );
    query.bindValue(":name", projectName);
    query.bindValue(":scene", sceneBlob);
    query.bindValue(":thumbnail", thumbnail);
    query.bindValue(":version", Constants::CONTENT_VERSION);
    query.bindValue(":guid", guid);

    return executeAndCheckQuery(query, "CreateProject");
}

bool Database::createFolder(const QString &folderName, const QString &parentFolder, const QString &guid, const QString &projectGuid, bool visible)
{
    QSqlQuery query;
    query.prepare(
        "INSERT INTO folders (name, parent, version, date_created, last_updated, project_guid, guid, visible) "
        "VALUES (:name, :parent, :version, datetime(), datetime(), :project_guid, :guid, :visible)"
    );
    query.bindValue(":name", folderName);
    query.bindValue(":parent", parentFolder);
    query.bindValue(":version", Constants::CONTENT_VERSION);
    query.bindValue(":project_guid", projectGuid);
    query.bindValue(":guid", guid);
    query.bindValue(":visible", visible);
    return executeAndCheckQuery(query, "CreateFolder");
}

QString Database::createAssetEntry(
	const QString &guid,
	const QString &assetName,
	const int &type,
	const QString &parentFolder,
	const QString &projectGuid,
	const QString &license,
	const QString &author,
	const QByteArray &thumbnail,
	const QByteArray &properties,
	const QByteArray &tags,
	const QByteArray &asset,
	const AssetViewFilter view_filter)
{
	QSqlQuery query;
	query.prepare(
		"INSERT INTO assets"
		" (name, thumbnail, parent, type, project_guid, collection, version, date_created,"
		" last_updated, guid, properties, author, asset, license, tags, view_filter)"
		" VALUES (:name, :thumbnail, :parent, :type, :project_guid, 0, :version, datetime(),"
		" datetime(), :guid, :properties, :author, :asset, :license, :tags, :view_filter)"
	);

	query.bindValue(":name", assetName);
	query.bindValue(":thumbnail", thumbnail);
	query.bindValue(":parent", parentFolder);
	query.bindValue(":type", type);
	query.bindValue(":project_guid", projectGuid);
	query.bindValue(":version", Constants::CONTENT_VERSION);
	query.bindValue(":guid", guid);
	query.bindValue(":properties", properties);
	query.bindValue(":author", author.isEmpty() ? getAuthorName() : author);
	query.bindValue(":asset", asset);
	query.bindValue(":license", license);
	query.bindValue(":tags", tags);
	query.bindValue(":view_filter", view_filter);

	if (executeAndCheckQuery(query, "CreateAssetEntry")) {
		return guid;
	}

	return QString();
}

QString Database::createAssetEntry(
	const QString &projectGuid,
	const QString &guid,
	const QString &assetName,
	const int &type,
	const QByteArray &asset,
	const QByteArray &properties,
	const AssetViewFilter view_filter)
{
	QSqlQuery query;
	query.prepare(
		"INSERT INTO assets"
		" (name, thumbnail, parent, type, project_guid, collection, version, date_created,"
		" last_updated, guid, properties, author, asset, license, tags, view_filter)"
		" VALUES (:name, :thumbnail, :parent, :type, :project_guid, 0, :version, datetime(),"
		" datetime(), :guid, :properties, :author, :asset, :license, :tags, :view_filter)"
	);

	query.bindValue(":name", assetName);
	query.bindValue(":thumbnail", QByteArray());
	query.bindValue(":parent", QString());
	query.bindValue(":type", type);
	query.bindValue(":project_guid", projectGuid);
	query.bindValue(":version", Constants::CONTENT_VERSION);
	query.bindValue(":guid", guid);
	query.bindValue(":properties", properties);
	query.bindValue(":author", QString());
	query.bindValue(":asset", asset);
	query.bindValue(":license", QString());
	query.bindValue(":tags", QByteArray());
	query.bindValue(":view_filter", view_filter);

	if (executeAndCheckQuery(query, "CreateAssetEntry")) {
		return guid;
	}

	return QString();
}

bool Database::updateAssetViewFilter(const QString& guid, const int& filter)
{
	QSqlQuery query;
	query.prepare("UPDATE assets SET view_filter = ? WHERE guid = ?");
	query.addBindValue(filter);
	query.addBindValue(guid);
	return executeAndCheckQuery(query, "UpdateAssetViewFilter");
}

bool Database::updateProjectDesktop(const QString &guid, int desktop)
{
	QSqlQuery query;
	query.prepare("UPDATE projects SET desktop = ? WHERE guid = ?");
	query.addBindValue(desktop);
	query.addBindValue(guid);
	return executeAndCheckQuery(query, "UpdateProjectDesktop");
}

bool Database::updateProjectPosition(const QString &guid, float x, float y)
{
	QSqlQuery query;
	query.prepare("UPDATE projects SET desktop_x = ?, desktop_y = ? WHERE guid = ?");
	query.addBindValue(x);
	query.addBindValue(y);
	query.addBindValue(guid);
	return executeAndCheckQuery(query, "UpdateProjectPosition");
}

bool Database::updateProjectSliderPos(const QString &guid, int row, int index)
{
	QSqlQuery query;
	query.prepare("UPDATE projects SET slider_row = ?, slider_index = ? WHERE guid = ?");
	query.addBindValue(row);
	query.addBindValue(index);
	query.addBindValue(guid);
	return executeAndCheckQuery(query, "UpdateProjectSliderPos");
}

void Database::updateSchema()
{
	// apply schema updates in order, those already applied will do nothing
	QSqlQuery query;
	query.prepare(version080SchemaUpdate);
	executeAndCheckQuery(query, "080SchemaUpdate");
}

bool Database::updateMetadataVersion(const QString& version)
{
	QSqlQuery query;
	query.prepare(
		"UPDATE metadata SET version = ?"
	);
	query.addBindValue(version);

	return executeAndCheckQuery(query, "updateMetadataVersion");
}

bool Database::createDependency(
    const int &dependerType,
    const int &dependeeType,
    const QString &depender,
    const QString &dependee,
    const QString &projectGuid)
{
    QSqlQuery query;
    auto guid = GUIDManager::generateGUID();
    query.prepare(
		"INSERT INTO dependencies (depender_type, dependee_type, project_guid, depender, dependee, id) "
		"VALUES (:depender_type, :dependee_type, :project_guid, :depender, :dependee, :id)"
	);
    query.bindValue(":depender_type", dependerType);
    query.bindValue(":dependee_type", dependeeType);
    if (!projectGuid.isEmpty()) query.bindValue(":project_guid", projectGuid);
    query.bindValue(":depender", depender);
    query.bindValue(":dependee", dependee);
    query.bindValue(":id", guid);

    return executeAndCheckQuery(query, "insertGlobalDependency");
}

bool Database::addFavorite(const QString &guid)
{
    const auto asset = fetchAsset(guid);

    QSqlQuery query;
    query.prepare(
        "INSERT INTO favorites (asset_guid, name, date_created, version, thumbnail) "
        "VALUES (:asset_guid, :name, datetime(), :version, :thumbnail)"
    );
    query.bindValue(":asset_guid", guid);
    query.bindValue(":name", asset.name);
    query.bindValue(":version", Constants::CONTENT_VERSION);
    query.bindValue(":thumbnail", asset.thumbnail);

    return executeAndCheckQuery(query, "insertFavorite");
}

bool Database::removeFavorite(const QString &guid)
{
    QSqlQuery query;
    query.prepare("DELETE FROM favorites WHERE asset_guid = ?");
    query.addBindValue(guid);

    return executeAndCheckQuery(query, "removeFavorite");
}

QVector<FolderRecord> Database::fetchCrumbTrail(const QString &guid, const QString &projectGuid)
{
	std::function<void(QVector<FolderRecord>&, const QString&)> fetchFolders
		= [&](QVector<FolderRecord> &folders, const QString &guid) -> void
	{
		QSqlQuery query;
		query.prepare("SELECT guid, parent, name FROM folders WHERE guid = ? AND project_guid = ?");
		query.addBindValue(guid);
		query.addBindValue(projectGuid);
		executeAndCheckQuery(query, "fetchCrumbTrail");

		QStringList parentFolder;
		while (query.next()) {
			FolderRecord data;
			QSqlRecord record = query.record();
			data.guid = record.value(0).toString();
			data.parent = record.value(1).toString();
			data.name = record.value(2).toString();

			parentFolder.push_back(data.parent);
			folders.push_back(data);
		}

		for (const QString &folder : parentFolder) {
			fetchFolders(folders, folder);
		}
	};

	QVector<FolderRecord> folders;
	fetchFolders(folders, guid);

    FolderRecord home;
	home.guid = projectGuid;
	home.name = "Assets";
	folders.push_back(home);

	std::reverse(folders.begin(), folders.end());

	return folders;
}

QVector<FolderRecord> Database::fetchChildFolders(const QString &parent, const QString &projectGuid)
{
	QSqlQuery query;
	query.prepare("SELECT guid, parent, name, count, visible FROM folders WHERE parent = ? AND project_guid = ?");
	query.addBindValue(parent);
	query.addBindValue(projectGuid);
	executeAndCheckQuery(query, "FetchChildFolders");

	QVector<FolderRecord> folderData;
	while (query.next()) {
        FolderRecord data;
		QSqlRecord record = query.record();
		for (int i = 0; i < record.count(); i++) {
			data.guid = record.value(0).toString();
			data.parent = record.value(1).toString();
			data.name = record.value(2).toString();
			data.count = record.value(3).toInt();
			data.visible = record.value(4).toBool();
		}

		folderData.push_back(data);
	}

	return folderData;
}

QVector<AssetRecord> Database::fetchAssetThumbnails(const QStringList &guids)
{
	// Construct the guid list to use and chop of the extraneous comma to make it valid
	QString guidInString;
	for (const QString &guid : guids) guidInString += "'" + guid + "',";
	guidInString.chop(1);

	QSqlQuery query;
	query.prepare("SELECT guid, thumbnail, name FROM assets WHERE guid IN (" + guidInString + ")");
	executeAndCheckQuery(query, "fetchAssetThumbnails");

	QVector<AssetRecord> assetData;
	while (query.next()) {
        AssetRecord data;
		QSqlRecord record = query.record();
		for (int i = 0; i < record.count(); i++) {
			data.guid		= record.value(0).toString();
			data.thumbnail	= record.value(1).toByteArray();
			data.name		= record.value(2).toString();
		}

		assetData.push_back(data);
	}

	return assetData;
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

// Guid-parameterised (SCRIPTING_SPEC §1.1/§1.6.1): callers no longer have to
// MUTATE Globals::project to delete a project that is not the open one.
bool Database::deleteProject(const QString &guid)
{
    DbTransaction tx(db);

    QSqlQuery query;
    query.prepare("DELETE FROM projects WHERE guid = ?");
    query.addBindValue(guid);
    bool q = executeAndCheckQuery(query, "DeleteProject");

    QSqlQuery dquery;
    dquery.prepare("DELETE FROM dependencies WHERE project_guid = ?");
    dquery.addBindValue(guid);
    bool d = executeAndCheckQuery(dquery, "DeleteDependencies");

    if (!(d && q)) return false;   // tx rolls back — no half-deleted project
    return tx.commit();
}

bool Database::destroyTable(const QString &table)
{
	QSqlQuery query;
	query.prepare(QString("DROP TABLE IF EXISTS %1").arg(table));
	return executeAndCheckQuery(query, QString("DropAssetTable[%1]").arg(table));
}

void Database::wipeDatabase()
{
	DbTransaction tx(db);
	destroyTable("projects");
	destroyTable("thumbnails");
	destroyTable("collections");
	destroyTable("assets");
	destroyTable("dependencies");
	destroyTable("author");
	destroyTable("folders");
	destroyTable("metadata");
	tx.commit();
}

bool Database::deleteAsset(const QString &guid)
{
    QSqlQuery query;
    query.prepare("DELETE FROM assets WHERE guid = ?");
    query.addBindValue(guid);

    // The catalog's content mapping and any project pins go with the row
    // (phase 4): the asset_files delete trigger decrements files.refcount,
    // which is what assets.gc (later) reaps by.
    QSqlQuery dropFiles;
    dropFiles.prepare("DELETE FROM asset_files WHERE asset_guid = ?");
    dropFiles.addBindValue(guid);
    executeAndCheckQuery(dropFiles, "DeleteAssetFiles");

    QSqlQuery dropPins;
    dropPins.prepare("DELETE FROM project_assets WHERE asset_guid = ?");
    dropPins.addBindValue(guid);
    executeAndCheckQuery(dropPins, "DeleteAssetPins");

    for (int i = 0; i < AssetManager::getAssets().count(); i++) {
        if (AssetManager::getAssets()[i]->assetGuid == guid) AssetManager::getAssets().remove(i);
    }

	return executeAndCheckQuery(query, "DeleteAsset");
}

bool Database::deleteCollection(const int &collectionId)
{
    // Drawers (ASSET_DRAWERS_SPEC §2): the virtual root (-1) and Uncategorized
    // (0, the fallback home) are not deletable. Deleting a drawer moves the
    // whole subtree's assets to Uncategorized, then removes the subtree rows.
    if (collectionId <= 0) return false;

    const QVector<int> subtree = fetchCollectionSubtree(collectionId);
    if (subtree.isEmpty()) return false;   // no such drawer

    DbTransaction tx(db);

    QStringList placeholders;
    for (int i = 0; i < subtree.size(); ++i) placeholders << "?";

    QSqlQuery reassign;
    reassign.prepare("UPDATE assets SET collection = 0, last_updated = datetime() "
                     "WHERE collection IN (" + placeholders.join(", ") + ")");
    for (const int id : subtree) reassign.addBindValue(id);
    if (!executeAndCheckQuery(reassign, "DeleteCollectionReassignAssets")) return false;

    QSqlQuery query;
    query.prepare("DELETE FROM collections WHERE collection_id IN (" + placeholders.join(", ") + ")");
    for (const int id : subtree) query.addBindValue(id);
    if (!executeAndCheckQuery(query, "DeleteCollection")) return false;
    return tx.commit();
}

bool Database::deleteFolder(const QString &guid)
{
    QSqlQuery query;
    query.prepare("DELETE FROM folders WHERE guid = ?");
    query.addBindValue(guid);
    return executeAndCheckQuery(query, "DeleteFolder");
}

bool Database::deleteDependency(const QString &dependee)
{
    QSqlQuery query;
    query.prepare("DELETE FROM dependencies WHERE dependee = ?");
    query.addBindValue(dependee);
    return executeAndCheckQuery(query, "deleteDependency");
}

bool Database::deleteDependency(const QString &depender, const QString &dependee)
{
    QSqlQuery query;
    query.prepare("DELETE FROM dependencies WHERE depender = ? AND dependee = ?");
    query.addBindValue(depender);
    query.addBindValue(dependee);
    return executeAndCheckQuery(query, "deleteDependency");
}

bool Database::removeDependenciesByType(const QString &depender, const ModelTypes &type)
{
    QSqlQuery query;
    query.prepare("DELETE FROM dependencies WHERE depender = ? AND dependee_type = ?");
    query.addBindValue(depender);
    query.addBindValue(static_cast<int>(type));

    return executeAndCheckQuery(query, "RemoveDependenciesByType");
}

bool Database::deleteRecord(const QString &table, const QString &row, const QVariant &value)
{
    QSqlQuery query;
    query.prepare("DELETE FROM " + table + " WHERE " + row + " = ?");
    query.addBindValue(value);
    return executeAndCheckQuery(query, "DeleteRecord[" + table + ", " + row + "]");
}

bool Database::renameProject(const QString &guid, const QString &newName)
{
    QSqlQuery query;
    query.prepare("UPDATE projects SET name = ? WHERE guid = ?");
    query.addBindValue(newName);
    query.addBindValue(guid);
    return executeAndCheckQuery(query, "RenameProject");
}

bool Database::renameFolder(const QString &guid, const QString &newName)
{
    QSqlQuery query;
    query.prepare("UPDATE folders SET name = ? WHERE guid = ?");
    query.addBindValue(newName);
    query.addBindValue(guid);
    return executeAndCheckQuery(query, "RenameFolder");
}

bool Database::renameCollection(const int &collectionId, const QString &newName)
{
    QSqlQuery query;
    query.prepare("UPDATE collections SET name = ? WHERE collection_id = ?");
    query.addBindValue(newName);
    query.addBindValue(collectionId);
    return executeAndCheckQuery(query, "RenameCollection");
}

bool Database::renameAsset(const QString &guid, const QString &newName)
{
    QSqlQuery query;
    query.prepare("UPDATE assets SET name = ? WHERE guid = ?");
    query.addBindValue(newName);
    query.addBindValue(guid);
    return executeAndCheckQuery(query, "RenameAsset");
}

bool Database::updateProject(const QByteArray &sceneBlob, const QByteArray &thumbnail, const QString &projectGuid)
{
    QSqlQuery query;
    query.prepare("UPDATE projects SET scene = ?, last_written = datetime(), thumbnail = ? WHERE guid = ?");
    query.addBindValue(sceneBlob);
    query.addBindValue(thumbnail);
    query.addBindValue(projectGuid);
    return executeAndCheckQuery(query, "UpdateProject");
}

// Blob-only update: keeps the existing thumbnail (headless saves have no
// viewport to screenshot and must not clear the tile image).
bool Database::updateProjectBlob(const QByteArray &sceneBlob, const QString &projectGuid)
{
    QSqlQuery query;
    query.prepare("UPDATE projects SET scene = ?, last_written = datetime() WHERE guid = ?");
    query.addBindValue(sceneBlob);
    query.addBindValue(projectGuid);
    return executeAndCheckQuery(query, "UpdateProjectBlob");
}

bool Database::updateAssetThumbnail(const QString &guid, const QByteArray &thumbnail)
{
	QSqlQuery query;
	query.prepare("UPDATE assets SET thumbnail = ? WHERE guid = ?");
	query.addBindValue(thumbnail);
	query.addBindValue(guid);
	return executeAndCheckQuery(query, "UpdateAssetThumbnail");
}

bool Database::updateAssetAsset(const QString &guid, const QByteArray &asset)
{
	QSqlQuery query;
	query.prepare("UPDATE assets SET asset = ? WHERE guid = ?");
	query.addBindValue(asset);
	query.addBindValue(guid);
	return executeAndCheckQuery(query, "UpdateAssetAsset");
}

bool Database::updateSceneThumbnail(const QString & guid, const QByteArray &thumbnail)
{
    QSqlQuery query;
    query.prepare("UPDATE projects SET thumbnail = ? WHERE guid = ?");
    query.addBindValue(thumbnail);
    // The guid parameter was previously ignored in favour of the ambient
    // current-project read; the only caller passes the current project guid.
    query.addBindValue(guid);
    return executeAndCheckQuery(query, "updateSceneThumbnail");
}

bool Database::updateAssetMetadata(const QString &guid, const QString &name, const QByteArray &tags)
{
    QSqlQuery query;
    query.prepare("UPDATE assets SET name = ?, tags = ?, last_updated = datetime() WHERE guid = ?");
    query.addBindValue(name);
    query.addBindValue(tags);
    query.addBindValue(guid);
    return executeAndCheckQuery(query, "updateAssetMetadata");
}

bool Database::updateAssetProperties(const QString &guid, const QByteArray &asset)
{
    QSqlQuery query;
    query.prepare("UPDATE assets SET properties = ? WHERE guid = ?");
    query.addBindValue(asset);
    query.addBindValue(guid);
    return executeAndCheckQuery(query, "UpdateAssetProperties");
}

AssetRecord Database::fetchAsset(const QString &guid)
{
    QSqlQuery query;
    query.prepare("SELECT name, thumbnail, guid, parent, type, properties, view_filter, date_created FROM assets WHERE guid = ? ");
    query.addBindValue(guid);
    executeAndCheckQuery(query, "fetchAsset");

    if (query.exec()) {
        if (query.first()) {
            AssetRecord data;
            data.name = query.value(0).toString();
            data.thumbnail = query.value(1).toByteArray();
            data.guid = query.value(2).toString();
            data.parent = query.value(3).toString();
            data.type = query.value(4).toInt();
            data.properties = query.value(5).toByteArray();
            data.view_filter = query.value(6).toInt();
            data.dateCreated = query.value(7).toDateTime();
            return data;
        }
    }
    else {
        irisLog("There was an error getting the asset! " + query.lastError().text());
    }

    return AssetRecord();
}

QStringList Database::fetchLibraryAssetGuids()
{
    QSqlQuery query;
    // view_filter IN (2,3) — AssetsView AND Effects are both library rows
    // (ASSET_PIPELINE_SPEC preflight amendment 2).
    query.prepare("SELECT guid FROM assets WHERE view_filter IN (?, ?)");
    query.addBindValue(static_cast<int>(AssetViewFilter::AssetsView));
    query.addBindValue(static_cast<int>(AssetViewFilter::Effects));
    executeAndCheckQuery(query, "FetchLibraryAssetGuids");

    QStringList guids;
    while (query.next()) guids << query.value(0).toString();
    return guids;
}

QMap<QString, qint64> Database::fetchAssetFileSizes()
{
    QSqlQuery query;
    query.prepare(
        "SELECT af.asset_guid, SUM(f.size) FROM asset_files af "
        "JOIN files f ON f.oid = af.oid GROUP BY af.asset_guid");
    executeAndCheckQuery(query, "FetchAssetFileSizes");

    QMap<QString, qint64> sizes;
    while (query.next())
        sizes.insert(query.value(0).toString(), query.value(1).toLongLong());
    return sizes;
}

QVector<AssetRecord> Database::fetchAssetsForAssetView()
{
    QSqlQuery query;
    // Phase 0 (ASSET_PIPELINE_SPEC §1.2): the asset JSON BLOB is deliberately
    // NOT selected — no grid consumer reads it, and dragging it in loaded
    // every library row's node JSON into memory on each grid refresh.
    query.prepare(
        "SELECT A.name, A.thumbnail, A.guid, C.collection_id, A.type, A.collection, A.properties, "
        "A.author, A.license, A.tags, A.project_guid, A.view_filter "
        "FROM assets A "
        // LEFT JOIN: a row whose collection no longer exists must still show
        // (pre-drawers deleteCollection orphaned assets instead of reassigning)
        "LEFT JOIN collections C ON A.collection = C.collection_id "
        "WHERE A.view_filter = :view_filter "
        "AND A.guid NOT IN (SELECT dependee from dependencies) "
        "ORDER BY A.name DESC"
    );
    query.bindValue(":view_filter", AssetViewFilter::AssetsView);
    executeAndCheckQuery(query, "FetchAssets");

    QVector<AssetRecord> tileData;
    while (query.next()) {
        AssetRecord data;
        QSqlRecord record = query.record();
        for (int i = 0; i < record.count(); i++) {
            data.name = record.value(0).toString();
            data.thumbnail = record.value(1).toByteArray();
            data.guid = record.value(2).toString();
            data.collection = record.value(3).toInt();
            data.type = record.value(4).toInt();
            data.collection = record.value(5).toInt();
            data.properties = record.value(6).toByteArray();
            data.author = record.value(7).toString();
            data.license = record.value(8).toString();
            data.tags = record.value(9).toByteArray();
			data.view_filter = record.value(11).toInt();
        }


        tileData.push_back(data);
    }

    return tileData;
}

QVector<AssetRecord> Database::fetchChildAssets(const QString &parent, const QString &projectGuid, int filter, bool showDependencies)
{
    QString dependentQuery =
        "SELECT name, thumbnail, guid, parent, type, properties "
        "FROM assets A WHERE parent = ? AND project_guid = ? ";

    QString nonDependentQuery =
        "SELECT name, thumbnail, guid, parent, type, properties "
        "FROM assets A WHERE parent = ? AND project_guid = ? "
        "AND A.guid NOT IN (SELECT dependee FROM dependencies) ";

    QString orderQuery = "ORDER BY A.name DESC";

    QString assetsQuery;
    if (showDependencies) {
        assetsQuery.append(dependentQuery);
        if (filter > 0) assetsQuery.append("AND type = ? ");
    }
    else {
        assetsQuery.append(nonDependentQuery);
        if (filter > 0) assetsQuery.append("AND type = ? ");
    }

    assetsQuery.append(orderQuery);

    QSqlQuery query;
    query.prepare(assetsQuery);
    query.addBindValue(parent);
    query.addBindValue(projectGuid);
    if (filter > 0) query.addBindValue(filter);
    executeAndCheckQuery(query, "fetchChildAssets");

    QVector<AssetRecord> tileData;
    while (query.next()) {
        AssetRecord data;
        QSqlRecord record = query.record();
        for (int i = 0; i < record.count(); i++) {
            data.name = record.value(0).toString();
            data.thumbnail = record.value(1).toByteArray();
            data.guid = record.value(2).toString();
            data.parent = record.value(3).toString();
            data.type = record.value(4).toInt();
            data.properties = record.value(5).toByteArray();
        }

        tileData.push_back(data);
    }

    return tileData;
}

QVector<AssetRecord> Database::fetchAssetsFromParent(const QString & guid)
{
    auto guids = fetchAssetGUIDAndDependencies(guid, false);
    QVector<AssetRecord> assets;
    for (const auto &guid : guids) {
        assets.push_back(fetchAsset(guid));
    }
    return assets;
}

DatabaseMetadataRecord Database::getDbMetadata()
{
	QSqlQuery query;
	query.prepare("SELECT date_created, hash, version, data FROM metadata");

	if (query.exec()) {
		query.next();
	}
	else {
		irisLog(
			"There was an error fetching db metadata " + query.lastError().text()
		);

		return DatabaseMetadataRecord();
	}

	DatabaseMetadataRecord record;
	record.dateCreated = query.value(0).toDateTime();
	record.hash = query.value(1).toString();
	record.version = query.value(2).toString();
	record.data = query.value(3).toByteArray();

	auto numbers = record.version.split(".");
	record.major = numbers[0].toInt();
	record.minor = numbers[1].toInt();

	if (numbers[2].length() > 1) numbers[2].chop(1);
	int dbPatch = numbers[2].toInt();

	record.patch = dbPatch;

	return record;
}

QVector<AssetRecord> Database::fetchAssetsByType(const int &type, const QString &projectGuid)
{
    QSqlQuery query;
    query.prepare("SELECT guid, type, name, thumbnail, asset FROM assets WHERE type = ? AND project_guid = ?");
    query.addBindValue(type);
    query.addBindValue(projectGuid);
    executeAndCheckQuery(query, "fetchAssetsByType");

    QVector<AssetRecord> tileData;
    while (query.next()) {
        AssetRecord data;
        QSqlRecord record = query.record();
        for (int i = 0; i < record.count(); i++) {
            data.guid = record.value(0).toString();
            data.type = record.value(1).toInt();
            data.name = record.value(2).toString();
			data.thumbnail = query.value(3).toByteArray();
            data.asset = record.value(4).toByteArray();
        }

        tileData.push_back(data);
    }

    return tileData;
}

QVector<AssetRecord> Database::fetchAssetsByViewFilter(const AssetViewFilter& filter)
{
	QSqlQuery query;
	query.prepare("SELECT guid, type, name, thumbnail, asset FROM assets WHERE view_filter = ?");
	query.addBindValue(filter);
	executeAndCheckQuery(query, "fetchAssetsByViewFilter");

	QVector<AssetRecord> tileData;
	while (query.next()) {
		AssetRecord data;
		QSqlRecord record = query.record();
		for (int i = 0; i < record.count(); i++) {
			data.guid = record.value(0).toString();
			data.type = record.value(1).toInt();
			data.name = record.value(2).toString();
			data.thumbnail = query.value(3).toByteArray();
			data.asset = record.value(4).toByteArray();
		}

		tileData.push_back(data);
	}

	return tileData;
}

void Database::createExportBundle(const QStringList & objectGuids, const QString & outTempFilePath)
{
    QSqlDatabase exportConnection = QSqlDatabase();
    exportConnection = QSqlDatabase::addDatabase(Constants::DB_DRIVER, "NodeExportConnection");
    exportConnection.setDatabaseName(outTempFilePath);

    if (exportConnection.isValid()) {
        if (!exportConnection.open()) {
            irisLog(QString("Couldn't open a database connection! %1").arg(exportConnection.lastError().text()));
            return;
        }
    }
    else {
        irisLog(QString("The database connection is invalid! %1").arg(exportConnection.lastError().text()));
    }

    // Export DB writes in one transaction (phase 0): atomic bundle file,
    // one fsync instead of one per row.
    exportConnection.transaction();

    QSqlQuery createAssetsTable(exportConnection);
    createAssetsTable.prepare(assetsTableSchema);
    executeAndCheckQuery(createAssetsTable, "CreateAssetsTable");

    QSqlQuery createDependenciesTable(exportConnection);
    createDependenciesTable.prepare(dependenciesTableSchema);
    executeAndCheckQuery(createDependenciesTable, "CreateDependenciesTable");

    QVector<AssetRecord> assetList;

    QStringList fullAssetList;
    for (const auto &guid : objectGuids) {
        fullAssetList.append(fetchAssetGUIDAndDependencies(guid));
    }

    for (const auto &asset : fullAssetList) {
        QSqlQuery selectAssetQuery;
        selectAssetQuery.prepare(
            "SELECT guid, type, name, collection, times_used, project_guid, date_created, last_updated, "
            "author, license, hash, version, parent, tags, properties, asset, thumbnail, view_filter FROM assets WHERE guid = ?"
        );
        selectAssetQuery.addBindValue(asset);

        if (selectAssetQuery.exec()) {
            if (selectAssetQuery.first()) {
                AssetRecord data;
                data.guid = selectAssetQuery.value(0).toString();
                data.type = selectAssetQuery.value(1).toInt();
                data.name = selectAssetQuery.value(2).toString();
                data.collection = selectAssetQuery.value(3).toInt();
                data.timesUsed = selectAssetQuery.value(4).toInt();
                data.projectGuid = selectAssetQuery.value(5).toString();
                data.dateCreated = selectAssetQuery.value(6).toDateTime();
                data.lastUpdated = selectAssetQuery.value(7).toDateTime();
                data.author = selectAssetQuery.value(8).toString();
                data.license = selectAssetQuery.value(9).toString();
                data.hash = selectAssetQuery.value(10).toString();
                data.version = selectAssetQuery.value(11).toString();
                data.parent = selectAssetQuery.value(12).toString();
                data.tags = selectAssetQuery.value(13).toByteArray();
                data.properties = selectAssetQuery.value(14).toByteArray();
                data.asset = selectAssetQuery.value(15).toByteArray();
                data.thumbnail = selectAssetQuery.value(16).toByteArray();
                data.view_filter = selectAssetQuery.value(17).toInt();
                assetList.push_back(data);
            }
        }
        else {
            irisLog("There was an error fetching an asset " + selectAssetQuery.lastError().text());
        }
    }

    for (const auto &asset : assetList) {
        QSqlQuery insertExportAssetQuery(exportConnection);
        insertExportAssetQuery.prepare(
            "INSERT INTO assets"
            " (guid, type, name, collection, times_used, project_guid, date_created, last_updated, author,"
            " license, hash, version, parent, tags, properties, asset, thumbnail, view_filter)"
            " VALUES(:guid, :type, :name, :collection, :times_used, :project_guid, :date_created, :last_updated, :author,"
            " :license, :hash, :version, :parent, :tags, :properties, :asset, :thumbnail, :view_filter)"
        );

        insertExportAssetQuery.bindValue(":guid", asset.guid);
        insertExportAssetQuery.bindValue(":type", asset.type);
        insertExportAssetQuery.bindValue(":name", asset.name);
        insertExportAssetQuery.bindValue(":collection", asset.collection);
        insertExportAssetQuery.bindValue(":times_used", asset.timesUsed);
        insertExportAssetQuery.bindValue(":project_guid", asset.projectGuid);
        insertExportAssetQuery.bindValue(":date_created", asset.dateCreated);
        insertExportAssetQuery.bindValue(":last_updated", asset.lastUpdated);
        insertExportAssetQuery.bindValue(":author", asset.author);
        insertExportAssetQuery.bindValue(":license", asset.license);
        insertExportAssetQuery.bindValue(":hash", asset.hash);
        insertExportAssetQuery.bindValue(":version", asset.version);
        insertExportAssetQuery.bindValue(":parent", asset.parent);
        insertExportAssetQuery.bindValue(":tags", asset.tags);
        insertExportAssetQuery.bindValue(":properties", asset.properties);

        //if (asset.type == static_cast<int>(ModelTypes::Object)) {
        //    QJsonObject assetJson;
        //    SceneWriter::writeSceneNode(assetJson, node);
        //    qDebug() << assetJson;
        //    insertExportAssetQuery.bindValue(":asset", QJsonDocument(assetJson).toBinaryData());
        //}
        //else {
            insertExportAssetQuery.bindValue(":asset", asset.asset);
        //}

        insertExportAssetQuery.bindValue(":thumbnail", asset.thumbnail);
        insertExportAssetQuery.bindValue(":view_filter", asset.view_filter);

        executeAndCheckQuery(insertExportAssetQuery, "insertExportAssetQuery");
    }

    QVector<DependencyRecord> dependenciesToExport;

    for (const auto &asset : assetList) {
        // Dependency-export fix (ASSET_PIPELINE_SPEC §1.7 / phase 0): follow the
        // asset's OUTGOING edges (what it depends on — depender = asset), and
        // iterate EVERY row; the old query walked the wrong direction
        // (dependee = asset → the asset's dependents) and `if (first())`
        // exported at most one dependency per asset.
        QSqlQuery selectDep;
        selectDep.prepare(
            "SELECT depender_type, dependee_type, project_guid, depender, dependee, id FROM dependencies WHERE "
            "depender = ? AND depender_type = ?"
        );
        selectDep.addBindValue(asset.guid);
        selectDep.addBindValue(asset.type);

        if (selectDep.exec()) {
            while (selectDep.next()) {
                DependencyRecord record;
                record.dependerType = selectDep.value(0).toInt();
                record.dependeeType = selectDep.value(1).toInt();
                record.projectGuid = selectDep.value(2).toString();
                record.depender = selectDep.value(3).toString();
                record.dependee = selectDep.value(4).toString();
                record.id = selectDep.value(5).toString();

                dependenciesToExport.append(record);
            }
        }
        else {
            irisLog("There was an error fetching a dependency" + selectDep.lastError().text());
        }
    }

    QStringList assetDependencies;

    for (const auto &dep : dependenciesToExport) {
        QSqlQuery exportDep(exportConnection);
        exportDep.prepare(
            "INSERT INTO dependencies (depender_type, dependee_type, project_guid, depender, dependee, id) "
            "VALUES (:depender_type, :dependee_type, :project_guid, :depender, :dependee, :id)"
        );

        exportDep.bindValue(":depender_type", dep.dependerType);
        exportDep.bindValue(":dependee_type", dep.dependeeType);
        exportDep.bindValue(":project_guid", dep.projectGuid);
        exportDep.bindValue(":depender", dep.depender);
        exportDep.bindValue(":dependee", dep.dependee);
        exportDep.bindValue(":id", dep.id);

        executeAndCheckQuery(exportDep, "exportDep");
    }

    exportConnection.commit();
    exportConnection.close();
    exportConnection = QSqlDatabase();
    QSqlDatabase::removeDatabase("NodeExportConnection");
}

int Database::createCollection(const QString &collectionName, const int parent)
{
    // parent -1 = top level; anything else must be an existing drawer.
    if (parent != -1 && fetchCollectionSubtree(parent).isEmpty()) return -1;

    QSqlQuery query;
    query.prepare("INSERT INTO " + Constants::DB_COLLECT_TABLE +
        " (name, date_created, parent)" +
        " VALUES (:name, datetime(), :parent)");
    query.bindValue(":name", collectionName);
    query.bindValue(":parent", parent);

    if (!executeAndCheckQuery(query, "CreateCollection")) return -1;
    return query.lastInsertId().toInt();
}

bool Database::setCollectionParent(const int collectionId, const int parent)
{
    // The virtual root (-1) and Uncategorized (0, the fallback home) stay put.
    if (collectionId <= 0) return false;
    if (parent == collectionId) return false;

    const QVector<int> subtree = fetchCollectionSubtree(collectionId);
    if (subtree.isEmpty()) return false;                        // no such drawer
    if (parent != -1) {
        if (subtree.contains(parent)) return false;             // cycle
        if (fetchCollectionSubtree(parent).isEmpty()) return false;   // no such parent
    }

    QSqlQuery query;
    query.prepare("UPDATE collections SET parent = ? WHERE collection_id = ?");
    query.addBindValue(parent);
    query.addBindValue(collectionId);
    return executeAndCheckQuery(query, "SetCollectionParent");
}

bool Database::switchAssetCollection(const int id, const QString &guid)
{
    QSqlQuery query;
    query.prepare("UPDATE " + Constants::DB_ASSETS_TABLE + " SET collection = ?, last_updated = datetime() WHERE guid = ?");
    query.addBindValue(id);
    query.addBindValue(guid);

    return executeAndCheckQuery(query, "switchAssetCollection");
}

void Database::insertThumbnailGlobal(const QString &world_guid,
                                     const QString &name,
                                     const QByteArray &thumbnail,
								     const QString &thumbnail_guid)
{
    QSqlQuery query;
    query.prepare("INSERT INTO " + Constants::DB_THUMBS_TABLE + " (world_guid, name, thumbnail, guid)"
                  " VALUES (:world_guid, :name, :thumbnail, :guid)");
    query.bindValue(":world_guid",  world_guid);
    query.bindValue(":thumbnail",   thumbnail);
    query.bindValue(":name",        name);
    query.bindValue(":guid",        thumbnail_guid);

    executeAndCheckQuery(query, "insertThumbnailGlobal");
}

QByteArray Database::fetchAssetData(const QString &guid) const
{
	QSqlQuery query;
	query.prepare("SELECT asset FROM assets WHERE guid = ?");
	query.addBindValue(guid);

	if (query.exec()) {
		if (query.first()) return query.value(0).toByteArray();
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

QVector<AssetRecord> Database::fetchThumbnails()
{
	QSqlQuery query;
	query.prepare("SELECT name, thumbnail, guid, type FROM assets WHERE type = 5");
	executeAndCheckQuery(query, "fetchThumbnails");

	QVector<AssetRecord> tileData;
	while (query.next()) {
        AssetRecord data;
		QSqlRecord record = query.record();
		for (int i = 0; i < record.count(); i++) {
			data.name		= record.value(0).toString();
			data.thumbnail	= record.value(1).toByteArray();
			data.guid		= record.value(2).toString();
			data.type		= record.value(3).toInt();
		}

		tileData.push_back(data);
	}

	return tileData;
}

QVector<AssetRecord> Database::fetchFavorites()
{
    QSqlQuery query;
    query.prepare(
        "SELECT F.asset_guid, F.name, F.date_created, A.type, F.thumbnail FROM favorites F "
        "LEFT JOIN assets A ON A.guid = F.asset_guid"
    );
    executeAndCheckQuery(query, "fetchFavorites");

    QVector<AssetRecord> tileData;
    while (query.next()) {
        AssetRecord data;
        QSqlRecord record = query.record();
        for (int i = 0; i < record.count(); i++) {
            data.guid = record.value(0).toString();
            data.name = record.value(1).toString();
            data.dateCreated = record.value(2).toDateTime();
            data.type = record.value(3).toInt();
            data.thumbnail = record.value(4).toByteArray();
        }

        tileData.push_back(data);
    }

    return tileData;
}

QVector<AssetRecord> Database::fetchFilteredAssets(const QString &guid, const int &type)
{
	QSqlQuery query;
	query.prepare("SELECT name, guid FROM assets WHERE project_guid = ? AND type = ?");
	query.addBindValue(guid);
	query.addBindValue(type);
	executeAndCheckQuery(query, "fetchFilteredAssets");

	QVector<AssetRecord> tileData;
	while (query.next()) {
        AssetRecord data;
		QSqlRecord record = query.record();
		for (int i = 0; i < record.count(); i++) {
			data.name = record.value(0).toString();
			data.guid = record.value(1).toString();
		}

		tileData.push_back(data);
	}

	return tileData;
}

QVector<CollectionRecord> Database::fetchCollections()
{
    QSqlQuery query;
    query.prepare("SELECT name, collection_id, COALESCE(parent, -1) FROM " + Constants::DB_COLLECT_TABLE +
                  " ORDER BY name, date_created DESC");
    executeAndCheckQuery(query, "fetchCollections");

    QVector<CollectionRecord> tileData;
    while (query.next()) {
        CollectionRecord data;
        data.name = query.value(0).toString();
        data.id = query.value(1).toInt();
        data.parent = query.value(2).toInt();
        tileData.push_back(data);
    }

    return tileData;
}

QVector<int> Database::fetchCollectionSubtree(const int collectionId)
{
    // The drawer plus all its descendants, breadth-first. Empty when the id
    // names no row (the virtual root -1 is not a row).
    QMultiHash<int, int> children;   // parent -> child ids
    QSet<int> known;
    for (const auto &coll : fetchCollections()) {
        children.insert(coll.parent, coll.id);
        known.insert(coll.id);
    }

    QVector<int> subtree;
    if (!known.contains(collectionId)) return subtree;

    QVector<int> pending { collectionId };
    while (!pending.isEmpty()) {
        const int id = pending.takeFirst();
        if (subtree.contains(id)) continue;   // defensive: a cycled row must not loop forever
        subtree.push_back(id);
        for (const int child : children.values(id)) pending.push_back(child);
    }
    return subtree;
}

int Database::countAssetsInCollections(const QVector<int> &collectionIds)
{
    if (collectionIds.isEmpty()) return 0;

    QStringList placeholders;
    for (int i = 0; i < collectionIds.size(); ++i) placeholders << "?";

    QSqlQuery query;
    query.prepare("SELECT COUNT(*) FROM assets WHERE collection IN (" + placeholders.join(", ") + ")");
    for (const int id : collectionIds) query.addBindValue(id);

    if (query.exec()) {
        if (query.first()) return query.value(0).toInt();
    }
    else {
        irisLog("countAssetsInCollections query failed! " + query.lastError().text());
    }

    return 0;
}

QVector<ProjectTileData> Database::fetchProjects(int desktop)
{
    QSqlQuery query;
    if (desktop > 0) {
        // COALESCE: rows from before the desktop migration (NULL) belong to Desktop 1
        query.prepare(
            "SELECT name, thumbnail, guid, COALESCE(desktop, 1), desktop_x, desktop_y, "
            "slider_row, slider_index "
            "FROM projects WHERE COALESCE(desktop, 1) = ? ORDER BY last_written DESC"
        );
        query.addBindValue(desktop);
    }
    else {
        query.prepare(
            "SELECT name, thumbnail, guid, COALESCE(desktop, 1), desktop_x, desktop_y, "
            "slider_row, slider_index "
            "FROM projects ORDER BY last_written DESC"
        );
    }
    executeAndCheckQuery(query, "FetchProjects");

    QVector<ProjectTileData> tileData;
    while (query.next())  {
        ProjectTileData data;
        QSqlRecord record = query.record();
        data.name       = record.value(0).toString();
        data.thumbnail  = record.value(1).toByteArray();
        data.guid       = record.value(2).toString();
        data.desktop    = record.value(3).toInt();
        data.hasPosition = !record.value(4).isNull() && !record.value(5).isNull();
        if (data.hasPosition) {
            data.posX = record.value(4).toFloat();
            data.posY = record.value(5).toFloat();
        }
        data.hasSliderPos = !record.value(6).isNull() && !record.value(7).isNull();
        if (data.hasSliderPos) {
            data.sliderRow   = record.value(6).toInt();
            data.sliderIndex = record.value(7).toInt();
        }

        tileData.push_back(data);
    }

    return tileData;
}

QByteArray Database::getSceneBlobGlobal(const QString &projectGuid) const
{
    QSqlQuery query;
    query.prepare("SELECT scene FROM projects WHERE guid = ?");
    query.addBindValue(projectGuid);

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

bool Database::createBlobFromNode(const iris::SceneNodePtr &node, const QString &writePath)
{
    QSqlDatabase exportConnection = QSqlDatabase();
    exportConnection = QSqlDatabase::addDatabase(Constants::DB_DRIVER, "NodeExportConnection");
    exportConnection.setDatabaseName(writePath);

    if (exportConnection.isValid()) {
        if (!exportConnection.open()) {
            irisLog(QString("Couldn't open a database connection! %1").arg(exportConnection.lastError().text()));
            return false;
        }
    }
    else {
        irisLog(QString("The database connection is invalid! %1").arg(exportConnection.lastError().text()));
        return false;
    }

    // Export DB writes in one transaction (phase 0): atomic bundle file,
    // one fsync instead of one per row.
    exportConnection.transaction();

    QSqlQuery createAssetsTable(exportConnection);
    createAssetsTable.prepare(assetsTableSchema);
    if (!executeAndCheckQuery(createAssetsTable, "CreateAssetsTable")) return false;

    QSqlQuery createDependenciesTable(exportConnection);
    createDependenciesTable.prepare(dependenciesTableSchema);
    if (!executeAndCheckQuery(createDependenciesTable, "CreateDependenciesTable")) return false;

    QVector<AssetRecord> assetList;
    QStringList allAssetsToExport;

    for (const auto &guid : AssetHelper::getChildGuids(node)) {
        for (const auto &assetGuid : fetchAssetAndAllDependencies(guid)) {
            allAssetsToExport.append(assetGuid);
        }
    }

    for (const auto &asset : allAssetsToExport) {
        QSqlQuery selectAssetQuery;
        selectAssetQuery.prepare(
            "SELECT guid, type, name, collection, times_used, project_guid, date_created, last_updated, "
            "author, license, hash, version, parent, tags, properties, asset, thumbnail, view_filter FROM assets WHERE guid = ?"
        );
        selectAssetQuery.addBindValue(asset);

        if (selectAssetQuery.exec()) {
            if (selectAssetQuery.first()) {
                AssetRecord data;
                data.guid           = selectAssetQuery.value(0).toString();
                data.type           = selectAssetQuery.value(1).toInt();
                data.name           = selectAssetQuery.value(2).toString();
                data.collection     = selectAssetQuery.value(3).toInt();
                data.timesUsed      = selectAssetQuery.value(4).toInt();
                data.projectGuid    = selectAssetQuery.value(5).toString();
                data.dateCreated    = selectAssetQuery.value(6).toDateTime();
                data.lastUpdated    = selectAssetQuery.value(7).toDateTime();
                data.author         = selectAssetQuery.value(8).toString();
                data.license        = selectAssetQuery.value(9).toString();
                data.hash           = selectAssetQuery.value(10).toString();
                data.version        = selectAssetQuery.value(11).toString();
                data.parent         = selectAssetQuery.value(12).toString();
                data.tags           = selectAssetQuery.value(13).toByteArray();
                data.properties     = selectAssetQuery.value(14).toByteArray();
                data.asset          = selectAssetQuery.value(15).toByteArray();
                data.thumbnail      = selectAssetQuery.value(16).toByteArray();
                data.view_filter	= selectAssetQuery.value(17).toInt();
                assetList.push_back(data);
            }
        }
        else {
            irisLog("There was an error fetching an asset " + selectAssetQuery.lastError().text());
        }
    }

    for (const auto &asset : assetList) {
        QSqlQuery insertExportAssetQuery(exportConnection);
        insertExportAssetQuery.prepare(
            "INSERT INTO assets"
            " (guid, type, name, collection, times_used, project_guid, date_created, last_updated, author,"
            " license, hash, version, parent, tags, properties, asset, thumbnail, view_filter)"
            " VALUES(:guid, :type, :name, :collection, :times_used, :project_guid, :date_created, :last_updated, :author,"
            " :license, :hash, :version, :parent, :tags, :properties, :asset, :thumbnail, :view_filter)"
        );

        insertExportAssetQuery.bindValue(":guid", asset.guid);
        insertExportAssetQuery.bindValue(":type", asset.type);
        insertExportAssetQuery.bindValue(":name", asset.name);
        insertExportAssetQuery.bindValue(":collection", asset.collection);
        insertExportAssetQuery.bindValue(":times_used", asset.timesUsed);
        insertExportAssetQuery.bindValue(":project_guid", asset.projectGuid);
        insertExportAssetQuery.bindValue(":date_created", asset.dateCreated);
        insertExportAssetQuery.bindValue(":last_updated", asset.lastUpdated);
        insertExportAssetQuery.bindValue(":author", asset.author);
        insertExportAssetQuery.bindValue(":license", asset.license);
        insertExportAssetQuery.bindValue(":hash", asset.hash);
        insertExportAssetQuery.bindValue(":version", asset.version);
        insertExportAssetQuery.bindValue(":parent", asset.parent);
        insertExportAssetQuery.bindValue(":tags", asset.tags);
        insertExportAssetQuery.bindValue(":properties", asset.properties);

        // Write a copy of objects IN the tree AS IS to preserve hierarchical structure
        if (asset.type == static_cast<int>(ModelTypes::Object) ||
            asset.type == static_cast<int>(ModelTypes::ParticleSystem))
        {
            QJsonObject assetJson;
            SceneWriter::writeSceneNode(assetJson, node);
            insertExportAssetQuery.bindValue(":asset", QJsonDocument(assetJson).toJson());
        }
        else {
            insertExportAssetQuery.bindValue(":asset", asset.asset);
        }

        insertExportAssetQuery.bindValue(":thumbnail", asset.thumbnail);
        insertExportAssetQuery.bindValue(":view_filter", asset.view_filter);

        executeAndCheckQuery(insertExportAssetQuery, "insertExportAssetQuery");
    }

    QVector<DependencyRecord> dependenciesToExport;

    for (const auto &asset : assetList) {
        // Dependency-export fix (phase 0): outgoing edges, every row — see
        // the matching comment in createExportBundle.
        QSqlQuery selectDep;
        selectDep.prepare(
            "SELECT depender_type, dependee_type, project_guid, depender, dependee, id "
            "FROM dependencies WHERE "
            "depender = ? AND depender_type = ?"
        );
        selectDep.addBindValue(asset.guid);
        selectDep.addBindValue(asset.type);

        if (selectDep.exec()) {
            while (selectDep.next()) {
                DependencyRecord record;
                record.dependerType = selectDep.value(0).toInt();
                record.dependeeType = selectDep.value(1).toInt();
                record.projectGuid  = selectDep.value(2).toString();
                record.depender     = selectDep.value(3).toString();
                record.dependee     = selectDep.value(4).toString();
                record.id           = selectDep.value(5).toString();
                dependenciesToExport.append(record);
            }
        }
        else {
            irisLog("There was an error fetching a dependency" + selectDep.lastError().text());
        }
    }

    QStringList assetDependencies;

    for (const auto &dep : dependenciesToExport) {
        QSqlQuery exportDep(exportConnection);
        exportDep.prepare(
            "INSERT INTO dependencies (depender_type, dependee_type, project_guid, depender, dependee, id) "
            "VALUES (:depender_type, :dependee_type, :project_guid, :depender, :dependee, :id)"
        );

        exportDep.bindValue(":depender_type", dep.dependerType);
        exportDep.bindValue(":dependee_type", dep.dependeeType);
        exportDep.bindValue(":project_guid", dep.projectGuid);
        exportDep.bindValue(":depender", dep.depender);
        exportDep.bindValue(":dependee", dep.dependee);
        exportDep.bindValue(":id", dep.id);

        executeAndCheckQuery(exportDep, "exportDep");
    }

    exportConnection.commit();
    exportConnection.close();
    exportConnection = QSqlDatabase();
    QSqlDatabase::removeDatabase("NodeExportConnection");

    return true;
}

bool Database::createBlobFromAsset(const QString &guid, const QString &writePath)
{
    QSqlDatabase exportConnection = QSqlDatabase();
    exportConnection = QSqlDatabase::addDatabase(Constants::DB_DRIVER, "NodeExportConnection");
    exportConnection.setDatabaseName(writePath);

    if (exportConnection.isValid()) {
        if (!exportConnection.open()) {
            irisLog(QString("Couldn't open a database connection! %1").arg(exportConnection.lastError().text()));
            return false;
        }
    }
    else {
        irisLog(QString("The database connection is invalid! %1").arg(exportConnection.lastError().text()));
        return false;
    }

    // Export DB writes in one transaction (phase 0): atomic bundle file,
    // one fsync instead of one per row.
    exportConnection.transaction();

    QSqlQuery createAssetsTable(exportConnection);
    createAssetsTable.prepare(assetsTableSchema);
    if (!executeAndCheckQuery(createAssetsTable, "CreateAssetsTable")) return false;

    QSqlQuery createDependenciesTable(exportConnection);
    createDependenciesTable.prepare(dependenciesTableSchema);
    if (!executeAndCheckQuery(createDependenciesTable, "CreateDependenciesTable")) return false;

    QVector<AssetRecord> assetList;
    QStringList allAssetsToExport;

    for (const auto &assetGuid : fetchAssetAndAllDependencies(guid)) {
        allAssetsToExport.append(assetGuid);
    }

    for (const auto &asset : allAssetsToExport) {
        QSqlQuery selectAssetQuery;
        selectAssetQuery.prepare(
            "SELECT guid, type, name, collection, times_used, project_guid, date_created, last_updated, "
            "author, license, hash, version, parent, tags, properties, asset, thumbnail, view_filter FROM assets WHERE guid = ?"
        );
        selectAssetQuery.addBindValue(asset);

        if (selectAssetQuery.exec()) {
            if (selectAssetQuery.first()) {
                AssetRecord data;
                data.guid = selectAssetQuery.value(0).toString();
                data.type = selectAssetQuery.value(1).toInt();
                data.name = selectAssetQuery.value(2).toString();
                data.collection = selectAssetQuery.value(3).toInt();
                data.timesUsed = selectAssetQuery.value(4).toInt();
                data.projectGuid = selectAssetQuery.value(5).toString();
                data.dateCreated = selectAssetQuery.value(6).toDateTime();
                data.lastUpdated = selectAssetQuery.value(7).toDateTime();
                data.author = selectAssetQuery.value(8).toString();
                data.license = selectAssetQuery.value(9).toString();
                data.hash = selectAssetQuery.value(10).toString();
                data.version = selectAssetQuery.value(11).toString();
                data.parent = selectAssetQuery.value(12).toString();
                data.tags = selectAssetQuery.value(13).toByteArray();
                data.properties = selectAssetQuery.value(14).toByteArray();
                data.asset = selectAssetQuery.value(15).toByteArray();
                data.thumbnail = selectAssetQuery.value(16).toByteArray();
                data.view_filter = selectAssetQuery.value(17).toInt();
                assetList.push_back(data);
            }
        }
        else {
            irisLog("There was an error fetching an asset " + selectAssetQuery.lastError().text());
        }
    }

    for (const auto &asset : assetList) {
        QSqlQuery insertExportAssetQuery(exportConnection);
        insertExportAssetQuery.prepare(
            "INSERT INTO assets"
            " (guid, type, name, collection, times_used, project_guid, date_created, last_updated, author,"
            " license, hash, version, parent, tags, properties, asset, thumbnail, view_filter)"
            " VALUES(:guid, :type, :name, :collection, :times_used, :project_guid, :date_created, :last_updated, :author,"
            " :license, :hash, :version, :parent, :tags, :properties, :asset, :thumbnail, :view_filter)"
        );

        insertExportAssetQuery.bindValue(":guid", asset.guid);
        insertExportAssetQuery.bindValue(":type", asset.type);
        insertExportAssetQuery.bindValue(":name", asset.name);
        insertExportAssetQuery.bindValue(":collection", asset.collection);
        insertExportAssetQuery.bindValue(":times_used", asset.timesUsed);
        insertExportAssetQuery.bindValue(":project_guid", asset.projectGuid);
        insertExportAssetQuery.bindValue(":date_created", asset.dateCreated);
        insertExportAssetQuery.bindValue(":last_updated", asset.lastUpdated);
        insertExportAssetQuery.bindValue(":author", asset.author);
        insertExportAssetQuery.bindValue(":license", asset.license);
        insertExportAssetQuery.bindValue(":hash", asset.hash);
        insertExportAssetQuery.bindValue(":version", asset.version);
        insertExportAssetQuery.bindValue(":parent", asset.parent);
        insertExportAssetQuery.bindValue(":tags", asset.tags);
        insertExportAssetQuery.bindValue(":properties", asset.properties);
        insertExportAssetQuery.bindValue(":asset", asset.asset);

        insertExportAssetQuery.bindValue(":thumbnail", asset.thumbnail);
        insertExportAssetQuery.bindValue(":view_filter", asset.view_filter);

        executeAndCheckQuery(insertExportAssetQuery, "insertExportAssetQuery");
    }

    QVector<DependencyRecord> dependenciesToExport;

    for (const auto &asset : assetList) {
        // Dependency-export fix (phase 0): outgoing edges, every row — see
        // the matching comment in createExportBundle.
        QSqlQuery selectDep;
        selectDep.prepare(
            "SELECT depender_type, dependee_type, project_guid, depender, dependee, id "
            "FROM dependencies WHERE "
            "depender = ? AND depender_type = ?"
        );
        selectDep.addBindValue(asset.guid);
        selectDep.addBindValue(asset.type);

        if (selectDep.exec()) {
            while (selectDep.next()) {
                DependencyRecord record;
                record.dependerType = selectDep.value(0).toInt();
                record.dependeeType = selectDep.value(1).toInt();
                record.projectGuid = selectDep.value(2).toString();
                record.depender = selectDep.value(3).toString();
                record.dependee = selectDep.value(4).toString();
                record.id = selectDep.value(5).toString();
                dependenciesToExport.append(record);
            }
        }
        else {
            irisLog("There was an error fetching a dependency" + selectDep.lastError().text());
        }
    }

    QStringList assetDependencies;

    for (const auto &dep : dependenciesToExport) {
        QSqlQuery exportDep(exportConnection);
        exportDep.prepare(
            "INSERT INTO dependencies (depender_type, dependee_type, project_guid, depender, dependee, id) "
            "VALUES (:depender_type, :dependee_type, :project_guid, :depender, :dependee, :id)"
        );

        exportDep.bindValue(":depender_type", dep.dependerType);
        exportDep.bindValue(":dependee_type", dep.dependeeType);
        exportDep.bindValue(":project_guid", dep.projectGuid);
        exportDep.bindValue(":depender", dep.depender);
        exportDep.bindValue(":dependee", dep.dependee);
        exportDep.bindValue(":id", dep.id);

        executeAndCheckQuery(exportDep, "exportDep");
    }

    exportConnection.commit();
    exportConnection.close();
    exportConnection = QSqlDatabase();
    QSqlDatabase::removeDatabase("NodeExportConnection");

    return true;
}

void Database::createExportScene(const QString &outTempFilePath, const QString &projectGuid)
{
    QSqlQuery query;
    query.prepare("SELECT name, scene, thumbnail, version, last_written, last_accessed, guid FROM projects WHERE guid = ?");
    query.addBindValue(projectGuid);

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
    auto sceneVersion = query.value(3).toString();
    auto sceneLastW = query.value(4).toDateTime();
    auto sceneLastA = query.value(5).toDateTime();
    auto sceneGuid  = query.value(6).toString();

    QSqlDatabase dbe = QSqlDatabase::addDatabase(Constants::DB_DRIVER, "myUniqueSQLITEConnection");
    dbe.setDatabaseName(QDir(outTempFilePath).filePath(projectGuid + ".db"));
    dbe.open();

    QString schema = "CREATE TABLE IF NOT EXISTS projects ("
                     "    name              VARCHAR(64),"
                     "    thumbnail         BLOB,"
                     "    last_accessed     DATETIME,"
                     "    last_written      DATETIME,"
                     "    date_created      DATETIME DEFAULT CURRENT_TIMESTAMP,"
                     "    scene             BLOB,"
                     "    version           VARCHAR(8),"
                     "    description       TEXT,"
                     "    url               TEXT,"
                     "    guid              VARCHAR(32) PRIMARY KEY"
                     ")";

    QSqlQuery query2(dbe);
    query2.prepare(schema);
    executeAndCheckQuery(query2, "createExportGlobalDb");

    QSqlQuery query3(dbe);
    query3.prepare(
        "INSERT INTO projects "
        "(name, scene, thumbnail, version, last_written, last_accessed, guid) "
        "VALUES (:name, :scene, :thumbnail, :version, :last_written, :last_accessed, :guid)"
    );
    query3.bindValue(":name",           sceneName);
    query3.bindValue(":scene",          sceneBlob);
    query3.bindValue(":thumbnail",      sceneThumb);
    query3.bindValue(":version",      sceneVersion);
    query3.bindValue(":last_written",   sceneLastW);
    query3.bindValue(":last_accessed",  sceneLastA);
    query3.bindValue(":guid",           sceneGuid);

    executeAndCheckQuery(query3, "insertSceneGlobal");

    QString createAssetsTableSchema =
        "CREATE TABLE IF NOT EXISTS assets ("
        "    guid              VARCHAR(32),"
        "	 type			   INTEGER,"
        "    name              VARCHAR(128),"
        "	 collection		   INTEGER,"
        "	 times_used		   INTEGER,"
        "    project_guid      VARCHAR(32),"
        "    date_created      DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "    last_updated      DATETIME,"
        "	 author			   VARCHAR(128),"
        "    license		   VARCHAR(64),"
        "    hash              VARCHAR(16),"
        "    version           VARCHAR(8),"
        "    parent            VARCHAR(32),"
        "    tags			   BLOB,"
        "    properties        BLOB,"
        "    asset             BLOB,"
        "    thumbnail         BLOB,"
		"    view_filter       INTEGER"
        ")";

    QSqlQuery createAssetsTableQuery(dbe);
    createAssetsTableQuery.prepare(createAssetsTableSchema);
    executeAndCheckQuery(createAssetsTableQuery, "createExportGlobalDb");

    QString dependenciesSchema =
        "CREATE TABLE IF NOT EXISTS dependencies ("
        "	 depender_type  INTEGER,"
        "	 dependee_type  INTEGER,"
        "    project_guid	VARCHAR(32),"
        "    depender		VARCHAR(32),"
        "    dependee		VARCHAR(32),"
        "    id				VARCHAR(32) PRIMARY KEY"
        ")";

    QSqlQuery dependenciesCreateSchema(dbe);
    dependenciesCreateSchema.prepare(dependenciesSchema);
    executeAndCheckQuery(dependenciesCreateSchema, "createExportDependencies");

    QString folderSchema =
        "CREATE TABLE IF NOT EXISTS folders ("
        "    guid              VARCHAR(32) PRIMARY KEY,"
        "    parent            VARCHAR(32),"
        "    project_guid      VARCHAR(32),"
        "    name              VARCHAR(128),"
        "    date_created      DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "    last_updated      DATETIME,"
        "    hash              VARCHAR(16),"
        "    version           VARCHAR(8),"
        "    count			   INTEGER,"
        "    visible           INTEGER"
        ")";

    QSqlQuery folderCreateSchema(dbe);
    folderCreateSchema.prepare(folderSchema);
    executeAndCheckQuery(folderCreateSchema, "folderCreateSchema");

    QVector<AssetRecord> assetList;

    QSqlQuery selectAssetQuery;
    selectAssetQuery.prepare(
        "SELECT guid, type, name, collection, times_used, project_guid, date_created, last_updated, author, "
        "license, hash, version, parent, tags, properties, asset, thumbnail, view_filter FROM assets "
        "WHERE project_guid = ? "
        "   OR guid IN (SELECT asset_guid FROM project_assets WHERE project_guid = ?)"
    );
    // Reference-with-pin (phase 4): pinned LIBRARY assets (project_guid empty)
    // are project members too and must ride along in the archive snapshot.
    selectAssetQuery.addBindValue(projectGuid);
    selectAssetQuery.addBindValue(projectGuid);
    executeAndCheckQuery(selectAssetQuery, "selectAssetQuery");

    while (selectAssetQuery.next()) {
        QSqlRecord record = query.record();
        AssetRecord data;
        data.guid = selectAssetQuery.value(0).toString();
        data.type = selectAssetQuery.value(1).toInt();
        data.name = selectAssetQuery.value(2).toString();
        data.collection = selectAssetQuery.value(3).toInt();
        data.timesUsed = selectAssetQuery.value(4).toInt();
        data.projectGuid = selectAssetQuery.value(5).toString();
        data.dateCreated = selectAssetQuery.value(6).toDateTime();
        data.lastUpdated = selectAssetQuery.value(7).toDateTime();
        data.author = selectAssetQuery.value(8).toString();
        data.license = selectAssetQuery.value(9).toString();
        data.hash = selectAssetQuery.value(10).toString();
        data.version = selectAssetQuery.value(11).toString();
        data.parent = selectAssetQuery.value(12).toString();
        data.tags = selectAssetQuery.value(13).toByteArray();
        data.properties = selectAssetQuery.value(14).toByteArray();
        data.asset = selectAssetQuery.value(15).toByteArray();
        data.thumbnail = selectAssetQuery.value(16).toByteArray();
        data.view_filter = selectAssetQuery.value(17).toInt();
        assetList.push_back(data);
    }

    for (const auto &asset : assetList) {
        QSqlQuery insertExportAssetQuery(dbe);
        insertExportAssetQuery.prepare(
            "INSERT INTO assets"
            " (guid, type, name, collection, times_used, project_guid, date_created, last_updated, author,"
            " license, hash, version, parent, tags, properties, asset, thumbnail, view_filter)"
            " VALUES(:guid, :type, :name, :collection, :times_used, :project_guid, :date_created, :last_updated, :author,"
            " :license, :hash, :version, :parent, :tags, :properties, :asset, :thumbnail, :view_filter)"
        );

        insertExportAssetQuery.bindValue(":guid", asset.guid);
        insertExportAssetQuery.bindValue(":type", asset.type);
        insertExportAssetQuery.bindValue(":name", asset.name);
        insertExportAssetQuery.bindValue(":collection", asset.collection);
        insertExportAssetQuery.bindValue(":times_used", asset.timesUsed);
        insertExportAssetQuery.bindValue(":project_guid", asset.projectGuid);
        insertExportAssetQuery.bindValue(":date_created", asset.dateCreated);
        insertExportAssetQuery.bindValue(":last_updated", asset.lastUpdated);
        insertExportAssetQuery.bindValue(":author", asset.author);
        insertExportAssetQuery.bindValue(":license", asset.license);
        insertExportAssetQuery.bindValue(":hash", asset.hash);
        insertExportAssetQuery.bindValue(":version", asset.version);
        insertExportAssetQuery.bindValue(":parent", asset.parent);
        insertExportAssetQuery.bindValue(":tags", asset.tags);
        insertExportAssetQuery.bindValue(":properties", asset.properties);
        insertExportAssetQuery.bindValue(":asset", asset.asset);
        insertExportAssetQuery.bindValue(":thumbnail", asset.thumbnail);
        insertExportAssetQuery.bindValue(":view_filter", asset.view_filter);

        executeAndCheckQuery(insertExportAssetQuery, "insertExportAssetQuery");
    }

    QVector<DependencyRecord> dependenciesToExport;

    QSqlQuery selectDep;
    selectDep.prepare(
		"SELECT depender_type, dependee_type, project_guid, depender, dependee, id FROM dependencies "
		"WHERE project_guid = ? "
		"   OR depender IN (SELECT asset_guid FROM project_assets WHERE project_guid = ?)"
	);
    selectDep.addBindValue(projectGuid);
    selectDep.addBindValue(projectGuid);
    executeAndCheckQuery(selectDep, "selectDep");

    while (selectDep.next()) {
        DependencyRecord record;
        record.dependerType = selectDep.value(0).toInt();
        record.dependeeType = selectDep.value(1).toInt();
        record.projectGuid = selectDep.value(2).toString();
        record.depender = selectDep.value(3).toString();
        record.dependee = selectDep.value(4).toString();
        record.id = selectDep.value(5).toString();
        dependenciesToExport.append(record);
    }

    for (const auto &dep : dependenciesToExport) {
        QSqlQuery exportDep(dbe);
        exportDep.prepare(
            "INSERT INTO dependencies (depender_type, dependee_type, project_guid, depender, dependee, id) "
            "VALUES (:depender_type, :dependee_type, :project_guid, :depender, :dependee, :id)"
        );

        exportDep.bindValue(":depender_type", dep.dependerType);
        exportDep.bindValue(":dependee_type", dep.dependeeType);
        exportDep.bindValue(":project_guid", dep.projectGuid);
        exportDep.bindValue(":depender", dep.depender);
        exportDep.bindValue(":dependee", dep.dependee);
        exportDep.bindValue(":id", dep.id);

        executeAndCheckQuery(exportDep, "exportDep");
    }

    QVector<FolderRecord> foldersToExport;

    QSqlQuery selectFolder;
    selectFolder.prepare(
		"SELECT guid, name, parent, count, project_guid, date_created, last_updated, visible FROM folders WHERE project_guid = ?"
	);
    selectFolder.addBindValue(projectGuid);
    executeAndCheckQuery(selectFolder, "selectFolder");

    while (selectFolder.next()) {
        FolderRecord record;
        record.guid = selectFolder.value(0).toString();
        record.name = selectFolder.value(1).toString();
        record.parent = selectFolder.value(2).toString();
        record.count = selectFolder.value(3).toInt();
        record.projectGuid = selectFolder.value(4).toString();
        record.dateCreated = selectFolder.value(5).toDateTime();
        record.lastUpdated = selectFolder.value(6).toDateTime();
        record.visible = selectFolder.value(7).toBool();
        foldersToExport.append(record);
    }

    for (const auto &folder : foldersToExport) {
        QSqlQuery exportFolder(dbe);
        exportFolder.prepare(
            "INSERT INTO folders (guid, name, parent, count, project_guid, date_created, last_updated, visible) "
            "VALUES (:guid, :name, :parent, :count, :project_guid, :date_created, :last_updated, :visible)"
        );

        exportFolder.bindValue(":guid", folder.guid);
        exportFolder.bindValue(":name", folder.name);
        exportFolder.bindValue(":parent", folder.parent);
        exportFolder.bindValue(":count", folder.count);
        exportFolder.bindValue(":project_guid", folder.projectGuid);
        exportFolder.bindValue(":date_created", folder.dateCreated);
        exportFolder.bindValue(":last_updated", folder.lastUpdated);
        exportFolder.bindValue(":visible", folder.visible);

        executeAndCheckQuery(exportFolder, "exportFolder");
    }

    dbe.close();
}

bool Database::checkIfRecordExists(const QString & record, const QVariant &value, const QString &table, bool perProject, const QString &projectGuid)
{
	QSqlQuery query;
	QString queryString = "SELECT EXISTS (SELECT 1 FROM %1 WHERE %2 = ? ";
	if (!perProject) queryString.append("AND project_guid = ? ");
	queryString.append("LIMIT 1)");
	query.prepare(queryString.arg(table).arg(record));
	query.addBindValue(value);
	if (!perProject) query.addBindValue(projectGuid);

	if (query.exec()) {
		if (query.first()) return query.value(0).toBool();
	}
	else {
		irisLog("There was an error fetching a record" + query.lastError().text());
	}

	return false;
}

bool Database::checkIfDependencyExists(const QString& depender, const QString& dependee)
{
	QSqlQuery query;
	query.prepare(QString("SELECT 1 from dependencies WHERE depender = ? and dependee = ?"));
	query.addBindValue(depender);
	query.addBindValue(dependee);
	if (query.exec()) {
		if (query.first()) return query.value(0).toBool();
	}
	else {
			irisLog("There was an error fetching a record " + query.lastError().text());
	}
	return false;
}

bool Database::checkIfProjectVersionSupported(const QString &pathToDb)
{
    return checkIfVersionSupported(pathToDb, "projects");
}

bool Database::checkIfJafModelVersionSupported(const QString &pathToDb)
{
    return checkIfVersionSupported(pathToDb, "assets");
}

QStringList Database::fetchFolderNameByParent(const QString &guid)
{
	QSqlQuery query;
	query.prepare("SELECT name FROM folders WHERE parent = ?");
	query.addBindValue(guid);
	executeAndCheckQuery(query, "fetchFolderNameByParent");

	QStringList folders;
	while (query.next()) {
		QSqlRecord record = query.record();
		folders.append(record.value(0).toString());
	}

	return folders;
}

QStringList Database::fetchAssetNameByParent(const QString &guid)
{
    QSqlQuery query;
    query.prepare("SELECT name FROM assets WHERE parent = ?");
    query.addBindValue(guid);
    executeAndCheckQuery(query, "FetchAssetNameByParent");

    QStringList assets;
    while (query.next()) {
        QSqlRecord record = query.record();
        assets.append(record.value(0).toString());
    }

    return assets;
}

QStringList Database::fetchFolderAndChildFolders(const QString &guid)
{
	std::function<void(QStringList&, const QString&)> fetchFolders
		= [&](QStringList &folders, const QString &guid) -> void
	{
		QSqlQuery query;
		query.prepare("SELECT guid FROM folders WHERE parent = ?");
		query.addBindValue(guid);
		executeAndCheckQuery(query, "fetchFolderAndDependencies");

		QStringList subFolders;
		while (query.next()) {
			QSqlRecord record = query.record();
			subFolders.append(record.value(0).toString());
			folders.append(record.value(0).toString());
		}

		for (const QString &folder : subFolders) {
			fetchFolders(folders, folder);
		}
	};

	QStringList folders;
	fetchFolders(folders, guid);
	folders.append(guid);

	return folders;
}

QStringList Database::fetchChildFolderAssets(const QString &guid)
{
	QSqlQuery query;
	query.prepare("SELECT guid FROM assets WHERE parent = ?");
	query.addBindValue(guid);
	executeAndCheckQuery(query, "fetchChildFolderAssets");

	QStringList assets;
	while (query.next()) {
		QString data;
		QSqlRecord record = query.record();
		assets.append(record.value(0).toString());
	}

	return assets;
}

QStringList Database::fetchAssetDependeesByType(const QString & guid, const ModelTypes &type)
{
    QSqlQuery query;
    query.prepare(
        "SELECT assets.guid FROM dependencies "
        "INNER JOIN assets ON dependencies.dependee = assets.guid "
        "WHERE depender = ? AND dependee_type = ?");
    query.addBindValue(guid);
    query.addBindValue(static_cast<int>(type));
    executeAndCheckQuery(query, "fetchAssetDependeesByType");

    QStringList dependencies;
    while (query.next()) {
        QSqlRecord record = query.record();
        dependencies.append(record.value(0).toString());
    }
    return dependencies;
}

QStringList Database::fetchAssetAndDependencies(const QString &guid)
{
	QSqlQuery query;
	query.prepare(
		"SELECT assets.name, assets.guid FROM dependencies "
		"INNER JOIN assets ON dependencies.dependee = assets.guid "
		"WHERE depender = ?"
	);
	query.addBindValue(guid);
	executeAndCheckQuery(query, "fetchAssetAndDependencies");

	QStringList dependencies;
	dependencies.append(fetchAsset(guid).name);
	while (query.next()) {
		QSqlRecord record = query.record();
		dependencies.append(record.value(0).toString());
	}

	for (int i = 0; i < dependencies.size(); ++i) {
		if (QFileInfo(dependencies[i]).suffix().isEmpty()) {
			dependencies.removeAt(i);
		}
	}

	return dependencies;
}


QStringList Database::fetchAssetGUIDAndDependencies(const QString &guid, bool appendSelf)
{
	QSqlQuery query;
	query.prepare(
        "SELECT assets.guid FROM dependencies INNER JOIN assets ON "
        "dependencies.dependee = assets.guid WHERE depender = ?"
    );
	query.addBindValue(guid);
	executeAndCheckQuery(query, "fetchAssetGUIDAndDependencies");

	QStringList dependencies;
    if (appendSelf) dependencies.append(guid);
	while (query.next()) {
		QSqlRecord record = query.record();
		dependencies.append(record.value(0).toString());
	}

	return dependencies;
}

QStringList Database::fetchAssetAndAllDependencies(const QString & guid)
{
    QStringList topLevelAssets;
    QStringList assetAndDependencies;

    for (const auto &asset : fetchAssetGUIDAndDependencies(guid)) {
        topLevelAssets.append(asset);
        assetAndDependencies.append(asset);
    }

    // Dependency level is always max 2
    for (const auto &asset : topLevelAssets) {
        for (const auto &guid : fetchAssetGUIDAndDependencies(asset)) {
            assetAndDependencies.append(guid);
        }
    }

    assetAndDependencies.removeDuplicates();

    return assetAndDependencies;
}

QVector<DependencyRecord> Database::fetchAssetDependencies(const AssetRecord &record)
{
    QSqlQuery query;
    query.prepare(
        "SELECT D.depender_type, D.dependee_type, D.depender, D.dependee, D.id, D.project_guid "
        "FROM dependencies D INNER JOIN assets ON "
        "D.dependee = assets.guid WHERE depender = ? AND depender_type = ?"
    );
    query.addBindValue(record.guid);
    query.addBindValue(record.type);
    executeAndCheckQuery(query, "FetchAssetDependencies");

    QVector<DependencyRecord> dependencies;
    while (query.next()) {
        DependencyRecord dr;
        QSqlRecord record = query.record();
        dr.dependerType = record.value(0).toInt();
        dr.dependeeType = record.value(1).toInt();
        dr.depender = record.value(2).toString();
        dr.dependee = record.value(3).toString();
        dr.id = record.value(4).toString();
        dr.projectGuid = record.value(5).toString();
        dependencies.append(dr);
    }

    return dependencies;
}

QStringList Database::deleteFolderAndDependencies(const QString &guid)
{
	QStringList files;

	// Get all child folders
	for (const auto &folder : fetchFolderAndChildFolders(guid)) {
		// For every folder, fetch assets inside
		for (const auto &asset : fetchChildFolderAssets(folder)) {
			// For every asset, find their dependencies
			for (const auto &dep : fetchAssetAndDependencies(asset)) {
				files.append(dep);
			}

			deleteAsset(asset);
			
            for (const auto &dep : fetchAssetGUIDAndDependencies(asset, false)) {
                deleteDependency(asset, dep);
            }
		}

		deleteFolder(folder);
	}

	for (int i = 0; i < files.size(); ++i) {
		if (QFileInfo(files[i]).suffix().isEmpty()) {
			files.removeAt(i);
		}
	}

	files.removeDuplicates();
	return files;
}

QStringList Database::deleteAssetAndDependencies(const QString & guid)
{
	QStringList files;

	DbTransaction tx(db);

	// For every asset, find their dependencies
	for (const auto &asset : fetchAssetGUIDAndDependencies(guid)) {
		for (const auto &dep : fetchAssetAndDependencies(asset)) {
			files.append(dep);
		}

		deleteAsset(asset);
		
        for (const auto &dep : fetchAssetGUIDAndDependencies(asset, false)) {
            deleteDependency(asset, dep);
        }
	}

	tx.commit();

    for (int i = 0; i < files.size(); ++i) {
	    if (QFileInfo(files[i]).suffix().isEmpty()) {
		    files.removeAt(i);
	    }
    }

    files.removeDuplicates();
    return files;
}

QString Database::fetchAssetGUIDByName(const QString & name, const QString &projectGuid)
{
	QSqlQuery query;
	query.prepare("SELECT guid FROM assets WHERE name = ? AND project_guid = ?");
	query.addBindValue(name);
	query.addBindValue(projectGuid);

	if (query.exec()) {
		if (query.first()) {
			return query.value(0).toString();
		}
	}
	else {
		irisLog(
			"There was an error fetching a guid for an asset (" + name + ")" + query.lastError().text()
		);
	}

	return QString();
}

QString Database::fetchObjectMesh(const QString &guid, const int ertype, const int eetype)
{
	QSqlQuery query;
	query.prepare("SELECT dependee FROM dependencies WHERE depender = ? AND depender_type = ? AND dependee_type = ?");
	query.addBindValue(guid);
	query.addBindValue(ertype);
	query.addBindValue(eetype);

	if (query.exec()) {
		if (query.first()) {
			return query.value(0).toString();
		}
	}
	else {
		irisLog(
			"There was an error fetching a guid" + query.lastError().text()
		);
	}

	return QString();
}

QString Database::fetchMeshObject(const QString &guid, const int ertype, const int eetype)
{
	QSqlQuery query;
	query.prepare("SELECT depender FROM dependencies WHERE dependee = ? AND depender_type = ? AND dependee_type == ?");
	query.addBindValue(guid);
	query.addBindValue(ertype);
	query.addBindValue(eetype);

	if (query.exec()) {
		if (query.first()) {
			return query.value(0).toString();
		}
	}
	else {
		irisLog(
			"There was an error fetching a guid" + query.lastError().text()
		);
	}

	return QString();
}

QStringList Database::hasMultipleDependers(const QString &guid)
{
    QSqlQuery query;
    query.prepare("SELECT depender FROM dependencies WHERE dependee = ?");
    query.addBindValue(guid);
    executeAndCheckQuery(query, "HasMultipleDependers");

    QStringList dependers;
    while (query.next()) {
        QSqlRecord record = query.record();
        dependers.append(record.value(0).toString());
    }
    return dependers;
}

bool Database::hasDependencies(const QString &guid)
{
    QSqlQuery query;
    query.prepare("SELECT COUNT(*) FROM dependencies WHERE depender = ?");
    query.addBindValue(guid);
    executeAndCheckQuery(query, "HasDependencies");

    if (query.exec()) {
        if (query.first()) {
            return query.value(0).toBool();
        }
    }
    else {
        irisLog("There was an error getting the dependency count! " + query.lastError().text());
    }

    return false;
}

bool Database::importProject(const QString &inFilePath, const QString &newSceneGuid, QString &worldName, QMap<QString, QString> &outGuids)
{
    QSqlDatabase dbe = QSqlDatabase::addDatabase(Constants::DB_DRIVER, GUIDManager::generateGUID());
    dbe.setDatabaseName(inFilePath + ".db");
    dbe.open();

    // One transaction around the whole multi-row import (phase 0): a failed
    // import leaves nothing behind, and per-row autocommit fsyncs are gone.
    DbTransaction tx(db);

    QSqlQuery query(dbe);
    query.prepare("SELECT name, scene, thumbnail, version, last_written, last_accessed, guid FROM projects");

    if (query.exec()) {
        query.next();
    }
    else {
        irisLog(
            "There was an error fetching a record to be imported " + query.lastError().text()
        );
    }

    auto sceneName = worldName = query.value(0).toString();
    auto sceneBlob = query.value(1).toByteArray();
    auto sceneThumb = query.value(2).toByteArray();
    auto sceneVersion = query.value(3).toString();
    auto sceneLastW = query.value(4).toDateTime();
    auto sceneLastA = query.value(5).toDateTime();
    auto oldSceneGuid = query.value(6).toString();

    QVector<AssetRecord> assetList;

    QSqlQuery selectAssetQuery(dbe);
    selectAssetQuery.prepare(
        "SELECT guid, type, name, collection, times_used, project_guid, date_created, last_updated, author, "
        "license, hash, version, parent, tags, properties, asset, thumbnail FROM assets"
    );
    executeAndCheckQuery(selectAssetQuery, "selectAssetQuery");

    QMap<QString, QString> assetGuids; /* old x new guid */

    while (selectAssetQuery.next()) {
        QSqlRecord record = query.record();
        AssetRecord data;

        auto guid = selectAssetQuery.value(0).toString();

        auto newGuid = GUIDManager::generateGUID();
        assetGuids.insert(guid, newGuid);

        outGuids.insert(guid, newGuid);

        data.guid = newGuid;
        data.type = selectAssetQuery.value(1).toInt();
        data.name = selectAssetQuery.value(2).toString();
        data.collection = selectAssetQuery.value(3).toInt();
        data.timesUsed = selectAssetQuery.value(4).toInt();
        data.projectGuid = selectAssetQuery.value(5).toString();
        data.dateCreated = selectAssetQuery.value(6).toDateTime();
        data.lastUpdated = selectAssetQuery.value(7).toDateTime();
        data.author = selectAssetQuery.value(8).toString();
        data.license = selectAssetQuery.value(9).toString();
        data.hash = selectAssetQuery.value(10).toString();
        data.version = selectAssetQuery.value(11).toString();
        data.parent = selectAssetQuery.value(12).toString();
        data.tags = selectAssetQuery.value(13).toByteArray();
        data.properties = selectAssetQuery.value(14).toByteArray();
        data.asset = selectAssetQuery.value(15).toByteArray();
        data.thumbnail = selectAssetQuery.value(16).toByteArray();
		data.view_filter = AssetViewFilter::Editor;
        assetList.push_back(data);
    }

    QSqlQuery query3;
    query3.prepare(
        "INSERT INTO projects "
        "(name, scene, thumbnail, version, last_written, last_accessed, guid) "
        "VALUES (:name, :scene, :thumbnail, :version, :last_written, :last_accessed, :guid)"
    );

    auto doc = QJsonDocument::fromJson(sceneBlob);
    QString docToString = doc.toJson(QJsonDocument::Compact);

    QMapIterator<QString, QString> i(assetGuids);
    while (i.hasNext()) {
        i.next();
        docToString.replace(i.key(), i.value());
    }

    QJsonDocument updatedDoc = QJsonDocument::fromJson(docToString.toUtf8());
    sceneBlob = updatedDoc.toJson();

    query3.bindValue(":name", sceneName);
    query3.bindValue(":scene", sceneBlob);
    query3.bindValue(":thumbnail", sceneThumb);
    query3.bindValue(":version", sceneVersion);
    query3.bindValue(":last_written", sceneLastW);
    query3.bindValue(":last_accessed", sceneLastA);
    query3.bindValue(":guid", newSceneGuid);

    executeAndCheckQuery(query3, "insertSceneGlobal");

    for (auto &asset : assetList) {
        QSqlQuery insertImportAssetQuery;
        insertImportAssetQuery.prepare(
            "INSERT INTO assets"
            " (guid, type, name, collection, times_used, project_guid, date_created, last_updated, author,"
            " license, hash, version, parent, tags, properties, asset, thumbnail, view_filter)"
            " VALUES(:guid, :type, :name, :collection, :times_used, :project_guid, :date_created, :last_updated, :author,"
            " :license, :hash, :version, :parent, :tags, :properties, :asset, :thumbnail, :view_filter)"
        );

        insertImportAssetQuery.bindValue(":guid", asset.guid);
        insertImportAssetQuery.bindValue(":type", asset.type);
        insertImportAssetQuery.bindValue(":name", asset.name);
        insertImportAssetQuery.bindValue(":collection", asset.collection);
        insertImportAssetQuery.bindValue(":times_used", asset.timesUsed);
        insertImportAssetQuery.bindValue(":project_guid", newSceneGuid);
        insertImportAssetQuery.bindValue(":date_created", asset.dateCreated);
        insertImportAssetQuery.bindValue(":last_updated", asset.lastUpdated);
        insertImportAssetQuery.bindValue(":author", asset.author);
        insertImportAssetQuery.bindValue(":license", asset.license);
        insertImportAssetQuery.bindValue(":hash", asset.hash);
        insertImportAssetQuery.bindValue(":version", asset.version);
        if (asset.parent == oldSceneGuid) {
            insertImportAssetQuery.bindValue(":parent", newSceneGuid);
        }
        else {
            insertImportAssetQuery.bindValue(":parent", asset.parent);
        }
        insertImportAssetQuery.bindValue(":tags", asset.tags);
        insertImportAssetQuery.bindValue(":properties", asset.properties);

        if (asset.type == static_cast<int>(ModelTypes::Object) ||
            asset.type == static_cast<int>(ModelTypes::Shader) ||
            asset.type == static_cast<int>(ModelTypes::Material))
        {
            auto doc = QJsonDocument::fromJson(asset.asset);
            QString docToString = doc.toJson(QJsonDocument::Compact);

            QMapIterator<QString, QString> i(assetGuids);
            while (i.hasNext()) {
                i.next();
                docToString.replace(i.key(), i.value());
            }

            QJsonDocument updatedDoc = QJsonDocument::fromJson(docToString.toUtf8());
            asset.asset = updatedDoc.toJson();
            insertImportAssetQuery.bindValue(":asset", asset.asset);
        }
        else {
            insertImportAssetQuery.bindValue(":asset", asset.asset);
        }

        insertImportAssetQuery.bindValue(":thumbnail", asset.thumbnail);
        insertImportAssetQuery.bindValue(":view_filter", asset.view_filter);

        executeAndCheckQuery(insertImportAssetQuery, "insertImportAssetQuery");
    }

    QVector<DependencyRecord> dependenciesToImport;

    QSqlQuery selectDep(dbe);
    selectDep.prepare("SELECT depender_type, dependee_type, project_guid, depender, dependee, id FROM dependencies");
    executeAndCheckQuery(selectDep, "selectDep");

    while (selectDep.next()) {
        DependencyRecord record;
        record.dependerType = selectDep.value(0).toInt();
        record.dependeeType = selectDep.value(1).toInt();
        record.projectGuid = selectDep.value(2).toString();
        record.depender = selectDep.value(3).toString();
        record.dependee = selectDep.value(4).toString();
        record.id = selectDep.value(5).toString();
        dependenciesToImport.append(record);
    }

    for (const auto &dep : dependenciesToImport) {
        QSqlQuery importDep;
        importDep.prepare(
            "INSERT INTO dependencies (depender_type, dependee_type, project_guid, depender, dependee, id) "
            "VALUES (:depender_type, :dependee_type, :project_guid, :depender, :dependee, :id)"
        );

        auto depender = assetGuids.value(dep.depender);
        auto dependee = assetGuids.value(dep.dependee);

        importDep.bindValue(":depender_type", dep.dependerType);
        importDep.bindValue(":dependee_type", dep.dependeeType);
        importDep.bindValue(":project_guid", newSceneGuid);
        importDep.bindValue(":depender", !depender.isEmpty() ? depender : dep.depender);
        importDep.bindValue(":dependee", !dependee.isEmpty() ? dependee : dep.dependee);
        importDep.bindValue(":id", GUIDManager::generateGUID());

        executeAndCheckQuery(importDep, "importDep");
    }

    QVector<FolderRecord> foldersToImport;

    QSqlQuery selectFolder(dbe);
    selectFolder.prepare("SELECT guid, name, parent, count, project_guid, date_created, last_updated, visible FROM folders");
    executeAndCheckQuery(selectFolder, "selectFolder");

    while (selectFolder.next()) {
        FolderRecord record;
        record.guid = selectFolder.value(0).toString();
        record.name = selectFolder.value(1).toString();
        record.parent = selectFolder.value(2).toString();
        record.count = selectFolder.value(3).toInt();
        record.projectGuid = selectFolder.value(4).toString();
        record.dateCreated = selectFolder.value(5).toDateTime();
        record.lastUpdated = selectFolder.value(5).toDateTime();
        record.visible = selectFolder.value(6).toBool();
        foldersToImport.append(record);
    }

    for (const auto &folder : foldersToImport) {
        QSqlQuery importFolder;
        importFolder.prepare(
            "INSERT INTO folders (guid, name, parent, count, project_guid, date_created, last_updated, visible) "
            "VALUES (:guid, :name, :parent, :count, :project_guid, :date_created, :last_updated, :visible)"
        );

        auto newFolderGuid = GUIDManager::generateGUID();
        importFolder.bindValue(":guid", newFolderGuid);
        importFolder.bindValue(":name", folder.name);
        if (folder.parent == oldSceneGuid) {
            importFolder.bindValue(":parent", newFolderGuid);
        }
        else {
            importFolder.bindValue(":parent", folder.parent);
        }
        importFolder.bindValue(":count", folder.count);
        importFolder.bindValue(":project_guid", newSceneGuid);
        importFolder.bindValue(":date_created", folder.dateCreated);
        importFolder.bindValue(":last_updated", folder.lastUpdated);
        importFolder.bindValue(":visible", folder.visible);

        executeAndCheckQuery(importFolder, "importFolder");
    }

    dbe.close();

    return tx.commit();
}

QString Database::importAsset(
	const ModelTypes &jafType,
	const QString & pathToDb,
	const QMap<QString, QString>& newNames,
    QMap<QString, QString> &outGuids,
    QVector<AssetRecord> &assetRecords,
	AssetViewFilter view_filter_to,
	const QString &projectGuid,
	const QString &parent)
{
    QSqlDatabase importConnection = QSqlDatabase();
    importConnection = QSqlDatabase::addDatabase(Constants::DB_DRIVER, "NodeImportConnection");
    importConnection.setDatabaseName(pathToDb);

    if (importConnection.isValid()) {
        if (!importConnection.open()) {
            irisLog(QString("Couldn't open a database connection! %1").arg(importConnection.lastError().text()));
        }
    }
    else {
        irisLog(QString("The database connection is invalid! %1").arg(importConnection.lastError().text()));
    }

	// Whole .jaf import lands in one transaction on the main library (phase 0).
	DbTransaction tx(db);

	QSqlQuery selectAssetQuery(importConnection);
	selectAssetQuery.prepare(
		"SELECT guid, type, name, collection, times_used, project_guid, date_created, last_updated, "
		"author, license, hash, version, parent, tags, properties, asset, thumbnail FROM assets"
	);
	executeAndCheckQuery(selectAssetQuery, "fetchImportAssets");

	QMap<QString, QString> assetGuids; /* old x new guid */

	QString guidToReturn = GUIDManager::generateGUID();

	QVector<AssetRecord> assetsToImport;

	while (selectAssetQuery.next()) {
        AssetRecord data;
		QSqlRecord record = selectAssetQuery.record();

		for (int i = 0; i < record.count(); i++) {
			if (selectAssetQuery.value(1).toInt() == static_cast<int>(jafType)) {
				data.guid = guidToReturn;
				assetGuids.insert(record.value(0).toString(), guidToReturn);
			}
			else {
				QString guid = GUIDManager::generateGUID();
				assetGuids.insert(record.value(0).toString(), guid);
				data.guid = guid;
			}

            outGuids.insert(record.value(0).toString(), data.guid);

			data.type = record.value(1).toInt();

			// If we find a file with the same name, rename it (may or may not change)
            if (!newNames.isEmpty()) {
                for (const auto &name : newNames) {
                    if (!newNames.value(record.value(2).toString()).isEmpty()) {
                        data.name = newNames.value(record.value(2).toString());
                    }
                    else data.name = record.value(2).toString();
                }
            }
            else {
                data.name = record.value(2).toString();
            }

			data.collection = record.value(3).toInt();
			data.timesUsed = record.value(4).toInt();
            // Store assets don't have parents so keep this field NULL
			if (!parent.isEmpty()) data.projectGuid = projectGuid;
			data.dateCreated = record.value(6).toDateTime();
			data.lastUpdated = record.value(7).toDateTime();
			data.author = record.value(8).toString();
			data.license = record.value(9).toString();
			data.hash = record.value(10).toString();
			data.version = record.value(11).toString();
            if (!parent.isEmpty()) data.parent = parent;
			data.tags = record.value(13).toByteArray();
			data.properties = record.value(14).toByteArray();
			data.asset = record.value(15).toByteArray();
			data.thumbnail = record.value(16).toByteArray();
			data.view_filter = view_filter_to;
		}

		assetsToImport.push_back(data);
	}

    for (auto &asset : assetsToImport) {
        if (asset.type == static_cast<int>(ModelTypes::Shader)   ||
            asset.type == static_cast<int>(ModelTypes::Material) ||
            asset.type == static_cast<int>(ModelTypes::Sky) ||
            asset.type == static_cast<int>(ModelTypes::Object))
        {
            auto doc = QJsonDocument::fromJson(asset.asset);
            QString docToString = doc.toJson(QJsonDocument::Compact);

            QMapIterator<QString, QString> i(assetGuids);
            while (i.hasNext()) {
                i.next();
                docToString.replace(i.key(), i.value());
            }

            QJsonDocument updatedDoc = QJsonDocument::fromJson(docToString.toUtf8());
            asset.asset = updatedDoc.toJson();
        }
    }

    assetRecords = assetsToImport;

	QSqlQuery selectDepQuery(importConnection);
	selectDepQuery.prepare("SELECT depender_type, dependee_type, depender, dependee FROM dependencies");
	executeAndCheckQuery(selectDepQuery, "fetchImportDeps");

	QVector<DependencyRecord> depsToImport;
	while (selectDepQuery.next()) {
        DependencyRecord data;
		QSqlRecord record = selectDepQuery.record();
		for (int i = 0; i < record.count(); i++) {
			data.dependerType = record.value(0).toInt();
			data.dependeeType = record.value(1).toInt();
			if (!parent.isEmpty()) data.projectGuid = parent;
			data.depender = assetGuids.value(record.value(2).toString());
			data.dependee = assetGuids.value(record.value(3).toString());

			data.id = GUIDManager::generateGUID();
		}

		if (!data.depender.isEmpty()) depsToImport.push_back(data);
	}

	for (const auto &asset : assetsToImport) {
		QSqlQuery insertAssetQuery;
		insertAssetQuery.prepare(
			"INSERT INTO assets"
			" (guid, type, name, collection, times_used, project_guid, date_created, last_updated, author,"
			" license, hash, version, parent, tags, properties, asset, thumbnail, view_filter)"
			" VALUES(:guid, :type, :name, :collection, :times_used, :project_guid, :date_created, :last_updated, :author,"
			" :license, :hash, :version, :parent, :tags, :properties, :asset, :thumbnail, :view_filter)"
		);

        if (jafType == ModelTypes::Texture) {
            guidToReturn = asset.guid;
        }

		insertAssetQuery.bindValue(":guid", asset.guid);
		insertAssetQuery.bindValue(":type", asset.type);
		insertAssetQuery.bindValue(":name", asset.name);
		insertAssetQuery.bindValue(":collection", asset.collection);
		insertAssetQuery.bindValue(":times_used", asset.timesUsed);
		insertAssetQuery.bindValue(":project_guid", asset.projectGuid);
		insertAssetQuery.bindValue(":date_created", asset.dateCreated);
		insertAssetQuery.bindValue(":last_updated", asset.lastUpdated);
		insertAssetQuery.bindValue(":author", asset.author);
		insertAssetQuery.bindValue(":license", asset.license);
		insertAssetQuery.bindValue(":hash", asset.hash);
		insertAssetQuery.bindValue(":version", asset.version);
		insertAssetQuery.bindValue(":parent", asset.parent);
		insertAssetQuery.bindValue(":tags", asset.tags);
		insertAssetQuery.bindValue(":properties", asset.properties);
		insertAssetQuery.bindValue(":asset", asset.asset);
		insertAssetQuery.bindValue(":thumbnail", asset.thumbnail);
		insertAssetQuery.bindValue(":view_filter", asset.view_filter);

		executeAndCheckQuery(insertAssetQuery, "insertAssetQuery");
	}

	for (const auto &dep : depsToImport) {
        QSqlQuery importDep;
        importDep.prepare(
            "INSERT INTO dependencies (depender_type, dependee_type, project_guid, depender, dependee, id) "
            "VALUES (:depender_type, :dependee_type, :project_guid, :depender, :dependee, :id)"
        );
        importDep.bindValue(":depender_type",   dep.dependerType);
        importDep.bindValue(":dependee_type",   dep.dependeeType);
        importDep.bindValue(":project_guid",    dep.projectGuid);
        importDep.bindValue(":depender",        dep.depender);
        importDep.bindValue(":dependee",        dep.dependee);
        importDep.bindValue(":id",              dep.id);
		executeAndCheckQuery(importDep, "ImportDep");
	}

    importConnection.close();
    importConnection = QSqlDatabase();
    QSqlDatabase::removeDatabase("NodeImportConnection");

	tx.commit();
	return guidToReturn;
}

QString Database::importAssetBundle(const QString & pathToDb, const QMap<QString, QString>& newNames, QMap<QString, QString>& outGuids, QVector<AssetRecord>& assetRecords, const QString &projectGuid, const QString & parent)
{
    QSqlDatabase importConnection = QSqlDatabase();
    importConnection = QSqlDatabase::addDatabase(Constants::DB_DRIVER, "NodeImportConnection");
    importConnection.setDatabaseName(pathToDb);

    if (importConnection.isValid()) {
        if (!importConnection.open()) {
            irisLog(QString("Couldn't open a database connection! %1").arg(importConnection.lastError().text()));
        }
    }
    else {
        irisLog(QString("The database connection is invalid! %1").arg(importConnection.lastError().text()));
    }

    // Whole bundle import lands in one transaction on the main library (phase 0).
    DbTransaction tx(db);

    QSqlQuery selectAssetQuery(importConnection);
    selectAssetQuery.prepare(
        "SELECT guid, type, name, collection, times_used, project_guid, date_created, last_updated, "
        "author, license, hash, version, parent, tags, properties, asset, thumbnail, view_filter FROM assets"
    );
    executeAndCheckQuery(selectAssetQuery, "fetchImportAssets");

    QMap<QString, QString> assetGuids; /* old x new guid */

    QString guidToReturn = GUIDManager::generateGUID();

    QVector<AssetRecord> assetsToImport;

    while (selectAssetQuery.next()) {
        AssetRecord data;
        QSqlRecord record = selectAssetQuery.record();

        for (int i = 0; i < record.count(); i++) {
            //if (selectAssetQuery.value(1).toInt() == static_cast<int>(jafType)) {
            //    data.guid = guidToReturn;
            //    assetGuids.insert(record.value(0).toString(), guidToReturn);
            //}
            //else {
                QString guid = GUIDManager::generateGUID();
                assetGuids.insert(record.value(0).toString(), guid);
                data.guid = guid;
            //}

            outGuids.insert(record.value(0).toString(), data.guid);

            data.type = record.value(1).toInt();

            // If we find a file with the same name, rename it (may or may not change)
            if (!newNames.isEmpty()) {
                for (const auto &name : newNames) {
                    if (!newNames.value(record.value(2).toString()).isEmpty()) {
                        data.name = newNames.value(record.value(2).toString());
                    }
                    else data.name = record.value(2).toString();
                }
            }
            else {
                data.name = record.value(2).toString();
            }

            data.collection = record.value(3).toInt();
            data.timesUsed = record.value(4).toInt();
            // Store assets don't have parents so keep this field NULL
            if (!parent.isEmpty()) data.projectGuid = projectGuid;
            data.dateCreated = record.value(6).toDateTime();
            data.lastUpdated = record.value(7).toDateTime();
            data.author = record.value(8).toString();
            data.license = record.value(9).toString();
            data.hash = record.value(10).toString();
            data.version = record.value(11).toString();
            if (!parent.isEmpty()) data.parent = parent;
            data.tags = record.value(13).toByteArray();
            data.properties = record.value(14).toByteArray();
            data.asset = record.value(15).toByteArray();
            data.thumbnail = record.value(16).toByteArray();
            data.view_filter = record.value(17).toInt();
        }

        assetsToImport.push_back(data);
    }

    for (auto &asset : assetsToImport) {
        if (asset.type == static_cast<int>(ModelTypes::Shader) ||
            asset.type == static_cast<int>(ModelTypes::Material) ||
            asset.type == static_cast<int>(ModelTypes::Object))
        {
            auto doc = QJsonDocument::fromJson(asset.asset);
            QString docToString = doc.toJson(QJsonDocument::Compact);

            QMapIterator<QString, QString> i(assetGuids);
            while (i.hasNext()) {
                i.next();
                docToString.replace(i.key(), i.value());
            }

            QJsonDocument updatedDoc = QJsonDocument::fromJson(docToString.toUtf8());
            asset.asset = updatedDoc.toJson();
        }
    }

    assetRecords = assetsToImport;

    QSqlQuery selectDepQuery(importConnection);
    selectDepQuery.prepare("SELECT depender_type, dependee_type, depender, dependee FROM dependencies");
    executeAndCheckQuery(selectDepQuery, "fetchImportDeps");

    QVector<DependencyRecord> depsToImport;
    while (selectDepQuery.next()) {
        DependencyRecord data;
        QSqlRecord record = selectDepQuery.record();
        for (int i = 0; i < record.count(); i++) {
            data.dependerType = record.value(0).toInt();
            data.dependeeType = record.value(1).toInt();
            if (!parent.isEmpty()) data.projectGuid = parent;
            data.depender = assetGuids.value(record.value(2).toString());
            data.dependee = assetGuids.value(record.value(3).toString());
            data.id = GUIDManager::generateGUID();
        }

        depsToImport.push_back(data);
    }

    for (const auto &asset : assetsToImport) {
        QSqlQuery insertAssetQuery;
        insertAssetQuery.prepare(
            "INSERT INTO assets"
            " (guid, type, name, collection, times_used, project_guid, date_created, last_updated, author,"
            " license, hash, version, parent, tags, properties, asset, thumbnail, view_filter)"
            " VALUES(:guid, :type, :name, :collection, :times_used, :project_guid, :date_created, :last_updated, :author,"
            " :license, :hash, :version, :parent, :tags, :properties, :asset, :thumbnail, :view_filter)"
        );

        //if (jafType == ModelTypes::Texture) {
        //    guidToReturn = asset.guid;
        //}

        insertAssetQuery.bindValue(":guid", asset.guid);
        insertAssetQuery.bindValue(":type", asset.type);
        insertAssetQuery.bindValue(":name", asset.name);
        insertAssetQuery.bindValue(":collection", asset.collection);
        insertAssetQuery.bindValue(":times_used", asset.timesUsed);
        insertAssetQuery.bindValue(":project_guid", asset.projectGuid);
        insertAssetQuery.bindValue(":date_created", asset.dateCreated);
        insertAssetQuery.bindValue(":last_updated", asset.lastUpdated);
        insertAssetQuery.bindValue(":author", asset.author);
        insertAssetQuery.bindValue(":license", asset.license);
        insertAssetQuery.bindValue(":hash", asset.hash);
        insertAssetQuery.bindValue(":version", asset.version);
        insertAssetQuery.bindValue(":parent", asset.parent);
        insertAssetQuery.bindValue(":tags", asset.tags);
        insertAssetQuery.bindValue(":properties", asset.properties);
        insertAssetQuery.bindValue(":asset", asset.asset);
        insertAssetQuery.bindValue(":thumbnail", asset.thumbnail);
        insertAssetQuery.bindValue(":view_filter", asset.view_filter);

        executeAndCheckQuery(insertAssetQuery, "insertAssetQuery");
    }

    for (const auto &dep : depsToImport) {
        QSqlQuery importDep;
        importDep.prepare(
            "INSERT INTO dependencies (depender_type, dependee_type, project_guid, depender, dependee, id) "
            "VALUES (:depender_type, :dependee_type, :project_guid, :depender, :dependee, :id)"
        );
        importDep.bindValue(":depender_type", dep.dependerType);
        importDep.bindValue(":dependee_type", dep.dependeeType);
        importDep.bindValue(":project_guid", dep.projectGuid);
        importDep.bindValue(":depender", dep.depender);
        importDep.bindValue(":dependee", dep.dependee);
        importDep.bindValue(":id", dep.id);
        executeAndCheckQuery(importDep, "ImportDep");
    }

    importConnection.close();
    importConnection = QSqlDatabase();
    QSqlDatabase::removeDatabase("NodeImportConnection");

    tx.commit();
    return guidToReturn;
}

bool Database::checkIfVersionSupported(const QString &pathToDb, const QString &table_name)
{
    bool result = false;

    // A UNIQUE, same-thread connection (was the hard-coded name
    // "NodeImportConnection"): the import pipeline's version probe now runs
    // on ImportBatchRunner's worker thread — a fixed name would collide with
    // the jaf commit path's connection of the same name on the UI thread,
    // and QSqlDatabase connections must be created and used on one thread.
    // Scoped so the QSqlDatabase/QSqlQuery handles die before removeDatabase.
    const QString connectionName =
        QStringLiteral("JafVersionProbe-%1").arg(QUuid::createUuid().toString(QUuid::Id128));
    {
        QSqlDatabase importConnection =
            QSqlDatabase::addDatabase(Constants::DB_DRIVER, connectionName);
        importConnection.setDatabaseName(pathToDb);

        if (importConnection.isValid()) {
            if (!importConnection.open()) {
                irisLog(QString("Couldn't open a database connection! %1").arg(importConnection.lastError().text()));
                QSqlDatabase::removeDatabase(connectionName);
                return result;
            }
        }  else {
            irisLog(QString("The database connection is invalid! %1").arg(importConnection.lastError().text()));
            QSqlDatabase::removeDatabase(connectionName);
            return result;
        }

        QSqlQuery selectAssetQuery(importConnection);
        selectAssetQuery.prepare(QString("SELECT  version FROM %1 LIMIT 1").arg(table_name));
        if (selectAssetQuery.exec() && selectAssetQuery.first()) {
            QString version = selectAssetQuery.value(0).toString();
            bool version_ok(false);
            QString import_version = version.mid(0, 3);
            int version_int = import_version.toFloat(&version_ok) * 10;

            if (version_ok) {
                if (version_int >= Constants::MIN_JAF_VERSION) {
                    result = true;
                }
            }
        }

        importConnection.close();
    }
    QSqlDatabase::removeDatabase(connectionName);
    return result;
}

