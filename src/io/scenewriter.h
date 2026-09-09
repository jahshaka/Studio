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
#include "services/assetcas.h"   // AssetCas::GuidPreference (the guid a path means)
#include "io/sceneformat.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
//#include "../irisgl/src/core/scenenode.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/animation/keyframeanimation.h"
#include "irisgl/irisglfwd.h"

class EditorData;
class Project;

class SceneWriter : public AssetIOBase
{
	// The live Project (Phase 4: was the Globals::project static). Static
	// because both project reads live in *static* writer methods
	// (writeParticleData / writeSceneNodeMaterial) that a dozen call sites
	// invoke unqualified, so there is no instance to hang it off. Wired once by
	// the shell in MainWindow::setupServices.
	//
	// A `static Database *handle` used to sit beside it (ENGINEERING_DEBT_SPEC
	// item 7). It was DEAD: `setDatabaseHandle` had no caller on a writer (all
	// seven were SceneReader / asset-panel objects with a setter of the same
	// name), so it stayed null for the process's whole life while two writer
	// paths dereferenced it — UB that only ever "worked" because
	// Database::fetchAssetGUIDByName touches no member. Those two sites call
	// it on the CLASS now (scenewriter.cpp), which is what they always meant,
	// and the member is gone.
	static Project *projectHandle;

	/// The base directory the STATIC writers relativize against.
	///
	/// AssetIOBase::dir is per instance now (it used to be one static QDir
	/// shared by every reader and writer in the process — the import worker's
	/// SceneReader rewrote it under the UI thread's). SceneWriter's write
	/// family is static for the same reason `projectHandle` is: a
	/// dozen call sites invoke it unqualified with no instance in sight
	/// ("SceneWriter statics", ENGINEERING_DEBT_SPEC). Until that debt is
	/// paid, the static writers keep their own base, published by
	/// getSceneObject() — the exact behaviour they had before, and no longer
	/// entangled with the readers'.
	static QDir staticRelativeBase;
	/// getRelativePath() for the static writers.
	static QString relativeToStaticBase(QString filename);
public:
	static void setProject(Project *p) {
		projectHandle = p;
	}
    /// The scene as bytes. There is deliberately NO write-to-file overload:
    /// a scene is stored as a blob in the projects table (one atomic UPDATE),
    /// and the file-writing one that used to sit here was dead code doing an
    /// unchecked truncate on the only copy of a world.
    QByteArray getSceneObject(QString projectPath,
                              iris::ScenePtr scene,
                              iris::PostProcessManagerPtr postMan,
                              EditorData *editorData);

public:
    void writeScene(QJsonObject& projectObj, iris::ScenePtr scene);
    void writePostProcessData(QJsonObject& projectObj, iris::PostProcessManagerPtr postMan);
    void writeEditorData(QJsonObject& projectObj, EditorData* ediorData = nullptr);

    /// A SUBTREE plus where it belongs (src/io/sceneformat.h). The unit undo
    /// v1.5 captures, `node.serialize` returns and a paste rebuilds.
    ///
    /// Cheap enough to run on every structural edit: it is a JSON walk over
    /// metadata the handles already hold — no mesh bytes, no textures, no
    /// database round trip (materials travel as asset guids exactly as they do
    /// in a scene file).
    static SceneFragment captureFragment(const iris::SceneNodePtr &node, bool relative = true);

    static void writeSceneNode(QJsonObject& sceneNodeObj, iris::SceneNodePtr node, bool relative = true);
	static void writeAnimationData(QJsonObject& sceneNodeObj, iris::SceneNodePtr node);
    static void writeMeshData(QJsonObject& sceneNodeObject, iris::MeshNodePtr node, bool relative = true);
	static void writeParticleData(QJsonObject& sceneNodeObject, iris::ParticleSystemNodePtr node);
	static void writeSceneNodeMaterial(QJsonObject& matObj, iris::MaterialPtr mat, bool relative = true);
    static void writeLightData(QJsonObject& sceneNodeObject, iris::LightNodePtr node);
    /// Decals (DECALS_SPEC): the IMAGE is stored as a guid, never a path — the
    /// reader resolves it pin-first through the CAS like every other asset.
    static void writeDecalData(QJsonObject& sceneNodeObject, iris::DecalNodePtr node);
    /// Scene-graph cameras (CAMERAS_SPEC §3). Nothing to do with the
    /// project's `editor.camera` block, which is the viewport's explorer and
    /// stays exactly where it is.
    static void writeCameraData(QJsonObject& sceneNodeObject, iris::CameraNodePtr node);

	static QJsonObject jsonColor(QColor color);
	static QJsonObject jsonVector2(iris::Vec2 vec);
	static QJsonObject jsonVector3(iris::Vec3 vec);
	static QJsonObject jsonVector4(iris::Vec4 vec);
	static QJsonObject jsonQuaternion(iris::Quat q);

    /// The asset guid behind a RESOLVED file path, for the writers that
    /// persist references as guids (particle emitters, material texture
    /// properties, skeletal-clip sources). Goes through the CAS oid first —
    /// since the store landed an object's file name is its sha256, so the old
    /// match-by-display-name found nothing and the reference was written as ""
    /// — then falls back to the legacy by-name lookup for files that still
    /// live in a project folder. Empty means "not a catalogued asset";
    /// callers decide what to write then.
    ///
    /// `prefer` says what the reference MEANS, because one stored object can
    /// back several assets: a texture map must ask for the Texture asset or it
    /// gets the model the texture was imported inside (AssetCas::
    /// GuidPreference — the GLB texture-loss defect, 2026-09-09).
    static QString assetGuidForTexturePath(
        const QString &path,
        AssetCas::GuidPreference prefer = AssetCas::GuidPreference::Texture);

    static QString getSceneNodeTypeName(iris::SceneNodeType nodeType);
	static QString getLightNodeTypeName(iris::LightType lightType);
	static QString getKeyTangentTypeName(iris::TangentType tangentType);
	static QString getKeyHandleModeName(iris::HandleMode handleMode);
};

#endif // SCENEWRITER_H
