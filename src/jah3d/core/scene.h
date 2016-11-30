#ifndef SCENE_H
#define SCENE_H

#include <QList>
#include <QSharedPointer>
#include "../graphics/texture2d.h"
#include "../materials/defaultskymaterial.h"
//#include "scenenode.h"
//#include "../scenegraph/cameranode.h"
//#include "../scenegraph/meshnode.h"

namespace jah3d
{

class LightNode;
class SceneNode;
class Scene;
class CameraNode;
class Mesh;
class Material;

typedef QSharedPointer<jah3d::LightNode> LightNodePtr;
//typedef QSharedPointer<SceneNode> SceneNodePtr;
typedef QSharedPointer<jah3d::Scene> ScenePtr;
typedef QSharedPointer<jah3d::SceneNode> SceneNodePtr;
typedef QSharedPointer<jah3d::CameraNode> CameraNodePtr;
typedef QSharedPointer<jah3d::Material> MaterialPtr;

class Scene:public QEnableSharedFromThis<Scene>
{
private:

    //QList<MeshNodePtr> meshes;

public:
    CameraNodePtr camera;
    SceneNodePtr rootNode;
    QList<LightNodePtr> lights;

    Mesh* skyMesh;
    Texture2DPtr skyTexture;

    //should be MaterialPtr
    DefaultSkyMaterialPtr skyMaterial;

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
