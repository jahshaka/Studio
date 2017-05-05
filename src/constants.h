#ifndef CONSTANTS_H
#define CONSTANTS_H

#include <QString>

// this holds global constants used in the application, DON'T use any complex types here
// this is not the same as "globals.h" as this file should ONLY hold strings or numbers
// that are used in several places across the application

namespace Constants
{
    const QString SHADER_DEFS       = "/app/shader_defs/";
    const QString DEFAULT_SHADER    = "/app/shader_defs/Default.json";
}

#endif // CONSTANTS_H
