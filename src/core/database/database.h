#ifndef DATABASE_H
#define DATABASE_H

#include <QSqlDatabase>
#include <QSqlDriver>
#include <QSqlError>
#include <QSqlQuery>
#include <QCryptographicHash>

#include "../../core/project.h"

class Database
{
public:
    Database();
    ~Database();

    void executeAndCheckQuery(QSqlQuery&, const QString&);
    void fetchRecord(const QString &name);
    void initializeDatabase(QString name);
    void closeDb();

    void createGlobalDb();
    void createGlobalDbThumbs();
    void deleteProject();
    void insertSceneGlobal(const QString &world_guid, const QByteArray &sceneBlob);
    void insertThumbnailGlobal(const QString &world_guid,
                               const QString &name,
                               const QByteArray &thumbnail);
    bool hasCachedThumbnail(const QString& name);
    QVector<ProjectTileData> fetchProjects();
    QByteArray getSceneBlobGlobal() const;
    QByteArray fetchCachedThumbnail(const QString& name) const;
    void updateSceneGlobal(const QByteArray &sceneBlob, const QByteArray &thumbnail);
    void createExportScene(const QString& outTempFilePath);
    QString importProject(const QString& inFilePath);

    void createProject(QString projectName);
    void insertScene(const QString &projectName, const QByteArray &sceneBlob);
    void updateScene(const QByteArray &sceneBlob);
    QByteArray getSceneBlob() const;

    QSqlDatabase getDb() { return db; }

private:
    QSqlDatabase db;
};

#endif // DATABASE_H
