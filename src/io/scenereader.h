#ifndef SCENEREADER_H
#define SCENEREADER_H

#include <QSharedPointer>
#include "sceneiobase.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QJsonValueRef>
#include <QJsonDocument>

#include "../jah3d/core/scene.h"
#include "../jah3d/core/scenenode.h"
#include "../jah3d/scenegraph/meshnode.h"
#include "../jah3d/scenegraph/lightnode.h"
#include "../jah3d/materials/defaultmaterial.h"
#include "../jah3d/graphics/texture2d.h"
#include "../jah3d/graphics/graphicshelper.h"

class SceneReader:public SceneIOBase
{
    QHash<QString,QList<jah3d::Mesh*>> meshes;
public:
    QSharedPointer<jah3d::Scene> readScene(QString filePath)
    {
        dir = SceneIOBase::getDirFromFileName(filePath);
        QFile file(filePath);
        file.open(QIODevice::ReadOnly);

        auto data = file.readAll();
        auto doc = QJsonDocument::fromJson(data);

        auto projectObj = doc.object();
        auto scene = readScene(projectObj);

        return scene;
    }

    QSharedPointer<jah3d::Scene> readScene(QJsonObject& projectObj)
    {
        auto scene = jah3d::Scene::create();

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
    QSharedPointer<jah3d::SceneNode> readSceneNode(QJsonObject& nodeObj)
    {
        QSharedPointer<jah3d::SceneNode> sceneNode;

        QString nodeType = nodeObj["type"].toString("empty");
        if(nodeType=="mesh")
            sceneNode = createMesh(nodeObj).staticCast<jah3d::SceneNode>();
        else if(nodeType=="light")
            sceneNode = createLight(nodeObj).staticCast<jah3d::SceneNode>();
        else
            sceneNode = jah3d::SceneNode::create();

        //read transform
        readSceneNodeTransform(nodeObj,sceneNode);

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

    /**
     * Reads pos, rot and scale properties from json object
     * if scale isnt available then it's set to (1,1,1) by default
     * @param nodeObj
     * @param sceneNode
     */
    void readSceneNodeTransform(QJsonObject& nodeObj,QSharedPointer<jah3d::SceneNode> sceneNode)
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
    QSharedPointer<jah3d::MeshNode> createMesh(QJsonObject& nodeObj)
    {
        auto meshNode = jah3d::MeshNode::create();

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
    QSharedPointer<jah3d::LightNode> createLight(QJsonObject& nodeObj)
    {
        auto lightNode = jah3d::LightNode::create();

        lightNode->setLightType(getLightTypeFromName(nodeObj["lightType"].toString("point")));
        lightNode->intensity = (float)nodeObj["intensity"].toDouble(1.0f);
        lightNode->radius = (float)nodeObj["radius"].toDouble(1.0f);
        lightNode->spotCutOff = (float)nodeObj["spotCutOff"].toDouble(30.0f);

        //todo: move this to the sceneview widget or somewhere more appropriate
        lightNode->icon = jah3d::Texture2D::load("app/icons/bulb.png");
        lightNode->iconSize = 0.5f;

        return lightNode;
    }

    jah3d::LightType getLightTypeFromName(QString lightType)
    {
        if(lightType=="point")
            return jah3d::LightType::Point;

        if(lightType=="directional")
            return jah3d::LightType::Directional;

        if(lightType=="spot")
            return jah3d::LightType::Spot;

        return jah3d::LightType::Point;

    }

    /**
     * Extracts material from node's json object.
     * Creates default material if one isnt defined in nodeObj
     * @param nodeObj
     * @return
     */
    QSharedPointer<jah3d::Material> readMaterial(QJsonObject& nodeObj)
    {
        if(nodeObj["material"].isNull())
        {
            return jah3d::DefaultMaterial::create();
        }

        QJsonObject matObj = nodeObj["material"].toObject();
        auto material = jah3d::DefaultMaterial::create();

        auto colObj = matObj["ambientColor"].toObject();
        material->setAmbientColor(readColor(colObj));

        colObj = matObj["diffuseColor"].toObject();
        material->setDiffuseColor(readColor(colObj));

        auto tex = matObj["diffuseTexture"].toString("");
        if(!tex.isEmpty()) material->setDiffuseTexture(jah3d::Texture2D::load(getAbsolutePath(tex)));

        colObj = matObj["specularColor"].toObject();
        material->setSpecularColor(readColor(colObj));
        material->setShininess((float)matObj["shininess"].toDouble(0.0f));

        tex = matObj["specularTexture"].toString("");
        if(!tex.isEmpty()) material->setSpecularTexture(jah3d::Texture2D::load(getAbsolutePath(tex)));

        material->setTextureScale((float)matObj["textureScale"].toDouble(1.0f));

        return material;

    }

    /**
     * Reads r,g,b and a from color json object
     * returns default QColor() if colorObj is null
     * @param colorObj
     * @return
     */
    QColor readColor(const QJsonObject& colorObj)
    {
        if(colorObj.isEmpty())
        {
            return QColor();
        }

        QColor col;
        col.setRed(colorObj["r"].toInt(0));
        col.setGreen(colorObj["g"].toInt(0));
        col.setBlue(colorObj["b"].toInt(0));
        col.setAlpha(colorObj["a"].toInt(0));

        return col;
    }

    /**
     * Reads x,y and z from vector json object
     * returns default QVector3D() if colorObj is null
     * @param vecObj
     * @return
     */
    QVector3D readVector3(const QJsonObject& vecObj)
    {
        if(vecObj.isEmpty())
        {
            return QVector3D();
        }

        QVector3D vec;
        vec.setX(vecObj["x"].toDouble(0));
        vec.setY(vecObj["y"].toDouble(0));
        vec.setZ(vecObj["z"].toDouble(0));

        return vec;
    }

    /**
     * Returns mesh from mesh file at index
     * if the mesh doesnt exist, nullptr is returned
     * @param filePath
     * @param index
     * @return
     */
    jah3d::Mesh* getMesh(QString filePath,int index)
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
            auto meshList = jah3d::GraphicsHelper::loadAllMeshesFromFile(filePath);
            meshes.insert(filePath,meshList);

            if(index < meshList.size())
                return meshList[index];

            return nullptr;//maybe the mesh was modified after the file was saved
        }
    }


};

#endif // SCENEREADER_H
