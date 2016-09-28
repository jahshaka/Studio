/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef TEXTUREHELPERS_H
#define TEXTUREHELPERS_H

#include <QTextureImage>

class TextureHelper
{
public:
    /**
     * loads a texture image given the @fileName
     * returns nullptr if the filename is empty
     */
    static Qt3DRender::QTextureImage* loadTexture(QString fileName)
    {
        if(fileName.isEmpty())
            return nullptr;

        auto img = new Qt3DRender::QTextureImage();
        img->setSource(QUrl::fromLocalFile(fileName));
        return img;

    }

    /**
     * @brief removes all images from texture
     * @param texture
     */
    static void clearImages(Qt3DRender::QAbstractTexture* texture)
    {
        for(auto img:texture->textureImages())
            texture->removeTextureImage(img);
    }

    /**
     * @brief Returns first image from texture
     * if there is no image then a nullptr is returned
     * @param texture
     * @return
     */
    static Qt3DRender::QTextureImage* getImageFromTexture(Qt3DRender::QAbstractTexture* texture)
    {
        for(auto img:texture->textureImages())
            return qobject_cast<Qt3DRender::QTextureImage*>(img);

        return nullptr;
    }
};

#endif // TEXTUREHELPERS_H
