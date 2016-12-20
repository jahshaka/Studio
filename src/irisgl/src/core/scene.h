#ifndef SCENE_H
#define SCENE_H

#include <QList>
#include <QSharedPointer>
#include "../graphics/texture2d.h"
#include "../materials/defaultskymaterial.h"

namespace iris
{

class LightNode;
class SceneNode;
class Scene;
class CameraNode;
class Mesh;
class Material;

typedef QSharedPointer<iris::LightNode> LightNodePtr;
typedef QSharedPointer<iris::Scene> ScenePtr;
typedef QSharedPointer<iris::SceneNode> SceneNodePtr;
typedef QSharedPointer<iris::CameraNode> CameraNodePtr;
typedef QSharedPointer<iris::Material> MaterialPtr;

class Scene: public QEnableSharedFromThis<Scene>
{
public:
    CameraNodePtr camera;
    SceneNodePtr rootNode;
    QList<LightNodePtr> lights;

    Mesh* skyMesh;
    Texture2DPtr skyTexture;

    //should be MaterialPtr
    DefaultSkyMaterialPtr skyMaterial;

    //fog properties
    QColor fogColor;
    float fogStart;
    float fogEnd;
    bool fogEnabled;

    Scene();
public:
    static ScenePtr create();

    /**
     * Returns the scene's root node. A scene should always have a root node so it should be assumed
     * that the returned value is never null.
     * @return
     */
    SceneNodePtr getRootNode()
    {
        return rootNode;
    }

    void setSkyTexture(Texture2DPtr tex);
    QString getSkyTextureSource();
    void clearSkyTexture();


    void update(float dt);
    void render();

    /**
     * Adds node to scene. If node is a LightNode then it is added to a list of lights.
     * @param node
     */
    void addNode(SceneNodePtr node);

    /**
     *  Removes node from scene. If node is a LightNode then it is removed to a list of lights.
     * @param node
     */
    void removeNode(SceneNodePtr node);

    /**
     * Sets the active camera of the scene
     * @param cameraNode
     */
    void setCamera(CameraNodePtr cameraNode);
};

}


#endif // SCENE_H
