#ifndef CONSTANTS_H
#define CONSTANTS_H

#include <QString>
#include <QSize>

// this holds global constants used in the application, DON'T use any complex types here
// this is not the same as "globals.h" as this file should ONLY hold basic data types
// that are used in several places across the application,

namespace Constants
{
    const QString CONTENT_VERSION   = "0.4a";
    const QString PROJ_EXT          = ".jah";
    const QStringList PROJECT_DIRS  = { "Textures", "Models", "Shaders", "Materials" };
    const QString SHADER_DEFS       = "/app/shader_defs/";
    const QString DEFAULT_SHADER    = "/app/shader_defs/Default.shader";
    const QString SAMPLES_FOLDER    = "/scenes";
    const QString PROJECT_FOLDER    = "/JahProjects";
    const QString JAH_FOLDER        = "/Jahshaka";
    const QString JAH_DATABASE      = "JahLibrary.db";
    const QString DEF_EXPORT_FILE   = "export.zip";
    const QSize   TILE_SIZE         = QSize(460, 215);
    const int     UI_FONT_SIZE      = 10;

    const QString DB_DRIVER         = "QSQLITE";
    const QString DB_ROOT_TABLE     = "JAH_PROJECT";

    const QString DB_PROJECTS_TABLE = "projects";
    const QString DB_THUMBS_TABLE   = "thumbnails";
	const QString DB_ASSETS_TABLE	= "assets";

    const QList<QString> IMAGE_EXTS = { "png", "PNG", "jpg" };
    const QList<QString> MODEL_EXTS = { "obj", "fbx", "dae", "blend"};

    const int FPS_90                = 11; // milliseconds
    const int FPS_60                = 17; // milliseconds
}

#endif // CONSTANTS_H
