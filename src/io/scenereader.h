#ifndef SCENEREADER_H
#define SCENEREADER_H

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
#include "../jah3d/graphics/texture2d.h"

class SceneReader:public SceneIOBase
{
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

        return sceneNode;
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
        if(!source.isEmpty())
            meshNode->setMesh(source);

        //material
        auto material = readMaterial(nodeObj);
        meshNode->setMaterial(material);

        return meshNode;
    }

    QSharedPointer<jah3d::LightNode> createLight(QJsonObject& nodeObj)
    {
        auto lightNode = jah3d::LightNode::create();

        lightNode->setLightType(getLightTypeFromName(nodeObj["lightType"].toString("point")));
        lightNode->intensity = (float)nodeObj["intensity"].toDouble(1.0f);
        lightNode->radius = (float)nodeObj["radius"].toDouble(1.0f);
        lightNode->spotCutOff = (float)nodeObj["spotCutOff"].toDouble(30.0f);

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
     * Creates default material if one doesnt exist
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
        if(!tex.isEmpty()) material->setDiffuseTexture(jah3d::Texture2D::load(tex));

        colObj = matObj["specularColor"].toObject();
        material->setSpecularColor(readColor(colObj));
        material->setShininess((float)matObj["shininess"].toDouble(0.0f));

        tex = matObj["specularTexture"].toString("");
        if(!tex.isEmpty()) material->setSpecularTexture(jah3d::Texture2D::load(tex));

        return material;

    }

    /**
     * Reads r,g,b and a from color json object
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


};

#endif // SCENEREADER_H
