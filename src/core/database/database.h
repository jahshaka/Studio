#ifndef DATABASE_H
#define DATABASE_H

#include <QSqlDatabase>
#include <QSqlDriver>
#include <QSqlError>
#include <QSqlQuery>

class Database
{
public:
    Database();

    bool fetchRecord(const QString &name);

    void updateScene(const QByteArray &sceneBlob);
    QByteArray getSceneBlob() const;

private:
    QSqlDatabase db;
};

#endif // DATABASE_H
