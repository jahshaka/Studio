#include <Qt>
#include <qvector.h>

#include <QQuaternion>
#include <QVector3D>
#include <QDir>
#include <QFile>

#include "scenewriter.h"
#include "assetiobase.h"

/*
void SceneWriter::writeScene(QString tilePath,QSharedPointer<iris::Scene> scene)
{
    dir = SceneIOBase::getDirFromFileName(fileName);
    QFile file(fileName);
    file.open(QIODevice::ReadWrite|QIODevice::Truncate);

    writeScene(xml,scene);
}
*/
