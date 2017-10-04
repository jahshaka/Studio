#ifndef DATABASE_H
#define DATABASE_H

#include <QSqlDatabase>
#include <QSqlDriver>
#include <QSqlError>
#include <QSqlQuery>
#include <QCryptographicHash>

class Database
{
public:
    Database();
    ~Database();

    void executeAndCheckQuery(QSqlQuery&);
    void fetchRecord(const QString &name);
    void initializeDatabase(QString name);
    void closeDb();

    void createGlobalDb();
    void insertSceneGlobal(const QString &projectName, const QByteArray &sceneBlob);
    QVector<QStringList> fetchProjects();
    QByteArray getSceneBlobGlobal() const;
    void updateSceneGlobal(const QByteArray &sceneBlob);

    void createProject(QString projectName);
    void insertScene(const QString &projectName, const QByteArray &sceneBlob);
    void updateScene(const QByteArray &sceneBlob);
    QByteArray getSceneBlob() const;

    QSqlDatabase getDb() { return db; }

private:
    QSqlDatabase db;
};

#endif // DATABASE_H
