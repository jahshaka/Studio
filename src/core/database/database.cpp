#include "database.h"
#include "../../constants.h"
#include "../../irisgl/src/irisglfwd.h"
#include "../../irisgl/src/core/irisutils.h"
#include "../../core/project.h"
#include "../../globals.h"
#include "../guidmanager.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlRecord>

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

void Database::insertSceneGlobal(const QString &projectName, const QByteArray &sceneBlob)
{
    QSqlQuery query;
    query.prepare("INSERT INTO " + Constants::DB_PROJECTS_TABLE + " (name, scene, version, guid)"
                  "VALUES (:name, :scene, :version, :hash)");
    query.bindValue(":name",    projectName);
    query.bindValue(":scene",   sceneBlob);
    query.bindValue(":version", Constants::CONTENT_VERSION);
    query.bindValue(":guid",    GUIDManager::generateGUID());

    executeAndCheckQuery(query, "insertSceneGlobal");
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

QVector<QStringList> Database::fetchProjects()
{
    QSqlQuery query;
    query.prepare("SELECT name, guid FROM " + Constants::DB_PROJECTS_TABLE);
    executeAndCheckQuery(query, "fetchProjects");

    QVector<QStringList> list;
    while (query.next()) {
        QSqlRecord record = query.record();
        QStringList row;
        for (int i = 0; i < record.count(); i++) {
            row << record.value(i).toString();
        }
        list.append(row);
    }

    return list;
}

QByteArray Database::getSceneBlobGlobal() const
{
    QSqlQuery query;
    query.prepare("SELECT scene FROM " + Constants::DB_PROJECTS_TABLE + " WHERE name = ?");
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

void Database::updateSceneGlobal(const QByteArray &sceneBlob)
{
    QSqlQuery query;
    query.prepare("UPDATE " + Constants::DB_PROJECTS_TABLE + " SET scene = :blob WHERE name = :name");
    query.bindValue(":blob",    sceneBlob);
    query.bindValue(":name",    Globals::project->getProjectName());

    executeAndCheckQuery(query, "updateSceneGlobal");
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
