#include "logger.h"
#include <QFile>
#include <QTextStream>
#include <QDebug>

namespace iris
{

Logger* Logger::instance = nullptr;

Logger::Logger()
{
    file = nullptr;
    out = nullptr;
}

void Logger::init(QString logFilePath)
{
    file = new QFile(logFilePath);
    file->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    if (file->isOpen()) {
        out = new QTextStream(file);
        out->setCodec("UTF-8");
    }
}

void Logger::info(QString text)
{
    if (out != nullptr) {
        *out << "[info]: "<<text<<"\n";
        out->flush();
    }
    qInfo() << text;
}

void Logger::warn(QString text)
{
    if (out != nullptr) {
        *out << "[warn]: "<<text<<"\n";
        out->flush();
    }
    qWarning() << text;
}

void Logger::error(QString text)
{
    if (out != nullptr) {
        *out << "[error]: "<<text<<"\n";
        out->flush();
    }
    qCritical() << text;
}

Logger *Logger::getSingleton()
{
    if (instance == nullptr)
        instance = new Logger();
    return instance;
}

}

void irisLog(const QString &text)
{
    iris::Logger::getSingleton()->info(text);
}