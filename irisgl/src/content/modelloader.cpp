#include "../irisglfwd.h"
#include "modelloader.h"
#include "graphics/model.h"
#include "../graphics/mesh.h"
#include "../graphics/skeleton.h"

#include "assimp/postprocess.h"
#include "assimp/Importer.hpp"
#include "assimp/scene.h"
#include "assimp/mesh.h"

#include <QFile>

namespace iris
{
ModelLoader::ModelLoader(GraphicsDevicePtr device)
{
	this->device = device;
}

// Extracts meshes and skeleton from scene
ModelPtr ModelLoader::load(QString filePath)
{
	// legacy -- update TODO
	Assimp::Importer importer;
	const aiScene *scene;

	QFile file(filePath);
	if (!file.exists())
	{
		irisLog("model " + filePath + " does not exists");
		return ModelPtr();
	}

	if (filePath.startsWith(":") || filePath.startsWith("qrc:")) {
		// loads mesh from resource
		file.open(QIODevice::ReadOnly);
		auto data = file.readAll();
		scene = importer.ReadFileFromMemory((void*)data.data(),
			data.length(),
			aiProcessPreset_TargetRealtime_Fast);
	}
	else {
		// load mesh from file
		scene = importer.ReadFile(filePath.toStdString().c_str(),
			aiProcessPreset_TargetRealtime_Fast);
	}

	if (!scene) {
		irisLog("model " + filePath + ": error parsing file");
		return ModelPtr();
	}

	if (scene->mNumMeshes <= 0) {
		irisLog("model " + filePath + ": scene has no meshes");
		return ModelPtr();
	}

	QList<MeshPtr> meshes;
	for (int i = 0; i < scene->mNumMeshes; i++) {
		auto mesh = scene->mMeshes[0];
		auto meshObj = MeshPtr(new Mesh(scene->mMeshes[0]));
		auto skel = Mesh::extractSkeleton(mesh, scene);

		if (!!skel)
			meshObj->setSkeleton(skel);

		meshes.append(meshObj);
	}

	auto skeleton = ModelLoader::extractSkeletonFromScene(scene);
	auto anims = Mesh::extractAnimations(scene);
	auto model = new Model(meshes, skeleton);
	for (auto animName : anims.keys())
	{
		model->addSkeletalAnimation(animName, anims[animName]);
	}
	
	return ModelPtr(model);
}

SkeletonPtr ModelLoader::extractSkeletonFromScene(aiScene* scene)
{
	auto skel = Skeleton::create();

	std::function<void(aiNode*)> evalChildren;
	evalChildren = [skel, &evalChildren](aiNode* node, BonePtr parentBone) {
		auto bone = Bone::create(QString(node->mName.C_Str()));

		skel->addBone(bone);
		if (!!parentBone)
			parentBone->addChild(bone);
		

		for (unsigned i = 0; i < parent->mNumChildren; i++)
		{
			auto childNode = parent->mChildren[i];
			evalChildren(childNode, parentBone);
		}
	};

	//auto bone = Bone::create(QString(meshBone->mName.C_Str()));
	evalChildren(scene->mRootNode, BonePtr());
}

}