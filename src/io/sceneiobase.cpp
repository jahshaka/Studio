#include <Qt>
#include <qvector.h>

#include <QQuaternion>
#include <QVector3D>
#include <QDir>
#include <QFile>

#include "sceneiobase.h"

QDir SceneIOBase::getDirFromFileName(QString filename)
{
    QFileInfo info(filename);
    return info.absoluteDir();
}

QString SceneIOBase::getRelativePath(QString filename)
{
    //if it's a resource then return it
    if(filename.trimmed().startsWith(":") || filename.trimmed().startsWith("qrc:"))
        return filename;

    return dir.relativeFilePath(filename);
}

QString SceneIOBase::getAbsolutePath(QString filename)
{
    //if it's a resource then return it
    if(filename.trimmed().startsWith(":") || filename.trimmed().startsWith("qrc:"))
        return filename;

    auto absPath = dir.absoluteFilePath(filename);

    //file should exist, else return null string
    if(!QFile(absPath).exists())
        return QString::null;

    //everything went ok :)
    return absPath;
}
