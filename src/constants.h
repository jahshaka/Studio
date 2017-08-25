#ifndef CONSTANTS_H
#define CONSTANTS_H

#include <QString>
#include <QSize>

// this holds global constants used in the application, DON'T use any complex types here
// this is not the same as "globals.h" as this file should ONLY hold strings or numbers
// that are used in several places across the application,

namespace Constants
{
    const QString CONTENT_VERSION   = "0.3beta";
    const QString JAH_EXT           = ".jah";
    const QString PROJ_EXT          = ".jah";
    const QStringList PROJECT_DIRS  = { "Textures", "Models", "Shaders", "Materials", "Metadata" };
    const QString SHADER_DEFS       = "/app/shader_defs/";
    const QString DEFAULT_SHADER    = "/app/shader_defs/Default.shader";
    const QString SAMPLES_FOLDER    = "/scenes";
    const QString PROJECT_FOLDER    = "/JahProjects";
    const QSize   TILE_SIZE         = QSize(460, 215);

    const QString DB_DRIVER         = "QSQLITE";
    const QString DB_ROOT_TABLE     = "JAH_PROJECT";
}

#endif // CONSTANTS_H
