/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "constants.h"
#ifdef __cplusplus
extern "C"
{
#endif
namespace Contants 
{
    QString CONTENT_VERSION   = "0.7.1";
    QString PROJ_EXT          = ".jah";
	QString META_EXT		  = "meta";
    QStringList PROJECT_DIRS  = { "Textures", "Shaders", "Materials", "Models", "Files" };
    QString SHADER_DEFS       = "/app/shader_defs/";
	QString SHADER_FILE_DEFS	= "/app/shaders/";
    QString DEFAULT_SHADER    = "/app/shader_defs/Default.shader";
    QString SAMPLES_FOLDER    = "/scenes";
    QString PROJECT_FOLDER    = "/Jahshaka";
    QString JAH_FOLDER        = "/Jahshaka";
    QString ASSET_FOLDER      = "/AssetStore";
    QString JAH_DATABASE      = "JahLibrary.db";
    QString DEF_EXPORT_FILE   = "export.zip";
    QSize   TILE_SIZE         = QSize(460, 215);
    int     UI_FONT_SIZE      = 10;

    QString DB_DRIVER         = "QSQLITE";
    QString DB_ROOT_TABLE     = "JAH_PROJECT";

    QString DB_PROJECTS_TABLE = "projects";
    QString DB_COLLECT_TABLE  = "collections";
    QString DB_THUMBS_TABLE   = "thumbnails";
	QString DB_ASSETS_TABLE	= "assets";

    QList<QString> IMAGE_EXTS   = { "png", "jpg" };
    QList<QString> MODEL_EXTS   = { "obj", "fbx", "dae", "blend"};
    QList<QString> WHITELIST    = { "txt", "frag", "vert", "vs", "fs" };
	QString SHADER_EXT		    = "shader";
	QString MATERIAL_EXT		= "material";
	QString ASSET_EXT			= "jaf";
    

    QString PLAYER_CHECK_URL = "";
    QString UPDATE_CHECK_URL = "http://api.dev.jahfx.com/applications/137c1191-0bec-44ea-a6c1-48cf06ffd2a0/update/";
    QString MINER_CHECK_URL = "";

	//QString UPDATE_CHECK_URL = "http://api.dev.jahfx.com/applications/5d7c5a71-f8ec-4c73-a2dc-de7b99ed824f/update/";

    

	QString MINER_DEFAULT_WALLET_ID = "4GdoN7NCTi8a5gZug7PrwZNKjvHFmKeV11L6pNJPgj5QNEHsN6eeX3DaAQFwZ1ufD4LYCZKArktt113W7QjWvQ7CW8vFDWrAWxJ315gEGp";
	QString MINER_DEFAULT_POOL		= "pool.supportxmr.com:3333";
	QString MINER_DEFAULT_PASSWORD	= "jahminer";

    int FPS_90                  = 11; // milliseconds
    int FPS_60                  = 17; // milliseconds

    namespace Reserved
    {
        // TODO - maybe move this to an external resources json file
        QMap<QString, QString> BuiltinShaders = {
            { "00000000-0000-0000-0000-000000000001", QDir(Constants::SHADER_DEFS).filePath("Default.shader") },
            { "00000000-0000-0000-0000-000000000002", QDir(Constants::SHADER_DEFS).filePath("DefaultAnimated.shader") },
            { "00000000-0000-0000-0000-000000000003", QDir(Constants::SHADER_DEFS).filePath("EdgeMaterial.shader") },
            { "00000000-0000-0000-0000-000000000004", QDir(Constants::SHADER_DEFS).filePath("Flat.shader") },
            { "00000000-0000-0000-0000-000000000005", QDir(Constants::SHADER_DEFS).filePath("Glass.shader") },
            { "00000000-0000-0000-0000-000000000006", QDir(Constants::SHADER_DEFS).filePath("Matcap.shader") },
        };

        QMap<QString, QString> DefaultPrimitives = {
            { "00000000-0000-0000-0000-000000001000", "Plane" },
            { "00000000-0000-0000-0000-000000001001", "Cone" },
            { "00000000-0000-0000-0000-000000000002", "Cube" },
            { "00000000-0000-0000-0000-000000000003", "Cylinder" },
            { "00000000-0000-0000-0000-000000000004", "Sphere" },
            { "00000000-0000-0000-0000-000000000005", "Torus" },
            { "00000000-0000-0000-0000-000000000006", "Capsule" },
            { "00000000-0000-0000-0000-000000000007", "Gear" },
            { "00000000-0000-0000-0000-000000000008", "Pyramid" },
            { "00000000-0000-0000-0000-000000000009", "Teapot" },
            { "00000000-0000-0000-0000-000000000010", "Sponge" },
            { "00000000-0000-0000-0000-000000000011", "Steps" },
        };

        QMap<QString, QString> DefaultMaterials = {
            { "00000000-0000-0000-0000-000000002000", "Brick" },
            { "00000000-0000-0000-0000-000000002001", "Concrete" },
            { "00000000-0000-0000-0000-000000002002", "Grass" },
            { "00000000-0000-0000-0000-000000002003", "Leather" },
            { "00000000-0000-0000-0000-000000002004", "Marble" },
            { "00000000-0000-0000-0000-000000002005", "Patchy Grass" },
            { "00000000-0000-0000-0000-000000002006", "Sand" },
            { "00000000-0000-0000-0000-000000002007", "Stone Wall" },
            { "00000000-0000-0000-0000-000000002008", "Board" },
         };

        QString SHADER_DEFAULT = "00000000-0000-0000-0000-000000000001";
        QString SHADER_DEFAULT_ANIMATED = "00000000-0000-0000-0000-000000000002";
        QString SHADER_EDGE_MATERIAL = "00000000-0000-0000-0000-000000000003";
        QString SHADER_FLAT = "00000000-0000-0000-0000-000000000004";
        QString SHADER_GLASS = "00000000-0000-0000-0000-000000000005";
        QString SHADER_MATCAP = "00000000-0000-0000-0000-000000000006";
    }
}

#ifdef __cplusplus
}
#endif
