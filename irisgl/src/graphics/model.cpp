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
	if (!!activeAnimation && !!skeleton) {
		animTime += dt;
		float time = animTime;
		skeleton->applyAnimation(activeAnimation, time);

		for (auto mesh : meshes) {
			// apply animations
			if (mesh->hasSkeleton()) {
				auto skel = mesh->getSkeleton();

				// should fail, calculate the transforms manually
				skel->applyAnimation(activeAnimation, time);
			}
		}
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
