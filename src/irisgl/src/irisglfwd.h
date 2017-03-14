/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef IRISGLFWD_H
#define IRISGLFWD_H

/* This class contains forward declarations of all classes in irisgl
 */

#include <QSharedPointer>
//#include "animation/keyframeanimation.h"

namespace iris
{

class CameraNode;
class LightNode;
class ViewerNode;
class ParticleSystemNode;
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
class FloatKeyFrame;
class ViewerMaterial;
class CustomMaterial;
class RenderItem;
class PickingResult;
template<typename T> class Key;
typedef Key<float> FloatKey;

typedef QSharedPointer<iris::Animation> AnimationPtr;
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
typedef QSharedPointer<ViewerMaterial> ViewerMaterialPtr;
typedef QSharedPointer<CustomMaterial> CustomMaterialPtr;
typedef QSharedPointer<ViewerNode> ViewerNodePtr;
typedef QSharedPointer<ParticleSystemNode> ParticleSystemNodePtr;



}

#endif // IRISGLFWD_H
