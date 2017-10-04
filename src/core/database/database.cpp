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
    // qDebug() << QSqlDatabase::drivers();
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

void Database::executeAndCheckQuery(QSqlQuery &query)
{
    if (!query.exec()) {
        irisLog("Query failed to execute: " + query.lastError().text());
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
                     "    name              VARCHAR(128),"
                     "    thumbnail         BLOB,"
                     "    last_accessed     DATETIME,"
                     "    last_written      DATETIME,"
                     "    date_created      DATETIME DEFAULT CURRENT_TIMESTAMP,"
                     "    scene             BLOB,"
                     "    version           VARCHAR(8),"
                     "    description       TEXT,"
                     "    url               TEXT,"
                     "    hash              VARCHAR(32) PRIMARY KEY"
                     ")";

    QSqlQuery query;
    query.prepare(schema);
    executeAndCheckQuery(query);
}

void Database::insertSceneGlobal(const QString &projectName, const QByteArray &sceneBlob)
{
    QSqlQuery query;
    query.prepare("INSERT INTO " + Constants::DB_PROJECTS_TABLE + " (name, scene, version, hash)"
                  "VALUES (:name, :scene, :version, :hash)");
    query.bindValue(":name",    projectName);
    query.bindValue(":scene",   sceneBlob);
    query.bindValue(":version", Constants::CONTENT_VERSION);
    query.bindValue(":hash",    GUIDManager::generateGUID());

    executeAndCheckQuery(query);
}

QVector<QStringList> Database::fetchProjects()
{
    QSqlQuery query;
    query.prepare("SELECT name, hash FROM " + Constants::DB_PROJECTS_TABLE);
    executeAndCheckQuery(query);

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

void Database::updateSceneGlobal(const QByteArray &sceneBlob)
{
    QSqlQuery query;
    query.prepare("UPDATE " + Constants::DB_PROJECTS_TABLE + " SET scene = :blob WHERE name = :name");
    query.bindValue(":blob",    sceneBlob);
    query.bindValue(":name",    Globals::project->getProjectName());

    executeAndCheckQuery(query);
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
    executeAndCheckQuery(query);
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

    executeAndCheckQuery(query);
}

void Database::updateScene(const QByteArray &sceneBlob)
{
    QSqlQuery query;
    query.prepare("UPDATE " + Constants::DB_ROOT_TABLE + " SET scene = :blob WHERE name = :name");
    query.bindValue(":blob",    sceneBlob);
    query.bindValue(":name",    Globals::project->getProjectName());

    executeAndCheckQuery(query);
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
