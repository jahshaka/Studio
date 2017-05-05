#ifndef ASSETMANAGER_H
#define ASSETMANAGER_H

#include <QImage>
#include <QList>
#include <QPixmap>


enum AssetType {
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
    Invalid
};

struct Asset {
    AssetType type;

    // path relative to project root
    QString path;
    QString fileName;

    // default value is null
    QPixmap thumbnail;
    bool deletable;

    Asset(AssetType type, QString path, QString fileName, QPixmap pmap) {
        this->type = type;
        this->path = path;
        this->fileName = fileName;
        this->thumbnail = pmap;

        deletable = false;
    }
};

class AssetManager
{
public:
    AssetManager();
    static QList<Asset*> assets;
};

#endif // ASSETMANAGER_H
