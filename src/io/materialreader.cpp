#include "materialreader.hpp"
#include <QDebug>

MaterialReader::MaterialReader()
{

}

bool MaterialReader::readJahShader(const QString &filePath)
{
    dir = AssetIOBase::getDirFromFileName(filePath);
    QFile file(filePath);
    file.open(QIODevice::ReadOnly);

    auto data = file.readAll();
    auto doc = QJsonDocument::fromJson(data);

    parsedShader = doc.object();
//    parseJahShader(doc.object());
}

void MaterialReader::parseJahShader(const QJsonObject &jahShader)
{
    // @TODO: add some early check here to escape early

    auto shaderName = jahShader["name"].toString();
    auto uniforms = jahShader["uniforms"].toObject();

//    QJsonArray children = rootNode["children"].toArray();

    for (auto childObj : uniforms) {
        if (childObj.toObject()["type"] == "slider") {

        }
    }

    qDebug() << shaderName;
}
