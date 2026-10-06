/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCENEWRITER_H
#define SCENEWRITER_H

#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include <QSharedPointer>
#include "io/assetiobase.h"
#include "io/sceneformat.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/animation/keyframeanimation.h"
#include "irisgl/irisglfwd.h"

class EditorData;

class SceneWriter : public AssetIOBase
{
public:
    /// The scene as bytes. There is deliberately NO write-to-file overload:
    /// a scene is stored as a blob in the projects table (one atomic UPDATE),
    /// and the file-writing one that used to sit here was dead code doing an
    /// unchecked truncate on the only copy of a world.
    QByteArray getSceneObject(QString projectPath,
                              iris::ScenePtr scene,
                              EditorData *editorData);

public:
    void writeScene(QJsonObject& projectObj, iris::ScenePtr scene);
    void writeEditorData(QJsonObject& projectObj, EditorData* ediorData = nullptr);

    /// A SUBTREE plus where it belongs (src/io/sceneformat.h). The unit undo
    /// v1.5 captures, `node.serialize` returns and a paste rebuilds.
    ///
    /// Cheap enough to run on every structural edit: it is a JSON walk over
    /// metadata the handles already hold — no mesh bytes, no textures, no
    /// database round trip (materials travel as asset guids exactly as they do
    /// in a scene file).
    static SceneFragment captureFragment(const iris::SceneNodePtr &node);

    static void writeSceneNode(QJsonObject& sceneNodeObj, iris::SceneNodePtr node);
	static void writeAnimationData(QJsonObject& sceneNodeObj, iris::SceneNodePtr node);
    static void writeMeshData(QJsonObject& sceneNodeObject, iris::MeshNodePtr node);
	static void writeParticleData(QJsonObject& sceneNodeObject, iris::ParticleSystemNodePtr node);
	static void writeSceneNodeMaterial(QJsonObject& matObj, iris::MaterialPtr mat);
    static void writeLightData(QJsonObject& sceneNodeObject, iris::LightNodePtr node);
    /// Decals (DECALS_SPEC): the IMAGE is stored as a guid, never a path — the
    /// reader resolves it pin-first through the CAS like every other asset.
    static void writeDecalData(QJsonObject& sceneNodeObject, iris::DecalNodePtr node);
    /// Scene-graph cameras (CAMERAS_SPEC §3). Nothing to do with the
    /// project's `editor.camera` block, which is the viewport's explorer and
    /// stays exactly where it is.
    static void writeCameraData(QJsonObject& sceneNodeObject, iris::CameraNodePtr node);

	static QJsonObject jsonVector2(iris::Vec2 vec);
	static QJsonObject jsonVector3(iris::Vec3 vec);
	static QJsonObject jsonVector4(iris::Vec4 vec);
	static QJsonObject jsonQuaternion(iris::Quat q);

    static QString getSceneNodeTypeName(iris::SceneNodeType nodeType);
	static QString getLightNodeTypeName(iris::LightType lightType);
	static QString getKeyTangentTypeName(iris::TangentType tangentType);
	static QString getKeyHandleModeName(iris::HandleMode handleMode);
};

#endif // SCENEWRITER_H
