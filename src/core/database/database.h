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

    void createProject(QString projectName);
    void insertScene(const QString &projectName, const QByteArray &sceneBlob);
    void updateScene(const QByteArray &sceneBlob);
    QByteArray getSceneBlob() const;

private:
    QSqlDatabase db;
};

#endif // DATABASE_H
