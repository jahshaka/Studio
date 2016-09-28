/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCENENODE_H
#define SCENENODE_H

#include "QString"
#include <vector>
#include <QVector3D>
#include <QQuaternion>
#include <QColor>

#include "../core/keyframes.h"
//#incluee "sceneparser.h"


class NodeKeyFrameAnimation;
class NodeKeyFrame;

class QXmlStreamWriter;
class QXmlStreamReader;

class QColor;
class FlatShadedMaterial;
class EndlessPlaneMaterial;
class EndlessPlane;
class SceneManager;
class EndlessPlaneNode;
class LineRenderer;
class SkyBoxNode;
class AdvanceMaterial;

namespace Qt3DCore
{
    class QEntity;
    class QTransform;
}

namespace Qt3DRender
{
    class QAbstractLight;
    class QMesh;
    class QMaterial;
    class QCamera;
}

namespace Qt3DExtras
{
    class QSphereMesh;
    class QCylinderMesh;
    class QPlaneMesh;
    class QTorusMesh;
    class QDiffuseMapMaterial;
}

enum class SceneNodeType
{
    Empty,
    Mesh,
    Cube,
    Sphere,
    TexturedPlane,
    Torus,
    Light,
    Cylinder,
    World,
    UserCamera,
    EndlessPlane
};

enum class MaterialType:int
{
    None = 0,
    Default = 1,
    DiffuseMap = 2,
    DiffuseSpecularMap = 3,
    NormalDiffuseMap = 4,
    Reflection = 5,
    Refraction = 6,
    Advanced = 7
};

class ColorMaterial;
class LineStrip;
class SceneNode;
class GridEntity;
class SceneParser;

class ISceneNodeListener
{
public:
    virtual void onTransformUpdated(SceneNode* node) = 0;
};

class SceneNode
{
protected:
    Qt3DCore::QEntity* entity;
    SceneNode* parent;
    Qt3DCore::QTransform* transform;
    QString name;
public:
    SceneManager* sceneMan;

    std::vector<SceneNode*> children;

    QVector3D pos;
    //QQuaternion rot;
    //use euler instead of quaternion for internal rot
    QVector3D rot;
    //QVector3D rotAngles;
    QVector3D scale;

    bool allowRot;
    bool allowMove;
    bool allowScale;

    SceneNodeType sceneNodeType;//should be protected

    AdvanceMaterial* mat;
    MaterialType matType;

    bool removable;
    bool _usesMaterial;
    bool _showGizmo;
    bool visible;

    //key frames
    TransformKeyFrameAnimation* transformAnim;
    MaterialKeyFrameAnimation* materialAnim;
    float animStartTime;
    float animLength;
    bool loopAnim;

    //line renderer for keyframes
    LineRenderer* line;
    ColorMaterial* lineMat;
    Qt3DCore::QEntity* lineEnt;
    bool showAnimPath;

    ISceneNodeListener* nodeListener;

    void updateFogParams(bool enabled,QColor color,float start,float end);

    void setListener(ISceneNodeListener* nodeListener)
    {
        this->nodeListener = nodeListener;
    }

    Qt3DCore::QEntity* getAnimPathEntity()
    {
        return lineEnt;
    }

    void setAnimStartTime(float time)
    {
        animStartTime = time;
    }
    float getAnimStartTime()
    {
        return animStartTime;
    }

    bool hasTransformAnimation()
    {
        return transformAnim!=nullptr;
    }

    bool hasMaterialAnimation()
    {
        return materialAnim!=nullptr;
    }

    bool usesMaterial()
    {
        return _usesMaterial;
    }

    void setShowAnimPath(bool shouldShow)
    {
        showAnimPath = shouldShow;
    }

    bool getShowAnimPath()
    {
        return showAnimPath;
    }

    bool shouldShowGizmo()
    {
        return _showGizmo;
    }

    Qt3DCore::QTransform* getTransform()
    {
        return transform;
    }

    void setAnimPath(std::vector<QVector3D> points);
    void updateAnimPathFromKeyFrames();

    SceneNodeType getSceneNodeType();

    explicit SceneNode(Qt3DCore::QEntity* entity);
    ~SceneNode();
    void addChild(SceneNode* child);
    void setSceneManager(SceneManager* scene);

    Qt3DCore::QEntity* getEntity();
    SceneNode* getParent()
    {
        return parent;
    }

    bool isVisible()
    {
        return visible;
    }

    bool isRemovable()
    {
        return removable;
    }

    QVector3D getScale()
    {
        return scale;
    }

    QString getName()
    {
        return name;
    }

    void setName(QString name)
    {
        this->name = name;
    }

    void show();

    void hide();

    void removeChild(SceneNode* child);
    void removeFromParent();

    void setMaterial(AdvanceMaterial* mat);
    AdvanceMaterial* getMaterial(){return mat;}

    virtual void _updateTransform(bool updateChildren = false);

    void update(float dt);
    void updateAnimation(float dt);
    void applyAnimationAtTime(float seconds);
    virtual void updateCustomProperties(float t)
    {
        Q_UNUSED(t);
    }

    //serialization and deserialization
    virtual void writeData(SceneParser& parser,QXmlStreamWriter& xml)
    {
        Q_UNUSED(parser);
        Q_UNUSED(xml);
    }
    virtual void readData(SceneParser& parser,QXmlStreamReader& xml)
    {
        Q_UNUSED(parser);
        Q_UNUSED(xml);
    }

    virtual SceneNode* duplicate()
    {
        return nullptr;
    }

    virtual bool isDuplicable()
    {
        return false;
    }

    static SceneNode* createCube(QString name);
    static SceneNode* createTorus(QString name);
    static SceneNode* createSphere(QString name);
    static SceneNode* loadModel(QString name,QString modelPath,QString texPath);
    static SceneNode* createEmpty();

};

//todo
class CameraNode:SceneNode
{
public:

};

/**
 * @brief The LightType enum
 */
enum class LightType
{
    Point,
    SpotLight,
    Directional
};


class LightNode:public SceneNode
{
    LightType lightType;
    Qt3DRender::QAbstractLight* light;
    Qt3DExtras::QSphereMesh* sphere;
public:
    LightKeyFrameAnimation* lightAnim;
    LightNode(Qt3DCore::QEntity* entity);
    LightNode(Qt3DCore::QEntity* entity,LightType type);

    void setLightType(LightType type);

    QColor getColor();
    void setColor(QColor color);
    void setIntensity(float intensity);
    float getIntensity();

    virtual void _updateTransform(bool updateChildren = false) override;
    virtual void updateCustomProperties(float t) override;

    static LightNode* createPointLight();
    static LightNode* createLight(QString name,LightType type);

    virtual void writeData(SceneParser& parser,QXmlStreamWriter& xml);
    virtual void readData(SceneParser& parser,QXmlStreamReader& xml);

    virtual SceneNode* duplicate() override;
    virtual bool isDuplicable()
    {
        return true;
    }
};

/* SHAPES */

class TorusNode:public SceneNode
{
public:
    Qt3DExtras::QTorusMesh* torus;

    TorusNode(Qt3DCore::QEntity* entity);

    void setRadius(float radius);
    void setMinorRadius(float minorRadius);
    void setRings(int rings);
    void setSlices(int slices);

    virtual void writeData(SceneParser& parser,QXmlStreamWriter& xml);
    virtual void readData(SceneParser& parser,QXmlStreamReader& xml);

    virtual SceneNode* duplicate() override;
    virtual bool isDuplicable()
    {
        return true;
    }

    static TorusNode* createTorus(QString name);
};

class SphereNode:public SceneNode
{
public:
    Qt3DExtras::QSphereMesh* sphere;

    SphereNode(Qt3DCore::QEntity* entity);

    void writeData(SceneParser& parser,QXmlStreamWriter& xml) override;
    void readData(SceneParser& parser,QXmlStreamReader& xml) override;


    virtual SceneNode* duplicate() override;
    virtual bool isDuplicable()
    {
        return true;
    }

    static SphereNode* createSphere(QString name);
};

class CylinderNode:public SceneNode
{
public:
    Qt3DExtras::QCylinderMesh* cylinder;

    CylinderNode(Qt3DCore::QEntity* entity);

    void writeData(SceneParser& parser,QXmlStreamWriter& xml) override;
    void readData(SceneParser& parser,QXmlStreamReader& xml) override;


    virtual SceneNode* duplicate() override;
    virtual bool isDuplicable()
    {
        return true;
    }

    static CylinderNode* createCylinder(QString name);
};

class TexturedPlaneNode:public SceneNode
{
public:
    Qt3DExtras::QPlaneMesh* plane;
    //Qt3DRender::QDiffuseMapMaterial* texMat;
    FlatShadedMaterial* texMat;
    QString texPath;

    TexturedPlaneNode(Qt3DCore::QEntity* entity);
    void setTexture(QString tex);

    void writeData(SceneParser& parser,QXmlStreamWriter& xml) override;
    void readData(SceneParser& parser,QXmlStreamReader& xml) override;

    /*
    virtual SceneNode* duplicate() override;
    virtual bool isDuplicable()
    {
        return true;
    }
    */

    static TexturedPlaneNode* createTexturedPlane(QString name);
};

class MeshNode:public SceneNode
{
    QString meshPath;
public:
    Qt3DRender::QMesh* mesh;

    MeshNode(Qt3DCore::QEntity* entity);
    void setSource(QString mesh);
    QString getSource();

    void writeData(SceneParser& parser,QXmlStreamWriter& xml) override;
    void readData(SceneParser& parser,QXmlStreamReader& xml) override;

    static MeshNode* loadMesh(QString name,QString path);
    static MeshNode* createMesh(QString name);

    virtual SceneNode* duplicate() override;
    virtual bool isDuplicable()
    {
        return true;
    }
};

class WorldNode:public SceneNode
{
    bool groundVisible;
    bool gridVisible;
public:
    SceneKeyFrameAnimation* sceneAnim;
    //QColor sceneColor;
    EndlessPlaneNode* ground;
    SkyBoxNode* sky;
    WorldNode(Qt3DCore::QEntity* entity);
    EndlessPlaneNode* getGround();
    GridEntity* grid;

    SkyBoxNode* getSky()
    {
        return sky;
    }

    void showGround();
    void hideGround();
    bool isGroundVisible();

    void showGrid();
    void hideGrid();
    bool isGridVisible();

    void writeData(SceneParser& parser,QXmlStreamWriter& xml) override;
    void readData(SceneParser& parser,QXmlStreamReader& xml) override;
};

class UserCameraNode:public SceneNode
{
    Qt3DRender::QCamera* cam;
    Qt3DCore::QEntity* headEnt;
    Qt3DCore::QEntity* bodyEnt;
public:
    UserCameraNode(Qt3DCore::QEntity* entity);
    Qt3DRender::QCamera* getCamera();

    void writeData(SceneParser& parser,QXmlStreamWriter& xml) override;
    void readData(SceneParser& parser,QXmlStreamReader& xml) override;

    void showBody();
    void hideBody();
};

class EndlessPlaneNode:public SceneNode
{
public:
    QString texPath;
    float textureScale;
    Qt3DExtras::QPlaneMesh* planeMesh;

    EndlessPlaneNode(Qt3DCore::QEntity* entity);
    void setTexture(QString tex);
    QString getTexture();

    float getTextureScale();
    void setTextureScale(float scale);

    void writeData(SceneParser& parser,QXmlStreamWriter& xml) override;
    void readData(SceneParser& parser,QXmlStreamReader& xml) override;

    static EndlessPlaneNode* createEndlessPlane(QString name);
};

#endif // SCENENODE_H
