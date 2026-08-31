#pragma once

#include <QString>
#include <QMap>
#include <QVector>
#include "irisgl/document/assets/mesh.h"
#include "irisgl/import/model.h"
#include "irisgl/document/assets/vertexlayout.h"
#include "irisgl/document/assets/vertexbuffer.h"
#include "irisgl/document/assets/texture.h"
#include "irisgl/document/assets/texture2d.h"
#include "irisgl/document/assets/shader.h"
#include "irisgl/document/materials/renderstates.h"
#include "irisgl/document/materials/rasterizerstate.h"
#include "irisgl/import/graphicshelper.h"
#include "irisgl/core/viewport.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/particlesystemnode.h"
#include "irisgl/document/scenegraph/viewernode.h"
#include "irisgl/import/modelloader.h"
#include "irisgl/document/animation/animableproperty.h"
#include "irisgl/document/animation/animation.h"
#include "irisgl/document/animation/keyframeanimation.h"
#include "irisgl/document/animation/propertyanim.h"
#include "irisgl/document/animation/skeletalanimation.h"
#include "irisgl/document/physics/environment.h"
#include "irisgl/document/physics/charactercontroller.h"
#include "irisgl/document/physics/physicshelper.h"
#include "irisgl/document/physics/physicsproperties.h"
#include <QStandardPaths>
#include <QDirIterator>
#include <QMessageBox>

#if(EFFECT_BUILD_AS_LIB)
#include "data/database/database.h"

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

	// whether a project database backs guid lookups (headless slices and the
	// standalone build run without one)
	bool hasDatabase() const { return database != nullptr; }
private:
	Database *database = nullptr;
	static TextureManager* instance;
};