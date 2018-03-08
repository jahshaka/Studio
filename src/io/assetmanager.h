#ifndef ASSETMANAGER_H
#define ASSETMANAGER_H

#include <QImage>
#include <QList>
#include <QPixmap>

// #include "../irisgl/src/assimp/include/assimp/Importer.hpp"
// #include "../irisgl/src/assimp/include/assimp/scene.h"
// #include "../irisgl/src/assimp/include/assimp/postprocess.h"

class aiScene;

#include "irisgl/src/graphics/graphicshelper.h"

enum class AssetType {
    Shader,
    Material,
    Mesh,
    Texture,
    Video,
    SoundClip,
    Music,
    ParticleSystem,
    File,
    Folder,
    Scene,
    Animation,
    Object,
    Invalid,
    Variant
};

struct Asset {
    AssetType           type;
    QString             path;
    QString             fileName;
	QString				assetGuid;
    QPixmap             thumbnail;
    bool                deletable;

    virtual QVariant    getValue() = 0;
    virtual void        setValue(QVariant val) = 0;
};

struct AssetVariant : public Asset
{
    AssetVariant() {
        type = AssetType::Variant;
        deletable = true;
    }

    virtual QVariant getValue() {
        return QVariant();
    }

    virtual void setValue(QVariant val) {
        Q_UNUSED(val);
    }
};

struct AssetFile : public Asset
{
    AssetFile() {
        type = AssetType::File;
        deletable = true;
    }

    virtual QVariant getValue() {
        return QVariant();
    }

    virtual void setValue(QVariant val) {
        Q_UNUSED(val)
    }
};

struct AssetFolder : public Asset
{
    AssetFolder() {
        type = AssetType::Folder;
        deletable = true;
    }

    virtual QVariant getValue() {
        return QVariant();
    }

    virtual void setValue(QVariant val) {
        Q_UNUSED(val);
    }
};

// note that this class is not able to be used for queued signal-slot connections
// not needed at the moment nor should it be in the foreseeable future
struct AssetObject : public Asset
{
    // this is a metatype so we can use aiScene's in variants
    AssimpObject *ao;

    AssetObject(AssimpObject *a, QString p, QString f) : ao(a) {
        type = AssetType::Object;
        path = p;
        fileName = f;
        deletable = true;
    }

    virtual QVariant getValue() override {
        QVariant v;
        v.setValue(ao);
        return v;
    }

    virtual void setValue(QVariant value) {
        // look into getting rid of the ptr
        // ao = value.value<AssimpObject*>();
    }
};

class AssetManager
{
public:
    static QList<Asset*> assets;
    static QList<Asset*>& getAssets();
    static void addAsset(Asset* asset);

    // returns asset by path
    // return null if no asset exists
    static Asset* getAssetByPath(QString absolutePath);


};

#endif // ASSETMANAGER_H
