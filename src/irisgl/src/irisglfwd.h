#ifndef IRISGLFWD_H
#define IRISGLFWD_H

/* This class contains forward declarations of all classes in irisgl
 */

#include <QSharedPointer>

namespace iris
{

class CameraNode;
class LightNode;
class Mesh;
class Material;
class MeshNode;
class VrDevice;
class SceneNode;
class Texture2D;
class Texture;
class DefaultSkyMaterial;
class Scene;
class Shader;
class VertexLayout;
class TriMesh;
struct RenderData;
class Viewport;
class BillboardMaterial;
class Billboard;
class FullScreenQuad;
class DefaultMaterial;
class ForwardRenderer;

typedef QSharedPointer<Shader> ShaderPtr;
typedef QSharedPointer<Scene> ScenePtr;
typedef QSharedPointer<SceneNode> SceneNodePtr;
typedef QSharedPointer<Material> MaterialPtr;
typedef QSharedPointer<DefaultMaterial> DefaultMaterialPtr;
typedef QSharedPointer<LightNode> LightNodePtr;
typedef QSharedPointer<CameraNode> CameraNodePtr;
typedef QSharedPointer<MeshNode> MeshNodePtr;
typedef QSharedPointer<DefaultSkyMaterial> DefaultSkyMaterialPtr;
typedef QSharedPointer<Texture2D> Texture2DPtr;
typedef QSharedPointer<ForwardRenderer> ForwardRendererPtr;


}

#endif // IRISGLFWD_H
