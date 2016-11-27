#ifndef SCENEWRITER_H
#define SCENEWRITER_H

#include <QSharedPointer>
#include "sceneiobase.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>

#include "../jah3d/core/scene.h"
#include "../jah3d/core/scenenode.h"
#include "../jah3d/scenegraph/meshnode.h"
#include "../jah3d/scenegraph/lightnode.h"
#include "../jah3d/materials/defaultmaterial.h"

/*
namespace jah3d
{
    class Scene;
}
*/

class SceneWriter:public SceneIOBase
{
public:
    void writeScene(QString filePath,QSharedPointer<jah3d::Scene> scene)
    {
        dir = SceneIOBase::getDirFromFileName(filePath);
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
    void writeScene(QJsonObject& projectObj,QSharedPointer<jah3d::Scene> scene)
    {
        QJsonObject rootNodeObj;
        writeSceneNode(rootNodeObj,scene->getRootNode());

        QJsonObject sceneObj;
        sceneObj["rootNode"] = rootNodeObj;

        projectObj["scene"] = sceneObj;

        //todo: add editor specific data
    }

    void writeSceneNode(QJsonObject& sceneNodeObj,QSharedPointer<jah3d::SceneNode> sceneNode)
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
            case jah3d::SceneNodeType::Mesh:
                writeMeshData(sceneNodeObj,sceneNode.staticCast<jah3d::MeshNode>());
            break;

            case jah3d::SceneNodeType::Light:
                writeLightData(sceneNodeObj,sceneNode.staticCast<jah3d::LightNode>());
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

    void writeMeshData(QJsonObject& sceneNodeObject,QSharedPointer<jah3d::MeshNode> meshNode)
    {
        //todo: handle generated meshes properly
        sceneNodeObject["mesh"] = meshNode->meshPath;

        //todo: check if material actually exists
        auto mat = meshNode->getMaterial().staticCast<jah3d::DefaultMaterial>();
        QJsonObject matObj;
        writeSceneNodeMaterial(matObj,mat);
        sceneNodeObject["material"] = matObj;

    }

    void writeSceneNodeMaterial(QJsonObject& matObj,QSharedPointer<jah3d::DefaultMaterial> mat)
    {
        matObj["ambientColor"] = jsonColor(mat->getDiffuseColor());

        matObj["diffuseColor"] = jsonColor(mat->getDiffuseColor());
        matObj["diffuseTexture"] = getRelativePath(mat->getDiffuseTextureSource());

        matObj["specularColor"] = jsonColor(mat->getSpecularColor());
        matObj["specularTexture"] = getRelativePath(mat->getSpecularTextureSource());
        matObj["shininess"] = mat->getShininess();

        matObj["normalTexture"] = getRelativePath(mat->getNormalTextureSource());
        matObj["normalIntensity"] = mat->getNormalIntensity();

        matObj["reflectionTexture"] = getRelativePath(mat->getReflectionTextureSource());
        matObj["reflectionInfluence"] = mat->getReflectionInfluence();
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

    void writeLightData(QJsonObject& sceneNodeObject,QSharedPointer<jah3d::LightNode> lightNode)
    {
        sceneNodeObject["lightType"] = getLightNodeTypeName(lightNode->lightType);
        sceneNodeObject["intensity"] = lightNode->intensity;
        sceneNodeObject["radius"] = lightNode->radius;
        sceneNodeObject["spotCutOff"] = lightNode->spotCutOff;
    }

    QString getSceneNodeTypeName(jah3d::SceneNodeType nodeType)
    {
        switch(nodeType)
        {
        case jah3d::SceneNodeType::Empty:
            return "empty";
        case jah3d::SceneNodeType::Light:
            return "light";
        case jah3d::SceneNodeType::Mesh:
            return "mesh";
        default:
            return "empty";
        }
    }

    QString getLightNodeTypeName(jah3d::LightType lightType)
    {
        switch(lightType)
        {
        case jah3d::LightType::Point:
            return "point";
        case jah3d::LightType::Directional:
            return "directional";
        case jah3d::LightType::Spot:
            return "spot";
        }
    }
};

#endif // SCENEWRITER_H
