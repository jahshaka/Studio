/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#include "texturemanager.h"
#include "../effectspage.h"
#include "data/project.h"
#include <QSqlDatabase>

#include "services/assetcas.h"
#include "services/assetstorepaths.h"
#include "services/shippedassets.h"

TextureManager* TextureManager::instance = 0;


void GraphTexture::setImage(QString path)
{
	this->path = path;
	this->dirty = true;
}

GraphTexture* TextureManager::createTexture()
{
	auto tex = new GraphTexture();
	tex->dirty = true;
	textures.append(tex);
	return tex;
}

void TextureManager::removeTexture(GraphTexture* tex)
{
	// a node may have been removed already; removeAt(-1) would assert
	int index = textures.indexOf(tex);
	if (index >= 0)
		this->textures.removeAt(index);
}

void TextureManager::loadUnloadedTextures()
{
	for (auto tex : this->textures) {
		if (tex->dirty) {
			tex->texture = iris::Texture2D::load(tex->path);
			tex->dirty = false;
		}
	}
}

void TextureManager::setDatabase(Database * dataBase)
{
	this->database = dataBase;
}

GraphTexture * TextureManager::loadTextureFromGuid(QString guid)
{
	auto tex = this->createTexture();
	tex->guid = guid;

	// no database (headless slice / standalone): keep the guid, the path
	// stays unresolved
	if (database == nullptr)
		return tex;

	auto asset = database->fetchAsset(guid);
	if (asset.guid.isEmpty()) {
		return tex;
	}

	auto p = loadTextureFromDatabase(guid); // load file paths from database

	tex->setImage(p);
	tex->guid = guid;

	return tex;
}

// Both of these resolve the same way now — through the CAS, by guid. The
// retired <root>/<guid>/<name> view is gone (deep audit 2026-09, area 6);
// AssetCas::resolveSource still falls back to that folder for the textures
// importTexture() below drops there directly, which is why routing here was
// safe before the sweep reclaims them.
QString TextureManager::loadTextureFromDisk(QString guid)
{
	return AssetCas::resolveSource(QSqlDatabase::database(), AssetStorePaths::root(), guid);
}

QString TextureManager::loadTextureFromDatabase(QString guid)
{
	return AssetCas::resolveSource(QSqlDatabase::database(), AssetStorePaths::root(), guid);
}

GraphTexture* TextureManager::importTexture(QString path, const assethome::Home &home)
{
	// THROUGH THE ONE IMPORT PIPELINE, BY CONTENT (MATERIAL_BUNDLE_SPEC P-2,
	// owner decision Q1). This routine used to write AROUND the store: a fresh
	// guid, a `QFile::copy` into the RETIRED `<store>/<guid>/<name>` folder and
	// a catalog row of type File under the Effects filter — no hash, no
	// asset_files row, no sidecar, no dedup, no pin. It ran for every image a
	// user picked in a texture node AND for every shipped template's images on
	// every instantiation, which is how the owner's library came to hold four
	// byte-identical copies of one checker.
	//
	// It is now the ordinary image import: a real library Texture row keyed on
	// the bytes (so a second pick of the same image answers the SAME row —
	// byte-identical duplicates are impossible by construction), pinned into
	// the open project when there is one, and OWNED by the material's home.
	auto tex = createTexture();
	if (database == nullptr) {
		// No library behind us (the headless slice, the standalone build): the
		// node keeps the path, exactly as it always did there.
		tex->path = path;
		return tex;
	}

	const ShippedAssets::Pinned imported =
	    ShippedAssets::importTexture(path, QFileInfo(path).fileName(), database, project, home);
	if (!imported.ok()) {
		qWarning("TextureManager::importTexture: %s", qUtf8Printable(imported.error));
		tex->path = path;
		return tex;
	}
	tex->path = imported.path;
	tex->guid = imported.guid;
	return tex;
}

TextureManager* TextureManager::getSingleton()
{
	if (instance == nullptr)
		instance = new TextureManager();

	return instance;
}