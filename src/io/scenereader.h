#ifndef SCENEREADER_H
#define SCENEREADER_H

#include <QSharedPointer>
#include "assetiobase.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QJsonValueRef>
#include <QJsonDocument>

#include "../irisgl/src/core/scene.h"
#include "../irisgl/src/core/scenenode.h"
#include "../irisgl/src/scenegraph/meshnode.h"
#include "../irisgl/src/scenegraph/lightnode.h"
#include "../irisgl/src/materials/defaultmaterial.h"
#include "../irisgl/src/graphics/texture2d.h"
#include "../irisgl/src/graphics/graphicshelper.h"
#include "../irisgl/src/animation/animation.h"
#include "../irisgl/src/animation/keyframeanimation.h"
#include "../irisgl/src/animation/keyframeset.h"

class SceneReader:public AssetIOBase
{
    QHash<QString,QList<iris::Mesh*>> meshes;
public:
    QSharedPointer<iris::Scene> readScene(QString filePath)
    {
        dir = AssetIOBase::getDirFromFileName(filePath);
        QFile file(filePath);
        file.open(QIODevice::ReadOnly);

        auto data = file.readAll();
        auto doc = QJsonDocument::fromJson(data);

        auto projectObj = doc.object();
        auto scene = readScene(projectObj);

        return scene;
    }

    QSharedPointer<iris::Scene> readScene(QJsonObject& projectObj)
    {
        auto scene = iris::Scene::create();

        //scene already contains root node, so just add children
        auto sceneObj = projectObj["scene"].toObject();
        auto rootNode = sceneObj["rootNode"].toObject();
        QJsonArray children = rootNode["children"].toArray();

        for(auto childObj:children)
        {
            auto sceneNodeObj = childObj.toObject();
            auto childNode = readSceneNode(sceneNodeObj);
            scene->getRootNode()->addChild(childNode);
        }

        return scene;
    }

    /**
     * Creates scene node from json data
     * @param nodeObj
     * @return
     */
    QSharedPointer<iris::SceneNode> readSceneNode(QJsonObject& nodeObj)
    {
        QSharedPointer<iris::SceneNode> sceneNode;

        QString nodeType = nodeObj["type"].toString("empty");
        if(nodeType=="mesh")
            sceneNode = createMesh(nodeObj).staticCast<iris::SceneNode>();
        else if(nodeType=="light")
            sceneNode = createLight(nodeObj).staticCast<iris::SceneNode>();
        else
            sceneNode = iris::SceneNode::create();

        //read transform
        readSceneNodeTransform(nodeObj,sceneNode);

        readAnimationData(nodeObj,sceneNode);

        //read name
        sceneNode->name = nodeObj["name"].toString("");

        QJsonArray children = nodeObj["children"].toArray();
        for(auto childObj:children)
        {
            auto sceneNodeObj = childObj.toObject();
            auto childNode = readSceneNode(sceneNodeObj);
            sceneNode->addChild(childNode);
        }

        return sceneNode;
    }


    void readAnimationData(QJsonObject& nodeObj,QSharedPointer<iris::SceneNode> sceneNode)
    {
        auto animObj = nodeObj["animation"].toObject();
        if(animObj.isEmpty())
            return;

        auto framesArray = animObj["frames"].toArray();
        if(framesArray.isEmpty())
            return;

        auto animation = iris::Animation::create();

        for(auto frameValue:framesArray)
        {
            auto frameObj = frameValue.toObject();

            auto frame = new iris::FloatKeyFrame();
            frame->name = frameObj["name"].toString("Frame");


            auto keysObj = frameObj["keys"].toArray();
            for(auto keyValue:keysObj)
            {
                auto keyObj = keyValue.toObject();
                float time = keyObj["time"].toDouble(0);
                float value = keyObj["value"].toDouble(0);
                frame->addKey(value,time);
            }
            animation->keyFrameSet->keyFrames.insert(frame->name,frame);
            animation->length = animObj["length"].toDouble(1);
            animation->loop = animObj["loop"].toBool(false);
        }

        sceneNode->animation = animation;
    }

    /**
     * Reads pos, rot and scale properties from json object
     * if scale isnt available then it's set to (1,1,1) by default
     * @param nodeObj
     * @param sceneNode
     */
    void readSceneNodeTransform(QJsonObject& nodeObj,QSharedPointer<iris::SceneNode> sceneNode)
    {
        auto pos = nodeObj["pos"].toObject();
        if(!pos.isEmpty())
        {
            sceneNode->pos = readVector3(pos);
        }

        auto rot = nodeObj["rot"].toObject();
        if(!rot.isEmpty())
        {
            //the rotation is stored as euler angles
            sceneNode->rot = QQuaternion::fromEulerAngles(readVector3(rot));
        }

        auto scale = nodeObj["scale"].toObject();
        if(!scale.isEmpty())
        {
            sceneNode->scale = readVector3(scale);
        }
        else
        {
            sceneNode->scale = QVector3D(1,1,1);
        }
    }

    /**
     * Creates mesh using scene node data
     * @param nodeObj
     * @return
     */
    QSharedPointer<iris::MeshNode> createMesh(QJsonObject& nodeObj)
    {
        auto meshNode = iris::MeshNode::create();

        auto source = nodeObj["mesh"].toString("");
        auto meshIndex = nodeObj["meshIndex"].toInt(0);
        if(!source.isEmpty())
        {
            auto mesh = getMesh(source,meshIndex);
            meshNode->setMesh(mesh);
            meshNode->meshPath = source;
            meshNode->meshIndex = meshIndex;
        }

        //material
        auto material = readMaterial(nodeObj);
        meshNode->setMaterial(material);

        return meshNode;
    }

    /**
     * Creates light from light node data
     * @param nodeObj
     * @return
     */
    QSharedPointer<iris::LightNode> createLight(QJsonObject& nodeObj)
    {
        auto lightNode = iris::LightNode::create();

        lightNode->setLightType(getLightTypeFromName(nodeObj["lightType"].toString("point")));
        lightNode->intensity = (float)nodeObj["intensity"].toDouble(1.0f);
        lightNode->distance = (float)nodeObj["radius"].toDouble(1.0f);
        lightNode->spotCutOff = (float)nodeObj["spotCutOff"].toDouble(30.0f);

        //todo: move this to the sceneview widget or somewhere more appropriate
        lightNode->icon = iris::Texture2D::load("app/icons/bulb.png");
        lightNode->iconSize = 0.5f;

        return lightNode;
    }

    iris::LightType getLightTypeFromName(QString lightType)
    {
        if(lightType=="point")
            return iris::LightType::Point;

        if(lightType=="directional")
            return iris::LightType::Directional;

        if(lightType=="spot")
            return iris::LightType::Spot;

        return iris::LightType::Point;

    }

    /**
     * Extracts material from node's json object.
     * Creates default material if one isnt defined in nodeObj
     * @param nodeObj
     * @return
     */
    QSharedPointer<iris::Material> readMaterial(QJsonObject& nodeObj)
    {
        if(nodeObj["material"].isNull())
        {
            return iris::DefaultMaterial::create();
        }

        QJsonObject matObj = nodeObj["material"].toObject();
        auto material = iris::DefaultMaterial::create();

        auto colObj = matObj["ambientColor"].toObject();
        material->setAmbientColor(readColor(colObj));

        colObj = matObj["diffuseColor"].toObject();
        material->setDiffuseColor(readColor(colObj));

        auto tex = matObj["diffuseTexture"].toString("");
        if(!tex.isEmpty()) material->setDiffuseTexture(iris::Texture2D::load(getAbsolutePath(tex)));

        colObj = matObj["specularColor"].toObject();
        material->setSpecularColor(readColor(colObj));
        material->setShininess((float)matObj["shininess"].toDouble(0.0f));

        tex = matObj["specularTexture"].toString("");
        if(!tex.isEmpty()) material->setSpecularTexture(iris::Texture2D::load(getAbsolutePath(tex)));

        material->setTextureScale((float)matObj["textureScale"].toDouble(1.0f));

        return material;

    }

    /**
     * Returns mesh from mesh file at index
     * if the mesh doesnt exist, nullptr is returned
     * @param filePath
     * @param index
     * @return
     */
    iris::Mesh* getMesh(QString filePath,int index)
    {
        //if the mesh is already in the hashmap then it was already loaded, just return the indexed mesh
        if(meshes.contains(filePath))
        {
            auto meshList = meshes[filePath];
            if(index < meshList.size())
                return meshList[index];

            return nullptr;//maybe the mesh was modified after the file was saved
        }
        else
        {
            auto meshList = iris::GraphicsHelper::loadAllMeshesFromFile(filePath);
            meshes.insert(filePath,meshList);

            if(index < meshList.size())
                return meshList[index];

            return nullptr;//maybe the mesh was modified after the file was saved
        }
    }


};

#endif // SCENEREADER_H
