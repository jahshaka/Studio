#include "database.h"
#include "../../constants.h"
#include "../../irisgl/src/core/irisutils.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlRecord>

Database::Database()
{
    if (!QSqlDatabase::isDriverAvailable(Constants::DB_DRIVER)) {
        qErrnoWarning("DB driver not present!");
    }

    db = QSqlDatabase::addDatabase(Constants::DB_DRIVER);
    db.setDatabaseName(IrisUtils::getAbsoluteAssetPath(Constants::DB_PATH));

    if (!db.open()) {
        qDebug() << "ERROR: " << db.lastError();
    }

    fetchRecord("");


    // test
    QJsonObject projectObj;
    QJsonObject activeObj;
    activeObj["path"] = "Scenes/Path";
    activeObj["thumbnail"] = "thumb.jpg";
    activeObj["version"] = 0.3;
    projectObj["activeProject"] = activeObj;
    QJsonDocument projectSln(projectObj);

//    qDebug() << projectSln.toBinaryData()
    QSqlQuery query;
    query.prepare("insert into test_table (name, scene) values ('dragons', :sceneBlob)");
    query.bindValue(":sceneBlob", projectSln.toBinaryData());
    if( !query.exec() ) {
            qDebug() << "Error inserting image into table:\n" << query.lastError();
    } else {
        qDebug() << "got it in";
    }
}

bool Database::fetchRecord(const QString &name)
{
    QSqlQuery query;
    query.prepare("select * from test_table");

    if (query.exec()) {
        int idName = query.record().indexOf("name");

        while (query.next()) {
           QString name = query.value(idName).toString();
           qDebug() << name;
        }

        return true;
    }

    return false;
}
