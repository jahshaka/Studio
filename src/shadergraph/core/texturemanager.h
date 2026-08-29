#pragma once

#include <QString>
#include <QMap>
#include <QVector>
#include "irisgl/IrisGL.h"
#include <QStandardPaths>
#include <QDirIterator>
#include <QMessageBox>

#if(EFFECT_BUILD_AS_LIB)
#include "../../core/database/database.h"
//#include "../uimanager.h"
//#include "../globals.h"
//#include "../core/guidmanager.h"
//#include "../../irisgl/src/core/irisutils.h"
//#include "../io/assetmanager.h"
#else
#include <QUuid>
#endif

class GraphTexture
{
public:
	bool dirty = true;
	iris::Texture2DPtr texture;
	QString path;
	QString uniformName;
	QString guid;// for embedded version

	void setImage(QString path);
};

class TextureManager
{
public:
	QVector<GraphTexture*> textures;

	//void addTexture(QString path);
	GraphTexture* createTexture();
	void removeTexture(GraphTexture* tex);
	void removeTextureByGuid(QString guid);
	void loadUnloadedTextures();
	void setDatabase(Database * dataBase);
	void clearTextures();

	/*
	Loads texture using it's Guid
	Returns graph texture even if it's still not in the database
	*/
	GraphTexture* loadTextureFromGuid(QString guid);
	QString loadTextureFromDisk(QString guid);
	QString loadTextureFromDatabase(QString guid);

	GraphTexture* importTexture(QString path);
	static TextureManager* getSingleton();
private:
	Database *database;
	static TextureManager* instance;
};