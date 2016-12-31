#ifndef IRISGLFWD_H
#define IRISGLFWD_H

/* This class contains forward declarations of all classes in irisgl
 */

#include <QSharedPointer>
#include "animation/keyframeanimation.h"

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
class KeyFrameSet;
class Animation;

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
typedef QSharedPointer<Texture> TexturePtr;
typedef QSharedPointer<ForwardRenderer> ForwardRendererPtr;
typedef QSharedPointer<KeyFrameSet> KeyFrameSetPtr;
typedef QSharedPointer<FloatKeyFrame> FloatKeyFramePtr;
typedef QSharedPointer<Animation> AnimationPtr;


}

#endif // IRISGLFWD_H
