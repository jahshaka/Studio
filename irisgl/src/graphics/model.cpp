/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "../irisglfwd.h"
#include "model.h"
#include "material.h"

#include "assimp/postprocess.h"
#include "assimp/Importer.hpp"
#include "assimp/scene.h"
#include "assimp/mesh.h"

#include <QString>
#include <QFile>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions_3_2_Core>
#include <QOpenGLTexture>
#include <QtMath>

#include "graphicsdevice.h"
#include "vertexlayout.h"
#include "../geometry/trimesh.h"
#include "skeleton.h"
#include "../animation/skeletalanimation.h"
#include "../geometry/boundingsphere.h"
#include "../geometry/aabb.h"

#include <functional>

namespace iris
{

Model::Model(QList<MeshPtr> meshes)
{
	this->meshes = meshes;
	animTime = 0;
}

Model::Model(QList<MeshPtr> meshes, QMap<QString, SkeletalAnimationPtr> skeletalAnimations)
{
	this->meshes = meshes;
	this->skeletalAnimations = skeletalAnimations;
}

void Model::setSkeleton(const SkeletonPtr &value)
{
    skeleton = value;
}

bool Model::hasSkeleton()
{
    return !!skeleton;
}

SkeletonPtr Model::getSkeleton()
{
    return skeleton;
}

void Model::addSkeletalAnimation(QString name, SkeletalAnimationPtr anim)
{
    skeletalAnimations.insert(name, anim);
}

QMap<QString, SkeletalAnimationPtr> Model::getSkeletalAnimations()
{
    return skeletalAnimations;
}

bool Model::hasSkeletalAnimations()
{
    return skeletalAnimations.count() != 0;
}

void Model::updateAnimation(float dt)
{
	if (!!activeAnimation) {
		animTime += dt;
		float time = animTime;
		QMap<QString, QMatrix4x4> skeletonSpaceMatrices;
		// The skeleton begins at this node, the root

		// recursively update the animation for each node
		std::function<void(SkeletalAnimationPtr anim, SceneNodePtr node, QMatrix4x4 parentTransform)> animateHierarchy;
		animateHierarchy = [&animateHierarchy, time, &skeletonSpaceMatrices](SkeletalAnimationPtr anim, SceneNodePtr node, QMatrix4x4 parentTransform)
		{
			// skeleton-space transform of current node
			QMatrix4x4 skelTrans;
			skelTrans.setToIdentity();

			if (anim->boneAnimations.contains(node->name)) {
				auto boneAnim = anim->boneAnimations[node->name];

				node->pos = boneAnim->posKeys->getValueAt(time);
				node->rot = boneAnim->rotKeys->getValueAt(time).normalized();
				//node->scale = QVector3D(1,1,1);
				node->scale = boneAnim->scaleKeys->getValueAt(time);
			}

			auto localTrans = node->getLocalTransform(); //calculates the local transform matrix
			skelTrans = parentTransform * localTrans; //skeleton space transform
			skeletonSpaceMatrices.insert(node->name, skelTrans);

			for (auto child : node->children) {
				animateHierarchy(anim, child, skelTrans);
			}
		};

		QMatrix4x4 rootTransform;
		rootTransform.setToIdentity();
		animateHierarchy(activeAnimation, this->sharedFromThis(), rootTransform);

		applyAnimationPose(this->sharedFromThis(), skeletonSpaceMatrices);
	}
}

void Model::draw(GraphicsDevicePtr device)
{
	for (auto& mesh : meshes)
		mesh->draw(device);
}

Model::~Model()
{
}

}
