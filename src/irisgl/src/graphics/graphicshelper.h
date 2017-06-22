/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef GRAPHICSHELPER_H
#define GRAPHICSHELPER_H

#include <QString>
#include <QList>
#include "../irisglfwd.h"
#include "../graphics/mesh.h"
#include "assimp/scene.h"

class QOpenGLShaderProgram;

class AssimpObject {
public:
    AssimpObject() {}
    AssimpObject(const aiScene *ai, QString p) : scene(ai), path(p) {}
    AssimpObject(const AssimpObject &ao) {}
    const aiScene *getSceneData() const { return scene; }
    QString getPath() { return path; }
    ~AssimpObject() {}

private:
    const aiScene *scene;
    QString path;
};

Q_DECLARE_METATYPE(AssimpObject)
Q_DECLARE_METATYPE(AssimpObject*)

namespace iris
{

class GraphicsHelper
{
public:
    static QOpenGLShaderProgram* loadShader(QString vsPath, QString fsPath);

    static QString loadAndProcessShader(QString shaderPath);

    /**
     * Loads all meshes from mesh file
     * Useful for loading a mesh file containing multiple meshes
     * Caller is responsible for releasing returned Mesh pointers
     * @param filePath
     * @return
     */
    static QList<MeshPtr> loadAllMeshesFromFile(QString filePath);

    static void loadAllMeshesAndAnimationsFromFile(QString filePath,
                                                   QList<MeshPtr> &meshes,
                                                   QMap<QString, SkeletalAnimationPtr> &animations);

    template <typename F>
    static void loadAllMeshesAndAnimationsFromStore(const QList<F> &store,
                                                    QString filePath,
                                                    QList<MeshPtr> &meshes,
                                                    QMap<QString, SkeletalAnimationPtr> &animations)
     {
         for (F ao : store) {
             if (ao->path == filePath) {
                QVariant aoVariant;
                aoVariant.setValue(ao->getValue());

                const aiScene* scene = aoVariant.value<AssimpObject*>()->getSceneData();

                if (scene != nullptr) {
                    meshes = loadAllMeshesFromAssimpScene(scene);
                    animations = Mesh::extractAnimations(scene, filePath);
                }

                break;
             }
         }
     }


    static QList<MeshPtr> loadAllMeshesFromAssimpScene(const aiScene* scene);
};

}

#endif // GRAPHICSHELPER_H
