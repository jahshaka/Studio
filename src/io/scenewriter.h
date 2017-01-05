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
#include "../irisgl/src/animation/animation.h"
#include "../irisgl/src/animation/keyframeanimation.h"
#include "../irisgl/src/animation/keyframeset.h"
/*
namespace iris
{
    class Scene;
}
*/

class SceneWriter:public AssetIOBase
{
public:
    void writeScene(QString filePath,iris::ScenePtr scene)
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
    void writeScene(QJsonObject& projectObj,iris::ScenePtr scene)
    {
        QJsonObject sceneObj;

        //scene properties
        if(!!scene->skyTexture)
            sceneObj["skyTexture"] = this->getRelativePath(scene->skyTexture->getSource());
        else
            sceneObj["skyTexture"] = "";

        sceneObj["skyColor"] = jsonColor(scene->skyColor);
        sceneObj["ambientColor"] = jsonColor(scene->ambientColor);

        sceneObj["fogColor"] = jsonColor(scene->fogColor);
        sceneObj["fogStart"] = scene->fogStart;
        sceneObj["fogEnd"] = scene->fogEnd;
        sceneObj["fogEnabled"] = scene->fogEnabled;


        QJsonObject rootNodeObj;
        writeSceneNode(rootNodeObj,scene->getRootNode());
        sceneObj["rootNode"] = rootNodeObj;

        projectObj["scene"] = sceneObj;

        //todo: add editor specific data
    }

    void writeSceneNode(QJsonObject& sceneNodeObj,iris::SceneNodePtr sceneNode)
    {
        sceneNodeObj["name"] = sceneNode->getName();
        sceneNodeObj["type"] = getSceneNodeTypeName(sceneNode->sceneNodeType);

        sceneNodeObj["pos"] = jsonVector3(sceneNode->pos);
        auto rot = sceneNode->rot.toEulerAngles();
        sceneNodeObj["rot"] = jsonVector3(rot);
        sceneNodeObj["scale"] = jsonVector3(sceneNode->scale);


        //todo: write data specific to node type
        switch (sceneNode->sceneNodeType) {
            case iris::SceneNodeType::Mesh:
                writeMeshData(sceneNodeObj,sceneNode.staticCast<iris::MeshNode>());
            break;
            case iris::SceneNodeType::Light:
                writeLightData(sceneNodeObj,sceneNode.staticCast<iris::LightNode>());
            break;
            default: break;
        }

        writeAnimationData(sceneNodeObj,sceneNode);

        QJsonArray childrenArray;
        for (auto childNode : sceneNode->children) {
            QJsonObject childNodeObj;
            writeSceneNode(childNodeObj, childNode);
            childrenArray.append(childNodeObj);
        }

        sceneNodeObj["children"] = childrenArray;
    }

    void writeAnimationData(QJsonObject& sceneNodeObj,iris::SceneNodePtr sceneNode)
    {
        auto anim = sceneNode->animation;
        if(!anim)
            return;

        QJsonObject animObj;
        animObj["name"] = anim->name;
        animObj["length"] = anim->length;
        animObj["loop"] = anim->loop;

        QJsonArray frames;
        for(auto frameName:anim->keyFrameSet->keyFrames.keys())
        {
            auto frame = anim->keyFrameSet->keyFrames[frameName];

            QJsonObject frameObj;
            QJsonArray keys;
            for(auto key:frame->keys)
            {
                QJsonObject keyObj;
                keyObj["time"] = key->time;
                keyObj["value"] = key->value;

                keys.append(keyObj);
            }

            frameObj["keys"] = keys;
            frameObj["name"] = frameName;
            frames.append(frameObj);
        }

        animObj["frames"] = frames;

        sceneNodeObj["animation"] = animObj;
    }

    void writeMeshData(QJsonObject& sceneNodeObject,iris::MeshNodePtr meshNode)
    {
        //todo: handle generated meshes properly
        sceneNodeObject["mesh"] = getRelativePath(meshNode->meshPath);
        sceneNodeObject["meshIndex"] = meshNode->meshIndex;

        //todo: check if material actually exists
        auto mat = meshNode->getMaterial().staticCast<iris::DefaultMaterial>();
        QJsonObject matObj;
        writeSceneNodeMaterial(matObj,mat);
        sceneNodeObject["material"] = matObj;
    }

    void writeSceneNodeMaterial(QJsonObject& matObj,iris::DefaultMaterialPtr mat)
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

    void writeLightData(QJsonObject& sceneNodeObject,iris::LightNodePtr lightNode)
    {
        sceneNodeObject["lightType"] = getLightNodeTypeName(lightNode->lightType);
        sceneNodeObject["intensity"] = lightNode->intensity;
        sceneNodeObject["distance"] = lightNode->distance;
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
        switch (lightType) {
            case iris::LightType::Point:
                return "point";
            case iris::LightType::Directional:
                return "directional";
            case iris::LightType::Spot:
                return "spot";
            default:
                return "none";
        }
    }
};

#endif // SCENEWRITER_H
