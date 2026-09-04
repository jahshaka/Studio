/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCENEIOBASE_H
#define SCENEIOBASE_H

#include "irisgl/core/math/vec.h"
#include <QDir>

class AssetIOBase
{
protected:

    // Holds the directory for the file being saved or loaded.
    // Used for creating relative file paths for assets upon saving a scene,
    // and absolute file paths upon loading one.
    //
    // PER INSTANCE, deliberately. It used to be a `static QDir` shared by
    // every SceneReader, SceneWriter, MaterialReader and MaterialPresetReader
    // in the process: a reader constructed on the import worker rewrote the
    // base directory out from under the reader the UI thread was in the middle
    // of using, and the two readers a nested load creates clobbered each other
    // even on one thread (deep audit 2026-09).
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
     * Reads x and y from vector json object
     * returns default iris::Vec2() if vecObj is null
     * @param vecObj
     * @return
     */
    iris::Vec2 readVector2(const QJsonObject& vecObj);

	/**
	 * Reads x,y and z from vector json object
	 * returns default iris::Vec3() if vecObj is null
	 * @param vecObj
	 * @return
	 */
	iris::Vec3 readVector3(const QJsonObject& vecObj);

	/**
	 * Reads x,y,z and w from vector json object
	 * returns default iris::Vec4() if vecObj is null
	 * @param vecObj
	 * @return
	 */
	iris::Vec4 readVector4(const QJsonObject& vecObj);

public:
	/**
	 * Reads r,g,b and a from color json object
	 * returns default QColor() if colorObj is null
	 * @param colorObj
	 * @return
	 */
	static QColor readColor(const QJsonObject& colorObj);
};

#endif // SCENEIOBASE_H
