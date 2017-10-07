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

void Database::executeAndCheckQuery(QSqlQuery &query, const QString& name)
{
    if (!query.exec()) {
        irisLog(name + " + Query failed to execute: " + query.lastError().text());
    }
}

void Database::fetchRecord(const QString &name)
{
//    QSqlQuery query;
//    query.prepare("select * from test_table");

//    if (query.exec()) {
//        int idName = query.record().indexOf("name");

//        while (query.next()) {
//           QString name = query.value(idName).toString();
//           qDebug() << name;
//        }

//        return true;
//    }

//    return false;
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

// sbm

// offer to change the location in the future maybe?
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

void Database::deleteProject()
{
    QSqlQuery query;
    query.prepare("DELETE FROM " + Constants::DB_PROJECTS_TABLE + " WHERE guid = ?");
    query.addBindValue(Globals::project->getProjectGuid());
    executeAndCheckQuery(query, "deleteProject");
}

void Database::insertSceneGlobal(const QString &projectName, const QByteArray &sceneBlob)
{
    QSqlQuery query;
    auto guid = GUIDManager::generateGUID();
    query.prepare("INSERT INTO " + Constants::DB_PROJECTS_TABLE + " (name, scene, version, guid)"
                  "VALUES (:name, :scene, :version, :guid)");
    query.bindValue(":name",    projectName);
    query.bindValue(":scene",   sceneBlob);
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
                  "VALUES (:world_guid, :name, :thumbnail, :guid)");
    query.bindValue(":world_guid",  world_guid);
    query.bindValue(":thumbnail",   thumbnail);
    query.bindValue(":name",        name);
    query.bindValue(":guid",        GUIDManager::generateGUID());

    executeAndCheckQuery(query, "insertThumbnailGlobal");
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
        qDebug() << "hasCachedThumbnail failed! " + query.lastError().text();
    }

    return false;
}

QVector<ProjectTileData> Database::fetchProjects()
{
    QSqlQuery query;
    query.prepare("SELECT name, thumbnail, guid FROM " + Constants::DB_PROJECTS_TABLE);
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
        irisLog("There was an error getting the blob! " + query.lastError().text());
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
    query.prepare("UPDATE " + Constants::DB_PROJECTS_TABLE + " SET scene = ?, thumbnail = ? WHERE guid = ?");
    query.addBindValue(sceneBlob);
    query.addBindValue(thumbnail);
    query.addBindValue(Globals::project->getProjectGuid());

    executeAndCheckQuery(query, "updateSceneGlobal");
}

void Database::createExportScene(const QString &outTempFilePath)
{
    QSqlQuery query;
    query.prepare("SELECT name, scene, thumbnail, guid FROM " + Constants::DB_PROJECTS_TABLE + " WHERE guid = ?");
    query.addBindValue(Globals::project->getProjectGuid());

    if (query.exec()) {
        query.next();
    } else {
        qDebug() << "createExportScene failed " + query.lastError().text();
        irisLog(
            "There was an error fetching a row to be exported " + query.lastError().text()
        );
    }

    auto sceneName  = query.value(0).toString();
    auto sceneBlob  = query.value(1).toByteArray();
    auto sceneThumb = query.value(2).toByteArray();
    auto sceneGuid  = query.value(3).toString();

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
    query3.prepare("INSERT INTO " + Constants::DB_PROJECTS_TABLE + " (name, scene, thumbnail, guid)"
                  "VALUES (:name, :scene, :thumbnail, :guid)");
    query3.bindValue(":name",       sceneName);
    query3.bindValue(":scene",      sceneBlob);
    query3.bindValue(":thumbnail",  sceneThumb);
    query3.bindValue(":guid",       sceneGuid);

    executeAndCheckQuery(query3, "insertSceneGlobal");

    dbe.close();
}

bool Database::importProject(const QString &inFilePath)
{
    QSqlDatabase dbe = QSqlDatabase::addDatabase(Constants::DB_DRIVER, "myUniqueSQLITEImportConnection");
    dbe.setDatabaseName(inFilePath + ".db");
    dbe.open();

    QSqlQuery query(dbe);
    query.prepare("SELECT name, scene, thumbnail, guid FROM " + Constants::DB_PROJECTS_TABLE);

    if (query.exec()) {
        query.next();
    } else {
        qDebug() << "importProject failed " + query.lastError().text();
        irisLog(
            "There was an error fetching a record to be imported " + query.lastError().text()
        );
    }

    auto sceneName  = query.value(0).toString();
    auto sceneBlob  = query.value(1).toByteArray();
    auto sceneThumb = query.value(2).toByteArray();
    auto sceneGuid  = query.value(3).toString();

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
        qDebug() << "hasExistingProject failed! " + query2.lastError().text();
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
        query3.prepare("INSERT INTO " + Constants::DB_PROJECTS_TABLE + " (name, scene, thumbnail, guid)"
                      "VALUES (:name, :scene, :thumbnail, :guid)");
        query3.bindValue(":name",       sceneName);
        query3.bindValue(":scene",      sceneBlob);
        query3.bindValue(":thumbnail",  sceneThumb);
        query3.bindValue(":guid",       sceneGuid);

        executeAndCheckQuery(query3, "insertSceneGlobal");

        Globals::project->setProjectGuid(sceneGuid);
        return true;
    }

    return false;
}

// esbmv

void Database::createProject(QString projectName)
{
    QString schema = "CREATE TABLE IF NOT EXISTS " + Constants::DB_ROOT_TABLE + " ("
                     "    name    VARCHAR(32),"
                     "    scene   BLOB,"
                     "    version VARCHAR(8),"
                     "    hash    VARCHAR(32) PRIMARY KEY"
                     ")";

    QSqlQuery query;
    query.prepare(schema);
    executeAndCheckQuery(query, "createProject");
}

void Database::insertScene(const QString &projectName, const QByteArray &sceneBlob)
{
    QSqlQuery query;
    query.prepare("INSERT INTO " + Constants::DB_ROOT_TABLE + " (name, scene, version, hash)"
                  "VALUES (:name, :scene, :version, :hash)");
    query.bindValue(":name",    projectName);
    query.bindValue(":scene",   sceneBlob);
    query.bindValue(":version", Constants::CONTENT_VERSION);
    query.bindValue(":hash",    GUIDManager::generateGUID());

    executeAndCheckQuery(query, "insertScene");
}

void Database::updateScene(const QByteArray &sceneBlob)
{
    QSqlQuery query;
    query.prepare("UPDATE " + Constants::DB_ROOT_TABLE + " SET scene = :blob WHERE name = :name");
    query.bindValue(":blob",    sceneBlob);
    query.bindValue(":name",    Globals::project->getProjectName());

    executeAndCheckQuery(query, "updateScene");
}

QByteArray Database::getSceneBlob() const
{
    QSqlQuery query;
    query.prepare("SELECT scene FROM " + Constants::DB_ROOT_TABLE + " WHERE name = ?");
    query.addBindValue(Globals::project->getProjectName());

    if (query.exec()) {
        if (query.first()) {
            return query.value(0).toByteArray();
        }
    } else {
        irisLog("There was an error getting the blob! " + query.lastError().text());
    }

    return QByteArray();
}
