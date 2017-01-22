/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCENEIOBASE_H
#define SCENEIOBASE_H

#include <QDir>

class AssetIOBase
{
protected:

    //holds the directory for the file being saved or loaded
    //used for creating relative file paths for assets upon saving scene
    //used for creating absolute file paths for assets upon loading scene
    QDir dir;

    void setAssetPath(QString assetPath);

    //returns QDir containing filename's parent folder
    static QDir getDirFromFileName(QString filename);

    //gets relative path for filename
    //assumes dir has already been assigned a value from saveScene or loadScene
    //should be called inside a SceneNode's writeData function
    //if path is resource, return original path
    QString getRelativePath(QString filename);

    //gets absolute path for filename
    //assumes dir has already been assigned a value from saveScene or loadScene
    //should be called inside a SceneNode's readData function
    //returns original string if filepath is a resource
    //returns null string if path doesnt exist
    QString getAbsolutePath(QString filename);


    /**
     * Reads r,g,b and a from color json object
     * returns default QColor() if colorObj is null
     * @param colorObj
     * @return
     */
    QColor readColor(const QJsonObject& colorObj);

    /**
     * Reads x,y and z from vector json object
     * returns default QVector3D() if colorObj is null
     * @param vecObj
     * @return
     */
    QVector3D readVector3(const QJsonObject& vecObj);
};

#endif // SCENEIOBASE_H
