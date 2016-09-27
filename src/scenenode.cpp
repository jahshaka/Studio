/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Karsten Becker <jahshaka@gmail.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "scenenode.h"
#include "nodekeyframeanimation.h"
#include "nodekeyframe.h"
#include "globals.h"

#include "QVector3D"
#include "QQuaternion"

#include <Qt3DCore/QEntity>
#include <Qt3DCore/QTransform>
#include <Qt3DRender/QCamera>

#include <Qt3DExtras/QTorusMesh>
#include <Qt3DExtras/QCylinderMesh>
#include <Qt3DExtras/QPlaneMesh>
#include <Qt3DExtras/QCuboidMesh>
#include <Qt3DExtras/QSphereMesh>
#include <Qt3DExtras/QPhongMaterial>
#include <Qt3DExtras/QDiffuseMapMaterial>
#include <Qt3DRender/QTextureImage>
#include <Qt3DRender/QMesh>
#include "endlessplane.h"
#include "materials.h"

//lights
#include <Qt3DRender/QAbstractLight>
#include <Qt3DRender/QPointLight>
#include <Qt3DRender/QDirectionalLight>
#include <Qt3DRender/QSpotLight>

//materials
#include <Qt3DExtras/QPhongMaterial>

//xml
#include <QXmlStreamWriter>
#include <QXmlStreamReader>

#include "keyframes.h"

#include <QNode>
//#include "linestrip.h"
#include "linerenderer.h"
#include "scenemanager.h"

#include "billboard.h"
#include "scenenodes/skyboxnode.h"
#include "gridentity.h"

#include "sceneparser.h"
#include "helpers/texturehelper.h"

SceneNode::SceneNode(Qt3DCore::QEntity* entity)
{
    parent = Q_NULLPTR;

    sceneMan = nullptr;
    this->entity = entity;
    name = entity->objectName();
    nodeListener = nullptr;

    transformAnim = new TransformKeyFrameAnimation();

    removable = true;
    _showGizmo = true;
    visible = true;

    //set defaults
    pos = QVector3D(0,0,0);
    scale = QVector3D(1,1,1);
    //rot = QQuaternion::fromEulerAngles(0,0,0);
    rot = QVector3D(0,0,0);

    transform = new Qt3DCore::QTransform();
    this->_updateTransform();

    this->entity->addComponent(transform);

    _usesMaterial = true;
    //mat = new Qt3DExtras::QPhongMaterial();
    //matType = MaterialType::Default;
    mat = new AdvanceMaterial();
    this->entity->addComponent(mat);

    sceneNodeType = SceneNodeType::Empty;
    animStartTime = 0;
    animLength = 25;
    loopAnim = false;

    //line = new LineStrip();
    line = new LineRenderer();
    lineMat = new ColorMaterial();
    lineMat->setColor(QColor::fromRgb(255,0,0));
    lineEnt = new Qt3DCore::QEntity();
    lineEnt->addComponent(lineMat);
    lineEnt->addComponent(line);

}


SceneNode::~SceneNode()
{
    delete transformAnim;
    delete materialAnim;
    delete line;
    delete lineMat;
    delete lineEnt;
}

void SceneNode::show()
{
    entity->setParent(this->parent->getEntity());
    visible = true;
}

void SceneNode::hide()
{
    entity->setParent(static_cast<Qt3DCore::QEntity*>(nullptr));
    visible = false;
}

void SceneNode::updateFogParams(bool enabled,QColor color,float start,float end)
{
    if(mat!=nullptr)
    {
        mat->updateFogParams(enabled,color,start,end);
    }

    for(auto node:children)
    {
        node->updateFogParams(enabled,color,start,end);
    }
}

//have to create a new line instead of using old one
//todo: make line transform world space
void SceneNode::setAnimPath(std::vector<QVector3D> points)
{
    //return;//disable line path for now
    if(line!=nullptr)
    {
        lineEnt->removeComponent(line);
        //delete line;
    }

    //2 points minimum
    if(points.size()<=1)
        return;

    std::vector<Line3D> lines;
    //generate lines from points
    for(unsigned int i=1;i<points.size();i++)
    {
        Line3D l;
        l.point1 = points[i-1];
        l.point2 = points[i];
        l.color = QColor(255,0,0,255);
        lines.push_back(l);
    }

    line = new LineRenderer(&lines);
    lineEnt->addComponent(line);

}

void SceneNode::updateAnimPathFromKeyFrames()
{
    //resolution is 1 by default
    //int resolution = 1;
    std::vector<QVector3D> points;
    if(transformAnim->pos->hasKeys())
    {
        for(unsigned int i=0;i<transformAnim->pos->keys.size();i++)
        {
            points.push_back(transformAnim->pos->keys[i]->value);
        }
    }

    setAnimPath(points);
}

void SceneNode::setMaterial(AdvanceMaterial* mat)
{
    if(this->mat!=nullptr)
    {
        this->entity->removeComponent(this->mat);
    }

    this->mat = mat;

    if(this->mat!=nullptr)
    {
        this->entity->addComponent(this->mat);
    }
}

// - assumes child has no previous parent
void SceneNode::addChild(SceneNode* child)
{
    Q_ASSERT(child->parent==Q_NULLPTR);

    //set parent to this and entity
    child->parent = this;
    child->entity->setParent(entity);

    children.push_back(child);
    child->setSceneManager(sceneMan);
}

void SceneNode::setSceneManager(SceneManager* scene)
{
    sceneMan = scene;
    if(scene==nullptr)
    {
        this->lineEnt->setParent(static_cast<Qt3DCore::QEntity*>(nullptr));
    }
    else
    {
        //map line entity to world space
        this->lineEnt->setParent(scene->getRootNode()->getEntity());

        //update fog params
        this->updateFogParams(scene->getFogEnabled(),
                              scene->getFogColor(),
                              scene->getFogStart(),
                              scene->getFogEnd());
    }

    for(auto node:children)
    {
        node->setSceneManager(scene);
    }


}

Qt3DCore::QEntity* SceneNode::getEntity()
{
    return entity;
}

SceneNodeType SceneNode::getSceneNodeType()
{
    return sceneNodeType;
}

void SceneNode::update(float dt)
{
    this->updateAnimation(dt);
}

void SceneNode::updateAnimation(float dt)
{

    if(transformAnim!=nullptr)
    {
        this->pos = transformAnim->pos->getValueAt(Globals::animFrameTime,this->pos);
        this->scale = transformAnim->scale->getValueAt(Globals::animFrameTime,this->scale);

        //QVector3D r = transformAnim->rot->getValueAt(Globals::animFrameTime,this->rot.toEulerAngles());
        //this->rot = QQuaternion::fromEulerAngles(r.y(),r.x(),r.z());
        this->rot = transformAnim->rot->getValueAt(Globals::animFrameTime,this->rot);

        this->_updateTransform();
    }

    //children too
    for(unsigned int i=0;i<children.size();i++)
    {
        children[i]->updateAnimation(dt);
    }
}

void SceneNode::applyAnimationAtTime(float time)
{
    if(transformAnim!=nullptr)
    {
        //float t = time;
        float t = time-animStartTime;
        if(t>0)
        {
            this->pos = transformAnim->pos->getValueAt(t,this->pos);
            this->scale = transformAnim->scale->getValueAt(t,this->scale);

            this->rot = transformAnim->rot->getValueAt(t,this->rot);

            updateCustomProperties(t);

            this->_updateTransform(false);
        }
    }

    //children too
    for(size_t i=0;i<children.size();i++)
    {
        children[i]->applyAnimationAtTime(time);
    }
}

void SceneNode::_updateTransform(bool updateChildren)
{
    transform->setTranslation(pos);
    transform->setScale3D(scale);
    transform->setRotation(QQuaternion::fromEulerAngles(rot.y(),rot.x(),rot.z()));

    if(updateChildren)
    {
        for(unsigned int i=0;i<children.size();i++)
        {
            children[i]->_updateTransform(updateChildren);
        }
    }

    if(nodeListener!=nullptr)
        nodeListener->onTransformUpdated(this);
}

void SceneNode::removeChild(SceneNode* child)
{
    auto iter = std::find(children.begin(),children.end(),child);
    if(iter!=children.end())
    {
        child->getEntity()->setParent((Qt3DCore::QNode*)nullptr);
        child->getAnimPathEntity()->setParent((Qt3DCore::QNode*)nullptr);
        children.erase(iter);
    }
    //else;
        //throw exception?
}

void SceneNode::removeFromParent()
{
    this->parent->removeChild(this);
}

//CREATORS
SceneNode* SceneNode::createCube(QString name)
{
    Qt3DCore::QEntity* ent = new Qt3DCore::QEntity();


    Qt3DExtras::QCuboidMesh* cubeMesh = new Qt3DExtras::QCuboidMesh();
    cubeMesh->setObjectName(name);
    ent->addComponent(cubeMesh);
    ent->addComponent(RenderLayers::defaultLayer);

    ent->setObjectName(name);

    auto node = new SceneNode(ent);
    node->scale = QVector3D(5,5,5);
    node->sceneNodeType = SceneNodeType::Cube;

    node->_updateTransform();
    return node;
}

SceneNode* SceneNode::createTorus(QString name)
{
    Qt3DCore::QEntity* ent = new Qt3DCore::QEntity();

    Qt3DExtras::QTorusMesh* torus = new Qt3DExtras::QTorusMesh();
    torus->setRadius(1.0f);
    torus->setMinorRadius(0.4f);
    torus->setRings(100);
    torus->setSlices(20);
    torus->setObjectName(name);
    ent->addComponent(torus);
    ent->addComponent(RenderLayers::defaultLayer);

    ent->setObjectName(name);

    auto node = new SceneNode(ent);
    node->sceneNodeType = SceneNodeType::Torus;
    return node;
}

SceneNode* SceneNode::createSphere(QString name)
{
    Qt3DCore::QEntity* ent = new Qt3DCore::QEntity();

    Qt3DExtras::QSphereMesh* sphere = new Qt3DExtras::QSphereMesh();
    sphere->setRadius(0.5f);

    sphere->setObjectName(name);
    ent->addComponent(sphere);
    ent->addComponent(RenderLayers::defaultLayer);

    ent->setObjectName(name);

    auto node = new SceneNode(ent);
    node->sceneNodeType = SceneNodeType::Sphere;
    return node;
}

SceneNode* SceneNode::loadModel(QString name,QString modelPath,QString texPath)
{
    Qt3DCore::QEntity* ent = new Qt3DCore::QEntity();

    //MESH
    Qt3DRender::QMesh* mesh = new Qt3DRender::QMesh();
    mesh->setSource(QUrl::fromLocalFile(modelPath));

    mesh->setObjectName(name);
    ent->addComponent(mesh);
    ent->addComponent(RenderLayers::defaultLayer);

    //auto mat = new Qt3DExtras::QPhongMaterial();

    //MATERIAL
    Qt3DExtras::QDiffuseMapMaterial *diffuseMapMaterial = new Qt3DExtras::QDiffuseMapMaterial();
    diffuseMapMaterial->setSpecular(QColor::fromRgbF(0.2f, 0.2f, 0.2f, 1.0f));
    diffuseMapMaterial->setShininess(2.0f);

    Qt3DRender::QTextureImage *chestDiffuseImage = new Qt3DRender::QTextureImage();
    chestDiffuseImage->setSource(QUrl::fromLocalFile(texPath));
    diffuseMapMaterial->diffuse()->addTextureImage(chestDiffuseImage);

    ent->addComponent(diffuseMapMaterial);

    ent->setObjectName(name);

    auto node = new SceneNode(ent);
    node->sceneNodeType = SceneNodeType::Mesh;
    return node;
}

SceneNode* SceneNode::createEmpty()
{
    auto node = new SceneNode(new Qt3DCore::QEntity());
    node->sceneNodeType = SceneNodeType::Empty;
    return node;
}

LightNode::LightNode(Qt3DCore::QEntity* entity):
    LightNode(entity,LightType::Point)
{
    sceneNodeType = SceneNodeType::Light;
}

LightNode::LightNode(Qt3DCore::QEntity* entity,LightType type):
    SceneNode(entity)
{
    using namespace Qt3DRender;

    sceneNodeType = SceneNodeType::Light;
    _showGizmo = false;

    light = nullptr;

    this->setLightType(type);
    this->light->setColor(QColor::fromRgb(255,255,255));
    this->light->setIntensity(1);

    this->entity->addComponent(light);
    this->entity->addComponent(RenderLayers::billboardLayer);

    auto bill = new Billboard();
    auto mat = new BillboardMaterial();

    //auto tex = new Qt3DRender::QTextureLoader();
    //tex->setSource(QUrl::fromLocalFile(":/assets/textures/bulb.png"));
    //tex->setSource(QUrl::fromLocalFile(":/app/icons/bulb.png"));
    mat->setTexture(TextureHelper::loadTexture(":/app/icons/bulb.png"));

    this->entity->addComponent(bill);
    this->entity->addComponent(mat);

    lightAnim = new LightKeyFrameAnimation();
}

void LightNode::setLightType(LightType type)
{
    using namespace Qt3DRender;

    //null light means its not attached as a component as yet
    if(light != nullptr)
        entity->removeComponent(light);

    //auto point = new QPointLight();
    lightType = type;

    switch(type)
    {
        case LightType::Point:
            light = new QPointLight();
            break;

        case LightType::Directional:
        {
            auto dirLight = new QDirectionalLight();
            dirLight->setWorldDirection(QVector3D(0,-1,0));
            light = dirLight;
        }
            break;

        case LightType::SpotLight:
            light = new QSpotLight();
            break;
    }

}

QColor LightNode::getColor()
{
    return light->color();
}

void LightNode::setColor(QColor color)
{
    light->setColor(color);
}

void LightNode::setIntensity(float intensity)
{
    light->setIntensity(intensity);
}

float LightNode::getIntensity()
{
    return light->intensity();
}

void LightNode::_updateTransform(bool updateChildren)
{
    SceneNode::_updateTransform(updateChildren);

    if(lightType == LightType::Directional)
    {
        auto l = static_cast<Qt3DRender::QDirectionalLight*>(light);
        auto defaultDir = QVector3D(0,-1,0);

        //auto dir = rot*defaultDir;
        auto dir = QQuaternion::fromEulerAngles(rot.y(),rot.x(),rot.z())*defaultDir;

        dir.normalize();
        l->setWorldDirection(dir);
    }

    if(lightType == LightType::SpotLight)
    {
        auto l = static_cast<Qt3DRender::QSpotLight*>(light);
        auto defaultDir = QVector3D(0,-1,0);

        auto dir = rot*defaultDir;
        dir.normalize();
        l->setLocalDirection(dir);
    }


}

void LightNode::updateCustomProperties(float t)
{
    this->setIntensity(this->lightAnim->intensity->getValueAt(t,this->getIntensity()));
    this->setColor(this->lightAnim->color->getValueAt(t,this->getColor()));
}

void LightNode::writeData(SceneParser& parser,QXmlStreamWriter& xml)
{
    Q_UNUSED(parser);
    Q_UNUSED(xml);

    if(this->lightType == LightType::Point)
        xml.writeAttribute("lighttype","point");
    else if(this->lightType == LightType::Point)
        xml.writeAttribute("lighttype","spot");
    else
        xml.writeAttribute("lighttype","directional");
}

void LightNode::readData(SceneParser& parser,QXmlStreamReader& xml)
{
    Q_UNUSED(parser);

    //bool ok = false;
    //float val;

    auto lightType = xml.attributes().value("lighttype").toString();
    if(lightType=="point")
    {
        this->setLightType(LightType::Point);
    }
    else if(lightType=="spot")
    {
        this->setLightType(LightType::SpotLight);
    }
    else
    {
        this->setLightType(LightType::Directional);
    }

    //ensures lights are oriented properly
    _updateTransform();

}

SceneNode* LightNode::duplicate()
{
    Qt3DCore::QEntity* ent = new Qt3DCore::QEntity();
    LightNode* light = new LightNode(ent,LightType::Point);

    light->setLightType(this->lightType);
    light->setIntensity(this->getIntensity());
    light->setColor(this->getColor());

    //todo: find a better way to copy transforms
    light->pos = this->pos;
    light->rot = this->rot;
    light->scale = this->scale;

    light->_updateTransform();

    return light;
}

LightNode* LightNode::createPointLight()
{
    Qt3DCore::QEntity* ent = new Qt3DCore::QEntity();
    LightNode* light = new LightNode(ent,LightType::Point);

    return light;
}

LightNode* LightNode::createLight(QString name,LightType type)
{
    Qt3DCore::QEntity* ent = new Qt3DCore::QEntity();
    LightNode* light = new LightNode(ent,type);
    light->name = name;

    return light;
}


/* SHAPES */

TorusNode::TorusNode(Qt3DCore::QEntity* entity):
    SceneNode(entity)
{
    sceneNodeType = SceneNodeType::Torus;

    torus = new Qt3DExtras::QTorusMesh();
    torus->setRadius(1.0f);
    torus->setMinorRadius(0.4f);
    torus->setRings(100);
    torus->setSlices(20);

    entity->addComponent(torus);
    entity->addComponent(RenderLayers::defaultLayer);
}

TorusNode* TorusNode::createTorus(QString name)
{
    Qt3DCore::QEntity* ent = new Qt3DCore::QEntity();

    auto node = new TorusNode(ent);
    node->scale = QVector3D(4,4,4);
    node->name = name;
    return node;
}

void TorusNode::setRadius(float radius)
{
    torus->setRadius(radius);
}

void TorusNode::setMinorRadius(float minorRadius)
{
    torus->setMinorRadius(minorRadius);
}

void TorusNode::setRings(int rings)
{
    torus->setRings(rings);
}

void TorusNode::setSlices(int slices)
{
    torus->setSlices(slices);
}

void TorusNode::writeData(SceneParser& parser,QXmlStreamWriter& xml)
{
    Q_UNUSED(parser);
    Q_UNUSED(xml);

    xml.writeAttribute("minorradius",QString("%1").arg(torus->minorRadius()));
    xml.writeAttribute("rings",QString("%1").arg(torus->rings()));
    xml.writeAttribute("slices",QString("%1").arg(torus->slices()));
}

void TorusNode::readData(SceneParser& parser,QXmlStreamReader& xml)
{
    Q_UNUSED(parser);

    bool ok = false;
    float val;

    val = xml.attributes().value("minorradius").toFloat(&ok);
    if(ok) torus->setMinorRadius(val);

    val = xml.attributes().value("rings").toFloat(&ok);
    if(ok) torus->setRings(val);

    val = xml.attributes().value("slices").toFloat(&ok);
    if(ok) torus->setSlices(val);
}


SceneNode* TorusNode::duplicate()
{
    Qt3DCore::QEntity* ent = new Qt3DCore::QEntity();
    TorusNode* node = new TorusNode(ent);

    node->setRadius(this->torus->radius());
    node->setRings(this->torus->rings());
    node->setSlices(this->torus->slices());

    //todo: find a better way to copy transforms
    node->pos = this->pos;
    node->rot = this->rot;
    node->scale = this->scale;

    node->_updateTransform();

    return node;
}

SphereNode::SphereNode(Qt3DCore::QEntity* entity):
    SceneNode(entity)
{
    sceneNodeType = SceneNodeType::Sphere;

    sphere = new Qt3DExtras::QSphereMesh();
    sphere->setRadius(0.5f);
    sphere->setRings(32);
    sphere->setSlices(32);

    entity->addComponent(sphere);
    entity->addComponent(RenderLayers::defaultLayer);
}

SphereNode* SphereNode::createSphere(QString name)
{
    Qt3DCore::QEntity* ent = new Qt3DCore::QEntity();

    auto node = new SphereNode(ent);
    node->scale = QVector3D(4,4,4);
    node->name = name;

    return node;
}
void SphereNode::writeData(SceneParser& parser,QXmlStreamWriter& xml)
{
    Q_UNUSED(parser);

    xml.writeAttribute("rings",QString("%1").arg(sphere->rings()));
    xml.writeAttribute("slices",QString("%1").arg(sphere->slices()));
}

void SphereNode::readData(SceneParser& parser,QXmlStreamReader& xml)
{
    Q_UNUSED(parser);

    bool ok = false;
    float val;

    val = xml.attributes().value("rings").toFloat(&ok);
    if(ok) sphere->setRings(val);

    val = xml.attributes().value("slices").toFloat(&ok);
    if(ok) sphere->setSlices(val);
}

SceneNode* SphereNode::duplicate()
{
    Qt3DCore::QEntity* ent = new Qt3DCore::QEntity();
    SphereNode* node = new SphereNode(ent);

    node->pos = this->pos;
    node->rot = this->rot;
    node->scale = this->scale;

    node->_updateTransform();

    return node;
}

//PLANE
TexturedPlaneNode::TexturedPlaneNode(Qt3DCore::QEntity* entity):
    SceneNode(entity)
{
    sceneNodeType = SceneNodeType::TexturedPlane;

    plane = new Qt3DExtras::QPlaneMesh();
    entity->addComponent(plane);
    entity->addComponent(RenderLayers::defaultLayer);

    //remove default material
    //entity->removeComponent(mat);
    //texMat = nullptr;
    //_usesMaterial = false;
}

TexturedPlaneNode* TexturedPlaneNode::createTexturedPlane(QString name)
{
    Qt3DCore::QEntity* ent = new Qt3DCore::QEntity();

    auto node = new TexturedPlaneNode(ent);
    node->name = name;
    return node;
}

void TexturedPlaneNode::writeData(SceneParser& parser,QXmlStreamWriter& xml)
{
    Q_UNUSED(parser);

    xml.writeAttribute("texture",texPath);
}

void TexturedPlaneNode::readData(SceneParser& parser,QXmlStreamReader& xml)
{
    Q_UNUSED(parser);

    auto val = xml.attributes().value("texture").toString();
    if(!val.isEmpty())
        setTexture(val);
}

void TexturedPlaneNode::setTexture(QString path)
{
    texPath = path;

    //rebuild material
    if(texMat!=nullptr)
    {
        entity->removeComponent(texMat);
        delete texMat;
    }

    texMat = new FlatShadedMaterial();

    auto tex = new Qt3DRender::QTextureLoader();
    tex->setSource(QUrl::fromLocalFile(path));
    //texMat->diffuse()->addTextureImage(tex);
    texMat->setTexture(tex);

    entity->addComponent(texMat);
}

//CYLINDER
CylinderNode::CylinderNode(Qt3DCore::QEntity* entity):
    SceneNode(entity)
{
    sceneNodeType = SceneNodeType::Cylinder;

    cylinder = new Qt3DExtras::QCylinderMesh();
    cylinder->setRadius(0.5f);
    cylinder->setRings(5);
    cylinder->setSlices(32);

    entity->addComponent(cylinder);
    entity->addComponent(RenderLayers::defaultLayer);
}

CylinderNode* CylinderNode::createCylinder(QString name)
{
    Qt3DCore::QEntity* ent = new Qt3DCore::QEntity();

    auto node = new CylinderNode(ent);
    node->scale = QVector3D(4,4,4);
    node->name = name;
    return node;
}

void CylinderNode::writeData(SceneParser& parser,QXmlStreamWriter& xml)
{
    Q_UNUSED(parser);

    xml.writeAttribute("rings",QString("%1").arg(cylinder->rings()));
    xml.writeAttribute("slices",QString("%1").arg(cylinder->slices()));
}

void CylinderNode::readData(SceneParser& parser,QXmlStreamReader& xml)
{
    Q_UNUSED(parser);

    bool ok = false;
    float val;

    val = xml.attributes().value("rings").toFloat(&ok);
    if(ok) cylinder->setRings(val);

    val = xml.attributes().value("slices").toFloat(&ok);
    if(ok) cylinder->setSlices(val);
}

SceneNode* CylinderNode::duplicate()
{
    Qt3DCore::QEntity* ent = new Qt3DCore::QEntity();
    auto node = new CylinderNode(ent);

    //
    node->pos = this->pos;
    node->rot = this->rot;
    node->scale = this->scale;

    node->_updateTransform();

    return node;
}


//MESH
MeshNode::MeshNode(Qt3DCore::QEntity* entity):
    SceneNode(entity)
{
    sceneNodeType = SceneNodeType::Mesh;

    mesh = new Qt3DRender::QMesh();

    entity->addComponent(mesh);
    entity->addComponent(RenderLayers::defaultLayer);
}

void MeshNode::setSource(QString path)
{
    mesh->setSource(QUrl::fromLocalFile(path));
    meshPath = path;
}

QString MeshNode::getSource()
{
    return meshPath;
}

MeshNode* MeshNode::createMesh(QString name)
{
    auto node = new MeshNode(new Qt3DCore::QEntity());
    node->name = name;
    return node;
}

MeshNode* MeshNode::loadMesh(QString name,QString path)
{
    Qt3DCore::QEntity* ent = new Qt3DCore::QEntity();

    ent->setObjectName(name);

    auto node = new MeshNode(ent);
    node->setSource(path);
    return node;
}

SceneNode* MeshNode::duplicate()
{
    Qt3DCore::QEntity* ent = new Qt3DCore::QEntity();

    auto node = new MeshNode(ent);
    node->setSource(this->getSource());
    node->setMaterial(this->getMaterial()->duplicate());

    //
    node->pos = this->pos;
    node->rot = this->rot;
    node->scale = this->scale;

    node->_updateTransform();

    return node;
}

void MeshNode::writeData(SceneParser& parser,QXmlStreamWriter& xml)
{
    xml.writeAttribute("mesh",parser.getRelativePath(this->getSource()));
}

void MeshNode::readData(SceneParser& parser,QXmlStreamReader& xml)
{
    //bool ok = false;
    //float val;

    auto stringval = xml.attributes().value("mesh").toString();
    if(!stringval.isEmpty())
    {
        auto path = parser.getAbsolutePath(stringval);
        this->setSource(path);
    }
}

WorldNode::WorldNode(Qt3DCore::QEntity* entity):
    SceneNode(entity)
{
    this->sceneNodeType = SceneNodeType::World;

    removable = false;
    this->_usesMaterial = false;
    this->name = "World";
    _showGizmo = false;

    groundVisible = false;
    grid = new GridEntity(entity);
    gridVisible = true;
    hideGrid();

    sky = SkyBoxNode::createSky();
    sky->getEntity()->setParent(entity);
}

EndlessPlaneNode* WorldNode::getGround()
{
    return ground;
}

void WorldNode::showGround()
{
    ground->getEntity()->setParent(entity);
    groundVisible = true;
}

void WorldNode::hideGround()
{
    ground->getEntity()->setParent(static_cast<Qt3DCore::QEntity*>(nullptr));
    groundVisible = false;
}

bool WorldNode::isGroundVisible()
{
    return groundVisible;
}

void WorldNode::showGrid()
{
    gridVisible = true;
    grid->setParent(entity);
}

void WorldNode::hideGrid()
{
    grid->setParent(static_cast<Qt3DCore::QEntity*>(nullptr));
    gridVisible = false;
}

bool WorldNode::isGridVisible()
{
    return gridVisible;
}

void WorldNode::writeData(SceneParser& parser,QXmlStreamWriter& xml)
{
    xml.writeAttribute("showgrid",gridVisible==true?"true":"false");
    xml.writeAttribute("skytexture",parser.getRelativePath(sky->getTexture()));
    xml.writeAttribute("skytype","equirectangular");
}

void WorldNode::readData(SceneParser& parser,QXmlStreamReader& xml)
{
    //bool ok = false;
    //float val;

    //i'll do something with this soon
    auto stringval = xml.attributes().value("skytype").toString();

    //true is default
    if(xml.attributes().value("showgrid").toString()=="false")
        this->hideGrid();
    else
        this->showGrid();

    stringval = xml.attributes().value("skytexture").toString();
    if(!stringval.isEmpty())
    {
        auto path = parser.getAbsolutePath(stringval);
        sky->setTexture(path);
    }
}

UserCameraNode::UserCameraNode(Qt3DCore::QEntity* entity):
    SceneNode(entity)
{
    this->sceneNodeType = SceneNodeType::UserCamera;
    this->name = "ViewPoint";
    this->removable = true;
    this->_usesMaterial = false;

    auto eyePos = QVector3D(0,0.43f,0);

    //camera
    cam = new Qt3DRender::QCamera(entity);
    cam->setPosition(eyePos);
    cam->setUpVector(QVector3D(0, 1, 0));
    cam->setViewCenter(QVector3D(0, 0, -1));

    //todo: set this up from mainwindow
    cam->lens()->setPerspectiveProjection(45.0f, 800/480.0f, 0.1f, 1000.0f);
    entity->addComponent(RenderLayers::defaultLayer);

    //head
    headEnt = new Qt3DCore::QEntity(entity);

    auto mesh = new Qt3DRender::QMesh(entity);
    //mesh->setSource(QUrl("qrc:/assets/models/head.obj"));
    mesh->setSource(QUrl::fromLocalFile(":/app/models/head.obj"));
    headEnt->addComponent(mesh);

    auto headTrans = new Qt3DCore::QTransform();
    headTrans->setTranslation(eyePos);
    headTrans->setScale(2);
    headEnt->addComponent(headTrans);

    auto blue = new Qt3DExtras::QDiffuseMapMaterial();
    blue->setAmbient(QColor::fromRgbF(1.0f,1.0f,1.0f));
    blue->diffuse()->addTextureImage(TextureHelper::loadTexture(":/app/models/head.png"));
    headEnt->addComponent(blue);

    //body
    bodyEnt = new Qt3DCore::QEntity(entity);

    _showGizmo = false;
}

Qt3DRender::QCamera* UserCameraNode::getCamera()
{
    return cam;
}

void UserCameraNode::showBody()
{
    headEnt->setEnabled(true);
    bodyEnt->setEnabled(true);
}

void UserCameraNode::hideBody()
{
    headEnt->setEnabled(false);
    bodyEnt->setEnabled(false);
}

void UserCameraNode::writeData(SceneParser& parser,QXmlStreamWriter& xml)
{
    Q_UNUSED(parser);
    Q_UNUSED(xml);
}

void UserCameraNode::readData(SceneParser& parser,QXmlStreamReader& xml)
{
    //bool ok = false;
    //float val;

    Q_UNUSED(parser);
    Q_UNUSED(xml);
}

//ENDLESS PLANE
EndlessPlaneNode::EndlessPlaneNode(Qt3DCore::QEntity* entity):
    SceneNode(entity)
{
    sceneNodeType = SceneNodeType::EndlessPlane;
    textureScale = 0.1f;

    planeMesh = new Qt3DExtras::QPlaneMesh();
    planeMesh->setMeshResolution(QSize(2,2));
    planeMesh->setWidth(10000);
    planeMesh->setHeight(10000);
    entity->addComponent(planeMesh);

    mat->setTextureScale(1000.0f);
    _showGizmo = false;
}

void EndlessPlaneNode::setTextureScale(float scale)
{
    Q_UNUSED(scale);
}

float EndlessPlaneNode::getTextureScale()
{
    return this->textureScale;
}

void EndlessPlaneNode::setTexture(QString path)
{
    mat->setDiffuseTexture(TextureHelper::loadTexture(path));

    //texMat->setTextureScale(textureScale);
}

//todo: remove
QString EndlessPlaneNode::getTexture()
{
    //if(texMat==nullptr)
    //    return "";

    return texPath;
}

void EndlessPlaneNode::writeData(SceneParser& parser,QXmlStreamWriter& xml)
{
    Q_UNUSED(parser);

    xml.writeAttribute("texture",texPath);
    xml.writeAttribute("texturescale",QString("%1").arg(textureScale));
}


void EndlessPlaneNode::readData(SceneParser& parser,QXmlStreamReader& xml)
{
    Q_UNUSED(parser);

    bool ok = false;
    float val;

    auto stringval = xml.attributes().value("texture").toString();
    if(!stringval.isEmpty())
        setTexture(stringval);

    val = xml.attributes().value("texturescale").toFloat(&ok);
    if(ok) textureScale = val;
}

EndlessPlaneNode* EndlessPlaneNode::createEndlessPlane(QString name)
{
    Qt3DCore::QEntity* ent = new Qt3DCore::QEntity();

    auto node = new EndlessPlaneNode(ent);
    node->name = name;
    return node;
}
