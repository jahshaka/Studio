/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef ASSETMANAGER_H
#define ASSETMANAGER_H

#include <QList>
#include <QImage>
#include <QPixmap>

#include <memory>

#include "irisgl/irisglfwd.h"

#include "data/project.h"

// AssetMaterial values hold the hydrated material behind a saved .material
// asset. PBR materials only exist behind the Material base (PbrMaterial is not
// a CustomMaterial), so the variant payload is the base pointer.
Q_DECLARE_METATYPE(iris::MaterialPtr)

struct Asset {
    ModelTypes          type = ModelTypes::Undefined;
    QString             path;
    QString             fileName;
	QString				assetGuid;
    QPixmap             thumbnail;

    // AssetManager OWNS every registered Asset (see assetmanager.cpp). The
    // destructor is virtual because the list is a QVector<Asset*> of derived
    // types: deleting through the base without it is undefined behaviour, and
    // for the payload-carrying subclasses it would also skip the QVariant that
    // pins a whole SceneNodePtr subtree.
    virtual ~Asset() = default;

    virtual QVariant    getValue() = 0;
    virtual void        setValue(QVariant val) = 0;
	QVariant			value;
};

// Notice this class doesn't do anything, this is intentional
// Explicitly define a type for whatever you want to hold
struct AssetVariant : public Asset
{
    AssetVariant() {
        type = ModelTypes::Variant;
    }

    virtual QVariant getValue() {
        return QVariant();
    }

    virtual void setValue(QVariant val) {
        Q_UNUSED(val);
    }
};

struct AssetNodeObject : public Asset
{
	AssetNodeObject() {
		type = ModelTypes::Object;
	}

	virtual QVariant getValue() {
		return value;
	}

	virtual void setValue(QVariant val) {
		value = val;
	}
};
struct AssetMaterial : public Asset
{
	AssetMaterial() {
		type = ModelTypes::Material;
	}

	virtual QVariant getValue() {
		return value;
	}

	virtual void setValue(QVariant val) {
		value = val;
	}
};

struct AssetFile : public Asset
{
    AssetFile() {
        type = ModelTypes::File;
    }

    virtual QVariant getValue() {
        return value;
    }

    virtual void setValue(QVariant val) {
        value = val;
    }
};

struct AssetTexture : public Asset
{
    AssetTexture() {
        type = ModelTypes::Texture;
    }

    virtual QVariant getValue() {
        return value;
    }

    virtual void setValue(QVariant val) {
        value = val;
    }
};

struct AssetParticleSystem : public Asset
{
    AssetParticleSystem() {
        type = ModelTypes::ParticleSystem;
    }

    virtual QVariant getValue() {
        return value;
    }

    virtual void setValue(QVariant val) {
        value = val;
    }
};

struct AssetSky : public Asset
{
	AssetSky()
    {
        type = ModelTypes::Sky;
    }

    virtual QVariant getValue()
    {
        return value;
    }

    virtual void setValue(QVariant val)
    {
        value = val;
    }
};

struct AssetMusic : public Asset
{
	AssetMusic()
	{
		type = ModelTypes::Music;
	}

	virtual QVariant getValue()
	{
		return value;
	}

	virtual void setValue(QVariant val)
	{
		value = val;
	}
};

/// The session asset registry — the assets the panels, the viewport's
/// drag-drop lookups and the scene reader see for the OPEN project.
///
/// OWNERSHIP (LEAK-1 L3): the registry OWNS every asset handed to addAsset(),
/// by std::unique_ptr, and destroys it when it leaves (removeAssets,
/// clearAssetList) or when the application ends (a Qt post routine, so the
/// assets go while QGuiApplication still exists — an Asset carries a QPixmap
/// and a QVariant payload). It used to be a bare static QVector<Asset*>: the
/// vector's buffer died with the process's statics and every asset still
/// registered at exit leaked (each import's AssetNodeObject, every session
/// registration). getAssets() is the raw, ordered VIEW; a pointer in it lives
/// until its asset leaves the registry. Nothing stores one across a clear.
class AssetManager
{
public:
    static const QVector<Asset*>& getAssets();
    /// Takes ownership of `asset`; returns it.
    static Asset* addAsset(std::unique_ptr<Asset> asset);
    /// Destroys every registered asset whose guid is `guid`; returns how many.
    static int removeAssets(const QString &guid);
    /// Destroys every registered asset and empties the list. This is the
    /// project-close / project-open boundary.
	static void clearAssetList();
	static Asset* getAssedByGuid(QString guid);
};

#endif // ASSETMANAGER_H
