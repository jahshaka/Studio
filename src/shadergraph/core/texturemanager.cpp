/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/
#include "texturemanager.h"
#include "../shadergraphmainwindow.h"
#include "../../globals.h"
#include "src/core/project.h"

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
	this->textures.removeAt(textures.indexOf(tex));
}

void TextureManager::removeTextureByGuid(QString guid)
{
	for (auto tex : textures) {
		if (tex->guid == guid) {
			// remove from list
			this->textures.removeAt(textures.indexOf(tex));
			
			// remove from db
			database->deleteAsset(guid);

			// remove from filesystem
			auto assetFolder = IrisUtils::join(
                QStandardPaths::writableLocation(QStandardPaths::AppDataLocation),
				"AssetStore", guid
			);

			if (QDir(assetFolder).exists()) {
				QDir(assetFolder).removeRecursively();
			}

			break;
		}
	}
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

void TextureManager::clearTextures()
{
	textures.clear();
}

GraphTexture * TextureManager::loadTextureFromGuid(QString guid)
{
	auto tex = this->createTexture();
	auto asset = database->fetchAsset(guid);
	if (asset.guid.isEmpty()) {
		return tex;
	}

	//auto p = loadTextureFromDisk(guid); // load if file paths are located on the disk
	//if (!QFileInfo::exists(p)) // if they are not located on disk
	auto p = loadTextureFromDatabase(guid); // load file paths from database

	tex->setImage(p);
	tex->guid = guid;

	return tex;
}

QString TextureManager::loadTextureFromDisk(QString guid)
{
	auto asset = database->fetchAsset(guid);
	

	auto imagePath = IrisUtils::join(
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation),
		"AssetStore", guid, asset.name
	);

	return imagePath;
}

QString TextureManager::loadTextureFromDatabase(QString guid)
{
	auto asset = database->fetchAsset(guid);

	auto imagePath = IrisUtils::join(
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation),
		"AssetStore", guid, asset.name
	);

	return imagePath;
}

GraphTexture* TextureManager::importTexture(QString path)
{
	auto assetPath = IrisUtils::join(
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation),
		"AssetStore"
	);

	auto texGuid = shadergraph::MainWindow::genGUID();

	const QString assetFolder = QDir(assetPath).filePath(texGuid);
	QDir().mkpath(assetFolder);

	QFileInfo fileInfo(path);
	QString fileToCopyTo = IrisUtils::join(assetFolder, fileInfo.fileName());
	bool copyFile = QFile::copy(fileInfo.absoluteFilePath(), fileToCopyTo);

    database->createAssetEntry(QString(),
		texGuid,
		fileInfo.fileName(),
		static_cast<int>(ModelTypes::File),
		QByteArray(),
		QByteArray(),
		AssetViewFilter::Effects);

	auto tex = createTexture();
	tex->path = fileToCopyTo;
	tex->guid = texGuid;

	return tex;
}

TextureManager* TextureManager::getSingleton()
{
	if (instance == nullptr)
		instance = new TextureManager();

	return instance;
}