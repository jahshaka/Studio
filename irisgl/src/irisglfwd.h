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
#include "core/logger.h"

namespace iris
{

class CameraNode;
class LightNode;
class ViewerNode;
class ParticleSystemNode;
class Mesh;
class Frustum;
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
class CustomMaterial;
class RenderItem;
class PickingResult;
class RenderTarget;
class Property;
class PostProcess;
class PostProcessContext;
class PostProcessManager;
class PropertyAnim;
class PropertyAnimInfo;
class FloatPropertyAnim;
class Vector3DPropertyAnim;
class ColorPropertyAnim;
class AnimableProperty;
class Bone;
class Skeleton;
class SkeletalAnimation;
template<typename T> class Key;
typedef Key<float> FloatKey;
class BoundingSphere;
class VertexBuffer;
class IndexBuffer;
class GraphicsDevice;
class ContentManager;
class SpriteBatch;
class Font;

typedef QSharedPointer<iris::Animation> AnimationPtr;
typedef QSharedPointer<Shader> ShaderPtr;
typedef QSharedPointer<Scene> ScenePtr;
typedef QSharedPointer<SceneNode> SceneNodePtr;
typedef QSharedPointer<Mesh> MeshPtr;
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
typedef QSharedPointer<CustomMaterial> CustomMaterialPtr;
typedef QSharedPointer<ViewerNode> ViewerNodePtr;
typedef QSharedPointer<ParticleSystemNode> ParticleSystemNodePtr;
typedef QSharedPointer<RenderTarget> RenderTargetPtr;
typedef QSharedPointer<PostProcess> PostProcessPtr;
typedef QSharedPointer<PostProcessManager> PostProcessManagerPtr;
typedef QSharedPointer<Bone> BonePtr;
typedef QSharedPointer<Skeleton> SkeletonPtr;
typedef QSharedPointer<SkeletalAnimation> SkeletalAnimationPtr;
typedef QSharedPointer<VertexBuffer> VertexBufferPtr;
typedef QSharedPointer<IndexBuffer> IndexBufferPtr;
typedef QSharedPointer<GraphicsDevice> GraphicsDevicePtr;
typedef QSharedPointer<ContentManager> ContentManagerPtr;
typedef QSharedPointer<SpriteBatch> SpriteBatchPtr;
typedef QSharedPointer<Font> FontPtr;



}

#endif // IRISGLFWD_H
