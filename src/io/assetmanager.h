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

#include "irisgl/irisglfwd.h"
#include "irisgl/import/graphicshelper.h"   // AssimpObject

#include "data/project.h"

// AssetMaterial values hold the hydrated material behind a saved .material
// asset. PBR materials only exist behind the Material base (PbrMaterial is not
// a CustomMaterial), so the variant payload is the base pointer.
Q_DECLARE_METATYPE(iris::MaterialPtr)

// (No `class aiScene;` here: nothing in this header names the type — the model
// payload is AssimpObject, declared by graphicshelper.h above, which carries
// its own forward declaration. ENGINEERING_DEBT_SPEC item 5, shape 3.)

struct Asset {
    ModelTypes          type;
    QString             path;
    QString             fileName;
	QString				assetGuid;
    QPixmap             thumbnail;
    bool                deletable;

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
        type = ModelTypes::Object;
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

struct AssetShader : public Asset
{
	AssetShader() {
		type = ModelTypes::Shader;
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
/// OWNERSHIP (deep audit 2026-09, area 3, critical): every pointer handed to
/// addAsset()/replaceAssets() belongs to this list and is DELETED when it
/// leaves it. Before that, `clearAssetList()` only called QVector::clear() —
/// so every project open leaked its whole session (~80 allocations for the
/// Showroom sample), and because the payload QVariant holds a SceneNodePtr,
/// each leaked Asset pinned an entire mesh subtree for the life of the
/// process. Nothing stores an Asset* across a clear (verified over all
/// getAssets() consumers), so deleting here is safe.
class AssetManager
{
public:
    static QVector<Asset*> assets;
    static QVector<Asset*>& getAssets();
    /// Takes ownership of `asset`.
    static void addAsset(Asset* asset);
    /// Takes ownership of `asset` and DESTROYS the asset previously
    /// registered under `oldAssetGuid`, if any.
	static void replaceAssets(QString oldAssetGuid, Asset* asset);
    /// Destroys every registered asset and empties the list. This is the
    /// project-close / project-open boundary.
	static void clearAssetList();
	static Asset* getAssedByGuid(QString guid);
};

#endif // ASSETMANAGER_H
