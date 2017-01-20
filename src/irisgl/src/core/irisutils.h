#ifndef IRISUTILS_H
#define IRISUTILS_H

#include <QDir>
#include <QCoreApplication>

class IrisUtils
{
public:
    static QString getAbsoluteAssetPath(QString relToApp)
    {
    #ifdef WIN32
        relToApp = QStringLiteral("..")+QDir::separator()+relToApp;
    #endif

        auto path = QDir::cleanPath(QCoreApplication::applicationDirPath() + QDir::separator() + relToApp);
        return path;
    }
};

#endif // IRISUTILS_H
