/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef ASSETMANAGER_H
#define ASSETMANAGER_H

#include <QList>
#include <QImage>
#include <QPixmap>

#include <irisgl/Graphics.h>

#include "core/project.h"

class aiScene;

struct Asset {
    ModelTypes          type;
    QString             path;
    QString             fileName;
	QString				assetGuid;
    QPixmap             thumbnail;
    bool                deletable;

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

class AssetManager
{
public:
    static QVector<Asset*> assets;
    static QVector<Asset*>& getAssets();
    static void addAsset(Asset* asset);
	static void replaceAssets(QString oldAssetGuid, Asset* asset);
	static void clearAssetList();
	static Asset* getAssedByGuid(QString guid);
};

#endif // ASSETMANAGER_H
