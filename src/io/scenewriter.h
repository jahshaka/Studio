#ifndef SCENEWRITER_H
#define SCENEWRITER_H

#include <QSharedPointer>
#include "assetiobase.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>

#include "../irisgl/src/core/scene.h"
#include "../irisgl/src/core/scenenode.h"
#include "../irisgl/src/scenegraph/meshnode.h"
#include "../irisgl/src/scenegraph/lightnode.h"
#include "../irisgl/src/materials/defaultmaterial.h"

/*
namespace iris
{
    class Scene;
}
*/

class SceneWriter:public AssetIOBase
{
public:
    void writeScene(QString filePath,QSharedPointer<iris::Scene> scene)
    {
        dir = AssetIOBase::getDirFromFileName(filePath);
        QFile file(filePath);
        file.open(QIODevice::WriteOnly|QIODevice::Truncate);

        QJsonObject projectObj;
        projectObj["version"]="0.1";
        writeScene(projectObj,scene);

        QJsonDocument saveDoc(projectObj);
        file.write(saveDoc.toJson());
        file.close();
    }

private:
    void writeScene(QJsonObject& projectObj,QSharedPointer<iris::Scene> scene)
    {
        QJsonObject rootNodeObj;
        writeSceneNode(rootNodeObj,scene->getRootNode());

        QJsonObject sceneObj;
        sceneObj["rootNode"] = rootNodeObj;

        projectObj["scene"] = sceneObj;

        //todo: add editor specific data
    }

    void writeSceneNode(QJsonObject& sceneNodeObj,QSharedPointer<iris::SceneNode> sceneNode)
    {
        sceneNodeObj["name"] = sceneNode->getName();
        sceneNodeObj["type"] = getSceneNodeTypeName(sceneNode->sceneNodeType);

        /*
        QJsonObject pos;
        pos["x"] = sceneNode->pos.x();
        pos["y"] = sceneNode->pos.y();
        pos["z"] = sceneNode->pos.z();
        sceneNodeObj["pos"] = pos;

        QJsonObject rot;
        auto r = sceneNode->rot.toEulerAngles();
        pos["x"] = r.x();
        pos["y"] = r.y();
        pos["z"] = r.z();
        sceneNodeObj["rot"] = rot;

        QJsonObject scale;
        scale["x"] = sceneNode->scale.x();
        scale["y"] = sceneNode->scale.y();
        scale["z"] = sceneNode->scale.z();
        sceneNodeObj["scale"] = scale;
        */

        sceneNodeObj["pos"] = jsonVector3(sceneNode->pos);
        auto rot = sceneNode->rot.toEulerAngles();
        sceneNodeObj["rot"] = jsonVector3(rot);
        sceneNodeObj["scale"] = jsonVector3(sceneNode->scale);


        //todo: write data specific to node type
        switch(sceneNode->sceneNodeType)
        {
            case iris::SceneNodeType::Mesh:
                writeMeshData(sceneNodeObj,sceneNode.staticCast<iris::MeshNode>());
            break;

            case iris::SceneNodeType::Light:
                writeLightData(sceneNodeObj,sceneNode.staticCast<iris::LightNode>());
            break;
        }

        QJsonArray childrenArray;
        for(auto childNode:sceneNode->children)
        {
            QJsonObject childNodeObj;
            writeSceneNode(childNodeObj,childNode);

            childrenArray.append(childNodeObj);
        }
        sceneNodeObj["children"] = childrenArray;
    }

    void writeMeshData(QJsonObject& sceneNodeObject,QSharedPointer<iris::MeshNode> meshNode)
    {
        //todo: handle generated meshes properly
        sceneNodeObject["mesh"] = meshNode->meshPath;
        sceneNodeObject["meshIndex"] = meshNode->meshIndex;

        //todo: check if material actually exists
        auto mat = meshNode->getMaterial().staticCast<iris::DefaultMaterial>();
        QJsonObject matObj;
        writeSceneNodeMaterial(matObj,mat);
        sceneNodeObject["material"] = matObj;
    }

    void writeSceneNodeMaterial(QJsonObject& matObj,QSharedPointer<iris::DefaultMaterial> mat)
    {
        matObj["ambientColor"] = jsonColor(mat->getAmbientColor());

        matObj["diffuseColor"] = jsonColor(mat->getDiffuseColor());
        matObj["diffuseTexture"] = getRelativePath(mat->getDiffuseTextureSource());

        matObj["specularColor"] = jsonColor(mat->getSpecularColor());
        matObj["specularTexture"] = getRelativePath(mat->getSpecularTextureSource());
        matObj["shininess"] = mat->getShininess();

        matObj["normalTexture"] = getRelativePath(mat->getNormalTextureSource());
        matObj["normalIntensity"] = mat->getNormalIntensity();

        matObj["reflectionTexture"] = getRelativePath(mat->getReflectionTextureSource());
        matObj["reflectionInfluence"] = mat->getReflectionInfluence();

        matObj["textureScale"] = mat->getTextureScale();
    }

    QJsonObject jsonColor(QColor color)
    {
        QJsonObject colObj;
        colObj["r"] = color.red();
        colObj["g"] = color.green();
        colObj["b"] = color.blue();
        colObj["a"] = color.alpha();

        return colObj;
    }

    QJsonObject jsonVector3(QVector3D vec)
    {
        QJsonObject obj;
        obj["x"] = vec.x();
        obj["y"] = vec.y();
        obj["z"] = vec.z();

        return obj;
    }

    void writeLightData(QJsonObject& sceneNodeObject,QSharedPointer<iris::LightNode> lightNode)
    {
        sceneNodeObject["lightType"] = getLightNodeTypeName(lightNode->lightType);
        sceneNodeObject["intensity"] = lightNode->intensity;
        sceneNodeObject["radius"] = lightNode->radius;
        sceneNodeObject["spotCutOff"] = lightNode->spotCutOff;
    }

    QString getSceneNodeTypeName(iris::SceneNodeType nodeType)
    {
        switch(nodeType)
        {
        case iris::SceneNodeType::Empty:
            return "empty";
        case iris::SceneNodeType::Light:
            return "light";
        case iris::SceneNodeType::Mesh:
            return "mesh";
        default:
            return "empty";
        }
    }

    QString getLightNodeTypeName(iris::LightType lightType)
    {
        switch(lightType)
        {
        case iris::LightType::Point:
            return "point";
        case iris::LightType::Directional:
            return "directional";
        case iris::LightType::Spot:
            return "spot";
        }
    }
};

#endif // SCENEWRITER_H
