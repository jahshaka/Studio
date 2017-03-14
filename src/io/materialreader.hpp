#ifndef MATERIALREADER_HPP
#define MATERIALREADER_HPP

#include <QSharedPointer>
#include "assetiobase.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QJsonValueRef>
#include <QJsonDocument>

#include "../irisgl/src/irisglfwd.h"

class MaterialReader : public AssetIOBase
{
public:
    MaterialReader();

    QJsonObject parsedShader;
    bool readJahShader(const QString &filePath);
    void parseJahShader(const QJsonObject&);
    QJsonObject getParsedShader() {
        return parsedShader;
    }

    // return texture
};

#endif // MATERIALREADER_HPP
