/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "graphicshelper.h"
#include <QOpenGLShaderProgram>

#include "../graphics/mesh.h"
#include "assimp/postprocess.h"
#include "assimp/Importer.hpp"
#include "assimp/scene.h"
#include "assimp/mesh.h"
#include "assimp/matrix4x4.h"
#include "assimp/vector3.h"
#include "assimp/quaternion.h"

#include "../graphics/vertexlayout.h"

namespace iris
{

QOpenGLShaderProgram* GraphicsHelper::loadShader(QString vsPath,QString fsPath)
{
    QOpenGLShader *vshader = new QOpenGLShader(QOpenGLShader::Vertex);
    vshader->compileSourceFile(vsPath);

    QOpenGLShader *fshader = new QOpenGLShader(QOpenGLShader::Fragment);
    fshader->compileSourceFile(fsPath);

    auto program = new QOpenGLShaderProgram;
    program->addShader(vshader);
    program->addShader(fshader);

    program->bindAttributeLocation("a_pos",(int)VertexAttribUsage::Position);
    program->bindAttributeLocation("a_texCoord",(int)VertexAttribUsage::TexCoord0);
    program->bindAttributeLocation("a_texCoord1",(int)VertexAttribUsage::TexCoord1);
    program->bindAttributeLocation("a_texCoord2",(int)VertexAttribUsage::TexCoord2);
    program->bindAttributeLocation("a_texCoord3",(int)VertexAttribUsage::TexCoord3);
    program->bindAttributeLocation("a_normal",(int)VertexAttribUsage::Normal);
    program->bindAttributeLocation("a_tangent",(int)VertexAttribUsage::Tangent);

    program->link();

    return program;
}

QList<iris::Mesh*> GraphicsHelper::loadAllMeshesFromFile(QString filePath)
{
    QList<Mesh*> meshes;

    Assimp::Importer importer;
    const aiScene *scene = importer.ReadFile(filePath.toStdString().c_str(), aiProcessPreset_TargetRealtime_Fast);

    if(scene)
    {
        for(unsigned i = 0; i < scene->mNumMeshes; i++)
        {
            auto mesh = new Mesh(scene->mMeshes[i]);
            meshes.append(mesh);
        }
    }

    return meshes;
}

}
