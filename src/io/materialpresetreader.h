#ifndef MATERIALREADER_H
#define MATERIALREADER_H

#include <QSharedPointer>
#include "assetiobase.h"

namespace iris
{
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

    MaterialPreset* readMaterialPreset(QString filename);
};

#endif // MATERIALREADER_H
