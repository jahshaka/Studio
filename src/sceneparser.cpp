/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Karsten Becker <jahshaka@gmail.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include <Qt>
#include <QXmlStreamWriter>
#include <qvector.h>
#include <qquaternion.h>
#include "scenemanager.h"
#include "scenenode.h"
#include <Qt3DCore/QEntity>

#include "keyframes.h"
#include <QQuaternion>
#include <QVector3D>
#include <qdebug.h>

#include "materialproxy.h"
#include "sceneparser.h"
#include "materials.h"
#include "helpers/texturehelper.h"

#include "sceneparser.h"
#include <QXmlStreamWriter>
#include <QFile>

QDir SceneParser::getDirFromFileName(QString filename)
{
    QFileInfo info(filename);
    return info.absoluteDir();
}

QString SceneParser::getRelativePath(QString filename)
{
    //if it's a resource then return it
    if(filename.trimmed().startsWith(":") || filename.trimmed().startsWith("qrc:"))
        return filename;

    return dir.relativeFilePath(filename);
}

QString SceneParser::getAbsolutePath(QString filename)
{
    //if it's a resource then return it
    if(filename.trimmed().startsWith(":") || filename.trimmed().startsWith("qrc:"))
        return filename;

    auto absPath = dir.absoluteFilePath(filename);

    //file should exist, else return null string
    if(!QFile(absPath).exists())
        return QString::null;

    //everything went ok :)
    return absPath;
}

void SceneParser::saveScene(QString fileName,SceneManager* scene)
{
    dir = SceneParser::getDirFromFileName(fileName);
    QFile file(fileName);
    file.open(QIODevice::ReadWrite|QIODevice::Truncate);

    QXmlStreamWriter xml;
    xml.setDevice(&file);
    xml.setAutoFormatting(true);
    xml.writeStartDocument();

    writeScene(xml,scene);

}

SceneManager* SceneParser::loadScene(QString fileName,Qt3DCore::QEntity* rootEntity)
{
    dir = SceneParser::getDirFromFileName(fileName);
    QFile file(fileName);
    file.open(QIODevice::ReadOnly);

    QXmlStreamReader xml;
    xml.setDevice(&file);

    auto scene = readScene(xml,rootEntity);

    return scene;
}

//writing functions
void SceneParser::writeScene(QXmlStreamWriter& xml,SceneManager* scene)
{
    xml.writeStartElement("scene");
    //scene properties
    writeSceneData(xml,scene);

    //write nodes
    writeNode(xml,scene->getRootNode());

    xml.writeEndElement();
}

void SceneParser::writeSceneData(QXmlStreamWriter& xml,SceneManager* scene)
{
    writeColorElement("backgroundcolor",xml,scene->bgColor);
    writeFloatElement("fogstart",xml,scene->getFogStart());
    writeFloatElement("fogend",xml,scene->getFogEnd());
    writeColorElement("fogcolor",xml,scene->getFogColor());
    writeStringElement("fogenabled",xml,scene->getFogEnabled()?"true":"false");

    //writeStringElement("showgrid",xml,scene->isGridVisible()?"true":"false");
}

void SceneParser::writeNodes(QXmlStreamWriter& xml,SceneNode* node)
{
    for(size_t i=0;i<node->children.size();i++)
    {
        SceneNode* child = node->children[i];
        writeNode(xml,child);
    }
}

void SceneParser::writeNode(QXmlStreamWriter& xml,SceneNode* node)
{
    auto nodeName = getScenNodeTypeName(node->sceneNodeType);
    xml.writeStartElement(nodeName);
    xml.writeAttribute("name",node->getName());
    xml.writeAttribute("visible",node->isVisible()?"true":"false");

        //write node-specific data
        //node->writeData(xml);
        writeNodeData(xml,node);
        writeTransform(xml,node);

        if(node->usesMaterial())
            writeMaterial(xml,node);

        //write regular keyframes
        if(node->hasTransformAnimation())
            writeKeyFrameAnimation(xml,node);

        //write children
        xml.writeStartElement("children");
            for(size_t i=0;i<node->children.size();i++)
                writeNode(xml,node->children[i]);
        xml.writeEndElement();

    xml.writeEndElement();
}

void SceneParser::writeNodeData(QXmlStreamWriter& xml,SceneNode* node)
{
    xml.writeStartElement("data");
        node->writeData(*this,xml);
    xml.writeEndElement();
}

void SceneParser::writeTransform(QXmlStreamWriter& xml,SceneNode* node)
{
    xml.writeStartElement("transform");

        xml.writeStartElement("pos");
            xml.writeAttribute("x",QString("%1").arg(node->pos.x()));
            xml.writeAttribute("y",QString("%1").arg(node->pos.y()));
            xml.writeAttribute("z",QString("%1").arg(node->pos.z()));
        xml.writeEndElement();

        //auto rot = node->rot.toEulerAngles();
        auto rot = node->rot;
        xml.writeStartElement("rot");
            xml.writeAttribute("x",QString("%1").arg(rot.x()));
            xml.writeAttribute("y",QString("%1").arg(rot.y()));
            xml.writeAttribute("z",QString("%1").arg(rot.z()));
        xml.writeEndElement();

        xml.writeStartElement("scale");
            xml.writeAttribute("x",QString("%1").arg(node->scale.x()));
            xml.writeAttribute("y",QString("%1").arg(node->scale.y()));
            xml.writeAttribute("z",QString("%1").arg(node->scale.z()));
        xml.writeEndElement();

    xml.writeEndElement();
}

void SceneParser::writeKeyFrameAnimation(QXmlStreamWriter& xml,SceneNode* node)
{
    xml.writeStartElement("transformanimation");
        writeVector3KeyFrame("pos",xml,node->transformAnim->pos);
        //writeQuaternionKeyFrame("rot",xml,node->transformAnim->rot);
        writeVector3KeyFrame("rot",xml,node->transformAnim->rot);
        writeVector3KeyFrame("scale",xml,node->transformAnim->scale);
    xml.writeEndElement();
}

void SceneParser::writeVector3KeyFrame(QString name,QXmlStreamWriter& xml,Vector3DKeyFrame* frame)
{
    xml.writeStartElement(name);

    for(size_t i=0;i<frame->keys.size();i++)
    {
        auto key = frame->keys[i];

        xml.writeStartElement("key");
            xml.writeAttribute("time",QString("%1").arg(key->time));
            xml.writeAttribute("x",QString("%1").arg(key->value.x()));
            xml.writeAttribute("y",QString("%1").arg(key->value.y()));
            xml.writeAttribute("z",QString("%1").arg(key->value.z()));
        xml.writeEndElement();
    }

    xml.writeEndElement();
}

void SceneParser::writeQuaternionKeyFrame(QString name,QXmlStreamWriter& xml,QuaternionKeyFrame* frame)
{
    xml.writeStartElement(name);

    for(size_t i=0;i<frame->keys.size();i++)
    {
        auto key = frame->keys[i];
        auto val = key->value.toEulerAngles();
        xml.writeStartElement("key");
            xml.writeAttribute("time",QString("%1").arg(key->time));
            xml.writeAttribute("x",QString("%1").arg(val.x()));
            xml.writeAttribute("y",QString("%1").arg(val.y()));
            xml.writeAttribute("z",QString("%1").arg(val.z()));
        xml.writeEndElement();
    }

    xml.writeEndElement();
}

void SceneParser::writeVector2KeyFrame(QString name,QXmlStreamWriter& xml,Vector2DKeyFrame* frame)
{
    xml.writeStartElement(name);

    for(size_t i=0;i<frame->keys.size();i++)
    {
        auto key = frame->keys[i];

        xml.writeStartElement("key");
            xml.writeAttribute("time",QString("%1").arg(key->time));
            xml.writeAttribute("x",QString("%1").arg(key->value.x()));
            xml.writeAttribute("y",QString("%1").arg(key->value.y()));
        xml.writeEndElement();
    }

    xml.writeEndElement();
}

void SceneParser::writeFloatKeyFrame(QString name,QXmlStreamWriter& xml,FloatKeyFrame* frame)
{
    xml.writeStartElement(name);

    for(size_t i=0;i<frame->keys.size();i++)
    {
        auto key = frame->keys[i];

        xml.writeStartElement("key");
            xml.writeAttribute("time",QString("%1").arg(key->time));
            xml.writeAttribute("value",QString("%1").arg(key->value));
        xml.writeEndElement();
    }

    xml.writeEndElement();
}

void SceneParser::writeIntKeyFrame(QString name,QXmlStreamWriter& xml,IntKeyFrame* frame)
{
    xml.writeStartElement(name);

    for(size_t i=0;i<frame->keys.size();i++)
    {
        auto key = frame->keys[i];

        xml.writeStartElement("key");
            xml.writeAttribute("time",QString("%1").arg(key->time));
            xml.writeAttribute("value",QString("%1").arg(key->value));
        xml.writeEndElement();
    }

    xml.writeEndElement();
}

void SceneParser::writeStringKeyFrame(QString name,QXmlStreamWriter& xml,StringKeyFrame* frame)
{
    xml.writeStartElement(name);

    for(size_t i=0;i<frame->keys.size();i++)
    {
        auto key = frame->keys[i];

        xml.writeStartElement("key");
            xml.writeAttribute("time",QString("%1").arg(key->time));
            xml.writeAttribute("value",QString("%1").arg(key->value));
        xml.writeEndElement();
    }

    xml.writeEndElement();
}

void SceneParser::writeMaterial(QXmlStreamWriter& xml,SceneNode* node)
{
    //skip materials for now
    //return;

    auto mat = node->getMaterial();

    xml.writeStartElement("material");

        writeColorElement("ambient",xml,mat->getAmbientColor());
        writeColorElement("diffuse",xml,mat->getDiffuseColor());
        writeColorElement("specular",xml,mat->getSpecularColor());

        writeFloatElement("shininess",xml,mat->getShininess());
        //writeFloatElement("alpha",xml,mat->getAlpha());

        //textures
        QString tex;
        tex = mat->getDiffuseTextureSource();
        if(!tex.isEmpty())
        {
            auto relPath = getRelativePath(tex);
            writeStringElement("diffusemap",xml,relPath);
            //writeStringElement("diffusemap",xml,tex);
        }

        tex = mat->getSpecularTextureSource();
        if(!tex.isEmpty())
        {
            auto relPath = getRelativePath(tex);
            writeStringElement("specularmap",xml,relPath);
        }

        tex = mat->getNormalTextureSource();
        if(!tex.isEmpty())
        {
            auto relPath = getRelativePath(tex);
            writeStringElement("normalmap",xml,relPath);
        }
        writeFloatElement("normalintensity",xml,mat->getNormalIntensity());

        tex = mat->getReflectionTextureSource();
        if(!tex.isEmpty())
        {
            auto relPath = getRelativePath(tex);
            writeStringElement("reflectionmap",xml,relPath);
        }
        writeFloatElement("reflectioninfluence",xml,mat->getReflectionInfluence());

        writeFloatElement("texturescale",xml,mat->getTextureScale());

    xml.writeEndElement();

}

void SceneParser::writeColorElement(QString name,QXmlStreamWriter& xml,const QColor& val)
{
    xml.writeStartElement(name);
        xml.writeAttribute("r",QString("%1").arg(val.red()));
        xml.writeAttribute("g",QString("%1").arg(val.green()));
        xml.writeAttribute("b",QString("%1").arg(val.blue()));
        xml.writeAttribute("a",QString("%1").arg(val.alpha()));
    xml.writeEndElement();
}

void SceneParser::writeFloatElement(QString name,QXmlStreamWriter& xml,const float val)
{
    xml.writeStartElement(name);
        xml.writeAttribute("value",QString("%1").arg(val));
    xml.writeEndElement();
}

void SceneParser::writeStringElement(QString name,QXmlStreamWriter& xml,const QString val)
{
    xml.writeStartElement(name);
        xml.writeAttribute("value",val);
    xml.writeEndElement();
}

QString SceneParser::getMaterialName(MaterialType type)
{
    switch(type)
    {
    case MaterialType::Default:
        return "default";
    case MaterialType::DiffuseMap:
        return "diffusemap";
    case MaterialType::DiffuseSpecularMap:
        return "diffusespecularmap";
    case MaterialType::NormalDiffuseMap:
        return "normaldiffusemap";
    case MaterialType::Reflection:
        return "reflection";
    case MaterialType::Refraction:
        return "refraction";
    default:
        return "default";
    }
}

QString SceneParser::getScenNodeTypeName(SceneNodeType type)
{
    switch(type)
    {
    case SceneNodeType::Cube:
        return "cube";
    case SceneNodeType::Cylinder:
        return "cylinder";
    case SceneNodeType::Empty:
        return "empty";
    case SceneNodeType::Light:
        return "light";
    case SceneNodeType::Mesh:
        return "mesh";
    case SceneNodeType::Sphere:
        return "sphere";
    case SceneNodeType::Torus:
        return "torus";
    case SceneNodeType::TexturedPlane:
        return "texturedplane";
    case SceneNodeType::EndlessPlane:
        return "endlessplane";
    case SceneNodeType::UserCamera:
        return "usercamera";
    case SceneNodeType::World:
        return "world";
    }

    return "scenenode";
}


//SceneManager* loadScene(QString fileName,Qt3DCore::QEntity* rootEntity);

//NOTE: ALL READ OPS SHOULD END ON ENDELEMENT - do not read after
SceneManager* SceneParser::readScene(QXmlStreamReader& xml,Qt3DCore::QEntity* rootEntity)
{
    auto scene = new SceneManager(new Qt3DCore::QEntity());
    scene->getRootNode()->getEntity()->setParent(rootEntity);

    while(!xml.isEndDocument())
    {
        //if(xml.isEndElement() && name==nodeType)
        //    break;
        qDebug()<<xml.name();

        if(xml.isStartElement())
        {
            auto name = xml.name().toString();
            if(name=="world")
            {
                auto rootNode = readNode(xml);
                scene->setRootNode(rootNode);
            }
            else if(name=="backgroundcolor")
            {
                scene->bgColor = this->readColorElement(xml);
            }
            else if(name=="fogstart")
            {
                scene->setFogStart(this->readFloatElement(xml));
            }
            else if(name=="fogend")
            {
                scene->setFogEnd(this->readFloatElement(xml));
            }
            else if(name=="fogcolor")
            {
                scene->setFogColor(this->readColorElement(xml));
            }
            else if(name=="fogenabled")
            {
                scene->setFogEnabled(this->readStringElement(xml)=="true"?true:false);
            }


            /*
             * writeFloatElement("fogstart",xml,scene->getFogStart());
    writeFloatElement("fogend",xml,scene->getFogEnd());
    writeColorElement("fogcolor",xml,scene->getFogColor());
    writeFloatElement("fogenabled",xml,scene->getFogEnabled());
    */
        }

        xml.readNext();
    }

    return scene;
}

SceneNode* SceneParser::readNode(QXmlStreamReader& xml)
{
    QString nodeType = xml.name().toString();
    QString nodeName = xml.attributes().value("name").toString();
    QString nodeVisible = xml.attributes().value("visible").toString();

    auto node = createSceneNodeFromTypeName(nodeType,nodeName);
    node->setName(nodeName);

    if(nodeVisible!="true")
        node->hide();

    //read further
    while(!xml.isEndDocument())
    {
        auto name = xml.name().toString();
        if(xml.isEndElement() && name==nodeType)
            break;

        if(xml.isStartElement())
        {
            //read transform
            //read animation
            //read data
            //read children
            qDebug()<<xml.name();

            if(xml.name()=="transform")
            {
                readTransform(xml,node);
            }
            if(xml.name()=="material")
            {
                readMaterial(xml,node);
            }
            else if(xml.name()=="data")
            {
                node->readData(*this,xml);
            }
            else if(xml.name()=="transformanimation")
            {
                readTransformAnimation(xml,node);
            }
            else if(xml.name()=="children")
            {
                xml.readNext();

                while(!(xml.isEndElement() && xml.name()=="children"))
                {

                    auto childName = xml.name();
                    //cant believe this was the cause of the error -___-
                    //should be expected when looking out for unknown types
                    if(childName=="")
                    {
                        xml.readNext();
                        continue;
                    }
                    //qDebug()<<"child name: "<<childName;
                    auto child = readNode(xml);
                    node->addChild(child);

                    xml.readNext();

                }
            }
        }

        xml.readNext();
    }

    node->_updateTransform();
    return node;
}

void SceneParser::readTransform(QXmlStreamReader& xml,SceneNode* node)
{
    while(!xml.isEndDocument())
    {
        if(xml.isEndElement() && xml.name()=="transform")
            break;

        if(xml.isStartElement())
        {
            if(xml.name()=="pos")
            {
                node->pos.setX(xml.attributes().value("x").toFloat());
                node->pos.setY(xml.attributes().value("y").toFloat());
                node->pos.setZ(xml.attributes().value("z").toFloat());
            }
            else if(xml.name()=="rot")
            {
                float x,y,z;
                x = xml.attributes().value("x").toFloat();//yaw
                y = xml.attributes().value("y").toFloat();//pitch
                z = xml.attributes().value("z").toFloat();//roll
                //node->rot = QQuaternion::fromEulerAngles(y,x,z);
                node->rot = QVector3D(x,y,z);

                //node->pos.setX(xml.attributes().value("x").toFloat());
                //node->pos.setY(xml.attributes().value("y").toFloat());
                //node->pos.setZ(xml.attributes().value("z").toFloat());
            }
            else if(xml.name()=="scale")
            {
                node->scale.setX(xml.attributes().value("x").toFloat());
                node->scale.setY(xml.attributes().value("y").toFloat());
                node->scale.setZ(xml.attributes().value("z").toFloat());
            }
        }

        xml.readNext();
    }
}

Vector3DKeyFrame* SceneParser::readVector3KeyFrame(QString frameName,QXmlStreamReader& xml)
{
    auto frame = new Vector3DKeyFrame();
    while(!xml.isEndDocument())
    {
        auto name = xml.name();
        if(xml.isEndElement() && name==frameName)
        {
            break;
        }

        if(name=="key" && xml.isStartElement())
        {
            QVector3D vec;
            vec.setX(xml.attributes().value("x").toFloat());
            vec.setY(xml.attributes().value("y").toFloat());
            vec.setZ(xml.attributes().value("z").toFloat());

            float time = xml.attributes().value("time").toFloat();
            frame->addKey(vec,time);
        }
        xml.readNext();
    }

    frame->sortKeys();
    return frame;
}

Vector2DKeyFrame* SceneParser::readVector2KeyFrame(QString frameName,QXmlStreamReader& xml)
{
    auto frame = new Vector2DKeyFrame();
    while(!xml.isEndDocument())
    {
        auto name = xml.name();
        if(xml.isEndElement() && name==frameName)
        {
            break;
        }

        if(name=="key" && xml.isStartElement())
        {
            QVector2D vec;
            vec.setX(xml.attributes().value("x").toFloat());
            vec.setY(xml.attributes().value("y").toFloat());

            float time = xml.attributes().value("time").toFloat();
            frame->addKey(vec,time);
        }
        xml.readNext();
    }

    frame->sortKeys();
    return frame;
}


FloatKeyFrame* SceneParser::readFloatKeyFrame(QString frameName,QXmlStreamReader& xml)
{
    auto frame = new FloatKeyFrame();
    while(!xml.isEndDocument())
    {
        auto name = xml.name();
        if(xml.isEndElement() && name==frameName)
        {
            break;
        }

        if(name=="key" && xml.isStartElement())
        {;
            float val = xml.attributes().value("val").toFloat();
            float time = xml.attributes().value("time").toFloat();
            frame->addKey(val,time);
        }
        xml.readNext();
    }

    frame->sortKeys();
    return frame;
}

QuaternionKeyFrame* SceneParser::readRotKeyFrame(QString frameName,QXmlStreamReader& xml)
{
    auto frame = new QuaternionKeyFrame();
    while(!xml.isEndDocument())
    {
        auto name = xml.name();
        if(xml.isEndElement() && name==frameName)
        {
            break;
        }

        if(name=="key" && xml.isStartElement())
        {
            float x(xml.attributes().value("x").toFloat());
            float y(xml.attributes().value("y").toFloat());
            float z(xml.attributes().value("z").toFloat());

            auto quat = QQuaternion::fromEulerAngles(x,y,z);

            float time = xml.attributes().value("time").toFloat();
            frame->addKey(quat,time);
        }

        xml.readNext();
    }

    frame->sortKeys();
    return frame;
}

void SceneParser::readTransformAnimation(QXmlStreamReader& xml,SceneNode* node)
{
    Q_UNUSED(node);
    while(!xml.isEndDocument())
    {
        if(xml.isEndElement() && xml.name()=="transformanimation")
            break;

        if(xml.isStartElement())
        {
            if(xml.name()=="pos")
            {
                xml.readNext();
                auto posFrame = readVector3KeyFrame("pos",xml);
                node->transformAnim->pos = posFrame;
            }
            else if(xml.name()=="rot")
            {
                xml.readNext();
                //auto rotFrame = readRotKeyFrame("rot",xml);
                auto rotFrame = readVector3KeyFrame("rot",xml);
                node->transformAnim->rot = rotFrame;
            }
            else if(xml.name()=="scale")
            {
                xml.readNext();
                auto scaleFrame = readVector3KeyFrame("scale",xml);
                node->transformAnim->scale = scaleFrame;
            }
        }

        xml.readNext();
    }
}

void SceneParser::readMaterial(QXmlStreamReader& xml,SceneNode* node)
{
    //auto mat = new AdvanceMaterial();
    auto mat = node->getMaterial();

    while(!xml.isEndDocument())
    {
        if(xml.isEndElement() && xml.name()=="material")
            break;

        if(xml.isStartElement())
        {
            auto name = xml.name();
            if(name=="ambient")
            {
                QColor col = readColorElement(xml);
                mat->setAmbientColor(col);
            }
            if(name=="diffuse")
            {
                QColor col = readColorElement(xml);
                mat->setDiffuseColor(col);
            }
            if(name=="specular")
            {
                QColor col = readColorElement(xml);
                mat->setSpecularColor(col);
            }
            if(name=="shininess")
            {
                float val = readFloatElement(xml);
                mat->setShininess(val);
            }
            if(name=="alpha")
            {
                //float val = readFloatElement(xml);
                //proxy->setAlpha(val);
            }

            if(name=="reflectionmap")
            {
                auto val = readStringElement(xml);
                if(!val.isEmpty() && !val.isNull())
                {
                    auto path = getAbsolutePath(val);
                    if(!path.isEmpty() && !path.isNull())
                    {
                        mat->setReflectionTexture(TextureHelper::loadTexture(path));
                    }
                }

            }

            if(name=="reflectioninfluence")
            {
                float val = readFloatElement(xml);
                mat->setReflectionInfluence(val);
            }

            if(name=="diffusemap")
            {
                auto val = readStringElement(xml);
                if(!val.isEmpty() && !val.isNull())
                {
                    auto path = getAbsolutePath(val);
                    if(!path.isEmpty() && !path.isNull())
                    {
                        mat->setDiffuseTexture(TextureHelper::loadTexture(path));
                    }
                }

            }
            if(name=="specularmap")
            {
                auto val = readStringElement(xml); 
                if(!val.isEmpty() && !val.isNull())
                {
                    auto path = getAbsolutePath(val);
                    if(!path.isEmpty() && !path.isNull())
                    {
                        mat->setSpecularTexture(TextureHelper::loadTexture(path));
                    }
                }


            }
            if(name=="normalmap")
            {
                auto val = readStringElement(xml);
                if(!val.isEmpty() && !val.isNull())
                {
                    auto path = getAbsolutePath(val);
                    if(!path.isEmpty() && !path.isNull())
                    {
                        mat->setNormalTexture(TextureHelper::loadTexture(path));
                    }
                }


            }
            if(name=="normalintensity")
            {
                float val = readFloatElement(xml);
                mat->setNormalIntensity(val);
            }
            if(name=="texturescale")
            {
                float val = readFloatElement(xml);
                mat->setTextureScale(val);
            }
        }

        xml.readNext();
    }

    node->setMaterial(mat);

}

QColor SceneParser::readColorElement(QXmlStreamReader& xml)
{
    QColor color;
    color.setRed(xml.attributes().value("r").toFloat());
    color.setGreen(xml.attributes().value("g").toFloat());
    color.setBlue(xml.attributes().value("b").toFloat());
    color.setAlpha(xml.attributes().value("a").toFloat());

    return color;
}

float SceneParser::readFloatElement(QXmlStreamReader& xml)
{
    float value = xml.attributes().value("value").toFloat();

    return value;
}

QString SceneParser::readStringElement(QXmlStreamReader& xml)
{
    QString value = xml.attributes().value("value").toString();

    return value;
}

//returns MaterialType::Default if a name cant be evaluated
MaterialType SceneParser::getMaterialType(QString typeName)
{
    if(typeName=="default")
        return MaterialType::Default;
    if(typeName=="diffusemap")
        return MaterialType::DiffuseMap;
    if(typeName=="diffusespecularmap")
        return MaterialType::DiffuseSpecularMap;
    if(typeName=="normaldiffusemap")
        return MaterialType::NormalDiffuseMap;
    if(typeName=="reflection")
        return MaterialType::Reflection;
    if(typeName=="refraction")
        return MaterialType::Refraction;

    return MaterialType::Default;
}

SceneNode* SceneParser::createSceneNodeFromTypeName(QString typeName,QString name)
{
    if(typeName == "world")
        return new WorldNode(new Qt3DCore::QEntity());
    if(typeName=="usercamera")
        return new UserCameraNode(new Qt3DCore::QEntity() );
    if(typeName=="cylinder")
        return CylinderNode::createCylinder(name);
    if(typeName=="sphere")
        return SphereNode::createSphere(name);
    if(typeName=="cube")
        return SceneNode::createCube(name);
    if(typeName=="mesh")
        return MeshNode::createMesh(name);
    if(typeName=="torus")
        return TorusNode::createTorus(name);
    if(typeName=="texturedplane")
        return TexturedPlaneNode::createTexturedPlane(name);
    if(typeName=="endlessplane")
        return EndlessPlaneNode::createEndlessPlane(name);
    if(typeName=="light")
        return new LightNode(new Qt3DCore::QEntity());

    return SceneNode::createEmpty();
}
