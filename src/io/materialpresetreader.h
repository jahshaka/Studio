/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef MATERIALREADER_H
#define MATERIALREADER_H

#include <QSharedPointer>
#include "assetiobase.h"

namespace iris {
    class Material;
}

class MaterialPreset;

/**
 * This class parses material definitions from json files
 */
class MaterialPresetReader:public AssetIOBase
{
public:
    MaterialPresetReader();

    QJsonObject getMatPreset(const QString &filename);

    MaterialPreset* readMaterialPreset(QString filename);
};

#endif // MATERIALREADER_H
