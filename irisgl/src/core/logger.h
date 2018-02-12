#ifndef LOGGER_H
#define LOGGER_H

#include <QString>


#define LOG_FILE_NAME "log.txt"

class QFile;
class QTextStream;


void irisLog(QString text);

namespace iris
{

class Logger
{
    QFile* file;
    QTextStream* out;

    static Logger* instance;
    Logger();

public:
    void init(QString logFilePath);

    void info(QString text);
    void warn(QString text);
    void error(QString text);

    static Logger* getSingleton();
};

}

#endif // LOGGER_H
