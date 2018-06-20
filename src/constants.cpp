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
    QString CONTENT_VERSION   = "0.7.0";
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

	QString UPDATE_CHECK_URL	= "http://api.dev.jahfx.com/applications/5d7c5a71-f8ec-4c73-a2dc-de7b99ed824f/update/";

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

        QMap<QString, QString> BuiltinPrimitives = {
            { "00000000-0000-0000-0000-000000001000", ":/content/primitives/cube.obj" },
            { "00000000-0000-0000-0000-000000002000", ":/content/primitives/cylinder.obj" },
            { "00000000-0000-0000-0000-000000003000", ":/content/primitives/plane.obj" },
            { "00000000-0000-0000-0000-000000004000", ":/content/primitives/sphere.obj" },
            { "00000000-0000-0000-0000-000000005000", ":/content/primitives/torus.obj" },
            { "00000000-0000-0000-0000-000000006000", ":/content/primitives/arrow.obj" },
            { "00000000-0000-0000-0000-000000007000", ":/content/primitives/capsule.obj" },
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