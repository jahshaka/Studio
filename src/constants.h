/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef CONSTANTS_H
#define CONSTANTS_H

#include <QDir>
#include <QMap>
#include <QPair>
#include <QString>
#include <QSize>

// this holds global constants used in the application, DON'T use any complex types here
// this is not the same as "globals.h" as this file should ONLY hold basic data types
// that are used in several places across the application,

#ifdef __cplusplus
extern "C"
{
#endif
namespace Constants
{
    extern QString CONTENT_VERSION;
    extern QString PROJ_EXT;
	extern QString META_EXT;
    extern QStringList PROJECT_DIRS;
    extern QString SHADER_DEFS;
	extern QString SHADER_FILE_DEFS;
    extern QString DEFAULT_SHADER;
    extern QString SAMPLES_FOLDER;
    extern QString PROJECT_FOLDER;
    extern QString JAH_FOLDER;
    extern QString ASSET_FOLDER;
    extern QString JAH_DATABASE;
    extern QString DEF_EXPORT_FILE;
    extern QSize   TILE_SIZE;
    extern int     UI_FONT_SIZE;

    extern QString DB_DRIVER;
    extern QString DB_ROOT_TABLE;

    extern QString DB_PROJECTS_TABLE;
    extern QString DB_COLLECT_TABLE;
    extern QString DB_THUMBS_TABLE;
	extern QString DB_ASSETS_TABLE;

    extern QList<QString> IMAGE_EXTS;
    extern QList<QString> MODEL_EXTS;
    extern QList<QString> WHITELIST;
	extern QString SHADER_EXT;
	extern QString MATERIAL_EXT;
	extern QString ASSET_EXT;

	extern QString UPDATE_CHECK_URL;

	extern QString MINER_DEFAULT_WALLET_ID;
	extern QString MINER_DEFAULT_POOL;

    extern int FPS_90;
    extern int FPS_60;

    namespace Reserved
    {
        // TODO - maybe move this to an external resources json file
        extern QMap<QString, QString> BuiltinShaders;
        extern QMap<QString, QString> DefaultPrimitives;
        extern QMap<QString, QString> DefaultMaterials;

        extern QString SHADER_DEFAULT;
        extern QString SHADER_DEFAULT_ANIMATED;
        extern QString SHADER_EDGE_MATERIAL;
        extern QString SHADER_FLAT;
        extern QString SHADER_GLASS;
        extern QString SHADER_MATCAP;
    }
}

#ifdef __cplusplus
}
#endif

#endif // CONSTANTS_H
