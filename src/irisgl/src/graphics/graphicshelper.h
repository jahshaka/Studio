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

class QOpenGLShaderProgram;


namespace iris
{
class Mesh;
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
    static QList<Mesh*> loadAllMeshesFromFile(QString filePath);
};

}

#endif // GRAPHICSHELPER_H
